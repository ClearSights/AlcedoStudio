// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <fstream>

#include "ui/alcedo_main/album_backend/application_close_coordinator.hpp"
#include "ui/photo_stack_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
class ProjectStackSaveTest : public PhotoStackProjectFixture {
 protected:
  std::filesystem::path blocked;
  void                  FailPackageSave() {
    blocked = host->project()->handler().package_path();
    blocked += ".tmp";
    std::filesystem::create_directory(blocked);
    std::ofstream(blocked / "keep_directory").put('x');
    ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
    Wait();
    ASSERT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnsaved);
    ASSERT_TRUE(host->project()->HasUnsavedStackChanges());
  }
  void AllowSave() {
    std::filesystem::remove(blocked / "keep_directory");
    std::filesystem::remove(blocked);
  }
};

TEST_F(ProjectStackSaveTest, RetryStackSavePersistsWithoutRepeatingCommittedMutation) {
  const auto before = Bytes();
  FailPackageSave();
  const auto        stack = result->change.stack_id;
  PhotoStackCommand move;
  move.operation = PhotoStackCommand::Operation::kMove;
  move.file_id   = files[0];
  move.direction = PhotoStackMove::kLater;
  ASSERT_TRUE(host->photo_stacks()->Start(move).isEmpty());
  Wait();
  ASSERT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnsaved);
  const std::vector expected{files[1], files[0], files[2]};
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(stack),
            expected);
  EXPECT_FALSE(host->project()->RetryStackSave());
  EXPECT_TRUE(host->project()->HasUnsavedStackChanges());
  EXPECT_FALSE(host->project()->StackSaveError().isEmpty());
  AllowSave();
  ASSERT_TRUE(host->project()->RetryStackSave());
  EXPECT_FALSE(host->project()->HasUnsavedStackChanges());
  EXPECT_NE(Bytes(), before);
  const auto package = host->project()->handler().package_path();
  ASSERT_TRUE(host->project()->CloseProject(false));
  ASSERT_TRUE(LoadPackedProject(*host, package));
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(stack),
            expected);
}

TEST_F(ProjectStackSaveTest, LibraryOnlyApplicationCloseRequiresPendingStackSaveConfirmation) {
  ApplicationCloseCoordinator close(nullptr, nullptr, host->project());
  EXPECT_FALSE(close.CloseNeedsConfirmation());
  FailPackageSave();
  EXPECT_TRUE(close.CloseNeedsConfirmation());
  close.Cancel();
  EXPECT_TRUE(host->project()->HasUnsavedStackChanges());
  AllowSave();
}

TEST_F(ProjectStackSaveTest, FailedStackSaveAbortsCloseAndKeepsProjectLoaded) {
  FailPackageSave();
  const auto                  project = host->project()->handler().project();
  ApplicationCloseCoordinator close(nullptr, nullptr, host->project());
  QSignalSpy finished(&close, &ApplicationCloseCoordinator::ApplicationCloseFinished);
  QSignalSpy aborted(&close, &ApplicationCloseCoordinator::ApplicationCloseAborted);
  close.BeginSave();
  ASSERT_TRUE(WaitForSignal(aborted));
  EXPECT_EQ(finished.count(), 0);
  EXPECT_FALSE(close.waiting());
  EXPECT_EQ(host->project()->handler().project(), project);
  EXPECT_TRUE(host->project()->HasUnsavedStackChanges());
  EXPECT_FALSE(aborted.front().front().toString().isEmpty());
  EXPECT_EQ(project->GetPhotoStackService()->ReadMemberIds(result->change.stack_id),
            Create().file_ids);
  AllowSave();
  close.BeginSave();
  ASSERT_TRUE(WaitForSignal(finished));
  EXPECT_FALSE(host->project()->HasUnsavedStackChanges());
}

TEST_F(ProjectStackSaveTest, ProjectReplacementDoesNotDiscardPendingStacksWithoutAnExplicitChoice) {
  FailPackageSave();
  const auto project = host->project()->handler().project();
  std::filesystem::create_directories(temp_dir_ / "other");
  const auto other =
      CreatePopulatedPackedProject(temp_dir_ / "other", {}, 0, std::vector<LibraryPhotoSpec>(2));
  ASSERT_TRUE(other);
  EXPECT_FALSE(
      host->project()->LoadProject(QString::fromStdWString(other->packed_path_.wstring())));
  EXPECT_EQ(host->project()->handler().project(), project);
  EXPECT_TRUE(host->project()->HasUnsavedStackChanges());
  AllowSave();
}

TEST_F(ProjectStackSaveTest, ExplicitDiscardLeavesTheLastSavedPackageUnchanged) {
  const auto before  = Bytes();
  const auto package = host->project()->handler().package_path();
  FailPackageSave();
  ApplicationCloseCoordinator close(nullptr, nullptr, host->project());
  QSignalSpy finished(&close, &ApplicationCloseCoordinator::ApplicationCloseFinished);
  close.BeginDiscard();
  EXPECT_EQ(finished.count(), 1);
  EXPECT_FALSE(host->project()->handler().project());
  EXPECT_FALSE(host->project()->HasUnsavedStackChanges());
  AllowSave();
  ASSERT_TRUE(LoadPackedProject(*host, package));
  EXPECT_EQ(Bytes(), before);
  host->library()->SetStackDisplayEnabled(true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library_display()->DisplayCount(), 4);
}

TEST_F(ProjectStackSaveTest, ActiveCommandRefusesExplicitSaveRetryAndProjectSave) {
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  EXPECT_FALSE(host->project()->RetryStackSave());
  EXPECT_FALSE(host->project()->SaveProject());
  ApplicationCloseCoordinator close(nullptr, nullptr, host->project());
  EXPECT_TRUE(close.CloseNeedsConfirmation());
  QSignalSpy aborted(&close, &ApplicationCloseCoordinator::ApplicationCloseAborted);
  close.BeginSave();
  EXPECT_EQ(aborted.count(), 1);
  EXPECT_FALSE(close.waiting());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kSaved);
}
}  // namespace
}  // namespace alcedo::ui::test

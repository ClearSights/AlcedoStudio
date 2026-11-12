// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/alcedo_main/album_backend/photo_stack_controller.hpp"

#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QThread>
#include <fstream>
#include <iterator>
#include <optional>

#include "storage/mapper/duckorm/duckdb_statement.hpp"
#include "support/storage_test_access.hpp"
#include "ui/photo_stack_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
class PhotoStackControllerTest : public PhotoStackProjectFixture {};

TEST_F(PhotoStackControllerTest, CommandUsesWorkerAndRefusesConflictingDeleteOrProjectClose) {
  const auto gui      = QThread::currentThread();
  bool       notified = false;
  QObject::connect(host->photo_stacks(), &PhotoStackController::Finished, host.get(),
                   [&](const auto&) {
                     EXPECT_EQ(QThread::currentThread(), gui);
                     notified = true;
                   });
  {
    auto connection =
        alcedo::StorageTestAccess::GetDatabase(*host->project()->handler().project()->GetStorage())
            .GetConnectionGuard();
    auto lock = connection.Lock();
    ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
    // Start returns while the database lock prevents the worker from committing. A command
    // run on this GUI thread could reenter the lock and would already have inserted rows.
    duckorm::Statement count(connection.conn_, "SELECT count(*) FROM PhotoStack");
    EXPECT_EQ(count.Execute().Get<int64_t>(0, 0), 0);
  }
  EXPECT_TRUE(host->photo_stacks()->Busy());
  EXPECT_EQ(host->background_tasks()->PrimaryTask().value("kind").toString(), "photoStackUpdate");
  EXPECT_FALSE(host->photo_stacks()->Start(Create()).isEmpty());
  EXPECT_FALSE(host->project()->ProjectCloseBlockReason().isEmpty());
  EXPECT_FALSE(host->interaction_policy()
                   ->EvaluateDeleteImages({QVariantMap{{"elementId", files[3]}}})
                   .value("allowed")
                   .toBool());
  Wait();
  EXPECT_TRUE(notified);
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kSaved);
  EXPECT_EQ(host->background_tasks()->RunningCount(), 0);
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(
                result->change.stack_id),
            Create().file_ids);
}

TEST_F(PhotoStackControllerTest, ExistingDeleteTaskRefusesStackCommandBeforeMutation) {
  BackgroundTaskSnapshot task;
  task.kind_    = BackgroundTaskKind::ImageDelete;
  task.state_   = BackgroundTaskState::Running;
  task.locks_   = {{InteractionCapability::CloseProject, 0, QStringLiteral("Deleting photos")}};
  const auto id = host->background_tasks()->RegisterTask(task);
  EXPECT_FALSE(host->photo_stacks()->Start(Create()).isEmpty());
  EXPECT_FALSE(host->photo_stacks()->Busy());
  EXPECT_FALSE(result);
  host->background_tasks()->FinishTask(id, BackgroundTaskState::Succeeded);
}

TEST_F(PhotoStackControllerTest, InvalidCreateRefusesTaskBeforeAnyWrite) {
  auto command     = Create();
  command.file_ids = {files[0], files[0]};
  EXPECT_FALSE(host->photo_stacks()->Start(command).isEmpty());
  EXPECT_FALSE(host->photo_stacks()->Busy());
  EXPECT_EQ(host->background_tasks()->RunningCount(), 0);
  EXPECT_FALSE(result);
}

TEST_F(PhotoStackControllerTest, DatabaseFailurePublishesNoOptimisticMembership) {
  auto command = Create();
  command.file_ids.push_back(9999999);
  const auto before = Bytes();
  ASSERT_TRUE(host->photo_stacks()->Start(command).isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kFailed);
  EXPECT_FALSE(result->error.isEmpty());
  EXPECT_FALSE(result->change.Changed());
  EXPECT_EQ(Bytes(), before);
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kSaved);
}

TEST_F(PhotoStackControllerTest, PackageSaveFailureReportsCommittedButUnsavedStack) {
  const auto before  = Bytes();
  auto       blocked = host->project()->handler().package_path();
  blocked += ".tmp";
  std::filesystem::create_directory(blocked);
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnsaved);
  EXPECT_FALSE(result->error.isEmpty());
  EXPECT_EQ(Bytes(), before);
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(
                result->change.stack_id),
            Create().file_ids);
  std::filesystem::remove(blocked);
}

TEST_F(PhotoStackControllerTest, MetadataSaveFailureRetainsItsDiagnostic) {
  const auto meta   = host->project()->handler().meta_path();
  const auto backup = std::filesystem::path(meta.string() + ".saved");
  std::filesystem::rename(meta, backup);
  std::filesystem::create_directory(meta);
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnsaved);
  EXPECT_TRUE(result->error.contains("Failed to open meta file for writing"));
  std::filesystem::remove(meta);
  std::filesystem::rename(backup, meta);
}

TEST_F(PhotoStackControllerTest, UnchangedCommandDoesNotAttemptPackageSave) {
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  const auto before  = Bytes();
  auto       blocked = host->project()->handler().package_path();
  blocked += ".tmp";
  std::filesystem::create_directory(blocked);
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnchanged);
  EXPECT_EQ(Bytes(), before);
  std::filesystem::remove(blocked);
}
}  // namespace
}  // namespace alcedo::ui::test

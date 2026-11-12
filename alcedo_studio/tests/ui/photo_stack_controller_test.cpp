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
TEST_F(PhotoStackControllerTest, LaterContextClickAndSelectionOwnAvailabilityAfterQueuedReads) {
  host->library()->SetStackDisplayEnabled(true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
  Wait();
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto stack = result->change.stack_id;
  host->library()->ToggleStack(
      host->library_display()->getItemAt(0).value("occurrenceKey").toString());
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  ASSERT_EQ(host->library_display()->DisplayCount(), 4);
  const auto first = host->library_display()->getItemAt(0);
  const auto last  = host->library_display()->getItemAt(2);
  ASSERT_EQ(first.value("fileId").toUInt(), files[0]);
  ASSERT_EQ(last.value("fileId").toUInt(), files[2]);
  auto* selection  = host->library()->selection();
  auto* controller = host->photo_stacks();
  selection->Replace(QVariantList{first});
  controller->PrepareMenu(first.value("occurrenceKey").toString());
  // Real production interleaving: another right click/selection before the first worker
  // completion is delivered by the GUI event queue. No injected revision values are used.
  selection->Replace(QVariantList{last});
  bool       wrong_availability = false;
  const auto connection =
      QObject::connect(controller, &PhotoStackController::MenuOptionsChanged, host.get(), [&] {
        if (controller->MenuOptions().value("later").toBool()) wrong_availability = true;
      });
  controller->PrepareMenu(last.value("occurrenceKey").toString());
  for (int i = 0; i < 200 && !controller->MenuOptions().value("ready").toBool(); ++i)
    ProcessEvents(10);
  ASSERT_TRUE(controller->MenuOptions().value("ready").toBool())
      << controller->Error().toStdString();
  EXPECT_FALSE(wrong_availability);
  EXPECT_TRUE(controller->MenuOptions().value("earlier").toBool());
  EXPECT_FALSE(controller->MenuOptions().value("later").toBool());
  QObject::disconnect(connection);
}

TEST_F(PhotoStackControllerTest, MenuActionNamesApplyEverySavedOrganizationCommandToExplicitFiles) {
  auto* library    = host->library();
  auto* controller = host->photo_stacks();
  library->SetStackDisplayEnabled(true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto item = [&](sl_element_id_t file) {
    return host->library_display()->getItemAt(host->library_display()->rowByElementId(file));
  };
  const auto run = [&](const QString& action, sl_element_id_t file) {
    result.reset();
    controller->RunAction(action, item(file).value("occurrenceKey").toString());
    if (!controller->Busy()) return false;
    Wait();
    return result && result->outcome == PhotoStackCommandResult::Outcome::kSaved &&
           WaitForLibraryQuery(*host);
  };
  library->selection()->Replace(QVariantList{item(files[0]), item(files[1]), item(files[2])});
  ASSERT_TRUE(run("stack-group", files[0])) << controller->Error().toStdString();
  const auto stack   = result->change.stack_id;
  const auto service = host->project()->handler().project()->GetPhotoStackService();
  EXPECT_EQ(service->ReadMemberIds(stack), (std::vector{files[0], files[1], files[2]}));
  library->ToggleStack(item(files[0]).value("occurrenceKey").toString());
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  library->selection()->Replace(QVariantList{item(files[3])});
  ASSERT_TRUE(run("stack-add", files[0])) << controller->Error().toStdString();
  EXPECT_EQ(service->ReadMemberIds(stack), files);
  ASSERT_TRUE(run("stack-earlier", files[3])) << controller->Error().toStdString();
  EXPECT_EQ(service->ReadMemberIds(stack), (std::vector{files[0], files[1], files[3], files[2]}));
  ASSERT_TRUE(run("stack-later", files[3])) << controller->Error().toStdString();
  EXPECT_EQ(service->ReadMemberIds(stack), files);
  ASSERT_TRUE(run("stack-cover", files[3])) << controller->Error().toStdString();
  EXPECT_EQ(item(files[3]).value("savedCoverId").toUInt(), files[3]);
  EXPECT_EQ(service->ReadMemberIds(stack), files);
  ASSERT_TRUE(run("stack-remove", files[3])) << controller->Error().toStdString();
  EXPECT_EQ(service->ReadMemberIds(stack), (std::vector{files[0], files[1], files[2]}));
  EXPECT_EQ(item(files[0]).value("savedCoverId").toUInt(), files[0]);
  ASSERT_TRUE(run("stack-ungroup", files[0])) << controller->Error().toStdString();
  EXPECT_EQ(host->library_display()->DisplayCount(), 4);
  for (const auto file : files) EXPECT_TRUE(item(file).value("stackId").toString().isEmpty());
}

}  // namespace alcedo::ui::test

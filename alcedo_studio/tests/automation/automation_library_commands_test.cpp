//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

// library.* and tasks.* parameter and state rules in an in-process headless session.

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>

#include "automation/automation_protocol.hpp"
#include "automation_in_process_session.hpp"
#include "ui/album_backend_test_fixture.hpp"

namespace alcedo::automation {
namespace {

using test::InProcessAutomationSession;

class AutomationLibraryCommandsTest : public ui::test::ApplicationModuleHostTestFixture {
 protected:
  void SetUp() override {
    ui::test::ApplicationModuleHostTestFixture::SetUp();
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, Folder());
  }
  auto Folder() const -> QString { return ui::test::PathToQString(temp_dir_); }
};

auto ErrorCode(const std::optional<AutomationResponse>& response)
    -> std::optional<AutomationErrorCode> {
  if (!response.has_value() || !response->error.has_value()) {
    return std::nullopt;
  }
  return response->error->code;
}

TEST_F(AutomationLibraryCommandsTest, LibraryCommandsWithoutProjectAreNotReady) {
  InProcessAutomationSession session;
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.list"))), AutomationErrorCode::NotReady);
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.folders"))),
            AutomationErrorCode::NotReady);
  EXPECT_EQ(
      ErrorCode(session.Call(QStringLiteral("library.import"), QJsonObject{{"folder", Folder()}})),
      AutomationErrorCode::NotReady);
}

TEST_F(AutomationLibraryCommandsTest, EmptyFolderImportFinishesWithZeroCounts) {
  InProcessAutomationSession session;
  ASSERT_FALSE(session
                   .CallResult(QStringLiteral("project.create"),
                               QJsonObject{{"folder", Folder()}, {"name", "empty_import"}})
                   .isEmpty());
  const QString empty_folder = QDir(Folder()).filePath(QStringLiteral("empty"));
  ASSERT_TRUE(QDir().mkpath(empty_folder));
  QFile text(QDir(empty_folder).filePath(QStringLiteral("notes.txt")));
  ASSERT_TRUE(text.open(QIODevice::WriteOnly));
  text.write("not an image");
  text.close();

  for (const bool recursive : {false, true}) {
    const QString task_id =
        session
            .CallResult(QStringLiteral("library.import"),
                        QJsonObject{{"folder", empty_folder}, {"recursive", recursive}})
            .value("task_id")
            .toString();
    ASSERT_FALSE(task_id.isEmpty());
    const QJsonObject task =
        session.CallResult(QStringLiteral("tasks.wait"), QJsonObject{{"task_id", task_id}});
    EXPECT_EQ(task.value("state").toString(), QStringLiteral("succeeded"));
    EXPECT_EQ(task.value("counts").toObject().value("total").toInt(), 0);
  }
  EXPECT_EQ(session.CallResult(QStringLiteral("library.list")).value("total").toInt(), 0);
}

TEST_F(AutomationLibraryCommandsTest, TaskWaitRejectsUnknownIdAndAmbiguousArguments) {
  InProcessAutomationSession session;
  const auto                 unknown =
      session.Call(QStringLiteral("tasks.wait"), QJsonObject{{"task_id", "import-99"}});
  ASSERT_EQ(ErrorCode(unknown), AutomationErrorCode::InvalidParams);
  EXPECT_EQ(unknown->error->data.toObject().value("pointer").toString(),
            QStringLiteral("/task_id"));
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("tasks.wait"))),
            AutomationErrorCode::InvalidParams);
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("tasks.wait"),
                                   QJsonObject{{"task_id", "import-1"}, {"idle", true}})),
            AutomationErrorCode::InvalidParams);
  // Nothing runs, so the idle wait answers at once.
  EXPECT_EQ(
      session.CallResult(QStringLiteral("tasks.wait"), QJsonObject{{"idle", true}}).value("idle"),
      QJsonValue(true));
}

TEST_F(AutomationLibraryCommandsTest, ImportRequiresExactlyOneSource) {
  InProcessAutomationSession session;
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.import"))),
            AutomationErrorCode::InvalidParams);
  EXPECT_EQ(
      ErrorCode(session.Call(QStringLiteral("library.import"),
                             QJsonObject{{"folder", Folder()}, {"paths", QJsonArray{Folder()}}})),
      AutomationErrorCode::InvalidParams);
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.list"), QJsonObject{{"limit", 0}})),
            AutomationErrorCode::InvalidParams);
}

TEST_F(AutomationLibraryCommandsTest,
       AutomationOmittedOptionInheritsPreferenceAndExplicitOptionOverridesIt) {
  InProcessAutomationSession session;
  const auto                 created =
      session.CallResult("project.create", {{"folder", Folder()}, {"name", "duplicates"}});
  ASSERT_FALSE(created.isEmpty());
  const auto       source = std::filesystem::path(__FILE__).parent_path().parent_path() /
                            "resources/raster/adobe_rgb_exif_r03_no_icc.jpg";
  const QJsonArray paths{ui::test::PathToQString(source)};
  session.host().import_behavior()->SetSkipDuplicateImports(false);
  auto import = [&](QJsonObject options) {
    options.insert("paths", paths);
    const auto id = session.CallResult("library.import", options).value("task_id");
    return session.CallResult("tasks.wait", {{"task_id", id}}).value("counts").toObject();
  };
  EXPECT_EQ(import({}).value("completed").toInt(), 1);
  const auto skipped = import({{"skip_duplicates", true}});
  EXPECT_EQ(skipped.value("duplicates").toInt(), 1);
  EXPECT_EQ(skipped.value("completed").toInt(), 0);
  EXPECT_EQ(skipped.value("failed").toInt(), 0);
  EXPECT_FALSE(session.host().import_behavior()->SkipDuplicateImports());
  EXPECT_EQ(import({}).value("completed").toInt(), 1);
  EXPECT_EQ(session.CallResult("library.list").value("total").toInt(), 2);
  const auto reopened = session.CallResult("project.open", {{"path", created.value("path")}});
  EXPECT_EQ(reopened.value("photo_count").toInt(), 2);
  EXPECT_EQ(import({{"skip_duplicates", true}}).value("duplicates").toInt(), 1);
}

TEST_F(AutomationLibraryCommandsTest, FolderRequestsKeepThePreferenceAndPerRequestOverride) {
  InProcessAutomationSession session;
  ASSERT_FALSE(
      session.CallResult("project.create", {{"folder", Folder()}, {"name", "folder_policy"}})
          .isEmpty());
  const auto input = QDir(Folder()).filePath("incoming");
  ASSERT_TRUE(QDir().mkpath(input));
  const auto fixture = std::filesystem::path(__FILE__).parent_path().parent_path() /
                       "resources/raster/adobe_rgb_exif_r03_no_icc.jpg";
  ASSERT_TRUE(QFile::copy(ui::test::PathToQString(fixture), QDir(input).filePath("image.jpg")));
  session.host().import_behavior()->SetSkipDuplicateImports(true);
  const auto first =
      session
          .CallResult("library.import",
                      {{"folder", input}, {"recursive", false}, {"skip_duplicates", false}})
          .value("task_id");
  EXPECT_EQ(session.CallResult("tasks.wait", {{"task_id", first}})
                .value("counts")
                .toObject()
                .value("completed")
                .toInt(),
            1);
  const auto second = session.CallResult("library.import", {{"folder", input}, {"recursive", true}})
                          .value("task_id");
  const auto counts =
      session.CallResult("tasks.wait", {{"task_id", second}}).value("counts").toObject();
  EXPECT_EQ(counts.value("duplicates").toInt(), 1);
  EXPECT_EQ(counts.value("failed").toInt(), 0);
  EXPECT_TRUE(session.host().import_behavior()->SkipDuplicateImports());
}

TEST_F(AutomationLibraryCommandsTest, InvalidDuplicateOverrideIsRejectedBeforeImport) {
  InProcessAutomationSession session;
  const auto                 response =
      session.Call("library.import", {{"folder", Folder()}, {"skip_duplicates", "true"}});
  EXPECT_EQ(ErrorCode(response), AutomationErrorCode::InvalidParams);
  EXPECT_FALSE(session.host().import_export()->ImportRunning());
}

TEST_F(AutomationLibraryCommandsTest, NativePickerCancellationAndFileDropUseTheCommonPreference) {
  InProcessAutomationSession session;
  ASSERT_FALSE(session.CallResult("project.create", {{"folder", Folder()}, {"name", "file_drop"}})
                   .isEmpty());
  auto* handler = session.host().import_export();
  handler->StartImport({});
  EXPECT_FALSE(handler->ImportRunning());
  session.host().import_behavior()->SetSkipDuplicateImports(true);
  const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "resources/raster/adobe_rgb_exif_r03_no_icc.jpg";
  handler->StartImport({ui::test::PathToQString(source), ui::test::PathToQString(source)});
  session.host().import_behavior()->SetSkipDuplicateImports(false);
  session.CallResult("tasks.wait", {{"idle", true}});
  EXPECT_EQ(handler->ImportTotal(), 2);
  EXPECT_EQ(handler->ImportCompleted(), 1);
  EXPECT_EQ(handler->ImportDuplicates(), 1);
  EXPECT_EQ(handler->ImportFailed(), 0);
  handler->StartImport({ui::test::PathToQString(source)});
  session.CallResult("tasks.wait", {{"idle", true}});
  EXPECT_EQ(handler->ImportCompleted(), 1);
  EXPECT_EQ(handler->ImportDuplicates(), 0);
}

TEST_F(AutomationLibraryCommandsTest, SelectionCommandsAreNotReadyInHeadlessHost) {
  InProcessAutomationSession session;
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.selection.get"))),
            AutomationErrorCode::NotReady);
  EXPECT_EQ(ErrorCode(session.Call(QStringLiteral("library.selection.set"),
                                   QJsonObject{{"element_ids", QJsonArray{1}}})),
            AutomationErrorCode::NotReady);
}

TEST_F(AutomationLibraryCommandsTest, SelectionSetReplacesGuiSelectionAndRejectsUnknownIds) {
  const QString raw_root = QStringLiteral(TEST_IMG_PATH) + QStringLiteral("/ci_rawfiles");
  InProcessAutomationSession session(QStringLiteral("gui"));
  ASSERT_FALSE(session
                   .CallResult(QStringLiteral("project.create"),
                               QJsonObject{{"folder", Folder()}, {"name", "selection_project"}})
                   .isEmpty());
  const QString task_id = session
                              .CallResult(QStringLiteral("library.import"),
                                          QJsonObject{{"folder", raw_root}, {"recursive", false}})
                              .value("task_id")
                              .toString();
  ASSERT_FALSE(task_id.isEmpty());
  session.CallResult(QStringLiteral("tasks.wait"),
                     QJsonObject{{"task_id", task_id}, {"timeout_ms", 600000}});
  const QJsonArray items =
      session.CallResult(QStringLiteral("library.list")).value("items").toArray();
  ASSERT_GE(items.size(), 2);
  const QJsonValue first  = items.at(0).toObject().value("element_id");
  const QJsonValue second = items.at(1).toObject().value("element_id");

  const QJsonArray selected =
      session
          .CallResult(QStringLiteral("library.selection.set"),
                      QJsonObject{{"element_ids", QJsonArray{second, first}}})
          .value("items")
          .toArray();
  EXPECT_EQ(selected.size(), 2);
  EXPECT_EQ(session.host().library()->selection()->SelectedCount(), 2);
  EXPECT_EQ(session.CallResult(QStringLiteral("library.selection.get")).value("items").toArray(),
            selected);

  const auto unknown = session.Call(QStringLiteral("library.selection.set"),
                                    QJsonObject{{"element_ids", QJsonArray{first, 999999}}});
  ASSERT_EQ(ErrorCode(unknown), AutomationErrorCode::InvalidParams);
  EXPECT_EQ(unknown->error->data.toObject().value("unknown_ids").toArray(), QJsonArray{999999});
  EXPECT_EQ(session.host().library()->selection()->SelectedCount(), 2);

  // A deleted photo leaves the selection.
  const QJsonObject deleted =
      session.CallResult(QStringLiteral("library.delete"),
                         QJsonObject{{"element_ids", QJsonArray{first}}, {"scope", "project"}});
  EXPECT_EQ(deleted.value("deleted_ids").toArray().size(), 1);
  EXPECT_EQ(session.host().library()->selection()->SelectedCount(), 1);
}

TEST_F(AutomationLibraryCommandsTest, DeleteOfUnknownIdOrOtherScopeDeletesNothing) {
  const QString raw_root = QStringLiteral(TEST_IMG_PATH) + QStringLiteral("/ci_rawfiles");
  InProcessAutomationSession session;
  ASSERT_FALSE(session
                   .CallResult(QStringLiteral("project.create"),
                               QJsonObject{{"folder", Folder()}, {"name", "delete_project"}})
                   .isEmpty());
  const QString task_id = session
                              .CallResult(QStringLiteral("library.import"),
                                          QJsonObject{{"folder", raw_root}, {"recursive", false}})
                              .value("task_id")
                              .toString();
  session.CallResult(QStringLiteral("tasks.wait"),
                     QJsonObject{{"task_id", task_id}, {"timeout_ms", 600000}});
  const QJsonArray items =
      session.CallResult(QStringLiteral("library.list")).value("items").toArray();
  ASSERT_FALSE(items.isEmpty());
  const QJsonValue first = items.at(0).toObject().value("element_id");

  const auto       unknown =
      session.Call(QStringLiteral("library.delete"),
                   QJsonObject{{"element_ids", QJsonArray{first, 999999}}, {"scope", "project"}});
  ASSERT_EQ(ErrorCode(unknown), AutomationErrorCode::InvalidParams);
  EXPECT_EQ(unknown->error->data.toObject().value("unknown_ids").toArray(), QJsonArray{999999});
  // The root folder is the current folder, so the album scope does not apply.
  EXPECT_EQ(
      ErrorCode(session.Call(QStringLiteral("library.delete"),
                             QJsonObject{{"element_ids", QJsonArray{first}}, {"scope", "album"}})),
      AutomationErrorCode::Rejected);
  EXPECT_EQ(session.CallResult(QStringLiteral("library.list")).value("total").toInt(),
            items.size());
}

}  // namespace
}  // namespace alcedo::automation

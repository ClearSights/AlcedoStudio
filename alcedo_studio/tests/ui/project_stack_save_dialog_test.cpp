// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QUrl>
#include <filesystem>
#include <memory>

#include "ui/alcedo_main/app_theme.hpp"
namespace alcedo::ui::test {
namespace {
TEST(ProjectStackSaveDialogTest, PendingStackSaveShowsRetryDiagnosticAndWholeProjectDiscardText) {
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(ALCEDO_QT_QML_IMPORT_PATH));
  engine.rootContext()->setContextProperty(QStringLiteral("appTheme"), &AppTheme::Instance());
  const auto    path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                       "src/ui/alcedo_main/qml/EditorCloseConfirmDialog.qml";
  QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
  std::unique_ptr<QObject> dialog(component.createWithInitialProperties(
      {{"pendingStackSave", true}, {"stackSaveError", QStringLiteral("Package disk is full")}}));
  ASSERT_TRUE(dialog) << component.errorString().toStdString();
  // Read the deferred content to instantiate production controls. This uses no synthetic input.
  ASSERT_TRUE(dialog->property("contentItem").value<QObject*>());
  auto* body = dialog->findChild<QObject*>("editorCloseConfirmBody");
  auto* save = dialog->findChild<QObject*>("editorCloseSaveButton");
  ASSERT_TRUE(body);
  ASSERT_TRUE(save);
  EXPECT_TRUE(
      body->property("text").toString().contains("all changes since the last project save"));
  auto* error = dialog->findChild<QObject*>("editorCloseStackSaveError");
  ASSERT_TRUE(error);
  EXPECT_EQ(error->property("text").toString(), "Package disk is full");
  EXPECT_EQ(save->property("text").toString(), "Retry Save");
  dialog->setProperty("pendingStackSave", false);
  EXPECT_EQ(save->property("text").toString(), "Save");
  EXPECT_TRUE(body->property("text").toString().contains("current image"));
}

}  // namespace
}  // namespace alcedo::ui::test

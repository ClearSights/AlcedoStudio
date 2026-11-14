// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTranslator>
#include <filesystem>
#include <memory>

#include "ui/alcedo_main/app_theme.hpp"

namespace alcedo::ui::test {
namespace {
TEST(PhotoStackDisclosureTest, CountAndDisclosureRemainAccessibleAtEveryClassicZoomLevel) {
  QQuickStyle::setStyle("Basic");
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(ALCEDO_QT_QML_IMPORT_PATH));
  engine.rootContext()->setContextProperty("appTheme", &AppTheme::Instance());
  const auto    path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                       "src/ui/alcedo_main/qml/PhotoStackDisclosure.qml";
  QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
  QQuickItem    parent;
  parent.setWidth(600);
  std::unique_ptr<QObject> control(
      component.createWithInitialProperties({{"parent", QVariant::fromValue(&parent)},
                                             {"occurrenceKey", "typed-key"},
                                             {"matchingCount", 2},
                                             {"queryCount", 3},
                                             {"totalCount", 8},
                                             {"temporaryCover", true}}));
  ASSERT_TRUE(control) << component.errorString().toStdString();
  auto* button = control->findChild<QObject*>("photoStackDisclosureButton");
  ASSERT_TRUE(button);
  const bool original_reduced_motion = AppTheme::Instance().reduceMotion();
  for (const bool reduced_motion : {false, true}) {
    AppTheme::Instance().setReduceMotion(reduced_motion);
    for (const int columns : {2, 3, 4, 5, 6, 8, 11, 14}) {
      parent.setWidth(560.0 / columns - 8);
      control->setProperty("compact", columns >= 8 || parent.width() < 240);
      QCoreApplication::processEvents();
      EXPECT_LE(control->property("width").toDouble(), parent.width());
      EXPECT_GE(button->property("width").toDouble(), 24);
      EXPECT_GE(button->property("height").toDouble(), 24);
      EXPECT_TRUE(control->property("actionText").toString().contains("2 matching photos"));
      EXPECT_TRUE(control->property("description").toString().contains("8 in stack"));
      EXPECT_TRUE(control->property("description").toString().contains("Temporary cover"));
    }
  }
  AppTheme::Instance().setReduceMotion(original_reduced_motion);
  QSignalSpy toggled(control.get(), SIGNAL(toggled()));
  // Invoke the declarative activation only; native pointer and keyboard delivery is manual.
  ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  EXPECT_EQ(toggled.count(), 1);
  control->setProperty("expanded", true);
  EXPECT_TRUE(control->property("actionText").toString().startsWith("Collapse"));
}

TEST(PhotoStackDisclosureTest,
     ChineseTranslationUpdatesAccessibleCountsWithoutChangingOccurrenceIdentity) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(ALCEDO_QT_QML_IMPORT_PATH));
  engine.rootContext()->setContextProperty("appTheme", &AppTheme::Instance());
  const auto repo =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
  QQmlComponent component(
      &engine,
      QUrl::fromLocalFile(QString::fromStdWString(
          (repo / "alcedo_studio/src/ui/alcedo_main/qml/PhotoStackDisclosure.qml").wstring())));
  std::unique_ptr<QObject> control(
      component.createWithInitialProperties({{"occurrenceKey", "[0,4,2,\"5\",\"uuid\",1,0]"},
                                             {"matchingCount", 2},
                                             {"queryCount", 2},
                                             {"totalCount", 8},
                                             {"expanded", true}}));
  ASSERT_TRUE(control) << component.errorString().toStdString();
  const auto  key = control->property("occurrenceKey");
  QTranslator translator;
  ASSERT_TRUE(translator.load(QString::fromUtf8(ALCEDO_STACK_ZH_CN_QM_FILE)));
  QCoreApplication::installTranslator(&translator);
  engine.retranslate();
  EXPECT_EQ(control->property("occurrenceKey"), key);
  EXPECT_TRUE(control->property("expanded").toBool());
  EXPECT_TRUE(control->property("actionText").toString().contains(QString::fromUtf8("收起")));
  EXPECT_TRUE(control->property("description").toString().contains(QString::fromUtf8("8 张")));
  QCoreApplication::removeTranslator(&translator);
}

TEST(PhotoStackDisclosureTest, BadgeShowsCountOrMatchingPositionWithCompactStatusInTooltip) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(ALCEDO_QT_QML_IMPORT_PATH));
  engine.rootContext()->setContextProperty("appTheme", &AppTheme::Instance());
  const auto    path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                       "src/ui/alcedo_main/qml/PhotoStackDisclosure.qml";
  QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
  QQuickItem    parent;
  parent.setWidth(240);
  std::unique_ptr<QObject> badge(
      component.createWithInitialProperties({{"parent", QVariant::fromValue(&parent)},
                                             {"occurrenceKey", "member"},
                                             {"matchingCount", 3},
                                             {"matchingPosition", 2},
                                             {"queryCount", 5},
                                             {"totalCount", 8}}));
  ASSERT_TRUE(badge) << component.errorString().toStdString();
  EXPECT_EQ(badge->property("badgeText").toString(), "3");
  EXPECT_TRUE(badge->property("description").toString().contains("5 in this view"));
  badge->setProperty("expanded", true);
  EXPECT_EQ(badge->property("badgeText").toString(), "2/3");
  badge->setProperty("filtered", true);
  EXPECT_EQ(badge->property("badgeText").toString(), "2/3 matched");
  auto* icon = badge->findChild<QObject*>("photoStackIcon");
  ASSERT_TRUE(icon);
  ASSERT_TRUE(QFile::exists(":/panel_icons/photo-stack.svg"));
  QCoreApplication::processEvents();
  EXPECT_EQ(icon->property("status").toInt(), 1);  // Image.Ready, not a missing resource.
  EXPECT_EQ(icon->property("color").value<QColor>(), QColor(255, 255, 255));
  parent.setWidth(58);
  badge->setProperty("compact", true);
  EXPECT_EQ(badge->property("badgeText").toString(), "2/3");
  EXPECT_TRUE(badge->property("description").toString().contains("2/3 matched"));
  EXPECT_TRUE(badge->property("description").toString().contains("8 in stack"));
  badge->setProperty("expanded", false);
  parent.setWidth(240);
  EXPECT_EQ(badge->property("badgeText").toString(), "3 matched");
  badge->setProperty("filtered", false);
  EXPECT_EQ(badge->property("badgeText").toString(), "3");
  EXPECT_FALSE(badge->property("description").toString().contains("matched"));
}

TEST(PhotoStackDisclosureTest, StackSubmenuHasReviewOrderAndNoTopLevelStackCommands) {
  QQmlEngine engine;
  engine.addImportPath(QStringLiteral(ALCEDO_QT_QML_IMPORT_PATH));
  engine.rootContext()->setContextProperty("appTheme", &AppTheme::Instance());
  // Only the scalar inputs required by the production menu components; no member-list cache.
  QQmlComponent inputs(&engine);
  inputs.setData(R"(
import QtQuick
QtObject {
    property QtObject photoStacks: QtObject {
        property bool busy: false
        property var menuOptions: ({group:true,add:true,remove:true,earlier:true,later:true,split:true})
    }
    property QtObject library: QtObject {
        property bool queryUpdating: false
        property bool hasActiveFilter: false
        property QtObject displayModel: QtObject {
            signal pagesChanged()
            function rowByKey(key) { return 0 }
            function getStackAt(row) {
                return {stackId:"uuid", fileId:2, savedCoverId:1, stackTotal:4, fragmentMatchCount:4}
            }
        }
    }
    property QtObject interactionPolicy: QtObject { property bool canEditPhotoStacks: true }
})",
                 QUrl());
  std::unique_ptr<QObject> app(inputs.create());
  ASSERT_TRUE(app) << inputs.errorString().toStdString();
  engine.rootContext()->setContextProperty("appModules", app.get());
  const auto directory = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                         "src/ui/alcedo_main/qml";
  QQmlComponent actions_component(&engine, QUrl::fromLocalFile(QString::fromStdWString(
                                               (directory / "PhotoStackActions.qml").wstring())));
  std::unique_ptr<QObject> actions(
      actions_component.createWithInitialProperties({{"occurrenceKey", "clicked-member"}}));
  ASSERT_TRUE(actions) << actions_component.errorString().toStdString();
  engine.rootContext()->setContextProperty("stackActionsUnderTest", actions.get());
  QQuickWindow window;
  engine.rootContext()->setContextProperty("menuParentUnderTest", window.contentItem());
  QQmlComponent menu_component(&engine);
  menu_component.setData(
      R"(
import QtQuick
import "."
ImageContextMenu {
    parent: menuParentUnderTest
    stackMenuActions: stackActionsUnderTest.actions
    actions: [{id:"copy-adjustments",label:"Copy Adjustments"},{id:"delete",label:"Delete"}]
})",
      QUrl::fromLocalFile(QString::fromStdWString((directory / "menu_test.qml").wstring())));
  std::unique_ptr<QObject> menu(menu_component.create());
  ASSERT_TRUE(menu) << menu_component.errorString().toStdString();
  QCoreApplication::processEvents();
  auto* stack = menu->findChild<QObject*>("imageContextStackMenu");
  ASSERT_TRUE(stack);
  EXPECT_FALSE(stack->property("visible").toBool());
  EXPECT_FALSE(menu->property("visible").toBool());
  EXPECT_EQ(menu->property("count").toInt(), 6);  // Rating, Add to, Stack, separator, two actions.
  EXPECT_EQ(stack->property("count").toInt(), 10);
  EXPECT_FALSE(menu->findChild<QObject*>("imageContextAction_stack-select-matching"));
  EXPECT_FALSE(menu->findChild<QObject*>("imageContextAction_stack-select-entire"));
  const QStringList labels{"Group into Stack",      "Ungroup Stack",
                           "Add Selected to Stack", "Remove from Stack",
                           "Set as Stack Cover",    "Move Up in Stack",
                           "Move Down in Stack",    "Split Stack",
                           "Expand/Collapse Stack", "Expand/Collapse All Stacks"};
  for (int i = 0; i < labels.size(); ++i) {
    QQuickItem* item = nullptr;
    ASSERT_TRUE(
        QMetaObject::invokeMethod(stack, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, i)));
    ASSERT_TRUE(item);
    EXPECT_EQ(item->property("text").toString(), labels[i]);
    EXPECT_GE(stack->property("width").toDouble(), item->implicitWidth());
    auto* content = item->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(content);
    bool found_caption = false;
    for (auto* child : content->childItems()) {
      if (child->property("text").toString() != labels[i]) continue;
      EXPECT_EQ(child->property("wrapMode").toInt(), 0);  // Text.NoWrap.
      found_caption = true;
    }
    EXPECT_TRUE(found_caption);
  }
  auto* library = app->property("library").value<QObject*>();
  ASSERT_TRUE(library);
  library->setProperty("hasActiveFilter", true);
  QQuickItem* all = nullptr;
  ASSERT_TRUE(
      QMetaObject::invokeMethod(stack, "itemAt", Q_RETURN_ARG(QQuickItem*, all), Q_ARG(int, 9)));
  ASSERT_TRUE(all);
  EXPECT_EQ(all->property("text").toString(), "Expand/Collapse All Matched Stacks");
  EXPECT_GE(stack->property("width").toDouble(), all->implicitWidth());
  QQuickItem* split = nullptr;
  ASSERT_TRUE(
      QMetaObject::invokeMethod(stack, "itemAt", Q_RETURN_ARG(QQuickItem*, split), Q_ARG(int, 7)));
  ASSERT_TRUE(split);
  QSignalSpy requested(menu.get(), SIGNAL(actionRequested(QString)));
  ASSERT_TRUE(QMetaObject::invokeMethod(split, "triggered"));
  ASSERT_EQ(requested.count(), 1);
  EXPECT_EQ(requested.front().front().toString(), "stack-split");
  actions->setProperty("active", false);
  QCoreApplication::processEvents();
  EXPECT_EQ(menu->property("count").toInt(), 5);  // No Stack entry outside the Library.
}
}  // namespace
}  // namespace alcedo::ui::test

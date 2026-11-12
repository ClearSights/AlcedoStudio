// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
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
}  // namespace
}  // namespace alcedo::ui::test

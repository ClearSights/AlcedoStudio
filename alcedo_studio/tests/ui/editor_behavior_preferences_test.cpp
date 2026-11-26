//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/alcedo_main/album_backend/editor_behavior_preferences.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace alcedo::ui::test {
namespace {

class EditorBehaviorPreferencesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(settings_dir_.isValid());
    QCoreApplication::setOrganizationName("PuerhLabTest");
    QCoreApplication::setApplicationName("EditorBehaviorPreferencesTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir_.path());
    QSettings settings;
    settings.clear();
    settings.sync();
  }

  QTemporaryDir settings_dir_;
};

TEST_F(EditorBehaviorPreferencesTest, LockedNodeMaskActionDefaultsToAsk) {
  EditorBehaviorPreferences preferences;
  EXPECT_EQ(preferences.locked_node_mask_action(), QStringLiteral("ask"));
}

TEST_F(EditorBehaviorPreferencesTest, LockedNodeMaskActionPersistsAcrossInstances) {
  EditorBehaviorPreferences first;
  QSignalSpy changed(&first, &EditorBehaviorPreferences::lockedNodeMaskActionChanged);

  EXPECT_TRUE(first.setLockedNodeMaskAction(QStringLiteral("newLayer")));
  EXPECT_EQ(first.locked_node_mask_action(), QStringLiteral("newLayer"));
  EXPECT_EQ(changed.count(), 1);
  // Re-setting the same value is accepted without another notification.
  EXPECT_TRUE(first.setLockedNodeMaskAction(QStringLiteral("newLayer")));
  EXPECT_EQ(changed.count(), 1);

  EditorBehaviorPreferences second;
  EXPECT_EQ(second.locked_node_mask_action(), QStringLiteral("newLayer"));
  EXPECT_EQ(QSettings{}.value("editor/lockedNodeMaskAction").toString(),
            QStringLiteral("newLayer"));
}

TEST_F(EditorBehaviorPreferencesTest, UnknownLockedNodeMaskActionIsRejectedWithoutChange) {
  EditorBehaviorPreferences preferences;
  ASSERT_TRUE(preferences.setLockedNodeMaskAction(QStringLiteral("currentNode")));
  QSignalSpy changed(&preferences, &EditorBehaviorPreferences::lockedNodeMaskActionChanged);

  EXPECT_FALSE(preferences.setLockedNodeMaskAction(QStringLiteral("cancel")));
  EXPECT_EQ(preferences.locked_node_mask_action(), QStringLiteral("currentNode"));
  EXPECT_EQ(changed.count(), 0);
  EXPECT_EQ(QSettings{}.value("editor/lockedNodeMaskAction").toString(),
            QStringLiteral("currentNode"));
}

TEST_F(EditorBehaviorPreferencesTest, UnknownStoredLockedNodeMaskActionReadsAsAsk) {
  QSettings{}.setValue("editor/lockedNodeMaskAction", QStringLiteral("legacyValue"));
  EditorBehaviorPreferences preferences;
  EXPECT_EQ(preferences.locked_node_mask_action(), QStringLiteral("ask"));
}

TEST_F(EditorBehaviorPreferencesTest, GuideSelectionCyclesAndEachOrientationPersists) {
  const auto                index = [](auto value) { return static_cast<int>(value); };
  EditorBehaviorPreferences preferences;
  EXPECT_EQ(preferences.cropGuideIndex(), index(CropGuide::Thirds));
  EXPECT_EQ(preferences.cropGuideVisibility(), index(CropGuideVisibility::Always));
  EXPECT_EQ(preferences.cropGuideOrientation(), -1);
  QSignalSpy changed(&preferences, &EditorBehaviorPreferences::cropGuideChanged);
  preferences.rotateCropGuide();
  EXPECT_EQ(changed.count(), 0);
  ASSERT_TRUE(preferences.selectCropGuideVisibility(index(CropGuideVisibility::DuringInput)));
  for (const auto guide :
       {CropGuide::Grid, CropGuide::Ratio, CropGuide::Spiral, CropGuide::Diagonal,
        CropGuide::Triangle, CropGuide::None, CropGuide::Thirds}) {
    preferences.cycleCropGuide();
    EXPECT_EQ(preferences.cropGuideIndex(), index(guide));
    EXPECT_EQ(preferences.cropGuideVisibility(), index(CropGuideVisibility::DuringInput));
  }
  ASSERT_TRUE(preferences.selectCropGuide(index(CropGuide::Spiral)));
  for (const int orientation : {1, 2, 3, 0, 1, 2}) {
    preferences.rotateCropGuide();
    EXPECT_EQ(preferences.cropGuideOrientation(), orientation);
  }
  ASSERT_TRUE(preferences.selectCropGuide(index(CropGuide::Triangle)));
  preferences.rotateCropGuide();
  EXPECT_EQ(preferences.cropGuideOrientation(), 1);

  EditorBehaviorPreferences reopened;
  EXPECT_EQ(reopened.cropGuideIndex(), index(CropGuide::Triangle));
  EXPECT_EQ(reopened.cropGuideVisibility(), index(CropGuideVisibility::DuringInput));
  EXPECT_EQ(reopened.cropGuideOrientation(), 1);
  reopened.rotateCropGuide();
  EXPECT_EQ(reopened.cropGuideOrientation(), 0);
  ASSERT_TRUE(reopened.selectCropGuide(index(CropGuide::Spiral)));
  EXPECT_EQ(reopened.cropGuideOrientation(), 2);
}

TEST_F(EditorBehaviorPreferencesTest, InvalidGuidePreferenceRequiresExplicitReset) {
  const QLatin1String key(EditorBehaviorPreferences::kCropGuideSettingsKey);
  const QVariant      invalid_values[] = {
      QStringLiteral("unreadable"),          QVariantMap{{"guide", 9}},
      QVariantMap{{"visibility", -2}},       QVariantMap{{"triangleOrientation", 2}},
      QVariantMap{{"spiralOrientation", 4}}, QVariantMap{{"guide", QStringLiteral("1")}},
  };
  for (const auto& invalid : invalid_values) {
    QSettings{}.setValue(key, invalid);
    EditorBehaviorPreferences preferences;
    QSignalSpy                changed(&preferences, &EditorBehaviorPreferences::cropGuideChanged);
    EXPECT_EQ(preferences.cropGuideIndex(), -1);
    EXPECT_EQ(preferences.cropGuideVisibility(), -1);
    EXPECT_EQ(preferences.cropGuideOrientation(), -1);
    EXPECT_FALSE(preferences.selectCropGuide(0));
    EXPECT_FALSE(preferences.selectCropGuideVisibility(0));
    preferences.cycleCropGuide();
    preferences.rotateCropGuide();
    EXPECT_EQ(changed.count(), 0);
    EXPECT_EQ(QSettings{}.value(key), invalid);

    preferences.resetCropGuide();
    EXPECT_EQ(changed.count(), 1);
    EditorBehaviorPreferences reopened;
    EXPECT_EQ(reopened.cropGuideIndex(), static_cast<int>(CropGuide::Thirds));
    EXPECT_EQ(reopened.cropGuideVisibility(), static_cast<int>(CropGuideVisibility::Always));
    ASSERT_TRUE(reopened.selectCropGuide(static_cast<int>(CropGuide::Triangle)));
    EXPECT_EQ(reopened.cropGuideOrientation(), 0);
    ASSERT_TRUE(reopened.selectCropGuide(static_cast<int>(CropGuide::Spiral)));
    EXPECT_EQ(reopened.cropGuideOrientation(), 0);
  }
}

/// The `odt` Model JSON of a settled panel write with method film_print and @p film_print.
auto FilmPrintWrite(const QString& film_print) -> QString {
  return QStringLiteral(R"({"odt":{"method":"film_print","encoding_space":"rec709",)"
                        R"("encoding_eotf":"gamma_2_2","peak_luminance":100,"film_print":%1}})")
      .arg(film_print);
}

TEST_F(EditorBehaviorPreferencesTest, FilmPrintSettingsAreEmptyBeforeAFilmPrintWrite) {
  EditorBehaviorPreferences preferences;
  EXPECT_TRUE(preferences.film_print_ui_value().isEmpty());
}

TEST_F(EditorBehaviorPreferencesTest, RememberedFilmPrintSettingsPersistAcrossInstances) {
  EditorBehaviorPreferences first;
  QSignalSpy changed(&first, &EditorBehaviorPreferences::filmPrintUiValueChanged);
  const auto write = FilmPrintWrite(QStringLiteral(
      R"({"negative":"kodak_portra_400","print_material":"negative_scan",)"
      R"("scene_light":"daylight","halation":2.5,"auto":"warm","scan_contrast":1.25})"));

  ASSERT_TRUE(first.rememberFilmPrint(write));
  EXPECT_EQ(changed.count(), 1);
  // The same settings again are accepted without another notification.
  EXPECT_TRUE(first.rememberFilmPrint(write));
  EXPECT_EQ(changed.count(), 1);

  // A new instance reads the stored settings: the written fields, and the defaults of the others.
  EditorBehaviorPreferences second;
  const auto                ui = second.film_print_ui_value();
  EXPECT_EQ(ui.value("method").toString(), QStringLiteral("film_print"));
  EXPECT_EQ(ui.value("negative").toString(), QStringLiteral("kodak_portra_400"));
  EXPECT_EQ(ui.value("print_material").toString(), QStringLiteral("negative_scan"));
  EXPECT_EQ(ui.value("scene_light").toString(), QStringLiteral("daylight"));
  EXPECT_DOUBLE_EQ(ui.value("halation").toDouble(), 2.5);
  EXPECT_DOUBLE_EQ(ui.value("light_spread").toDouble(), 1.0);
  EXPECT_EQ(ui.value("auto").toString(), QStringLiteral("warm"));
  EXPECT_DOUBLE_EQ(ui.value("scan_contrast").toDouble(), 1.25);
  EXPECT_EQ(ui.value("scanner_light").toString(), QStringLiteral("halogen"));
  EXPECT_TRUE(QSettings{}.contains("editor/filmPrint/lastSettings"));
}

TEST_F(EditorBehaviorPreferencesTest, WriteOfAnotherMethodOrAnInvalidFilmPrintChangesNothing) {
  EditorBehaviorPreferences preferences;
  ASSERT_TRUE(preferences.rememberFilmPrint(
      FilmPrintWrite(QStringLiteral(R"({"negative":"kodak_5219","scene_light":"tungsten"})"))));
  const auto stored = QSettings{}.value("editor/filmPrint/lastSettings");
  QSignalSpy changed(&preferences, &EditorBehaviorPreferences::filmPrintUiValueChanged);

  EXPECT_FALSE(preferences.rememberFilmPrint(
      QStringLiteral(R"({"odt":{"method":"open_drt","encoding_space":"rec709"}})")));
  EXPECT_FALSE(preferences.rememberFilmPrint(
      FilmPrintWrite(QStringLiteral(R"({"negative":"no_such_film"})"))));
  EXPECT_FALSE(
      preferences.rememberFilmPrint(FilmPrintWrite(QStringLiteral(R"({"halation":99})"))));
  EXPECT_FALSE(preferences.rememberFilmPrint(QStringLiteral("not json")));

  EXPECT_EQ(changed.count(), 0);
  EXPECT_EQ(preferences.film_print_ui_value().value("negative").toString(),
            QStringLiteral("kodak_5219"));
  EXPECT_EQ(QSettings{}.value("editor/filmPrint/lastSettings"), stored);
}

TEST_F(EditorBehaviorPreferencesTest, StoredFilmPrintSettingsTheModelDoesNotLoadReadAsNone) {
  QSettings{}.setValue("editor/filmPrint/lastSettings", QStringLiteral(R"({"negative":"gone"})"));
  EXPECT_TRUE(EditorBehaviorPreferences{}.film_print_ui_value().isEmpty());
  QSettings{}.setValue("editor/filmPrint/lastSettings", QStringLiteral("{not json"));
  EXPECT_TRUE(EditorBehaviorPreferences{}.film_print_ui_value().isEmpty());
}

}  // namespace
}  // namespace alcedo::ui::test

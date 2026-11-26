//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/alcedo_main/album_backend/editor_behavior_preferences.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QMetaType>
#include <QSettings>
#include <exception>
#include <json.hpp>
#include <optional>

#include "app/editor_parameter_catalog.hpp"
#include "edit/graph/drt_node_model.hpp"

namespace alcedo::ui {
namespace {

constexpr auto kFilmPrintMethod = "film_print";

/// The `film_print` object @p film_print in the form of DrtParamsModel::ToJson, read through the
/// load of the Model. Nullopt when the Model does not load it.
auto CanonicalFilmPrint(const nlohmann::json& film_print) -> std::optional<nlohmann::json> {
  if (!film_print.is_object()) {
    return std::nullopt;
  }
  DrtParamsModel model;
  try {
    model.LoadJson({{"method", kFilmPrintMethod}, {"film_print", film_print}});
  } catch (const std::exception&) {
    return std::nullopt;
  }
  const auto json = model.ToJson();
  const auto it   = json.find("film_print");
  if (it == json.end()) {
    return std::nullopt;
  }
  return *it;
}

/// The `odt` UI value of method `film_print` with the canonical object @p film_print. Empty when
/// the catalog does not read it.
auto FilmPrintUiValue(const nlohmann::json& film_print) -> QVariantMap {
  std::string error;
  const auto  ui = EditorParameterCatalog::ToUiValue(
      "odt", {{"odt", {{"method", kFilmPrintMethod}, {"film_print", film_print}}}}, &error);
  if (!ui.has_value()) {
    return {};
  }
  return QJsonDocument::fromJson(QByteArray::fromStdString(ui->dump())).object().toVariantMap();
}

}  // namespace

EditorBehaviorPreferences::EditorBehaviorPreferences(QObject* parent) : QObject(parent) {
  const QSettings settings;
  crop_guide_ = ReadCropGuide(settings.value(QLatin1String(kCropGuideSettingsKey), QVariantMap{}));
  const QString   stored =
      settings.value(QLatin1String(kLockedNodeMaskActionSettingKey), QLatin1String(kAskAction))
          .toString();
  locked_node_mask_action_ =
      IsLockedNodeMaskAction(stored) ? stored : QString::fromLatin1(kAskAction);

  const auto film_text =
      settings.value(QLatin1String(kFilmPrintSettingsKey)).toString().toStdString();
  if (!film_text.empty()) {
    const auto film_print =
        CanonicalFilmPrint(nlohmann::json::parse(film_text, nullptr, /*allow_exceptions=*/false));
    if (film_print.has_value()) {
      film_print_json_     = film_print->dump();
      film_print_ui_value_ = FilmPrintUiValue(*film_print);
    }
  }
}

auto EditorBehaviorPreferences::ReadCropGuide(const QVariant& value)
    -> std::optional<CropGuideSettings> {
  if (value.metaType().id() != QMetaType::QVariantMap) {
    return std::nullopt;
  }
  const auto fields = value.toMap();
  const auto index  = [&fields](const char* key, int default_value, int maximum) {
    const auto stored = fields.value(QLatin1String(key), default_value);
    if (stored.metaType().id() != QMetaType::Int) {
      return -1;
    }
    const int number = stored.toInt();
    return number >= 0 && number <= maximum ? number : -1;
  };
  const int guide =
      index("guide", static_cast<int>(CropGuide::Thirds), static_cast<int>(CropGuide::Triangle));
  const int visibility = index("visibility", static_cast<int>(CropGuideVisibility::Always),
                               static_cast<int>(CropGuideVisibility::Never));
  const int triangle =
      index("triangleOrientation", 0, CropGuideOrientationCount(CropGuide::Triangle) - 1);
  const int spiral =
      index("spiralOrientation", 0, CropGuideOrientationCount(CropGuide::Spiral) - 1);
  if (guide < 0 || visibility < 0 || triangle < 0 || spiral < 0) {
    return std::nullopt;
  }
  return CropGuideSettings{static_cast<CropGuide>(guide),
                           static_cast<CropGuideVisibility>(visibility), triangle, spiral};
}

void EditorBehaviorPreferences::saveCropGuide() {
  const auto& settings = *crop_guide_;
  QSettings{}.setValue(
      QLatin1String(kCropGuideSettingsKey),
      QVariantMap{{QStringLiteral("guide"), static_cast<int>(settings.guide)},
                  {QStringLiteral("visibility"), static_cast<int>(settings.visibility)},
                  {QStringLiteral("triangleOrientation"), settings.triangle_orientation},
                  {QStringLiteral("spiralOrientation"), settings.spiral_orientation}});
  emit cropGuideChanged();
}

auto EditorBehaviorPreferences::cropGuideIndex() const -> int {
  return crop_guide_ ? static_cast<int>(crop_guide_->guide) : -1;
}

auto EditorBehaviorPreferences::cropGuideVisibility() const -> int {
  return crop_guide_ ? static_cast<int>(crop_guide_->visibility) : -1;
}

auto EditorBehaviorPreferences::cropGuideOrientation() const -> int {
  if (crop_guide_ && crop_guide_->guide == CropGuide::Triangle) {
    return crop_guide_->triangle_orientation;
  }
  if (crop_guide_ && crop_guide_->guide == CropGuide::Spiral) {
    return crop_guide_->spiral_orientation;
  }
  return -1;
}

bool EditorBehaviorPreferences::selectCropGuide(int index) {
  if (!crop_guide_ || index < 0 || index > static_cast<int>(CropGuide::Triangle)) {
    return false;
  }
  if (index != cropGuideIndex()) {
    crop_guide_->guide = static_cast<CropGuide>(index);
    saveCropGuide();
  }
  return true;
}

bool EditorBehaviorPreferences::selectCropGuideVisibility(int index) {
  if (!crop_guide_ || index < 0 || index > static_cast<int>(CropGuideVisibility::Never)) {
    return false;
  }
  if (index != cropGuideVisibility()) {
    crop_guide_->visibility = static_cast<CropGuideVisibility>(index);
    saveCropGuide();
  }
  return true;
}

void EditorBehaviorPreferences::cycleCropGuide() {
  if (crop_guide_) {
    selectCropGuide((cropGuideIndex() + 1) % (static_cast<int>(CropGuide::Triangle) + 1));
  }
}

void EditorBehaviorPreferences::rotateCropGuide() {
  if (cropGuideOrientation() < 0) {
    return;
  }
  auto& settings    = *crop_guide_;
  int&  orientation = settings.guide == CropGuide::Triangle ? settings.triangle_orientation
                                                            : settings.spiral_orientation;
  orientation       = (orientation + 1) % CropGuideOrientationCount(settings.guide);
  saveCropGuide();
}

void EditorBehaviorPreferences::resetCropGuide() {
  crop_guide_.emplace();
  saveCropGuide();
}

auto EditorBehaviorPreferences::IsLockedNodeMaskAction(const QString& action) -> bool {
  return action == QLatin1String(kAskAction) || action == QLatin1String(kNewLayerAction) ||
         action == QLatin1String(kCurrentNodeAction);
}

bool EditorBehaviorPreferences::setLockedNodeMaskAction(const QString& action) {
  if (!IsLockedNodeMaskAction(action)) {
    return false;
  }
  if (action == locked_node_mask_action_) {
    return true;
  }
  locked_node_mask_action_ = action;
  QSettings{}.setValue(QLatin1String(kLockedNodeMaskActionSettingKey), locked_node_mask_action_);
  emit lockedNodeMaskActionChanged();
  return true;
}

bool EditorBehaviorPreferences::rememberFilmPrint(const QString& odtModelJson) {
  const auto parsed =
      nlohmann::json::parse(odtModelJson.toStdString(), nullptr, /*allow_exceptions=*/false);
  if (!parsed.is_object()) {
    return false;
  }
  const auto  wrapped = parsed.find("odt");
  const auto& odt     = wrapped != parsed.end() ? *wrapped : parsed;
  if (!odt.is_object()) {
    return false;
  }
  const auto method = odt.find("method");
  if (method == odt.end() || *method != kFilmPrintMethod || !odt.contains("film_print")) {
    return false;
  }
  const auto film_print = CanonicalFilmPrint(odt.at("film_print"));
  if (!film_print.has_value()) {
    return false;
  }
  auto text = film_print->dump();
  if (text == film_print_json_) {
    return true;
  }
  auto ui_value = FilmPrintUiValue(*film_print);
  if (ui_value.isEmpty()) {
    return false;
  }
  QSettings{}.setValue(QLatin1String(kFilmPrintSettingsKey), QString::fromStdString(text));
  film_print_json_     = std::move(text);
  film_print_ui_value_ = std::move(ui_value);
  emit filmPrintUiValueChanged();
  return true;
}

}  // namespace alcedo::ui

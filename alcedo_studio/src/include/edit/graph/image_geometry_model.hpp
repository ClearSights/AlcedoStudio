//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "edit/geometry/frame_orientation.hpp"
#include "edit/geometry/types.hpp"
#include "json.hpp"

namespace alcedo {

/**
 * @brief Width and height of a crop aspect ratio in stored axes. Only the quotient is meaningful.
 */
struct CropAspectRatio {
  float width  = 1.0f;
  float height = 1.0f;

  friend auto operator==(const CropAspectRatio&, const CropAspectRatio&) -> bool = default;
};

/**
 * @brief Focused document geometry update. Omitted fields retain current values.
 */
struct ImageGeometryUpdate {
  std::optional<NormalizedRect>  crop_rect;
  std::optional<float>           rotation_degrees;
  std::optional<std::string>     aspect_preset;
  std::optional<CropAspectRatio> aspect_ratio;
  std::optional<FrameOrientation> orientation;
  // Present on versioned editor writes and history values, outside stored geometry JSON.
  std::optional<std::uint32_t>    document_format_version;
};

/**
 * @brief Document-level crop and rotation. Not a user-visible graph node.
 *
 * The output is the crop frame: an axis-aligned rectangle of `w * W` by `h * H` source pixels
 * centered at `(x + w / 2, y + h / 2)`, through which the source is seen rotated by
 * @ref RotationDegrees about that center. Renders constrain the frame with
 * ClampCropToRotatedSource so no corner leaves the source. @ref AspectPreset and
 * @ref AspectRatio record the Geometry panel's aspect constraint so it is restored with the
 * document; they do not change the render.
 *
 * Viewport ROI and dynamic resolution are render-request data and are not stored.
 */
class ImageGeometryModel {
 public:
  static constexpr const char* kFreeAspectPreset = "free";
  static constexpr const char* kOriginalAspectPreset = "original";

  ImageGeometryModel() = default;

  [[nodiscard]] auto CropRect() const -> NormalizedRect { return crop_rect_; }
  [[nodiscard]] auto RotationDegrees() const -> float { return rotation_degrees_; }
  [[nodiscard]] auto AspectPreset() const -> const std::string& { return aspect_preset_; }
  [[nodiscard]] auto AspectRatio() const -> CropAspectRatio { return aspect_ratio_; }
  [[nodiscard]] auto Orientation() const -> FrameOrientation { return orientation_; }

  void               SetCropRect(NormalizedRect rect) { crop_rect_ = rect; }
  void               SetRotationDegrees(float degrees) { rotation_degrees_ = degrees; }

  /// Turn the framed image in displayed axes without changing crop or straightening.
  void               RotateImageQuarterTurn(QuarterTurnDirection direction, Extent2D source) {
    if (source.Empty()) {
      throw std::invalid_argument("Image rotation requires source dimensions");
    }
    orientation_ = TurnFrameOrientation(orientation_, direction);
  }

  /// Mirror the framed image in displayed axes. Reject a missing source extent or invalid axis
  /// before changing the orientation. Preserve the stored crop and straightening.
  void FlipImage(ImageFlipAxis axis, Extent2D source) {
    if (source.Empty()) {
      throw std::invalid_argument("Image flip requires source dimensions");
    }
    orientation_ = FlipFrameOrientation(orientation_, axis);
  }

  /// Reset all Crop and Rotate fields together. Other document settings remain intact.
  void ResetCropRotate() {
    ApplyUpdate(
        {NormalizedRect{}, 0.0f, kFreeAspectPreset, CropAspectRatio{}, FrameOrientation::Identity});
  }

  /**
   * @brief Apply geometry fields together so a validated patch cannot expose partial state.
   */
  void ApplyUpdate(ImageGeometryUpdate update) {
    const auto& preset = update.aspect_preset ? *update.aspect_preset : aspect_preset_;
    if (preset == kOriginalAspectPreset) {
      ValidateOriginalCrop(update.crop_rect.value_or(crop_rect_),
                           update.aspect_ratio.value_or(aspect_ratio_));
    }
    if (update.orientation) {
      static_cast<void>(FrameOrientationId(*update.orientation));
    }
    if (update.crop_rect.has_value()) {
      crop_rect_ = *update.crop_rect;
    }
    if (update.rotation_degrees.has_value()) {
      rotation_degrees_ = *update.rotation_degrees;
    }
    if (update.aspect_preset.has_value()) {
      aspect_preset_ = std::move(*update.aspect_preset);
    }
    if (update.aspect_ratio.has_value()) {
      aspect_ratio_ = *update.aspect_ratio;
    }
    if (update.orientation) {
      orientation_ = *update.orientation;
    }
  }

  /// Write the saved geometry. Revision 7 has no orientation field.
  [[nodiscard]] auto ToJson(bool include_orientation = true) const -> nlohmann::json {
    nlohmann::json json{
        {"crop_rect",
         nlohmann::json::array({crop_rect_.x, crop_rect_.y, crop_rect_.w, crop_rect_.h})},
        {"rotation_degrees", rotation_degrees_},
        {"aspect_preset", aspect_preset_},
        {"aspect_ratio", nlohmann::json::array({aspect_ratio_.width, aspect_ratio_.height})}};
    if (include_orientation) {
      json["orientation"] = FrameOrientationId(orientation_);
    }
    return json;
  }

  /**
   * @brief Reads a stored model. Keys written by earlier versions (such as `expand_to_fit`) are
   *        ignored; missing aspect fields keep their defaults.
   */
  static auto FromJson(const nlohmann::json& json) -> ImageGeometryModel {
    ImageGeometryModel model;
    if (json.contains("crop_rect") && json["crop_rect"].is_array() &&
        json["crop_rect"].size() >= 4) {
      model.crop_rect_.x = json["crop_rect"][0].get<float>();
      model.crop_rect_.y = json["crop_rect"][1].get<float>();
      model.crop_rect_.w = json["crop_rect"][2].get<float>();
      model.crop_rect_.h = json["crop_rect"][3].get<float>();
    }
    if (json.contains("rotation_degrees") && json["rotation_degrees"].is_number()) {
      model.rotation_degrees_ = json["rotation_degrees"].get<float>();
    }
    if (json.contains("aspect_preset") && json["aspect_preset"].is_string()) {
      model.aspect_preset_ = json["aspect_preset"].get<std::string>();
    }
    if (json.contains("aspect_ratio") && json["aspect_ratio"].is_array() &&
        json["aspect_ratio"].size() >= 2) {
      model.aspect_ratio_.width  = json["aspect_ratio"][0].get<float>();
      model.aspect_ratio_.height = json["aspect_ratio"][1].get<float>();
    }
    if (json.contains("orientation")) {
      model.orientation_ =
          ParseFrameOrientation(json.at("orientation").get_ref<const std::string&>());
    }
    if (model.aspect_preset_ == kOriginalAspectPreset) {
      const auto rect  = json.find("crop_rect");
      const auto ratio = json.find("aspect_ratio");
      if (rect == json.end() || !rect->is_array() || rect->size() != 4 || ratio == json.end() ||
          !ratio->is_array() || ratio->size() != 2) {
        throw std::invalid_argument("Original crop requires its stored frame and aspect ratio");
      }
      ValidateOriginalCrop(model.crop_rect_, model.aspect_ratio_);
    }
    return model;
  }

 private:
  /// Verify the stored frame and ratio. Source coverage is checked when the frame is resolved.
  static void ValidateOriginalCrop(NormalizedRect rect, CropAspectRatio ratio) {
    const auto center_x = rect.x + 0.5f * rect.w;
    const auto center_y = rect.y + 0.5f * rect.h;
    if (!(rect.w >= kGeometryMinNormalizedSize && rect.w <= 1.0f &&
          rect.h >= kGeometryMinNormalizedSize && rect.h <= 1.0f && center_x >= 0.0f &&
          center_x <= 1.0f && center_y >= 0.0f && center_y <= 1.0f && std::isfinite(ratio.width) &&
          ratio.width > 0.0f && std::isfinite(ratio.height) && ratio.height > 0.0f)) {
      throw std::invalid_argument("Invalid Original crop frame or aspect ratio");
    }
  }

  NormalizedRect  crop_rect_{};
  float           rotation_degrees_ = 0.0f;
  std::string     aspect_preset_    = kFreeAspectPreset;
  CropAspectRatio aspect_ratio_{};
  FrameOrientation orientation_ = FrameOrientation::Identity;
};

}  // namespace alcedo

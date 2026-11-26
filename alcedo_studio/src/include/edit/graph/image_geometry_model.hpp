//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "edit/geometry/render_request.hpp"
#include "edit/geometry/types.hpp"
#include "json.hpp"

namespace alcedo {

/**
 * @brief Width and height of a custom crop aspect ratio. Only the quotient is meaningful.
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
  std::optional<double>          horizontal;
  std::optional<double>          vertical;
  std::optional<double>          aspect;
  std::optional<double>          scale;
  std::optional<double>          offset_x;
  std::optional<double>          offset_y;
  std::optional<bool>            constrain_crop;
};

/// The image-geometry fields that a single reset operation restores.
enum class GeometryResetScope : std::uint8_t { Transform, CropAndRotate, All };

/**
 * @brief Document-level crop, straightening, and manual transforms in source reference axes.
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

  ImageGeometryModel() = default;

  [[nodiscard]] auto CropRect() const -> NormalizedRect { return render_params_.crop_rect; }
  [[nodiscard]] auto RotationDegrees() const -> float { return render_params_.rotation_degrees; }
  [[nodiscard]] auto AspectPreset() const -> const std::string& { return aspect_preset_; }
  [[nodiscard]] auto AspectRatio() const -> CropAspectRatio { return aspect_ratio_; }
  [[nodiscard]] auto RenderParams() const -> const ImageGeometryParams& { return render_params_; }
  /// True if the stored transform fields require the manual-transform document revision.
  [[nodiscard]] auto HasManualTransformSettings() const -> bool;

  void               SetCropRect(NormalizedRect rect) { render_params_.crop_rect = rect; }
  void SetRotationDegrees(float degrees) { render_params_.rotation_degrees = degrees; }

  /**
   * @brief Restore accepted geometry, such as history values, without refitting the stored crop.
   * @throws std::exception if a manual transform value is invalid. No state changes on failure.
   */
  void ApplyUpdate(const ImageGeometryUpdate& update);

  /// Validate a live change against the source and store the accepted crop with it.
  /// The source extent is required for a transform change or a crop with active transforms.
  /// During a transform drag, @p requested_crop is the framing at the start of the edit.
  /// Throws on invalid geometry before changing any field.
  void ApplyResolvedUpdate(const ImageGeometryUpdate& update, Extent2D source,
                           std::optional<NormalizedRect> requested_crop = std::nullopt);

  /// Restore the selected fields and resolve any required crop adjustment before publication.
  /// Requires the source extent. Throws on invalid geometry without changing the Model.
  void Reset(GeometryResetScope scope, Extent2D source);

  /// Encode either the current geometry or the crop-only revision's canonical fields.
  [[nodiscard]] auto ToJson(bool include_manual_transforms = true) const -> nlohmann::json;

  /// Read stored geometry, with identity defaults for missing transform and aspect fields.
  static auto        FromJson(const nlohmann::json& json, bool allow_manual_transforms = true)
      -> ImageGeometryModel;

 private:
  /// Compose the render input for validation without changing the live Model.
  [[nodiscard]] auto RenderParamsForUpdate(const ImageGeometryUpdate& update) const
      -> ImageGeometryParams;
  void StoreUpdate(const ImageGeometryUpdate& update, const ImageGeometryParams& accepted);

  ImageGeometryParams render_params_{};
  std::string     aspect_preset_    = kFreeAspectPreset;
  CropAspectRatio aspect_ratio_{};
};

}  // namespace alcedo

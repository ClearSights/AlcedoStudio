//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "app/editor_crop_aspect.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "edit/geometry/crop_frame.hpp"
#include "edit/history/pipeline_history_format.hpp"
#include "ui/alcedo_main/editor_support/modules/geometry.hpp"

namespace alcedo {
namespace {

constexpr double kCropOrientationEps = 1e-4;

/// Keep both sides within the Custom control range while storing the pixel quotient.
auto             CropRatioFromPixels(double width, double height) -> CropAspectRatio {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) {
    throw std::invalid_argument("Crop frame must have positive pixel dimensions");
  }
  const double scale =
      std::min(1.0 / std::min(width, height), kCropAspectDimensionMax / std::max(width, height));
  if (std::min(width, height) * scale <= ui::geometry::kCropAspectMinValue) {
    throw std::invalid_argument("Crop frame ratio exceeds the Custom aspect range");
  }
  return {static_cast<float>(width * scale), static_cast<float>(height * scale)};
}

/// Check the corners after the clamp, which enforces minimum crop sizes.
auto CropFitsRotatedSource(NormalizedRect crop, float rotation_degrees, Extent2D source) -> bool {
  const auto      corners  = CropFrameCornersInReference(crop, rotation_degrees, source);
  const auto      width    = static_cast<float>(source.width);
  const auto      height   = static_cast<float>(source.height);
  constexpr float roundoff = 8.0f * std::numeric_limits<float>::epsilon();
  return std::ranges::all_of(corners, [&](const auto& corner) {
    return corner.x >= -width * roundoff && corner.x <= width * (1.0f + roundoff) &&
           corner.y >= -height * roundoff && corner.y <= height * (1.0f + roundoff);
  });
}

}  // namespace

/// Return the nominal dimensions in the source orientation used by the prior editor.
auto SourceOrientedCropAspectPreset(std::string_view id, Extent2D source)
    -> std::optional<CropAspectRatio> {
  const auto preset = ui::geometry::ParseCropAspectPreset(id);
  if (!preset) {
    return std::nullopt;
  }
  const auto dimensions = ui::geometry::CropAspectPresetRatio(*preset);
  if (!dimensions) {
    return std::nullopt;
  }
  CropAspectRatio ratio{dimensions->at(0), dimensions->at(1)};
  if ((source.width < source.height) != (ratio.width < ratio.height)) {
    std::swap(ratio.width, ratio.height);
  }
  return ratio;
}

auto SetCropAspectLocked(const ImageGeometryModel& geometry, Extent2D source, bool locked)
    -> std::optional<ImageGeometryUpdate> {
  if (source.Empty()) {
    throw std::invalid_argument("Crop aspect lock requires source dimensions");
  }
  if (locked == (geometry.AspectPreset() != ImageGeometryModel::kFreeAspectPreset)) {
    return std::nullopt;
  }
  const auto          frame = geometry.CropRect();
  ImageGeometryUpdate update;
  update.aspect_preset = locked ? "custom" : ImageGeometryModel::kFreeAspectPreset;
  update.aspect_ratio  = locked ? CropRatioFromPixels(frame.w * static_cast<double>(source.width),
                                                      frame.h * static_cast<double>(source.height))
                                : CropAspectRatio{};
  update.document_format_version = kPipelineDocumentFormatVersion;
  return update;
}

auto ResetOriginalCrop(float rotation_degrees, Extent2D source) -> ImageGeometryUpdate {
  if (source.Empty()) {
    throw std::invalid_argument("Original crop requires source dimensions");
  }
  const auto crop = ClampCropToRotatedSource(NormalizedRect{}, rotation_degrees, source);
  if (std::abs(crop.w / crop.h - 1.0f) > kCropOrientationEps ||
      !CropFitsRotatedSource(crop, rotation_degrees, source)) {
    throw std::invalid_argument("Original crop cannot fit inside the rotated source");
  }
  ImageGeometryUpdate update;
  update.crop_rect               = crop;
  update.aspect_preset           = ImageGeometryModel::kOriginalAspectPreset;
  update.aspect_ratio            = CropRatioFromPixels(source.width, source.height);
  update.document_format_version = kOriginalCropDocumentFormatVersion;
  return update;
}

auto SwapCropAspect(const ImageGeometryModel& geometry, Extent2D source,
                    std::uint32_t document_version) -> std::optional<ImageGeometryUpdate> {
  if (source.Empty()) {
    throw std::invalid_argument("Swap Aspect requires source dimensions");
  }
  const auto   frame        = geometry.CropRect();
  const double image_aspect = static_cast<double>(source.width) / source.height;
  const double frame_aspect = frame.w * image_aspect / frame.h;
  if (!std::isfinite(frame_aspect) || frame_aspect <= 0.0) {
    throw std::invalid_argument("Crop frame must have positive pixel dimensions");
  }
  if (std::abs(frame_aspect - 1.0) <= kCropOrientationEps) {
    return std::nullopt;
  }

  // Exchanging both axes commutes with the display orientation, including reflections.
  // Keep the stored center and constrain in the same source axes as the renderer.
  ImageGeometryUpdate update;
  double              desired_aspect = 1.0 / frame_aspect;
  if (geometry.AspectPreset() != ImageGeometryModel::kFreeAspectPreset) {
    auto ratio = geometry.AspectRatio();
    if (document_version < kEffectiveCropAspectDocumentFormatVersion) {
      ratio = SourceOrientedCropAspectPreset(geometry.AspectPreset(), source).value_or(ratio);
    }
    std::swap(ratio.width, ratio.height);
    desired_aspect      = static_cast<double>(ratio.width) / ratio.height;
    update.aspect_ratio = ratio;
    if (geometry.AspectPreset() == ImageGeometryModel::kOriginalAspectPreset) {
      update.aspect_preset = "custom";
    }
  }
  double       height = frame.w * image_aspect;
  double       width  = height * desired_aspect / image_aspect;
  const double scale  = std::max({1.0, width, height});
  height /= scale;
  width /= scale;
  if (!std::isfinite(width) || !std::isfinite(height) || width < ui::geometry::kCropRectMinSize ||
      height < ui::geometry::kCropRectMinSize) {
    throw std::invalid_argument("Swapped crop aspect cannot fit inside the source");
  }
  const auto crop_width  = static_cast<float>(width);
  const auto crop_height = static_cast<float>(height);
  const auto accepted =
      ClampCropToRotatedSource({frame.x + (frame.w - crop_width) * 0.5f,
                                frame.y + (frame.h - crop_height) * 0.5f, crop_width, crop_height},
                               geometry.RotationDegrees(), source);
  const double accepted_aspect = accepted.w * image_aspect / accepted.h;
  if (std::abs(accepted_aspect / desired_aspect - 1.0) > kCropOrientationEps ||
      !CropFitsRotatedSource(accepted, geometry.RotationDegrees(), source)) {
    throw std::invalid_argument("Swapped crop aspect cannot fit inside the rotated source");
  }
  update.crop_rect               = accepted;
  update.document_format_version = kPipelineDocumentFormatVersion;
  return update;
}

}  // namespace alcedo

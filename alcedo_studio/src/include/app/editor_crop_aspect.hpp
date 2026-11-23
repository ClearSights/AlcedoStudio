//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "edit/geometry/types.hpp"
#include "edit/graph/image_geometry_model.hpp"

namespace alcedo {

/// Inclusive upper bound for each side of the Custom aspect ratio in the editor.
inline constexpr double kCropAspectDimensionMax = 100.0;

/// Resolve a nominal fixed preset against the source extent. Free and Custom have no preset size.
[[nodiscard]] auto      SourceOrientedCropAspectPreset(std::string_view id, Extent2D source)
    -> std::optional<CropAspectRatio>;

/// Keep the frame and switch between Free and the frame's current pixel ratio.
[[nodiscard]] auto SetCropAspectLocked(const ImageGeometryModel& geometry, Extent2D source,
                                       bool locked) -> std::optional<ImageGeometryUpdate>;

/// Fit the largest centered frame at the source ratio with the current straightening.
/// Reject an absent source or insufficient coverage before returning a document update.
[[nodiscard]] auto ResetOriginalCrop(float rotation_degrees, Extent2D source)
    -> ImageGeometryUpdate;

/// Exchange pixel proportions about the current frame's center without rotating the image.
/// Returns no update for a square frame. Throws if the frame cannot be fitted.
[[nodiscard]] auto SwapCropAspect(const ImageGeometryModel& geometry, Extent2D source,
                                  std::uint32_t document_version)
    -> std::optional<ImageGeometryUpdate>;

}  // namespace alcedo

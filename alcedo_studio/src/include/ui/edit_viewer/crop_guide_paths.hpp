//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <QPointF>
#include <QSizeF>
#include <vector>

namespace alcedo {

// These values are saved in the guide preference and index the Crop panel selectors.
enum class CropGuide {
  None     = 0,
  Thirds   = 1,
  Grid     = 2,
  Ratio    = 3,
  Spiral   = 4,
  Diagonal = 5,
  Triangle = 6,
};

enum class CropGuideVisibility { Always = 0, DuringInput = 1, Never = 2 };

using CropGuidePaths = std::vector<std::vector<QPointF>>;

/// Number of distinct orientations, including the default orientation.
[[nodiscard]] constexpr auto CropGuideOrientationCount(CropGuide guide) -> int {
  if (guide == CropGuide::Spiral) {
    return 4;
  }
  return guide == CropGuide::Triangle ? 2 : 1;
}

/// Build polylines in [0, 1] crop coordinates. Crop pixel dimensions determine angles and fit.
/// Returns no paths for None or invalid dimensions, guide, or orientation.
[[nodiscard]] auto BuildCropGuidePaths(CropGuide guide, const QSizeF& crop_pixels,
                                       int orientation = 0) -> CropGuidePaths;

}  // namespace alcedo

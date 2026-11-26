//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "edit/geometry/projective_transform.hpp"
#include "edit/geometry/render_request.hpp"

namespace alcedo::gpu_dag_test {

/// Full-size framing with uncovered edges and a spatially varying sample footprint.
inline constexpr ImageGeometryParams kTiltedFrame{.horizontal     = 62.0,
                                                  .vertical       = -48.0,
                                                  .scale          = 86.0,
                                                  .offset_x       = 6.0,
                                                  .offset_y       = -4.0,
                                                  .constrain_crop = false};

/// Interior probes at upper left, center, and lower right of a 64 by 48 render.
inline constexpr std::array          kTiltedFrameProbes{Point2d{14.5, 12.5}, Point2d{30.5, 23.5},
                                                        Point2d{44.5, 34.5}};

/// Solves the two scalar equations for kTiltedFrame before any viewport or resolution scaling.
inline auto InverseTiltedPosition(Extent2D extent, double x, double y) -> Point2d {
  const auto [width, height] = extent;
  const double q             = std::max(width, height) / 2.0;
  const double plane_x =
      ((x - width / 2.0) / q - 0.005 * kTiltedFrame.offset_x) / (kTiltedFrame.scale / 100.0);
  const double plane_y =
      ((y - height / 2.0) / q + 0.005 * kTiltedFrame.offset_y) / (kTiltedFrame.scale / 100.0);
  const double h             = kTiltedFrame.horizontal / 200.0;
  const double v             = kTiltedFrame.vertical / 200.0;
  const double normalization = 1.0 / std::sqrt(std::cos(h) * std::cos(v));
  const double z_from_x      = std::cos(v) * std::sin(h) / 1.5;
  const double z_from_y      = std::sin(v) / 1.5;
  const double a             = normalization * std::cos(h) + plane_x * z_from_x;
  const double b             = plane_x * z_from_y;
  const double c             = -normalization * std::sin(v) * std::sin(h) + plane_y * z_from_x;
  const double d             = normalization * std::cos(v) + plane_y * z_from_y;
  const double det           = a * d - b * c;
  return {width / 2.0 + q * (plane_x * d - b * plane_y) / det,
          height / 2.0 + q * (a * plane_y - c * plane_x) / det};
}

}  // namespace alcedo::gpu_dag_test

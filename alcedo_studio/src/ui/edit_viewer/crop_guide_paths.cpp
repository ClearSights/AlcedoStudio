//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/edit_viewer/crop_guide_paths.hpp"

#include <QRectF>
#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <utility>

namespace alcedo {
namespace {

/// Lines at the same relative positions on both axes.
auto GridPaths(std::initializer_list<double> positions) -> CropGuidePaths {
  CropGuidePaths paths;
  for (const double t : positions) {
    paths.push_back({{t, 0.0}, {t, 1.0}});
    paths.push_back({{0.0, t}, {1.0, t}});
  }
  return paths;
}

/// Fit a phi-proportioned rectangle inside the crop and trace the quarter arcs of its squares.
auto SpiralPath(double width, double height, int orientation) -> CropGuidePaths {
  constexpr double     kPhi           = std::numbers::phi;
  constexpr int        kArcs          = 12;
  constexpr int        kSamplesPerArc = 16;
  const double         scale          = std::min(width / kPhi, height);
  const double         fit_width      = scale * kPhi / width;
  const double         fit_height     = scale / height;
  QRectF               remaining(0.0, 0.0, kPhi, 1.0);
  std::vector<QPointF> path;
  path.reserve(1 + kArcs * kSamplesPerArc);
  for (int arc = 0; arc < kArcs; ++arc) {
    const double side = std::min(remaining.width(), remaining.height());
    QRectF       square(remaining.topLeft(), QSizeF(side, side));
    const int    edge = arc % 4;
    switch (edge) {
      case 0:
        remaining.setLeft(square.right());
        break;
      case 1:
        remaining.setTop(square.bottom());
        break;
      case 2:
        square.moveRight(remaining.right());
        remaining.setRight(square.left());
        break;
      case 3:
        square.moveBottom(remaining.bottom());
        remaining.setBottom(square.top());
        break;
    }
    const std::array corners{square.topLeft(), square.topRight(), square.bottomRight(),
                             square.bottomLeft()};
    const QPointF    center = corners[(edge + 2) % 4];
    const QPointF    start  = corners[(edge + 3) % 4] - center;
    const QPointF    end    = corners[(edge + 1) % 4] - center;
    for (int i = arc == 0 ? 0 : 1; i <= kSamplesPerArc; ++i) {
      const double  angle = std::numbers::pi * i / (2.0 * kSamplesPerArc);
      const QPointF point = center + start * std::cos(angle) + end * std::sin(angle);
      const double  x     = 0.5 + (point.x() / kPhi - 0.5) * fit_width;
      const double  y     = 0.5 + (point.y() - 0.5) * fit_height;
      path.emplace_back((orientation & 1) ? 1.0 - x : x, (orientation & 2) ? 1.0 - y : y);
    }
  }
  CropGuidePaths paths;
  paths.push_back(std::move(path));
  return paths;
}

}  // namespace

auto BuildCropGuidePaths(CropGuide guide, const QSizeF& crop_pixels, int orientation)
    -> CropGuidePaths {
  const double width  = crop_pixels.width();
  const double height = crop_pixels.height();
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0 ||
      orientation < 0 || orientation >= CropGuideOrientationCount(guide)) {
    return {};
  }
  switch (guide) {
    case CropGuide::None:
      return {};
    case CropGuide::Thirds:
      return GridPaths({1.0 / 3.0, 2.0 / 3.0});
    case CropGuide::Grid:
      return GridPaths(
          {1.0 / 8.0, 2.0 / 8.0, 3.0 / 8.0, 4.0 / 8.0, 5.0 / 8.0, 6.0 / 8.0, 7.0 / 8.0});
    case CropGuide::Ratio:
      return GridPaths({1.0 - 1.0 / std::numbers::phi, 1.0 / std::numbers::phi});
    case CropGuide::Spiral:
      return SpiralPath(width, height, orientation);
    case CropGuide::Diagonal: {
      const double distance = std::min(width, height);
      const double x        = distance / width;
      const double y        = distance / height;
      // On a square the opposite corners generate the same two strokes.
      if (width == height) {
        return {{{0.0, 0.0}, {1.0, 1.0}}, {{1.0, 0.0}, {0.0, 1.0}}};
      }
      return {{{0.0, 0.0}, {x, y}},
              {{1.0, 0.0}, {1.0 - x, y}},
              {{1.0, 1.0}, {1.0 - x, 1.0 - y}},
              {{0.0, 1.0}, {x, 1.0 - y}}};
    }
    case CropGuide::Triangle: {
      const double t = std::pow(width / std::hypot(width, height), 2);
      if (orientation == 1) {
        return {{{1.0, 0.0}, {0.0, 1.0}}, {{0.0, 0.0}, {1.0 - t, t}}, {{1.0, 1.0}, {t, 1.0 - t}}};
      }
      return {{{0.0, 0.0}, {1.0, 1.0}}, {{1.0, 0.0}, {t, t}}, {{0.0, 1.0}, {1.0 - t, 1.0 - t}}};
    }
  }
  return {};
}

}  // namespace alcedo

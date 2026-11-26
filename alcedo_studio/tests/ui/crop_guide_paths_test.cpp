//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/edit_viewer/crop_guide_paths.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>

namespace alcedo {
namespace {

TEST(CropGuidePathsTest, CompositionGuidesStayInsideCropAtAllOrientations) {
  for (const QSizeF size : {QSizeF(1200, 800), QSizeF(800, 1200), QSizeF(800, 800)}) {
    for (const auto guide : {CropGuide::Thirds, CropGuide::Grid, CropGuide::Ratio,
                             CropGuide::Spiral, CropGuide::Diagonal, CropGuide::Triangle}) {
      for (int orientation = 0; orientation < CropGuideOrientationCount(guide); ++orientation) {
        SCOPED_TRACE(static_cast<int>(guide));
        SCOPED_TRACE(orientation);
        const auto paths = BuildCropGuidePaths(guide, size, orientation);
        ASSERT_FALSE(paths.empty());
        for (const auto& path : paths) {
          ASSERT_GE(path.size(), 2u);
          for (const auto& point : path) {
            EXPECT_GE(point.x(), -1e-6);
            EXPECT_LE(point.x(), 1.0 + 1e-6);
            EXPECT_GE(point.y(), -1e-6);
            EXPECT_LE(point.y(), 1.0 + 1e-6);
          }
        }
      }
    }
  }
}

TEST(CropGuidePathsTest, AxisGuidesDivideBothAxesAtSpecifiedFractions) {
  const auto check_grid = [](CropGuide guide, std::initializer_list<double> positions) {
    const auto paths = BuildCropGuidePaths(guide, QSizeF(600, 400));
    ASSERT_EQ(paths.size(), positions.size() * 2);
    size_t i = 0;
    for (const double t : positions) {
      const auto& vertical   = paths[i++];
      const auto& horizontal = paths[i++];
      EXPECT_NEAR(vertical[0].x(), t, 1e-6);
      EXPECT_NEAR(vertical[1].x(), t, 1e-6);
      EXPECT_DOUBLE_EQ(vertical[0].y(), 0.0);
      EXPECT_DOUBLE_EQ(vertical[1].y(), 1.0);
      EXPECT_NEAR(horizontal[0].y(), t, 1e-6);
      EXPECT_NEAR(horizontal[1].y(), t, 1e-6);
      EXPECT_DOUBLE_EQ(horizontal[0].x(), 0.0);
      EXPECT_DOUBLE_EQ(horizontal[1].x(), 1.0);
    }
  };
  check_grid(CropGuide::Thirds, {1.0 / 3.0, 2.0 / 3.0});
  check_grid(CropGuide::Grid, {0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875});
  check_grid(CropGuide::Ratio,
             {1.0 / (std::numbers::phi * std::numbers::phi), 1.0 / std::numbers::phi});
}

TEST(CropGuidePathsTest, DiagonalAnglesAndTrianglePerpendicularsMatchCropPixels) {
  for (const QSizeF size : {QSizeF(1200, 500), QSizeF(500, 1200), QSizeF(800, 800)}) {
    const auto pixels = [&size](const QPointF& uv) {
      return QPointF(uv.x() * size.width(), uv.y() * size.height());
    };
    const auto diagonals = BuildCropGuidePaths(CropGuide::Diagonal, size);
    ASSERT_EQ(diagonals.size(), size.width() == size.height() ? 2u : 4u);
    for (const auto& line : diagonals) {
      const auto delta = pixels(line[1] - line[0]);
      EXPECT_NEAR(std::atan2(std::abs(delta.y()), std::abs(delta.x())), std::numbers::pi / 4.0,
                  1e-4);
      EXPECT_NEAR(std::abs(delta.x()), std::min(size.width(), size.height()), 1e-6);
    }
    for (int orientation = 0; orientation < 2; ++orientation) {
      const auto triangle = BuildCropGuidePaths(CropGuide::Triangle, size, orientation);
      ASSERT_EQ(triangle.size(), 3u);
      EXPECT_EQ(triangle[0][0], QPointF(orientation, 0.0));
      EXPECT_EQ(triangle[0][1], QPointF(1 - orientation, 1.0));
      const auto main = pixels(triangle[0][1] - triangle[0][0]);
      for (size_t i = 1; i < triangle.size(); ++i) {
        const auto   perpendicular = pixels(triangle[i][1] - triangle[i][0]);
        const double cosine =
            QPointF::dotProduct(main, perpendicular) /
            (std::hypot(main.x(), main.y()) * std::hypot(perpendicular.x(), perpendicular.y()));
        EXPECT_NEAR(std::acos(cosine), std::numbers::pi / 2.0, 1e-4);
        const double t = triangle[i][1].y();
        EXPECT_NEAR(triangle[i][1].x(), orientation == 0 ? t : 1.0 - t, 1e-6);
      }
    }
  }
}

TEST(CropGuidePathsTest, SpiralFitsWholeQuarterArcsAndMirrorsEachAxis) {
  for (const QSizeF size : {QSizeF(1200, 600), QSizeF(600, 1200), QSizeF(800, 800)}) {
    const auto spiral = BuildCropGuidePaths(CropGuide::Spiral, size);
    ASSERT_EQ(spiral.size(), 1u);
    const auto& path = spiral.front();
    ASSERT_GT(path.size(), 32u);
    const double  side = std::min(size.width() / std::numbers::phi, size.height());
    const QPointF center(path.front().x() * size.width() + side, path.front().y() * size.height());
    for (size_t i = 0; i <= 16; ++i) {
      const QPointF point(path[i].x() * size.width(), path[i].y() * size.height());
      EXPECT_NEAR(std::hypot(point.x() - center.x(), point.y() - center.y()), side, 1e-6);
    }
    EXPECT_NEAR(path[16].x() * size.width(), center.x(), 1e-6);
    EXPECT_NEAR(path[16].y() * size.height(), center.y() - side, 1e-6);
    for (int orientation = 1; orientation < 4; ++orientation) {
      const auto mirrored = BuildCropGuidePaths(CropGuide::Spiral, size, orientation);
      ASSERT_EQ(mirrored.size(), 1u);
      ASSERT_EQ(mirrored.front().size(), path.size());
      for (size_t i = 0; i < path.size(); ++i) {
        EXPECT_NEAR(mirrored.front()[i].x(), (orientation & 1) ? 1.0 - path[i].x() : path[i].x(),
                    1e-6);
        EXPECT_NEAR(mirrored.front()[i].y(), (orientation & 2) ? 1.0 - path[i].y() : path[i].y(),
                    1e-6);
      }
    }
  }
}

TEST(CropGuidePathsTest, InvalidDimensionsOrOrientationProduceNoPaths) {
  EXPECT_TRUE(BuildCropGuidePaths(CropGuide::Thirds, QSizeF(0, 200)).empty());
  EXPECT_TRUE(
      BuildCropGuidePaths(CropGuide::Diagonal, QSizeF(200, std::numeric_limits<double>::infinity()))
          .empty());
  EXPECT_TRUE(BuildCropGuidePaths(CropGuide::Triangle, QSizeF(300, 200), 2).empty());
  EXPECT_TRUE(BuildCropGuidePaths(CropGuide::None, QSizeF(300, 200)).empty());
}

}  // namespace
}  // namespace alcedo

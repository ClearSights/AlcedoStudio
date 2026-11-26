//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "edit/geometry/projective_transform.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "edit/geometry/render_geometry_resolver.hpp"

namespace alcedo {
namespace {

constexpr double kPixelTolerance = 1e-4;

auto Resolve(Extent2D extent, const ImageGeometryParams& image) -> ResolvedRenderGeometry {
  return ResolveRenderGeometry(MakeSourceGeometry(extent, extent), image, {}, {}, {});
}

void ExpectPointNear(Point2d actual, Point2d expected, double tolerance = kPixelTolerance) {
  EXPECT_NEAR(actual.x, expected.x, tolerance);
  EXPECT_NEAR(actual.y, expected.y, tolerance);
}

// Independent oracle: rotate the source point in 3D, project it, then straighten with scalar
// pixel-coordinate equations. Use no matrix construction or point mapping from the resolver.
auto ExpectedPlanePoint(const ImageGeometryParams& image, Extent2D extent, Point2d point)
    -> Point2d {
  const double q             = 0.5 * std::max(extent.width, extent.height);
  const double cx            = 0.5 * extent.width;
  const double cy            = 0.5 * extent.height;
  const double h             = 0.5 * image.horizontal / 100.0;
  const double v             = 0.5 * image.vertical / 100.0;
  const double x             = (point.x - cx) / q;
  const double y             = (point.y - cy) / q;
  const double yaw_x         = std::cos(h) * x;
  const double yaw_z         = -std::sin(h) * x;
  const double pitch_y       = std::cos(v) * y + std::sin(v) * yaw_z;
  const double pitch_z       = -std::sin(v) * y + std::cos(v) * yaw_z;
  const double magnification = 1.5 / (1.5 + pitch_z) / std::sqrt(std::cos(h) * std::cos(v));
  const double px            = (image.crop_rect.x + 0.5 * image.crop_rect.w) * extent.width - cx;
  const double py            = (image.crop_rect.y + 0.5 * image.crop_rect.h) * extent.height - cy;
  const double dx            = q * yaw_x * magnification - px;
  const double dy            = q * pitch_y * magnification - py;
  const double radians       = image.rotation_degrees * std::numbers::pi / 180.0;
  const double k             = std::pow(2.0, image.aspect / 200.0);
  const double scale         = image.scale / 100.0;
  return {cx + scale / k * (px + std::cos(radians) * dx - std::sin(radians) * dy) +
              0.005 * q * image.offset_x,
          cy + scale * k * (py + std::sin(radians) * dx + std::cos(radians) * dy) -
              0.005 * q * image.offset_y};
}

auto ToPlanePixels(const ResolvedRenderGeometry& geometry, Point2d point) -> Point2d {
  const auto& map   = geometry.projective.value();
  const auto  edit  = map.reference_to_edit.Map(point);
  const auto& frame = map.frame_in_reference_plane;
  return {frame.x + edit.x * frame.width / geometry.edit_extent.width,
          frame.y + edit.y * frame.height / geometry.edit_extent.height};
}

void ExpectResolveError(Extent2D extent, const ImageGeometryParams& image,
                        std::string_view message) {
  try {
    (void)Resolve(extent, image);
    ADD_FAILURE() << "Expected error containing " << message;
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string_view(error.what()).find(message), std::string_view::npos) << error.what();
  }
}

TEST(GpuDagGeometry, ProjectivePointRoundTripMatchesWithinTolerance) {
  for (const Extent2D extent : {Extent2D{6001, 3999}, {3001, 5003}, {4095, 4095}}) {
    ImageGeometryParams image;
    image.crop_rect        = {0.20f, 0.27f, 0.55f, 0.47f};
    image.horizontal       = 72.0;
    image.vertical         = -67.0;
    image.rotation_degrees = 17.3f;
    image.aspect           = 37.0;
    image.scale            = 121.0;
    image.offset_x         = -21.0;
    image.offset_y         = 15.0;
    const Extent2D    decoded{(extent.width + 1) / 2, (extent.height + 1) / 2};
    const ViewRequest view{{0.1f, 0.2f, 0.7f, 0.65f}, {503, 411}};
    const auto geometry = ResolveRenderGeometry(MakeSourceGeometry(decoded, extent), image, view,
                                                {}, {2.0f, 3.0f, false});
    ASSERT_TRUE(geometry.projective);
    EXPECT_FALSE(IsIdentityResample(geometry));
    const auto& map = *geometry.projective;
    for (double y : {0.5, 0.37 * extent.height, extent.height - 0.5}) {
      for (double x : {0.5, 0.63 * extent.width, extent.width - 0.5}) {
        ExpectPointNear(map.render_to_reference.Map(map.reference_to_render.Map({x, y})), {x, y});
      }
    }
    const Point2d sample{267.5, 183.5};
    const auto    reference = map.render_to_reference.Map(sample);
    ExpectPointNear(
        map.render_to_decoded.Map(sample),
        {reference.x * decoded.width / extent.width, reference.y * decoded.height / extent.height});

    // The required region includes every sampled source point and its full footprint.
    const auto region = geometry.required_decoded_region;
    EXPECT_GT(region.x, 0);
    EXPECT_GT(region.y, 0);
    EXPECT_LT(region.X1(), decoded.width);
    EXPECT_LT(region.Y1(), decoded.height);
    for (int y = 0; y <= 4; ++y) {
      for (int x = 0; x <= 4; ++x) {
        const auto source = map.render_to_decoded.Map(
            {0.25 * x * geometry.render_extent.width, 0.25 * y * geometry.render_extent.height});
        EXPECT_LE(region.x, source.x - 2.0);
        EXPECT_LE(region.y, source.y - 3.0);
        EXPECT_GE(region.X1(), source.x + 2.0);
        EXPECT_GE(region.Y1(), source.y + 3.0);
      }
    }
  }
}

TEST(GpuDagGeometry, PerspectiveSignsAndAspectScaleOffsetsMatchSpecification) {
  const Extent2D      extent{401, 299};
  ImageGeometryParams image;
  image.constrain_crop  = false;
  image.horizontal      = 100.0;
  const auto horizontal = Resolve(extent, image);
  const auto h_span     = [&](double x) {
    return ToPlanePixels(horizontal, {x, 229.5}).y - ToPlanePixels(horizontal, {x, 69.5}).y;
  };
  EXPECT_GT(h_span(360.5), h_span(40.5));
  ExpectPointNear(ToPlanePixels(horizontal, {360.5, 229.5}),
                  ExpectedPlanePoint(image, extent, {360.5, 229.5}));

  image.horizontal    = 0.0;
  image.vertical      = 100.0;
  const auto vertical = Resolve(extent, image);
  EXPECT_GT(ToPlanePixels(vertical, {360.5, 229.5}).x - ToPlanePixels(vertical, {40.5, 229.5}).x,
            ToPlanePixels(vertical, {360.5, 69.5}).x - ToPlanePixels(vertical, {40.5, 69.5}).x);
  const auto jacobian = vertical.projective->reference_to_edit.Jacobian({200.5, 149.5});
  EXPECT_NEAR(jacobian[0].x * jacobian[1].y - jacobian[0].y * jacobian[1].x, 1.0, 1e-12);

  image.vertical    = 0.0;
  image.aspect      = 100.0;
  image.scale       = 120.0;
  image.offset_x    = 25.0;
  image.offset_y    = 30.0;
  const auto affine = Resolve(extent, image);
  EXPECT_EQ(affine.render_extent, extent);
  ExpectPointNear(ToPlanePixels(affine, {200.5, 149.5}), {225.5625, 119.425});
  ExpectPointNear(ToPlanePixels(affine, {240.5, 169.5}),
                  {225.5625 + 48.0 / std::numbers::sqrt2, 119.425 + 24.0 * std::numbers::sqrt2});
  // Disabled constraints keep framing, including uncovered pixels for the border policy.
  EXPECT_LT(affine.projective->render_to_reference.Map({0.0, 0.0}).x, 0.0);
}

TEST(GpuDagGeometry, CombinedPerspectiveMatchesSpecifiedRotationOrder) {
  const Extent2D      extent{901, 1201};
  ImageGeometryParams image;
  image.horizontal       = -100.0;
  image.vertical         = 100.0;
  image.rotation_degrees = -32.5f;
  image.aspect           = -100.0;
  image.scale            = 150.0;
  image.offset_x         = -100.0;
  image.offset_y         = 100.0;
  image.crop_rect        = {0.15f, 0.18f, 0.38f, 0.54f};
  image.constrain_crop   = false;
  const auto geometry    = Resolve(extent, image);
  for (const Point2d point : {Point2d{40.5, 73.5}, {400.5, 705.5}, {890.5, 1150.5}}) {
    ExpectPointNear(ToPlanePixels(geometry, point), ExpectedPlanePoint(image, extent, point));
  }

  const auto&      map = geometry.projective->reference_to_render;
  const Point2d    point{321.5, 432.5};
  constexpr double kStep    = 0.01;
  const auto       x_low    = map.Map({point.x - kStep, point.y});
  const auto       x_high   = map.Map({point.x + kStep, point.y});
  const auto       y_low    = map.Map({point.x, point.y - kStep});
  const auto       y_high   = map.Map({point.x, point.y + kStep});
  const auto       jacobian = map.Jacobian(point);
  ExpectPointNear(jacobian[0],
                  {(x_high.x - x_low.x) / (2 * kStep), (x_high.y - x_low.y) / (2 * kStep)}, 1e-8);
  ExpectPointNear(jacobian[1],
                  {(y_high.x - y_low.x) / (2 * kStep), (y_high.y - y_low.y) / (2 * kStep)}, 1e-8);
}

TEST(GpuDagGeometry, ConstrainedFrameUsesNearestCenterAndPreservesRequestedRatio) {
  const Extent2D      extent{400, 200};
  ImageGeometryParams image;
  image.scale         = 50.0;
  image.offset_x      = 30.0;
  const auto centered = Resolve(extent, image);
  ASSERT_TRUE(centered.projective);
  const auto frame = centered.projective->frame_in_reference_plane;
  EXPECT_EQ(centered.edit_extent, (Extent2D{140, 70}));
  EXPECT_NEAR(frame.x, 130.0, kPixelTolerance);
  EXPECT_NEAR(frame.y, 65.0, kPixelTolerance);
  EXPECT_NEAR(frame.width / frame.height, 2.0, 1e-12);

  // The source maps to x = [200, 400] after a half-scale and a 100-pixel shift.
  // Request a crop centered at x = 160 so the nearest feasible center is at the left inset.
  image.offset_x        = 100.0;
  image.crop_rect       = {0.0f, 0.1f, 0.8f, 0.8f};
  const auto translated = Resolve(extent, image);
  ASSERT_TRUE(translated.projective);
  const auto moved = translated.projective->frame_in_reference_plane;
  EXPECT_NEAR(moved.x, 200.0, kPixelTolerance);
  EXPECT_NEAR(moved.x + 0.5 * moved.width, 200.0 + 200.0 * kGeometryMinNormalizedSize,
              kPixelTolerance);
  EXPECT_NEAR(moved.y + 0.5 * moved.height, 100.0, kPixelTolerance);
  EXPECT_NEAR(moved.width / moved.height, 2.0, 1e-12);
  EXPECT_EQ(translated.edit_extent, (Extent2D{1, 1}));
}

TEST(GpuDagGeometry, TransformedCropCornersRemainCoveredWhenTheStraighteningPivotMoves) {
  const Extent2D      extent{801, 603};
  ImageGeometryParams image;
  image.crop_rect        = {0.025f, 0.2f, 0.23f, 0.4f};
  image.horizontal       = 45.0;
  image.vertical         = -25.0;
  image.rotation_degrees = 31.0f;
  image.scale            = 73.0;
  image.offset_x         = 70.0;
  const auto  geometry   = Resolve(extent, image);
  const auto& map        = *geometry.projective;
  const auto& frame      = map.frame_in_reference_plane;
  EXPECT_GT(frame.x + 0.5 * frame.width, 160.0);
  EXPECT_NEAR(frame.width / frame.height, (0.23 * extent.width) / (0.4 * extent.height), 1e-7);
  for (const auto point : Rectangle2d{0.0, 0.0, double(geometry.render_extent.width),
                                      double(geometry.render_extent.height)}
                              .Corners()) {
    const auto reference = map.render_to_reference.Map(point);
    EXPECT_GE(reference.x, -kPixelTolerance);
    EXPECT_GE(reference.y, -kPixelTolerance);
    EXPECT_LE(reference.x, extent.width + kPixelTolerance);
    EXPECT_LE(reference.y, extent.height + kPixelTolerance);
  }
}

TEST(GpuDagGeometry, TransformedEditingBoundsFitTheUnstraightenedSourceAndRoundOnce) {
  const Extent2D      extent{401, 301};
  ImageGeometryParams image;
  image.output_frame     = GeometryOutputFrame::TransformedSourceBounds;
  image.horizontal       = 50.0;
  image.vertical         = -30.0;
  image.offset_x         = 20.0;
  image.offset_y         = -40.0;
  image.rotation_degrees = 34.0f;
  image.crop_rect        = {0.1f, 0.1f, 0.3f, 0.6f};
  const auto geometry    = Resolve(extent, image);
  ASSERT_TRUE(geometry.projective);
  image.rotation_degrees = 0.0f;
  double left            = std::numeric_limits<double>::infinity();
  double top             = left;
  double right           = -left;
  double bottom          = -left;
  for (const auto corner : Rectangle2d{0.0, 0.0, 401.0, 301.0}.Corners()) {
    const auto expected = ExpectedPlanePoint(image, extent, corner);
    left                = std::min(left, expected.x);
    top                 = std::min(top, expected.y);
    right               = std::max(right, expected.x);
    bottom              = std::max(bottom, expected.y);
    ExpectPointNear(ToPlanePixels(geometry, corner), expected);
  }
  const auto& frame = geometry.projective->frame_in_reference_plane;
  EXPECT_NEAR(frame.x, left, kPixelTolerance);
  EXPECT_NEAR(frame.y, top, kPixelTolerance);
  EXPECT_NEAR(frame.width, right - left, kPixelTolerance);
  EXPECT_NEAR(frame.height, bottom - top, kPixelTolerance);
  EXPECT_EQ(geometry.edit_extent.width, std::lround(right - left));
  EXPECT_EQ(geometry.edit_extent.height, std::lround(bottom - top));
}

TEST(GpuDagGeometry, InvalidProjectiveMappingReturnsErrorWithoutGeometry) {
  const Extent2D         extent{401, 301};
  using Control = double ImageGeometryParams::*;
  for (const auto [field, name] : std::array<std::pair<Control, std::string_view>, 6>{
           {{&ImageGeometryParams::horizontal, "horizontal"},
            {&ImageGeometryParams::vertical, "vertical"},
            {&ImageGeometryParams::aspect, "aspect"},
            {&ImageGeometryParams::scale, "scale"},
            {&ImageGeometryParams::offset_x, "offset_x"},
            {&ImageGeometryParams::offset_y, "offset_y"}}}) {
    ImageGeometryParams invalid;
    for (double value : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), 151.0}) {
      invalid.*field = value;
      ExpectResolveError(extent, invalid, name);
    }
  }

  ImageGeometryParams horizon;
  horizon.horizontal     = 100.0;
  horizon.scale          = 50.0;
  horizon.offset_x       = 100.0;
  horizon.constrain_crop = false;
  ExpectResolveError(extent, horizon, "horizon crosses");

  ImageGeometryParams empty;
  empty.scale       = 50.0;
  empty.crop_rect.h = kGeometryMinNormalizedSize;
  ExpectResolveError(extent, empty, "empty constrained crop frame");

  const auto singular = ProjectiveTransform::Scale(1.0, 1e-13);
  EXPECT_THROW((void)singular.Inverse(), std::runtime_error);
  const ProjectiveTransform near_horizon{{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 1.0, 0.0, 1.0}};
  EXPECT_THROW((void)near_horizon.Map({-1.0 + 1e-10, 0.0}), std::runtime_error);
  ExpectPointNear(near_horizon.Inverse().Map(near_horizon.Map({-0.25, 0.35})), {-0.25, 0.35});
}

TEST(GpuDagGeometry, DefaultManualInputsPreserveOddFrameDimensionsAndPixelCenters) {
  for (const Extent2D extent : {Extent2D{601, 399}, {301, 503}, {405, 405}}) {
    const auto geometry = Resolve(extent, {});
    EXPECT_FALSE(geometry.projective);
    EXPECT_EQ(geometry.edit_extent, extent);
    EXPECT_EQ(geometry.render_extent, extent);
    EXPECT_TRUE(IsIdentityResample(geometry));
    for (const auto pixel : {PixelCenter(0, 0), PixelCenter(extent.width / 2, extent.height / 2),
                             PixelCenter(extent.width - 1, extent.height - 1)}) {
      EXPECT_EQ(TransformPoint(geometry.render_to_decoded, pixel), pixel);
    }
  }
}

}  // namespace
}  // namespace alcedo

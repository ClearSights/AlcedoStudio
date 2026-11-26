//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "edit/geometry/projective_transform.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

#include "edit/geometry/crop_frame.hpp"

namespace alcedo {
namespace {

constexpr double kHorizonTolerance  = 1e-8;
constexpr double kSingularTolerance = 1e-12;

auto             Denominator(const ProjectiveTransform& matrix, Point2d point) -> double {
  const auto&  a = matrix.m;
  const double w = a[6] * point.x + a[7] * point.y + a[8];
  const double threshold =
      kHorizonTolerance * std::hypot(a[6], a[7], a[8]) * std::hypot(point.x, point.y, 1.0);
  if (!std::isfinite(w) || !(std::abs(w) > threshold)) {
    throw std::runtime_error("ProjectiveTransform: point is at or near the projective horizon");
  }
  return w;
}

auto DividePoint(const ProjectiveTransform& matrix, Point2d point, double w) -> Point2d {
  const auto&   a = matrix.m;
  const Point2d result{(a[0] * point.x + a[1] * point.y + a[2]) / w,
                       (a[3] * point.x + a[4] * point.y + a[5]) / w};
  if (!std::isfinite(result.x) || !std::isfinite(result.y)) {
    throw std::runtime_error("ProjectiveTransform: mapped point must be finite");
  }
  return result;
}

void ValidateControl(double value, double minimum, double maximum, const char* name) {
  if (!std::isfinite(value) || value < minimum || value > maximum) {
    throw std::runtime_error(std::string("ResolveRenderGeometry: ") + name +
                             " must be finite and within its allowed range");
  }
}

/// Evaluates Rx(-v) * Ry(h) and the perspective projection about the image center.
auto Perspective(double horizontal, double vertical) -> ProjectiveTransform {
  constexpr double kFocalLength = 1.5;
  const double     h            = 0.005 * horizontal;
  const double     v            = 0.005 * vertical;
  const double     cos_h        = std::cos(h);
  const double     sin_h        = std::sin(h);
  const double     cos_v        = std::cos(v);
  const double     sin_v        = std::sin(v);
  const double     n            = 1.0 / std::sqrt(cos_h * cos_v);
  return {{n * cos_h, 0.0, 0.0, -n * sin_v * sin_h, n * cos_v, 0.0, -cos_v * sin_h / kFocalLength,
           -sin_v / kFocalLength, 1.0}};
}

}  // namespace

auto Rectangle2d::Corners() const -> std::array<Point2d, 4> {
  return {{{x, y}, {x + width, y}, {x + width, y + height}, {x, y + height}}};
}

auto ProjectiveTransform::Translate(double x, double y) -> ProjectiveTransform {
  return {{1.0, 0.0, x, 0.0, 1.0, y, 0.0, 0.0, 1.0}};
}

auto ProjectiveTransform::Scale(double x, double y) -> ProjectiveTransform {
  return {{x, 0.0, 0.0, 0.0, y, 0.0, 0.0, 0.0, 1.0}};
}

auto ProjectiveTransform::Rotate(double radians) -> ProjectiveTransform {
  const double c = std::cos(radians);
  const double s = std::sin(radians);
  return {{c, -s, 0.0, s, c, 0.0, 0.0, 0.0, 1.0}};
}

auto ProjectiveTransform::FromMatrix(const Matrix3x3& matrix) -> ProjectiveTransform {
  ProjectiveTransform result;
  std::copy_n(matrix.m, result.m.size(), result.m.begin());
  return result;
}

auto ProjectiveTransform::ToFloatMatrix() const -> Matrix3x3 {
  Matrix3x3 result;
  for (std::size_t i = 0; i < m.size(); ++i) {
    result.m[i] = static_cast<float>(m[i]);
    if (!std::isfinite(result.m[i])) {
      throw std::runtime_error("ProjectiveTransform: shader coefficients must be finite");
    }
  }
  return result;
}

auto ProjectiveTransform::Map(Point2d point) const -> Point2d {
  return DividePoint(*this, point, Denominator(*this, point));
}

auto ProjectiveTransform::MapQuad(const std::array<Point2d, 4>& corners) const
    -> std::array<Point2d, 4> {
  std::array<Point2d, 4> mapped;
  const double           first_w = Denominator(*this, corners.front());
  mapped.front()                 = DividePoint(*this, corners.front(), first_w);
  for (std::size_t i = 1; i < corners.size(); ++i) {
    const double w = Denominator(*this, corners[i]);
    if (std::signbit(w) != std::signbit(first_w)) {
      throw std::runtime_error("ProjectiveTransform: projective horizon crosses the sampled frame");
    }
    mapped[i] = DividePoint(*this, corners[i], w);
  }
  return mapped;
}

auto ProjectiveTransform::Jacobian(Point2d point) const -> std::array<Point2d, 2> {
  const double w      = Denominator(*this, point);
  const auto   mapped = DividePoint(*this, point, w);
  return {{{(m[0] - mapped.x * m[6]) / w, (m[3] - mapped.y * m[6]) / w},
           {(m[1] - mapped.x * m[7]) / w, (m[4] - mapped.y * m[7]) / w}}};
}

auto ProjectiveTransform::Inverse() const -> ProjectiveTransform {
  // Scale before forming cofactors so the relative determinant test is insensitive to the
  // homogeneous matrix scale and avoids overflow and underflow in the determinant.
  double largest = 0.0;
  for (const double value : m) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("ProjectiveTransform: matrix coefficients must be finite");
    }
    largest = std::max(largest, std::abs(value));
  }
  if (largest == 0.0) {
    throw std::runtime_error("ProjectiveTransform: singular matrix");
  }
  std::array<double, 9> a;
  double                norm_squared = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    a[i] = m[i] / largest;
    norm_squared += a[i] * a[i];
  }
  ProjectiveTransform inverse{
      {a[4] * a[8] - a[5] * a[7], a[2] * a[7] - a[1] * a[8], a[1] * a[5] - a[2] * a[4],
       a[5] * a[6] - a[3] * a[8], a[0] * a[8] - a[2] * a[6], a[2] * a[3] - a[0] * a[5],
       a[3] * a[7] - a[4] * a[6], a[1] * a[6] - a[0] * a[7], a[0] * a[4] - a[1] * a[3]}};
  const auto&  adj = inverse.m;
  const double det = a[0] * adj[0] + a[1] * adj[3] + a[2] * adj[6];
  if (std::abs(det) <= kSingularTolerance * norm_squared * std::sqrt(norm_squared)) {
    throw std::runtime_error("ProjectiveTransform: singular matrix");
  }
  // A homogeneous inverse only needs to be proportional to the algebraic inverse.
  for (double& value : inverse.m) {
    value /= det;
  }
  return inverse;
}

auto ProjectiveTransform::InversePixelMap(Extent2D input, Extent2D output) const
    -> ProjectiveTransform {
  if (input.Empty() || output.Empty()) {
    throw std::runtime_error("ProjectiveTransform: pixel map extents must be positive");
  }
  const auto input_pixels = Scale(input.width, input.height);
  const auto output_uv    = Scale(1.0 / output.width, 1.0 / output.height);
  return input_pixels * (output_uv * *this * input_pixels).Inverse() * output_uv;
}

auto operator*(const ProjectiveTransform& left, const ProjectiveTransform& right)
    -> ProjectiveTransform {
  ProjectiveTransform result;
  for (std::size_t row = 0; row < 9; row += 3) {
    for (std::size_t col = 0; col < 3; ++col) {
      result.m[row + col] = left.m[row] * right.m[col] + left.m[row + 1] * right.m[col + 3] +
                            left.m[row + 2] * right.m[col + 6];
    }
  }
  return result;
}

auto QuadBounds(const std::array<Point2d, 4>& corners) -> Rectangle2d {
  const auto [left, right] = std::ranges::minmax_element(corners, {}, &Point2d::x);
  const auto [top, bottom] = std::ranges::minmax_element(corners, {}, &Point2d::y);
  return {left->x, top->y, right->x - left->x, bottom->y - top->y};
}

auto HasIdentityManualTransform(const ImageGeometryParams& image) -> bool {
  return image.horizontal == 0.0 && image.vertical == 0.0 && image.aspect == 0.0 &&
         image.scale == 100.0 && image.offset_x == 0.0 && image.offset_y == 0.0;
}

auto ResolveCropPlaneTransform(const ImageGeometryParams& image) -> CropPlaneTransform {
  ValidateControl(image.horizontal, -100.0, 100.0, "horizontal");
  ValidateControl(image.vertical, -100.0, 100.0, "vertical");
  ValidateControl(image.aspect, -100.0, 100.0, "aspect");
  ValidateControl(image.scale, 50.0, 150.0, "scale");
  ValidateControl(image.offset_x, -100.0, 100.0, "offset_x");
  ValidateControl(image.offset_y, -100.0, 100.0, "offset_y");
  const double aspect = std::exp2(image.aspect / 200.0);
  const double scale  = image.scale / 100.0;
  return {Perspective(image.horizontal, image.vertical),
          {scale / aspect, scale * aspect},
          {0.005 * image.offset_x, -0.005 * image.offset_y},
          image.constrain_crop};
}

auto CropPlaneTransform::PivotShift(float rotation_degrees) const -> ProjectiveTransform {
  const auto rotation = ProjectiveTransform::Rotate(NormalizeRotationDegrees(rotation_degrees) *
                                                    (std::numbers::pi / 180.0));
  // B * (I - R) gives the change in coverage when the crop's straightening pivot moves.
  return {{scale.x * (1.0 - rotation.m[0]), -scale.x * rotation.m[1], 0.0, -scale.y * rotation.m[3],
           scale.y * (1.0 - rotation.m[4]), 0.0, 0.0, 0.0, 1.0}};
}

namespace {

/// Resolves crop and full-source bounds with the same perspective and straightening equations.
auto ResolveImageFrame(const CropPlaneTransform& transform, NormalizedRect crop,
                       float rotation_degrees, Extent2D source, bool source_bounds_only)
    -> ProjectiveImageFrame {
  if (source.Empty() || !std::isfinite(rotation_degrees)) {
    throw std::runtime_error("ResolveRenderGeometry: source extent and rotation must be valid");
  }
  if (!std::isfinite(crop.x) || !std::isfinite(crop.y)) {
    throw std::runtime_error("ResolveRenderGeometry: crop position must be finite");
  }
  ValidateControl(crop.w, kGeometryMinNormalizedSize, 1.0, "crop width");
  ValidateControl(crop.h, kGeometryMinNormalizedSize, 1.0, "crop height");

  const double      q = 0.5 * std::max(source.width, source.height);
  const double      w = source.width / q;
  const double      h = source.height / q;
  const Rectangle2d source_bounds{-0.5 * w, -0.5 * h, w, h};
  Rectangle2d       frame{(crop.x - 0.5) * w, (crop.y - 0.5) * h, crop.w * w, crop.h * h};
  const Point2d     center{frame.x + 0.5 * frame.width, frame.y + 0.5 * frame.height};
  const auto rotation    = ProjectiveTransform::Rotate(NormalizeRotationDegrees(rotation_degrees) *
                                                       (std::numbers::pi / 180.0));
  const auto pivot_shift = transform.PivotShift(rotation_degrees);
  const auto pivot_translation = [&](Point2d pivot) {
    const auto shift = pivot_shift.Map(pivot);
    return ProjectiveTransform::Translate(transform.offset.x + shift.x,
                                          transform.offset.y + shift.y);
  };
  const auto linear = ProjectiveTransform::Scale(transform.scale.x, transform.scale.y) * rotation *
                      transform.perspective;
  auto       source_to_plane = pivot_translation(center) * linear;
  const auto coverage        = source_to_plane.MapQuad(source_bounds.Corners());
  if (source_bounds_only) {
    frame = QuadBounds(coverage);
  } else if (transform.constrain_crop) {
    const double minimum_scale = kGeometryMinNormalizedSize / std::min(crop.w, crop.h);
    frame = ConstrainCropToTransformedSource(frame, coverage, minimum_scale,
                                             {pivot_shift.m[0], pivot_shift.m[3]},
                                             {pivot_shift.m[1], pivot_shift.m[4]});
    source_to_plane =
        pivot_translation({frame.x + 0.5 * frame.width, frame.y + 0.5 * frame.height}) * linear;
  }

  const auto plane_to_source = source_to_plane.Inverse();
  (void)plane_to_source.MapQuad(frame.Corners());
  return {source_to_plane, plane_to_source, frame};
}

}  // namespace

auto ResolveProjectiveImageFrame(const ImageGeometryParams& image, Extent2D source)
    -> ProjectiveImageFrame {
  if (!std::isfinite(image.rotation_degrees)) {
    throw std::runtime_error("ResolveRenderGeometry: rotation must be finite");
  }
  const float rotation = image.output_frame == GeometryOutputFrame::TransformedSourceBounds
                             ? 0.0f
                             : image.rotation_degrees;
  return ResolveImageFrame(ResolveCropPlaneTransform(image), image.crop_rect, rotation, source,
                           image.output_frame != GeometryOutputFrame::CropFrame);
}

auto ResolveProjectiveCropFrame(const CropPlaneTransform& transform, NormalizedRect crop,
                                float rotation_degrees, Extent2D source) -> ProjectiveImageFrame {
  return ResolveImageFrame(transform, crop, rotation_degrees, source, false);
}

}  // namespace alcedo

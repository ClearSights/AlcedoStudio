//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <array>

#include "edit/geometry/render_request.hpp"

namespace alcedo {

struct Point2d {
  double x = 0.0;
  double y = 0.0;
  auto   operator==(const Point2d&) const -> bool = default;
};

struct Rectangle2d {
  double             x      = 0.0;
  double             y      = 0.0;
  double             width  = 0.0;
  double             height = 0.0;

  /// Returns the boundary corners in clockwise order starting at the top left.
  [[nodiscard]] auto Corners() const -> std::array<Point2d, 4>;
};

/** @brief Double-precision row-major homogeneous matrix for column vectors. */
struct ProjectiveTransform {
  std::array<double, 9>     m{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  auto                      operator==(const ProjectiveTransform&) const -> bool = default;

  [[nodiscard]] static auto Translate(double x, double y) -> ProjectiveTransform;
  [[nodiscard]] static auto Scale(double x, double y) -> ProjectiveTransform;
  [[nodiscard]] static auto Rotate(double radians) -> ProjectiveTransform;
  /// Promotes the nine coefficients of a pixel-space matrix without changing its mapping.
  [[nodiscard]] static auto FromMatrix(const Matrix3x3& matrix) -> ProjectiveTransform;
  /// Converts the nine coefficients for float shader parameters. Consumers must divide by w.
  [[nodiscard]] auto        ToFloatMatrix() const -> Matrix3x3;

  /** @brief Maps a point with homogeneous division. Throws at or near the projective horizon. */
  [[nodiscard]] auto        Map(Point2d point) const -> Point2d;

  /** @brief Maps a convex quad. Throws if the horizon touches or crosses the quad. */
  [[nodiscard]] auto MapQuad(const std::array<Point2d, 4>& corners) const -> std::array<Point2d, 4>;

  /** @brief Returns columns d(output)/dx and d(output)/dy. Uses the same validation as Map. */
  [[nodiscard]] auto Jacobian(Point2d point) const -> std::array<Point2d, 2>;

  /** @brief Inverts in the matrix's coordinate system. Use normalized units for image maps. */
  [[nodiscard]] auto Inverse() const -> ProjectiveTransform;

  /// Inverts a pixel-space mapping after scaling both coordinate systems to unit extents.
  [[nodiscard]] auto InversePixelMap(Extent2D input, Extent2D output) const -> ProjectiveTransform;
};

/// Applies the right-hand map first, then the left-hand map.
[[nodiscard]] auto operator*(const ProjectiveTransform& left, const ProjectiveTransform& right)
    -> ProjectiveTransform;

/// Returns the axis-aligned bounds of a mapped quad with no horizon crossing.
[[nodiscard]] auto QuadBounds(const std::array<Point2d, 4>& corners) -> Rectangle2d;

/// True when perspective, aspect, scale, and offsets have their identity values.
[[nodiscard]] auto HasIdentityManualTransform(const ImageGeometryParams& image) -> bool;

/// Manual transform of a presented editing frame, in the centered reference plane.
struct CropPlaneTransform {
  ProjectiveTransform perspective;
  Point2d             scale{1.0, 1.0};
  Point2d             offset;
  bool                constrain_crop                                      = true;

  auto                operator==(const CropPlaneTransform&) const -> bool = default;

  /// Translation induced by changing the straightening pivot, in the same units as the pivot.
  [[nodiscard]] auto  PivotShift(float rotation_degrees) const -> ProjectiveTransform;
};

/// Validates and converts the manual controls into the matrices used by the render and crop tools.
[[nodiscard]] auto ResolveCropPlaneTransform(const ImageGeometryParams& image)
    -> CropPlaneTransform;

/**
 * @brief Resolves the reference-plane transform and frame together before pixel rounding.
 *
 * Matrices map between normalized planes centered at the reference image center with the half
 * long edge as the unit. The frame is in the resulting plane. Throws on invalid input, singular
 * maps, horizon crossings, or an empty constrained crop.
 */
struct ProjectiveImageFrame {
  ProjectiveTransform source_to_plane;
  ProjectiveTransform plane_to_source;
  Rectangle2d         frame;
};

[[nodiscard]] auto ResolveProjectiveImageFrame(const ImageGeometryParams& image, Extent2D source)
    -> ProjectiveImageFrame;

/// Resolves a crop against the given manual transform. The returned frame uses normalized plane
/// units.
[[nodiscard]] auto ResolveProjectiveCropFrame(const CropPlaneTransform& transform,
                                              NormalizedRect crop, float rotation_degrees,
                                              Extent2D source) -> ProjectiveImageFrame;

}  // namespace alcedo

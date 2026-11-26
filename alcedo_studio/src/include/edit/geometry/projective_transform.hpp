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

  [[nodiscard]] static auto Translate(double x, double y) -> ProjectiveTransform;
  [[nodiscard]] static auto Scale(double x, double y) -> ProjectiveTransform;
  [[nodiscard]] static auto Rotate(double radians) -> ProjectiveTransform;

  /** @brief Maps a point with homogeneous division. Throws at or near the projective horizon. */
  [[nodiscard]] auto        Map(Point2d point) const -> Point2d;

  /** @brief Maps a convex quad. Throws if the horizon touches or crosses the quad. */
  [[nodiscard]] auto MapQuad(const std::array<Point2d, 4>& corners) const -> std::array<Point2d, 4>;

  /** @brief Returns columns d(output)/dx and d(output)/dy. Uses the same validation as Map. */
  [[nodiscard]] auto Jacobian(Point2d point) const -> std::array<Point2d, 2>;

  /** @brief Inverts in the matrix's coordinate system. Use normalized units for image maps. */
  [[nodiscard]] auto Inverse() const -> ProjectiveTransform;
};

/// Applies the right-hand map first, then the left-hand map.
[[nodiscard]] auto operator*(const ProjectiveTransform& left, const ProjectiveTransform& right)
    -> ProjectiveTransform;

/// Returns the axis-aligned bounds of a mapped quad with no horizon crossing.
[[nodiscard]] auto QuadBounds(const std::array<Point2d, 4>& corners) -> Rectangle2d;

/// True when perspective, aspect, scale, and offsets have their identity values.
[[nodiscard]] auto HasIdentityManualTransform(const ImageGeometryParams& image) -> bool;

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

}  // namespace alcedo

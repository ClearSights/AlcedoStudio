//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "edit/geometry/projective_transform.hpp"
#include "edit/geometry/types.hpp"

namespace alcedo {

/**
 * @brief Parameters for the GPU geometry resamplers.
 *
 * The row-major inverse map takes output pixel centers to decoded pixel centers.
 * The field order and scalar types must match the shader parameters.
 */
struct GpuRenderGeometry {
  float         render_to_decoded[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  std::uint32_t decoded_width        = 0;
  std::uint32_t decoded_height       = 0;
  std::uint32_t render_width         = 0;
  std::uint32_t render_height        = 0;
  float         border_rgba[4]       = {0.0f, 0.0f, 0.0f, 1.0f};
  std::uint32_t filter               = 0;
};

static_assert(offsetof(GpuRenderGeometry, decoded_width) == 36);
static_assert(offsetof(GpuRenderGeometry, border_rgba) == 52);
static_assert(offsetof(GpuRenderGeometry, filter) == 68);
static_assert(sizeof(GpuRenderGeometry) == 72);
static_assert(static_cast<std::uint32_t>(TextureFilter::Bilinear) == 0);
static_assert(static_cast<std::uint32_t>(TextureFilter::Bicubic) == 1);

/** @brief Host-only mappings in pixel coordinates, including the accepted pre-rounding frame. */
struct ProjectiveRenderGeometry {
  ProjectiveTransform reference_to_edit;
  ProjectiveTransform reference_to_render;
  ProjectiveTransform render_to_reference;
  ProjectiveTransform render_to_decoded;
  Rectangle2d         frame_in_reference_plane;
};

/**
 * @brief Single rounding result for crop, rotation, view, and dynamic resolution.
 *
 * Image, raster-mask UV, and LLF must read this object. They must not re-round.
 */
struct ResolvedRenderGeometry {
  Extent2D          decoded_extent{};
  Extent2D          full_reference_extent{};
  Extent2D          edit_extent{};
  Extent2D          render_extent{};

  Matrix3x3         decoded_to_reference = Matrix3x3::Identity();
  Matrix3x3         reference_to_edit    = Matrix3x3::Identity();
  Matrix3x3         edit_to_render       = Matrix3x3::Identity();
  Matrix3x3         reference_to_render  = Matrix3x3::Identity();
  Matrix3x3         render_to_reference  = Matrix3x3::Identity();
  Matrix3x3         render_to_decoded    = Matrix3x3::Identity();

  RectI             required_decoded_region{};
  RectI             required_reference_region{};

  TextureFilter     filter = TextureFilter::Bilinear;
  GpuRenderGeometry gpu_data{};

  /// When present, these maps own the image geometry and supply the inverse map in gpu_data.
  /// The decoded-to-reference and edit-to-render steps are still affine.
  std::optional<ProjectiveRenderGeometry> projective;

  /// Editing frames carry their resolved manual transform to the viewport with the pixels.
  /// It describes the presented frame for its lifetime and lets the UI project live crop drags
  /// with the same transform. The crop and angle themselves stay with the crop tool.
  std::optional<CropPlaneTransform>       editing_crop_plane;

  /// Complete pixel-center maps. Use Map or MapQuad for point coordinates.
  [[nodiscard]] auto                      RenderToReferenceMap() const -> ProjectiveTransform {
    return projective ? projective->render_to_reference
                      : ProjectiveTransform::FromMatrix(render_to_reference);
  }
  [[nodiscard]] auto ReferenceToRenderMap() const -> ProjectiveTransform {
    return projective ? projective->reference_to_render
                      : ProjectiveTransform::FromMatrix(reference_to_render);
  }
};

/**
 * @brief True when render_to_decoded is identity and decoded extent equals render extent.
 *
 * GraphCompiler (G4+) may skip GeometryResamplePass when this is true.
 */
[[nodiscard]] inline auto IsIdentityResample(const ResolvedRenderGeometry& geometry) -> bool {
  return !geometry.projective && geometry.decoded_extent == geometry.render_extent &&
         IsApproxIdentity(geometry.render_to_decoded);
}

/**
 * @brief True when this render covers the whole EditSpace, not a viewport ROI.
 *
 * A full-view dynamic-resolution frame still returns true. A visible subrect returns
 * false. LLF uses this to decide whether the frame can initialize the canonical reference.
 */
[[nodiscard]] inline auto CoversFullEditSpace(const ResolvedRenderGeometry& geometry) -> bool {
  if (geometry.edit_extent.Empty() || geometry.render_extent.Empty()) {
    return false;
  }
  const float det = geometry.edit_to_render.m[0] * geometry.edit_to_render.m[4] -
                    geometry.edit_to_render.m[1] * geometry.edit_to_render.m[3];
  if (!std::isfinite(det) || std::fabs(det) < 1e-12f) {
    return false;
  }
  const auto render_to_edit = InvertAffine(geometry.edit_to_render);
  const auto top_left       = TransformPoint(render_to_edit, {0.0f, 0.0f});
  const auto bottom_right =
      TransformPoint(render_to_edit, {static_cast<float>(geometry.render_extent.width),
                                      static_cast<float>(geometry.render_extent.height)});
  constexpr float kPixel = 1.5f;
  return top_left.x <= kPixel && top_left.y <= kPixel &&
         bottom_right.x >= static_cast<float>(geometry.edit_extent.width) - kPixel &&
         bottom_right.y >= static_cast<float>(geometry.edit_extent.height) - kPixel;
}

/**
 * @brief Render pixels per reference pixel along one render axis.
 *
 * Uses the center of the complete edit frame for parameters that take a single scale, so a
 * viewport ROI does not change the scale. It is at most 1.
 */
[[nodiscard]] inline auto RenderAxisScale(const ResolvedRenderGeometry& geometry, bool horizontal)
    -> float {
  const auto axis_scale = [](auto reference_pixels_per_render_pixel) {
    using T = decltype(reference_pixels_per_render_pixel);
    if (!(reference_pixels_per_render_pixel > T{0}) ||
        !std::isfinite(reference_pixels_per_render_pixel)) {
      return 1.0f;
    }
    return static_cast<float>(
        std::clamp(T{1} / reference_pixels_per_render_pixel, static_cast<T>(1.0e-4), T{1}));
  };
  const std::size_t axis = horizontal ? 0 : 1;
  if (!geometry.projective) {
    const auto& matrix = geometry.render_to_reference.m;
    return axis_scale(std::hypot(matrix[axis], matrix[3 + axis]));
  }
  const auto edit_to_render = ProjectiveTransform::FromMatrix(geometry.edit_to_render);
  const auto center =
      edit_to_render.Map({0.5 * geometry.edit_extent.width, 0.5 * geometry.edit_extent.height});
  const auto derivative = geometry.projective->render_to_reference.Jacobian(center)[axis];
  return axis_scale(std::hypot(derivative.x, derivative.y));
}

}  // namespace alcedo

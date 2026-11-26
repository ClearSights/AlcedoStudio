//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <cstdint>
#include <optional>

#include "edit/geometry/types.hpp"

namespace alcedo {

inline constexpr double kManualTransformMinimum      = -100.0;
inline constexpr double kManualTransformMaximum      = 100.0;
inline constexpr double kManualTransformScaleMinimum = 50.0;
inline constexpr double kManualTransformScaleMaximum = 150.0;

/**
 * @brief Which rectangle of the rotated source becomes the edit space.
 */
enum class GeometryOutputFrame : std::uint8_t {
  /// Product output: the requested crop frame, optionally constrained to source coverage.
  CropFrame,
  /// Geometry panel preview: the bounding box of the whole rotated source. Its corners outside
  /// the source show the border color. Used only with a full-frame crop.
  RotatedSourceBounds,
  /// Internal editing view: the whole source with manual transforms, without straightening.
  TransformedSourceBounds,
};

/**
 * @brief Geometry inputs in pre-orientation reference axes. Manual fields use UI units.
 *
 * The document supplies the crop, straightening, manual controls, and crop constraint.
 * Viewport and dynamic resolution are separate requests.
 */
struct ImageGeometryParams {
  NormalizedRect      crop_rect{};
  float               rotation_degrees = 0.0f;
  GeometryOutputFrame output_frame     = GeometryOutputFrame::CropFrame;
  double              horizontal       = 0.0;
  double              vertical         = 0.0;
  double              aspect           = 0.0;
  double              scale            = 100.0;
  double              offset_x         = 0.0;
  double              offset_y         = 0.0;
  bool                constrain_crop   = true;
  auto                operator==(const ImageGeometryParams&) const -> bool = default;
};

/**
 * @brief Per-frame view crop in EditSpace and optional widget pixel size.
 *
 * @p viewport_extent of (0, 0) means size comes from the visible edit-pixel rectangle.
 */
struct ViewRequest {
  NormalizedRect visible_rect_in_edit_space{};
  Extent2D       viewport_extent{};
};

/**
 * @brief Per-frame output scale. Not persisted on PipelineDocument.
 *
 * @p max_edge of 0 disables the long-edge clamp. @p quality selects the resample filter.
 */
struct ResolutionRequest {
  float           render_scale = 1.0f;
  std::uint32_t   max_edge     = 0;
  RenderQuality   quality      = RenderQuality::Preview;
};

/**
 * @brief Neighborhood a runtime behavior needs in source pixels.
 *
 * Pointwise: zeros. Bilinear: radius 1. Bicubic: radius 2. LLF may set
 * @p requires_full_reference.
 */
struct SamplingFootprint {
  float radius_x                 = 0.0f;
  float radius_y                 = 0.0f;
  bool  requires_full_reference  = false;
};

/**
 * @brief How one render reads the document crop and rotation.
 *
 * Only editor viewport requests set @ref GeometryEditingSource, while the Geometry panel is
 * open. Only the AI Mask analysis render sets @ref ReferenceSource. The document is never
 * changed; the value selects which user geometry the frame binds.
 */
enum class DocumentGeometryUse : std::uint8_t {
  ApplyCropAndRotation,   ///< Default: every product render applies the document geometry.
  GeometryEditingSource,  ///< Geometry panel open: manual transforms, without crop or
                          ///< straightening.
  /// AI Mask analysis render: the whole source without crop and rotation. EditSpace is
  /// ReferenceSpace, so `render_to_reference` is a pure scale.
  ReferenceSource,
};

struct RenderRequest {
  ViewRequest         view{};
  ResolutionRequest   resolution{};
  SamplingFootprint   footprint{};
  DocumentGeometryUse document_geometry = DocumentGeometryUse::ApplyCropAndRotation;
  /// Explicit internal final-frame input before manual transforms are stored in the document.
  /// Valid with ApplyCropAndRotation or GeometryEditingSource and a CropFrame input.
  std::optional<ImageGeometryParams> final_frame_geometry;
};

}  // namespace alcedo

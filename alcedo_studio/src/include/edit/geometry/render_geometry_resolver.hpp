//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include "edit/geometry/render_request.hpp"
#include "edit/geometry/resolved_render_geometry.hpp"
#include "edit/geometry/source_geometry.hpp"

namespace alcedo {

/**
 * @brief Resolves crop, image transforms, view ROI, and resolution into matrices and rectangles.
 *
 * Does not process pixels or allocate GPU memory. All integer rounding for image, mask,
 * and LLF happens here.
 *
 * @param source Decoded and full-reference extents. @p decoded_to_reference is rebuilt
 *        from those extents.
 * @param image Crop, rotation, internal manual transforms, and the output frame selection.
 * @param view Visible EditSpace rect and optional viewport size.
 * @param resolution Render scale, max edge, and filter quality.
 * @param footprint Source-pixel neighborhood. @p requires_full_reference forces full decoded
 *        and reference required rects (LLF).
 *
 * @throws std::runtime_error for invalid inputs, an impossible constrained frame, or a singular
 *         mapping or horizon crossing. The function has no shared state and publishes no result
 *         on error.
 */
[[nodiscard]] auto ResolveRenderGeometry(const SourceGeometry&     source,
                                         const ImageGeometryParams& image,
                                         const ViewRequest&         view,
                                         const ResolutionRequest&   resolution,
                                         const SamplingFootprint&   footprint)
    -> ResolvedRenderGeometry;

}  // namespace alcedo

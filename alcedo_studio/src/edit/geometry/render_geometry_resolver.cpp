//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "edit/geometry/render_geometry_resolver.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "edit/geometry/crop_frame.hpp"
#include "edit/geometry/projective_transform.hpp"

namespace alcedo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

void            ThrowIfInvalidExtent(const Extent2D& extent, const char* name) {
  if (extent.Empty()) {
    throw std::runtime_error(std::string("ResolveRenderGeometry: ") + name + " must be positive");
  }
}

auto ClampNormalizedRect(NormalizedRect rect, const char* name) -> NormalizedRect {
  if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.w) ||
      !std::isfinite(rect.h)) {
    throw std::runtime_error(std::string("ResolveRenderGeometry: ") + name + " must be finite");
  }
  rect.w = std::clamp(rect.w, kGeometryMinNormalizedSize, 1.0f);
  rect.h = std::clamp(rect.h, kGeometryMinNormalizedSize, 1.0f);
  rect.x = std::clamp(rect.x, 0.0f, 1.0f - rect.w);
  rect.y = std::clamp(rect.y, 0.0f, 1.0f - rect.h);
  return rect;
}

auto RoundExtent(float width, float height) -> Extent2D {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f) {
    throw std::runtime_error(
        "ResolveRenderGeometry: extent components must be positive and finite");
  }
  const auto rounded_w = static_cast<std::uint32_t>(std::max(1L, std::lround(width)));
  const auto rounded_h = static_cast<std::uint32_t>(std::max(1L, std::lround(height)));
  return Extent2D{rounded_w, rounded_h};
}

auto ClampMaxEdge(Extent2D extent, std::uint32_t max_edge) -> Extent2D {
  if (max_edge == 0) {
    return extent;
  }
  const auto long_edge = std::max(extent.width, extent.height);
  if (long_edge <= max_edge) {
    return extent;
  }
  const float scale = static_cast<float>(max_edge) / static_cast<float>(long_edge);
  return RoundExtent(static_cast<float>(extent.width) * scale,
                     static_cast<float>(extent.height) * scale);
}

auto ClampRoiRenderExtentToNativePixels(Extent2D requested, float visible_width,
                                        float visible_height) -> Extent2D {
  const auto native = RoundExtent(visible_width, visible_height);
  if (requested.width >= native.width && requested.height >= native.height) {
    return native;
  }
  const float scale =
      std::min({1.0f, static_cast<float>(requested.width) / static_cast<float>(native.width),
                static_cast<float>(requested.height) / static_cast<float>(native.height)});
  return RoundExtent(static_cast<float>(native.width) * scale,
                     static_cast<float>(native.height) * scale);
}

struct Aabb {
  float min_x = 0.0f;
  float min_y = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
};

auto AabbOfTransformedCorners(const Matrix3x3& matrix, Vector2 a, Vector2 b, Vector2 c, Vector2 d)
    -> Aabb {
  const Vector2 p[4] = {TransformPoint(matrix, a), TransformPoint(matrix, b),
                        TransformPoint(matrix, c), TransformPoint(matrix, d)};
  Aabb          box{p[0].x, p[0].y, p[0].x, p[0].y};
  for (int i = 1; i < 4; ++i) {
    box.min_x = std::min(box.min_x, p[i].x);
    box.min_y = std::min(box.min_y, p[i].y);
    box.max_x = std::max(box.max_x, p[i].x);
    box.max_y = std::max(box.max_y, p[i].y);
  }
  return box;
}

auto ClampRectI(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, Extent2D extent)
    -> RectI {
  const auto max_x = static_cast<std::int32_t>(extent.width);
  const auto max_y = static_cast<std::int32_t>(extent.height);
  x0               = std::clamp(x0, 0, max_x);
  x1               = std::clamp(x1, 0, max_x);
  y0               = std::clamp(y0, 0, max_y);
  y1               = std::clamp(y1, 0, max_y);
  if (x1 < x0) {
    std::swap(x0, x1);
  }
  if (y1 < y0) {
    std::swap(y0, y1);
  }
  if (x1 == x0) {
    if (x0 > 0) {
      --x0;
    } else if (x1 < max_x) {
      ++x1;
    }
  }
  if (y1 == y0) {
    if (y0 > 0) {
      --y0;
    } else if (y1 < max_y) {
      ++y1;
    }
  }
  return RectI{x0, y0, x1 - x0, y1 - y0};
}

auto RequiredRegionFromAabb(const Aabb& box, float radius_x, float radius_y, Extent2D extent)
    -> RectI {
  const auto x0 = static_cast<std::int32_t>(std::floor(box.min_x - radius_x));
  const auto y0 = static_cast<std::int32_t>(std::floor(box.min_y - radius_y));
  const auto x1 = static_cast<std::int32_t>(std::ceil(box.max_x + radius_x));
  const auto y1 = static_cast<std::int32_t>(std::ceil(box.max_y + radius_y));
  return ClampRectI(x0, y0, x1, y1, extent);
}

/// Packs the resolved inverse pixel map and sampling options for all GPU backends.
void FillGpuData(ResolvedRenderGeometry& geometry) {
  for (int i = 0; i < 9; ++i) {
    geometry.gpu_data.render_to_decoded[i] =
        geometry.projective ? static_cast<float>(geometry.projective->render_to_decoded.m[i])
                            : geometry.render_to_decoded.m[i];
  }
  geometry.gpu_data.decoded_width  = geometry.decoded_extent.width;
  geometry.gpu_data.decoded_height = geometry.decoded_extent.height;
  geometry.gpu_data.render_width   = geometry.render_extent.width;
  geometry.gpu_data.render_height  = geometry.render_extent.height;
  geometry.gpu_data.border_rgba[0] = 0.0f;
  geometry.gpu_data.border_rgba[1] = 0.0f;
  geometry.gpu_data.border_rgba[2] = 0.0f;
  geometry.gpu_data.border_rgba[3] = 1.0f;
  geometry.gpu_data.filter         = static_cast<std::uint32_t>(geometry.filter);
}

/// Rounds a positive frame extent once, within the range of the required-region rectangles.
auto RoundProjectiveExtent(double width, double height) -> Extent2D {
  constexpr double kMax = std::numeric_limits<std::int32_t>::max();
  if (!(width > 0.0 && height > 0.0 && width <= kMax && height <= kMax)) {
    throw std::runtime_error("ResolveRenderGeometry: projective frame extent is out of range");
  }
  return {static_cast<std::uint32_t>(std::max(1.0, std::round(width))),
          static_cast<std::uint32_t>(std::max(1.0, std::round(height)))};
}

/// Applies the viewport target, resolution cap, and native ROI limit before rounding.
auto ProjectiveRenderExtent(Rectangle2d visible_pixels, bool full_view, const ViewRequest& view,
                            const ResolutionRequest& resolution) -> Extent2D {
  const bool   use_viewport = !view.viewport_extent.Empty();
  const double target_w     = use_viewport ? view.viewport_extent.width : visible_pixels.width;
  const double target_h     = use_viewport ? view.viewport_extent.height : visible_pixels.height;
  double       width        = target_w * resolution.render_scale;
  double       height       = target_h * resolution.render_scale;
  if (resolution.max_edge > 0) {
    const double scale = std::min(1.0, resolution.max_edge / std::max(width, height));
    width *= scale;
    height *= scale;
  }
  if (!full_view) {
    const double scale =
        std::min({1.0, width / visible_pixels.width, height / visible_pixels.height});
    width  = visible_pixels.width * scale;
    height = visible_pixels.height * scale;
  }
  return RoundProjectiveExtent(width, height);
}

/// Bounds a projectively mapped frame by its corners and expands for the source-pixel footprint.
auto ProjectiveRequiredRegion(const ProjectiveTransform& map, Rectangle2d render_frame,
                              const SamplingFootprint& footprint, Extent2D extent) -> RectI {
  if (footprint.requires_full_reference) {
    return {0, 0, static_cast<std::int32_t>(extent.width),
            static_cast<std::int32_t>(extent.height)};
  }
  const auto bounds = QuadBounds(map.MapQuad(render_frame.Corners()));
  const auto clip   = [](double value, double limit) {
    return static_cast<std::int32_t>(std::clamp(value, 0.0, limit));
  };
  return ClampRectI(clip(std::floor(bounds.x - footprint.radius_x), extent.width),
                    clip(std::floor(bounds.y - footprint.radius_y), extent.height),
                    clip(std::ceil(bounds.x + bounds.width + footprint.radius_x), extent.width),
                    clip(std::ceil(bounds.y + bounds.height + footprint.radius_y), extent.height),
                    extent);
}

/// Converts normalized projective geometry into the pixel spaces required by a render request.
auto ResolveProjectiveRenderGeometry(const SourceGeometry& source, const ImageGeometryParams& image,
                                     const ViewRequest& view, const ResolutionRequest& resolution,
                                     const SamplingFootprint& footprint) -> ResolvedRenderGeometry {
  const auto     decoded   = source.decoded_extent;
  const auto     reference = source.full_reference_extent;
  constexpr auto kMax      = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
  if (std::max({decoded.width, decoded.height, reference.width, reference.height}) > kMax) {
    throw std::runtime_error("ResolveRenderGeometry: projective source extent is out of range");
  }
  const auto [source_to_plane, plane_to_source, frame] =
      ResolveProjectiveImageFrame(image, reference);
  const auto visible =
      ClampNormalizedRect(view.visible_rect_in_edit_space, "visible_rect_in_edit_space");
  const double q             = 0.5 * std::max(reference.width, reference.height);
  const double center_x      = 0.5 * reference.width;
  const double center_y      = 0.5 * reference.height;
  const auto   to_normalized = ProjectiveTransform::Scale(1.0 / q, 1.0 / q) *
                               ProjectiveTransform::Translate(-center_x, -center_y);
  const auto   to_reference =
      ProjectiveTransform::Translate(center_x, center_y) * ProjectiveTransform::Scale(q, q);

  ResolvedRenderGeometry result;
  result.decoded_extent        = decoded;
  result.full_reference_extent = reference;
  result.decoded_to_reference  = MakeDecodedToReference(decoded, reference);
  result.edit_extent           = RoundProjectiveExtent(frame.width * q, frame.height * q);
  const double edit_w          = result.edit_extent.width;
  const double edit_h          = result.edit_extent.height;
  const auto   to_edit = ProjectiveTransform::Scale(edit_w / frame.width, edit_h / frame.height) *
                         ProjectiveTransform::Translate(-frame.x, -frame.y);
  const Rectangle2d visible_pixels{visible.x * edit_w, visible.y * edit_h, visible.w * edit_w,
                                   visible.h * edit_h};
  result.render_extent =
      ProjectiveRenderExtent(visible_pixels, visible.IsFullFrame(), view, resolution);
  const double render_w  = result.render_extent.width;
  const double render_h  = result.render_extent.height;
  const double sx        = render_w / visible_pixels.width;
  const double sy        = render_h / visible_pixels.height;
  const auto   to_render = ProjectiveTransform::Scale(sx, sy) *
                           ProjectiveTransform::Translate(-visible_pixels.x, -visible_pixels.y);
  result.edit_to_render  = Matrix3x3::Scale(static_cast<float>(sx), static_cast<float>(sy)) *
                           Matrix3x3::Translate(static_cast<float>(-visible_pixels.x),
                                                static_cast<float>(-visible_pixels.y));

  // Only invert the homography in normalized coordinates. Invert view, crop, and decode
  // placement from their known scale and offset to avoid conditioning on pixel units.
  const auto render_to_plane =
      ProjectiveTransform::Translate(frame.x, frame.y) *
      ProjectiveTransform::Scale(frame.width / edit_w, frame.height / edit_h) *
      ProjectiveTransform::Translate(visible_pixels.x, visible_pixels.y) *
      ProjectiveTransform::Scale(1.0 / sx, 1.0 / sy);
  const auto reference_to_edit   = to_edit * source_to_plane * to_normalized;
  const auto render_to_reference = to_reference * plane_to_source * render_to_plane;
  const auto render_to_decoded =
      ProjectiveTransform::Scale(static_cast<double>(decoded.width) / reference.width,
                                 static_cast<double>(decoded.height) / reference.height) *
      render_to_reference;
  result.projective = ProjectiveRenderGeometry{
      reference_to_edit,
      to_render * reference_to_edit,
      render_to_reference,
      render_to_decoded,
      {frame.x * q + center_x, frame.y * q + center_y, frame.width * q, frame.height * q}};

  result.filter = resolution.quality == RenderQuality::Export ? TextureFilter::Bicubic
                                                              : TextureFilter::Bilinear;
  const Rectangle2d render_frame{0.0, 0.0, render_w, render_h};
  // The sampler needs its own taps as well as the footprint requested by later passes.
  const float             filter_radius = result.filter == TextureFilter::Bicubic ? 2.0f : 1.0f;
  const SamplingFootprint required_footprint{std::max(footprint.radius_x, filter_radius),
                                             std::max(footprint.radius_y, filter_radius),
                                             footprint.requires_full_reference};
  result.required_reference_region =
      ProjectiveRequiredRegion(render_to_reference, render_frame, required_footprint, reference);
  result.required_decoded_region =
      ProjectiveRequiredRegion(render_to_decoded, render_frame, required_footprint, decoded);
  FillGpuData(result);
  return result;
}

}  // namespace

auto ResolveRenderGeometry(const SourceGeometry& source, const ImageGeometryParams& image,
                           const ViewRequest& view, const ResolutionRequest& resolution,
                           const SamplingFootprint& footprint) -> ResolvedRenderGeometry {
  ThrowIfInvalidExtent(source.decoded_extent, "decoded_extent");
  ThrowIfInvalidExtent(source.full_reference_extent, "full_reference_extent");
  if (!std::isfinite(image.rotation_degrees)) {
    throw std::runtime_error("ResolveRenderGeometry: rotation must be finite");
  }
  if (!std::isfinite(resolution.render_scale) || resolution.render_scale <= 0.0f) {
    throw std::runtime_error("ResolveRenderGeometry: render_scale must be positive and finite");
  }
  if (!std::isfinite(footprint.radius_x) || !std::isfinite(footprint.radius_y) ||
      footprint.radius_x < 0.0f || footprint.radius_y < 0.0f) {
    throw std::runtime_error("ResolveRenderGeometry: footprint radius must be finite and >= 0");
  }

  if (!HasIdentityManualTransform(image) ||
      image.output_frame == GeometryOutputFrame::TransformedSourceBounds ||
      (image.output_frame == GeometryOutputFrame::CropFrame && !image.constrain_crop)) {
    return ResolveProjectiveRenderGeometry(source, image, view, resolution, footprint);
  }

  // The preview frame reads the whole source; only the product crop frame is constrained to
  // keep its rotated corners inside the source.
  const auto crop =
      image.output_frame == GeometryOutputFrame::CropFrame
          ? ClampCropToRotatedSource(image.crop_rect, image.rotation_degrees,
                                     source.full_reference_extent)
          : ClampNormalizedRect(image.crop_rect, "crop_rect");
  const auto visible =
      ClampNormalizedRect(view.visible_rect_in_edit_space, "visible_rect_in_edit_space");
  const float theta     = NormalizeRotationDegrees(image.rotation_degrees) * (kPi / 180.0f);

  const float full_w    = static_cast<float>(source.full_reference_extent.width);
  const float full_h    = static_cast<float>(source.full_reference_extent.height);
  const float left      = crop.x * full_w;
  const float top       = crop.y * full_h;
  const float right     = (crop.x + crop.w) * full_w;
  const float bottom    = (crop.y + crop.h) * full_h;
  const float crop_w    = right - left;
  const float crop_h    = bottom - top;
  const float cx        = 0.5f * (left + right);
  const float cy        = 0.5f * (top + bottom);

  const auto  to_center = Matrix3x3::Translate(-cx, -cy);
  const auto  rotate    = Matrix3x3::Rotate(theta);
  const auto  centered  = rotate * to_center;
  const auto  rotated_box =
      AabbOfTransformedCorners(centered, Vector2{left, top}, Vector2{right, top},
                               Vector2{right, bottom}, Vector2{left, bottom});
  const float aabb_w = std::max(rotated_box.max_x - rotated_box.min_x, kGeometryMinNormalizedSize);
  const float aabb_h = std::max(rotated_box.max_y - rotated_box.min_y, kGeometryMinNormalizedSize);

  ResolvedRenderGeometry geometry;
  geometry.decoded_extent        = source.decoded_extent;
  geometry.full_reference_extent = source.full_reference_extent;
  geometry.decoded_to_reference =
      MakeDecodedToReference(source.decoded_extent, source.full_reference_extent);

  if (image.output_frame == GeometryOutputFrame::RotatedSourceBounds) {
    geometry.edit_extent       = RoundExtent(aabb_w, aabb_h);
    const float sx             = static_cast<float>(geometry.edit_extent.width) / aabb_w;
    const float sy             = static_cast<float>(geometry.edit_extent.height) / aabb_h;
    geometry.reference_to_edit = Matrix3x3::Scale(sx, sy) *
                                 Matrix3x3::Translate(-rotated_box.min_x, -rotated_box.min_y) *
                                 centered;
  } else {
    geometry.edit_extent = RoundExtent(crop_w, crop_h);
    const float sx       = static_cast<float>(geometry.edit_extent.width) / crop_w;
    const float sy       = static_cast<float>(geometry.edit_extent.height) / crop_h;
    geometry.reference_to_edit =
        Matrix3x3::Translate(static_cast<float>(geometry.edit_extent.width) * 0.5f,
                             static_cast<float>(geometry.edit_extent.height) * 0.5f) *
        Matrix3x3::Scale(sx, sy) * centered;
  }

  const float edit_w   = static_cast<float>(geometry.edit_extent.width);
  const float edit_h   = static_cast<float>(geometry.edit_extent.height);
  const float v_left   = visible.x * edit_w;
  const float v_top    = visible.y * edit_h;
  const float v_right  = (visible.x + visible.w) * edit_w;
  const float v_bottom = (visible.y + visible.h) * edit_h;
  const float vis_w    = std::max(v_right - v_left, kGeometryMinNormalizedSize);
  const float vis_h    = std::max(v_bottom - v_top, kGeometryMinNormalizedSize);

  Extent2D    target;
  if (view.viewport_extent.width > 0 && view.viewport_extent.height > 0) {
    target = view.viewport_extent;
  } else {
    target = RoundExtent(vis_w, vis_h);
  }
  geometry.render_extent =
      ClampMaxEdge(RoundExtent(static_cast<float>(target.width) * resolution.render_scale,
                               static_cast<float>(target.height) * resolution.render_scale),
                   resolution.max_edge);
  // Preserve the pre-DAG preview behavior for a visible subregion: the image pipeline may
  // downsample native ROI pixels, but it must not manufacture a larger patch with bilinear or
  // bicubic interpolation. When the native ROI is smaller than the physical viewport target,
  // the viewer enlarges the patch with its nearest-neighbor sampler.
  if (!visible.IsFullFrame()) {
    geometry.render_extent =
        ClampRoiRenderExtentToNativePixels(geometry.render_extent, vis_w, vis_h);
  }

  const float rsx              = static_cast<float>(geometry.render_extent.width) / vis_w;
  const float rsy              = static_cast<float>(geometry.render_extent.height) / vis_h;
  geometry.edit_to_render      = Matrix3x3::Scale(rsx, rsy) * Matrix3x3::Translate(-v_left, -v_top);

  geometry.reference_to_render = geometry.edit_to_render * geometry.reference_to_edit;
  geometry.render_to_reference = InvertAffine(geometry.reference_to_render);
  geometry.render_to_decoded =
      InvertAffine(geometry.decoded_to_reference) * geometry.render_to_reference;

  geometry.filter         = resolution.quality == RenderQuality::Export ? TextureFilter::Bicubic
                                                                        : TextureFilter::Bilinear;

  const float render_w    = static_cast<float>(geometry.render_extent.width);
  const float render_h    = static_cast<float>(geometry.render_extent.height);
  const auto  decoded_box = AabbOfTransformedCorners(
      geometry.render_to_decoded, Vector2{0.0f, 0.0f}, Vector2{render_w, 0.0f},
      Vector2{render_w, render_h}, Vector2{0.0f, render_h});
  const auto reference_box = AabbOfTransformedCorners(
      geometry.render_to_reference, Vector2{0.0f, 0.0f}, Vector2{render_w, 0.0f},
      Vector2{render_w, render_h}, Vector2{0.0f, render_h});

  if (footprint.requires_full_reference) {
    geometry.required_decoded_region =
        RectI{0, 0, static_cast<std::int32_t>(geometry.decoded_extent.width),
              static_cast<std::int32_t>(geometry.decoded_extent.height)};
    geometry.required_reference_region =
        RectI{0, 0, static_cast<std::int32_t>(geometry.full_reference_extent.width),
              static_cast<std::int32_t>(geometry.full_reference_extent.height)};
  } else {
    geometry.required_decoded_region = RequiredRegionFromAabb(
        decoded_box, footprint.radius_x, footprint.radius_y, geometry.decoded_extent);
    geometry.required_reference_region = RequiredRegionFromAabb(
        reference_box, footprint.radius_x, footprint.radius_y, geometry.full_reference_extent);
  }

  FillGpuData(geometry);
  return geometry;
}

namespace {

/// Resolves the live crop with the manual transform of the pixels currently being edited.
auto ResolveEditingCropFrame(const ResolvedRenderGeometry& editing, NormalizedRect crop,
                             float rotation_degrees) -> ProjectiveImageFrame {
  if (!editing.editing_crop_plane || editing.full_reference_extent.Empty()) {
    throw std::runtime_error("ResolveRenderGeometry: missing editing frame geometry");
  }
  if (!std::isfinite(crop.x) || !std::isfinite(crop.y) || !std::isfinite(crop.w) ||
      !std::isfinite(crop.h)) {
    throw std::runtime_error("ResolveRenderGeometry: crop coordinates must be finite");
  }
  const auto source    = editing.full_reference_extent;
  auto       transform = *editing.editing_crop_plane;
  if (!editing.projective && transform.constrain_crop) {
    // The unchanged affine product render uses the original rotated-source constraint.
    crop                     = ClampCropToRotatedSource(crop, rotation_degrees, source);
    transform.constrain_crop = false;
  }
  return ResolveProjectiveCropFrame(transform, crop, rotation_degrees, source);
}

}  // namespace

auto ResolveEditingCropPlacement(const ResolvedRenderGeometry& editing, NormalizedRect crop,
                                 float rotation_degrees) -> EditingCropPlacement {
  const auto [source_to_plane, plane_to_source, frame] =
      ResolveEditingCropFrame(editing, crop, rotation_degrees);
  const auto    source = editing.full_reference_extent;
  const double  q      = 0.5 * std::max(source.width, source.height);
  const Point2d origin{source.width * 0.5, source.height * 0.5};
  const auto    to_reference =
      ProjectiveTransform::Translate(origin.x, origin.y) * ProjectiveTransform::Scale(q, q);
  const auto from_reference = ProjectiveTransform::Scale(1.0 / q, 1.0 / q) *
                              ProjectiveTransform::Translate(-origin.x, -origin.y);
  const auto uv_to_plane    = ProjectiveTransform::Translate(frame.x, frame.y) *
                              ProjectiveTransform::Scale(frame.width, frame.height);
  // Crop UVs are final-output coordinates before rounding. The inverse final map followed
  // by the presented editing map keeps the boundary in the same place at every render scale.
  const auto uv_to_render =
      editing.ReferenceToRenderMap() * to_reference * plane_to_source * uv_to_plane;
  (void)uv_to_render.MapQuad(Rectangle2d{0.0, 0.0, 1.0, 1.0}.Corners());

  // Removing the pivot-induced translation keeps the drag plane fixed as a crop edge moves.
  // A new box's center must be converted back to the requested pivot before validation.
  const auto pivot_shift = editing.editing_crop_plane->PivotShift(rotation_degrees);
  const auto shift = pivot_shift.Map({frame.x + 0.5 * frame.width, frame.y + 0.5 * frame.height});
  const auto shift_at_origin = pivot_shift.Map(origin);
  const ProjectiveTransform crop_center_to_drag_center{
      {1.0 - pivot_shift.m[0], -pivot_shift.m[1], shift_at_origin.x, -pivot_shift.m[3],
       1.0 - pivot_shift.m[4], shift_at_origin.y, 0.0, 0.0, 1.0}};
  const auto render_to_drag = ProjectiveTransform::Translate(-q * shift.x, -q * shift.y) *
                              to_reference * source_to_plane * from_reference *
                              editing.RenderToReferenceMap();
  return {uv_to_render,
          render_to_drag,
          crop_center_to_drag_center,
          {(frame.x - shift.x) * q + origin.x, (frame.y - shift.y) * q + origin.y, frame.width * q,
           frame.height * q}};
}

auto ResolveEditingCropRect(const ResolvedRenderGeometry& editing, NormalizedRect crop,
                            float rotation_degrees) -> NormalizedRect {
  const auto   frame  = ResolveEditingCropFrame(editing, crop, rotation_degrees).frame;
  const auto   source = editing.full_reference_extent;
  const double q      = 0.5 * std::max(source.width, source.height);
  return {static_cast<float>(0.5 + frame.x * q / source.width),
          static_cast<float>(0.5 + frame.y * q / source.height),
          static_cast<float>(frame.width * q / source.width),
          static_cast<float>(frame.height * q / source.height)};
}

auto ResolveGeometryEditingView(const SourceGeometry& source, const ImageGeometryParams& image,
                                const ViewRequest& view, const ResolutionRequest& resolution,
                                const SamplingFootprint& footprint) -> ResolvedRenderGeometry {
  const ImageGeometryParams editing{
      .output_frame = HasIdentityManualTransform(image)
                          ? GeometryOutputFrame::CropFrame
                          : GeometryOutputFrame::TransformedSourceBounds,
      .horizontal   = image.horizontal,
      .vertical     = image.vertical,
      .aspect       = image.aspect,
      .scale        = image.scale,
      .offset_x     = image.offset_x,
      .offset_y     = image.offset_y,
  };
  auto geometry               = ResolveRenderGeometry(source, editing, view, resolution, footprint);
  geometry.editing_crop_plane = ResolveCropPlaneTransform(image);
  (void)ResolveEditingCropPlacement(geometry, image.crop_rect, image.rotation_degrees);
  return geometry;
}

}  // namespace alcedo

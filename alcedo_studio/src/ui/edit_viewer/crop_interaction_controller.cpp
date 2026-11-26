//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/edit_viewer/crop_interaction_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

#include "edit/geometry/render_geometry_resolver.hpp"

namespace alcedo {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Measures rotation before aspect scaling. Distance checks still use widget pixels.
auto            PointerAngle(const QPointF& vector, double y_scale) -> float {
  return static_cast<float>(std::atan2(vector.y() * y_scale, vector.x()));
}

auto            BoxCorner(const QRectF& box, int corner_index) -> QPointF {
  switch (corner_index) {
    case 0:
      return box.topLeft();
    case 1:
      return box.topRight();
    case 2:
      return box.bottomRight();
    case 3:
      return box.bottomLeft();
    default:
      return box.center();
  }
}

struct WidgetCropPlacement {
  EditingCropPlacement crop;
  ProjectiveTransform  render_to_widget;
  ProjectiveTransform  widget_to_render;
};

/// Composes the editing render's pixel space with the viewport's logical-pixel fit.
auto PlaceCropInWidget(const CropOverlayState& crop, const MaskEditViewMapping& mapping)
    -> WidgetCropPlacement {
  if (!mapping.geometry.editing_crop_plane ||
      mapping.presentation != FramePresentationMode::FullFrame ||
      !MaskEditGeometry::IsValid(mapping)) {
    throw std::runtime_error("Crop tool: no valid editing image is presented");
  }

  const auto top_left = ViewportMapper::ImageUvToWidgetPoint(
      {0.0, 0.0}, mapping.widget, mapping.photograph, mapping.zoom, mapping.pan);
  const auto bottom_right = ViewportMapper::ImageUvToWidgetPoint(
      {1.0, 1.0}, mapping.widget, mapping.photograph, mapping.zoom, mapping.pan);
  const double sx = (bottom_right->x() - top_left->x()) / mapping.geometry.render_extent.width;
  const double sy = (bottom_right->y() - top_left->y()) / mapping.geometry.render_extent.height;
  if (!(sx > 0.0 && sy > 0.0 && std::isfinite(sx) && std::isfinite(sy))) {
    throw std::runtime_error("Crop tool: invalid viewport placement");
  }
  const auto placement = ResolveEditingCropPlacement(
      mapping.geometry, CropGeometry::ToNormalizedRect(crop.rect), crop.rotation_degrees);
  return {placement,
          ProjectiveTransform::Translate(top_left->x(), top_left->y()) *
              ProjectiveTransform::Scale(sx, sy),
          ProjectiveTransform::Scale(1.0 / sx, 1.0 / sy) *
              ProjectiveTransform::Translate(-top_left->x(), -top_left->y())};
}

/// Gets the same widget boundary used by the renderer and the pointer hit test.
auto MapCropCorners(const ProjectiveTransform& uv_to_widget) -> std::array<QPointF, 4> {
  const auto points = uv_to_widget.MapQuad(Rectangle2d{0.0, 0.0, 1.0, 1.0}.Corners());
  return {QPointF(points[0].x, points[0].y), QPointF(points[1].x, points[1].y),
          QPointF(points[2].x, points[2].y), QPointF(points[3].x, points[3].y)};
}

/// Converts a drag box to a crop request. The resolver then applies the final coverage limits.
auto CropFromDragBox(const QRectF& box, const ProjectiveTransform& center_to_crop, Extent2D source)
    -> NormalizedRect {
  const double width    = box.width() / source.width;
  const double height   = box.height() / source.height;
  const double max_side = std::max({1.0, width, height});
  const double w        = std::max<double>(kGeometryMinNormalizedSize, width / max_side);
  const double h        = std::max<double>(kGeometryMinNormalizedSize, height / max_side);
  const auto   center   = center_to_crop.Map({box.center().x(), box.center().y()});
  return {static_cast<float>(center.x / source.width - 0.5 * w),
          static_cast<float>(center.y / source.height - 0.5 * h), static_cast<float>(w),
          static_cast<float>(h)};
}

}  // namespace

auto CropInteractionController::MapCropToWidget(const CropOverlayState&    crop,
                                                const MaskEditViewMapping& mapping)
    -> std::optional<MappedCrop> {
  try {
    const auto placement    = PlaceCropInWidget(crop, mapping);
    const auto uv_to_widget = placement.render_to_widget * placement.crop.crop_uv_to_render;
    return MappedCrop{uv_to_widget, MapCropCorners(uv_to_widget)};
  } catch (const std::runtime_error&) {
    return std::nullopt;
  }
}

auto CropInteractionController::HandlePress(ViewerState& state, const MaskEditViewMapping& mapping,
                                            const QPointF& event_pos) -> CropInteractionResult {
  CropInteractionResult result;
  const auto            crop_state = state.GetCropOverlay();
  if (!crop_state.tool_enabled || !crop_state.overlay_visible) {
    return result;
  }
  // The crop tool owns left presses while it is visible, including presses it ignores.
  result.consumed = true;
  CropHitTestResult hit;
  try {
    const auto placement    = PlaceCropInWidget(crop_state, mapping);
    const auto uv_to_widget = placement.render_to_widget * placement.crop.crop_uv_to_render;
    const auto corners      = MapCropCorners(uv_to_widget);
    hit                     = CropGeometry::HitTestWidgetGeometry(corners, event_pos);
    hit.inside_crop         = CropGeometry::IsPointInsideQuad(corners, event_pos);
    const bool frame_control =
        hit.rotate_hit || hit.corner_index >= 0 || hit.edge != CropEdge::None;
    if (!frame_control) {
      const auto reference = MaskEditGeometry::MapItemToReference(mapping, event_pos, false);
      if (!reference) {
        return result;
      }
    }

    const auto center       = uv_to_widget.Map({0.5, 0.5});
    drag_center_widget_pos_ = QPointF(center.x, center.y);
    if (hit.rotate_hit) {
      const auto&  geometry     = mapping.geometry;
      const auto&  scale        = geometry.editing_crop_plane->scale;
      const auto   plane_width  = geometry.projective
                                      ? geometry.projective->frame_in_reference_plane.width
                                      : geometry.full_reference_extent.width;
      const auto   plane_height = geometry.projective
                                      ? geometry.projective->frame_in_reference_plane.height
                                      : geometry.full_reference_extent.height;
      const double widget_per_x =
          placement.render_to_widget.m[0] * geometry.render_extent.width * scale.x / plane_width;
      const double widget_per_y =
          placement.render_to_widget.m[4] * geometry.render_extent.height * scale.y / plane_height;
      drag_rotation_y_scale_ = widget_per_x / widget_per_y;
    } else {
      widget_to_drag_plane_ = placement.crop.render_to_drag_plane * placement.widget_to_render;
      drag_center_to_crop_center_ = placement.crop.crop_center_to_drag_center.InversePixelMap(
          mapping.geometry.full_reference_extent, mapping.geometry.full_reference_extent);
      const auto anchor  = widget_to_drag_plane_.Map({event_pos.x(), event_pos.y()});
      drag_anchor_frame_ = QPointF(anchor.x, anchor.y);
      const auto& frame  = placement.crop.drag_frame;
      drag_origin_box_   = QRectF(frame.x, frame.y, frame.width, frame.height);
    }
  } catch (const std::runtime_error&) {
    return result;
  }

  drag_anchor_widget_pos_    = event_pos;
  drag_pre_press_rect_       = crop_state.rect;
  drag_pre_press_rotation_   = crop_state.rotation_degrees;
  drag_edge_                 = CropEdge::None;
  drag_changed_              = false;
  drag_rotation_started_     = false;
  drag_rotation_reanchor_    = false;
  drag_rotation_delta_       = 0.0f;
  drag_last_pointer_angle_ =
      PointerAngle(event_pos - drag_center_widget_pos_, drag_rotation_y_scale_);
  drag_grab_offset_frame_    = QPointF();

  const auto hit_cursor      = OverlayCursorShape(CropGeometry::CursorForCropHit(hit));
  if (hit.rotate_hit) {
    drag_mode_    = CropDragMode::RotateHandle;
    result.cursor = hit_cursor;
  } else if (hit.corner_index >= 0) {
    drag_mode_ = CropDragMode::ResizeCorner;
    drag_fixed_corner_frame_ =
        BoxCorner(drag_origin_box_, CropGeometry::OppositeCropCornerIndex(hit.corner_index));
    drag_grab_offset_frame_ = drag_anchor_frame_ - BoxCorner(drag_origin_box_, hit.corner_index);
    result.cursor = hit_cursor;
  } else if (hit.edge != CropEdge::None) {
    drag_mode_    = CropDragMode::ResizeEdge;
    drag_edge_    = hit.edge;
    switch (hit.edge) {
      case CropEdge::Top:
        drag_grab_offset_frame_ = QPointF(0.0, drag_anchor_frame_.y() - drag_origin_box_.top());
        break;
      case CropEdge::Bottom:
        drag_grab_offset_frame_ = QPointF(0.0, drag_anchor_frame_.y() - drag_origin_box_.bottom());
        break;
      case CropEdge::Left:
        drag_grab_offset_frame_ = QPointF(drag_anchor_frame_.x() - drag_origin_box_.left(), 0.0);
        break;
      case CropEdge::Right:
        drag_grab_offset_frame_ = QPointF(drag_anchor_frame_.x() - drag_origin_box_.right(), 0.0);
        break;
      case CropEdge::None:
        break;
    }
    result.cursor = hit_cursor;
  } else if (hit.inside_crop) {
    drag_mode_    = CropDragMode::Move;
    result.cursor = hit_cursor;
  } else {
    drag_mode_    = CropDragMode::Create;
    result.cursor = Qt::CrossCursor;
  }
  return result;
}

auto CropInteractionController::HandleMove(ViewerState& state, const MaskEditViewMapping& mapping,
                                           Qt::MouseButtons buttons, const QPointF& event_pos)
    -> CropInteractionResult {
  CropInteractionResult result;
  if ((buttons & Qt::LeftButton) != Qt::LeftButton || drag_mode_ == CropDragMode::None) {
    return result;
  }
  CropOverlayState crop_state = state.GetCropOverlay();
  if (!crop_state.tool_enabled || !crop_state.overlay_visible) {
    return result;
  }
  result.consumed = true;

  auto   crop_request         = CropGeometry::ToNormalizedRect(drag_pre_press_rect_);
  float  new_rotation_degrees = drag_pre_press_rotation_;
  bool   rotation_changed     = false;
  if (drag_mode_ == CropDragMode::RotateHandle) {
    if (!drag_rotation_started_) {
      const QPointF travel = event_pos - drag_anchor_widget_pos_;
      if (QPointF::dotProduct(travel, travel) <
          CropGeometry::kCropRotateStartPx * CropGeometry::kCropRotateStartPx) {
        return result;
      }
      drag_rotation_started_ = true;
      result.request_repaint = true;
    }
    const QPointF current_vector = event_pos - drag_center_widget_pos_;
    if (QPointF::dotProduct(current_vector, current_vector) <
        CropGeometry::kCropRotateMinLeverPx * CropGeometry::kCropRotateMinLeverPx) {
      // A short lever turns pointer jitter into large angles: hold the angle.
      drag_rotation_reanchor_ = true;
      return result;
    }
    const float current_angle = PointerAngle(current_vector, drag_rotation_y_scale_);
    if (drag_rotation_reanchor_) {
      drag_rotation_reanchor_  = false;
      drag_last_pointer_angle_ = current_angle;
      return result;
    }
    const float step_degrees = CropGeometry::NormalizeAngleDegrees(
        (current_angle - drag_last_pointer_angle_) * (180.0f / kPi));
    drag_last_pointer_angle_ = current_angle;
    drag_rotation_delta_ =
        std::clamp(drag_rotation_delta_ + step_degrees, -CropGeometry::kCropRotateMaxDragDegrees,
                   CropGeometry::kCropRotateMaxDragDegrees);
    new_rotation_degrees =
        std::clamp(drag_pre_press_rotation_ - drag_rotation_delta_, -180.0f, 180.0f);
    if (std::abs(new_rotation_degrees) < CropGeometry::kCropRotateSnapDegrees) {
      new_rotation_degrees = 0.0f;
    }
    rotation_changed = true;
  } else {
    QPointF cursor;
    try {
      const auto point = widget_to_drag_plane_.Map({event_pos.x(), event_pos.y()});
      cursor           = QPointF(point.x, point.y);
    } catch (const std::runtime_error&) {
      return result;
    }
    QRectF box = drag_origin_box_;
    switch (drag_mode_) {
      case CropDragMode::Create:
        box = crop_state.aspect_locked
                  ? CropGeometry::MakeAspectLockedBoxFromDiagonal(drag_anchor_frame_, cursor,
                                                                  crop_state.aspect_ratio)
                  : CropGeometry::ResizeBoxFromFixedCorner(drag_anchor_frame_, cursor, false,
                                                           crop_state.aspect_ratio);
        break;
      case CropDragMode::Move:
        box = drag_origin_box_.translated(cursor - drag_anchor_frame_);
        break;
      case CropDragMode::ResizeEdge:
        box = CropGeometry::ResizeBoxEdge(drag_origin_box_, drag_edge_,
                                          cursor - drag_grab_offset_frame_,
                                          crop_state.aspect_locked, crop_state.aspect_ratio);
        break;
      case CropDragMode::ResizeCorner:
        box = CropGeometry::ResizeBoxFromFixedCorner(
            drag_fixed_corner_frame_, cursor - drag_grab_offset_frame_, crop_state.aspect_locked,
            crop_state.aspect_ratio);
        break;
      case CropDragMode::None:
      case CropDragMode::RotateHandle:
        break;
    }
    if (mapping.geometry.full_reference_extent.Empty()) {
      return result;
    }
    try {
      crop_request =
          CropFromDragBox(box, drag_center_to_crop_center_, mapping.geometry.full_reference_extent);
    } catch (const std::runtime_error&) {
      return result;
    }
  }

  QRectF new_rect;
  try {
    new_rect = CropGeometry::ToQRectF(
        ResolveEditingCropRect(mapping.geometry, crop_request, new_rotation_degrees));
  } catch (const std::runtime_error&) {
    // The pointer can leave the valid plane. Keep the last accepted crop during the drag.
    return result;
  }
  crop_state.rect = new_rect;
  if (rotation_changed) {
    crop_state.rotation_degrees = new_rotation_degrees;
  }
  state.SetCropOverlayState(crop_state);
  drag_changed_          = true;
  result.request_repaint = true;
  result.rect_changed    = new_rect;
  if (rotation_changed) {
    result.rotation_changed = new_rotation_degrees;
  }
  return result;
}

auto CropInteractionController::HandleRelease(ViewerState& state) -> CropInteractionResult {
  CropInteractionResult result;
  if (drag_mode_ == CropDragMode::None) {
    return result;
  }
  result.consumed     = true;
  result.unset_cursor = true;
  // The fine rotation grid goes away with the drag.
  result.request_repaint = RotationActive();
  if (drag_changed_) {
    const auto crop_state = state.GetCropOverlay();
    result.rect_changed   = crop_state.rect;
    result.rect_is_final  = true;
    if (drag_mode_ == CropDragMode::RotateHandle) {
      result.rotation_changed  = crop_state.rotation_degrees;
      result.rotation_is_final = true;
    }
  }
  Cancel();
  return result;
}

auto CropInteractionController::HandleDoubleClick(ViewerState&               state,
                                                  const MaskEditViewMapping& mapping)
    -> CropInteractionResult {
  CropInteractionResult result;
  auto                  crop_state = state.GetCropOverlay();
  if (!crop_state.tool_enabled || !crop_state.overlay_visible) {
    return result;
  }

  Cancel();
  try {
    crop_state.rect = CropGeometry::ToQRectF(ResolveEditingCropRect(mapping.geometry, {}, 0.0f));
  } catch (const std::runtime_error&) {
    return result;
  }
  crop_state.rotation_degrees = 0.0f;
  state.SetCropOverlayState(crop_state);

  result.consumed          = true;
  result.request_repaint   = true;
  result.rect_changed      = crop_state.rect;
  result.rect_is_final     = true;
  result.rotation_changed  = crop_state.rotation_degrees;
  result.rotation_is_final = true;
  return result;
}

void CropInteractionController::Cancel(ViewerState& state) {
  if (drag_mode_ != CropDragMode::None && drag_changed_) {
    auto crop_state             = state.GetCropOverlay();
    crop_state.rect             = drag_pre_press_rect_;
    crop_state.rotation_degrees = drag_pre_press_rotation_;
    state.SetCropOverlayState(crop_state);
  }
  Cancel();
}

void CropInteractionController::Cancel() {
  drag_mode_              = CropDragMode::None;
  drag_edge_              = CropEdge::None;
  drag_changed_           = false;
  drag_anchor_widget_pos_ = QPointF();
  drag_center_widget_pos_ = QPointF();
  drag_rotation_started_  = false;
  drag_rotation_reanchor_ = false;
  drag_rotation_delta_    = 0.0f;
}

}  // namespace alcedo

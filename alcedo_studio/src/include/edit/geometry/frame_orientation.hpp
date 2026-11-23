//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <array>
#include <stdexcept>
#include <string_view>

#include "edit/geometry/types.hpp"

namespace alcedo {

enum class QuarterTurnDirection { Left, Right };
enum class ImageFlipAxis { Horizontal, Vertical };

inline constexpr std::array<std::string_view, 8> kFrameOrientationIds{
    "identity",        "rotate_90_cw",  "rotate_180", "rotate_90_ccw",
    "flip_horizontal", "flip_vertical", "transpose",  "transverse"};

/// Stable identifier stored in document, history, and transfer JSON.
[[nodiscard]] constexpr auto FrameOrientationId(FrameOrientation orientation) -> std::string_view {
  return kFrameOrientationIds.at(static_cast<unsigned int>(orientation));
}

/// True if the displayed width and height follow the opposite stored axes.
[[nodiscard]] constexpr auto FrameOrientationSwapsAxes(FrameOrientation orientation) -> bool {
  using enum FrameOrientation;
  return orientation == Rotate90Clockwise || orientation == Rotate90Counterclockwise ||
         orientation == Transpose || orientation == Transverse;
}

/// Reflections reverse the sign of a straightening angle in displayed axes.
[[nodiscard]] constexpr auto FrameOrientationReflects(FrameOrientation orientation) -> bool {
  using enum FrameOrientation;
  return orientation == FlipHorizontal || orientation == FlipVertical || orientation == Transpose ||
         orientation == Transverse;
}

/// The orientation that maps displayed coordinates back to their stored axes.
[[nodiscard]] constexpr auto InverseFrameOrientation(FrameOrientation orientation)
    -> FrameOrientation {
  using enum FrameOrientation;
  switch (orientation) {
    case Rotate90Clockwise:
      return Rotate90Counterclockwise;
    case Rotate90Counterclockwise:
      return Rotate90Clockwise;
    default:
      return orientation;
  }
}

/// Rejects an unknown identifier before the caller can change a document.
[[nodiscard]] inline auto ParseFrameOrientation(std::string_view id) -> FrameOrientation {
  for (unsigned int i = 0; i < kFrameOrientationIds.size(); ++i) {
    if (id == kFrameOrientationIds[i]) {
      return static_cast<FrameOrientation>(i);
    }
  }
  throw std::invalid_argument("Unknown image orientation");
}

/// Compose the quarter turn after the existing orientation, in displayed axes.
[[nodiscard]] constexpr auto TurnFrameOrientation(FrameOrientation     orientation,
                                                  QuarterTurnDirection direction)
    -> FrameOrientation {
  using enum FrameOrientation;
  constexpr std::array right{Rotate90Clockwise, Rotate180, Rotate90Counterclockwise, Identity,
                             Transverse,        Transpose, FlipHorizontal,           FlipVertical};
  constexpr std::array left{Rotate90Counterclockwise,
                            Identity,
                            Rotate90Clockwise,
                            Rotate180,
                            Transpose,
                            Transverse,
                            FlipVertical,
                            FlipHorizontal};
  switch (direction) {
    case QuarterTurnDirection::Left:
      return left.at(static_cast<unsigned int>(orientation));
    case QuarterTurnDirection::Right:
      return right.at(static_cast<unsigned int>(orientation));
  }
  throw std::invalid_argument("Unknown image rotation direction");
}

/// Mirror left to right or top to bottom after the existing orientation, in displayed axes.
[[nodiscard]] constexpr auto FlipFrameOrientation(FrameOrientation orientation, ImageFlipAxis axis)
    -> FrameOrientation {
  using enum FrameOrientation;
  constexpr std::array horizontal{
      FlipHorizontal, Transpose, FlipVertical,      Transverse,
      Identity,       Rotate180, Rotate90Clockwise, Rotate90Counterclockwise};
  constexpr std::array vertical{
      FlipVertical, Transverse, FlipHorizontal,           Transpose,
      Rotate180,    Identity,   Rotate90Counterclockwise, Rotate90Clockwise};
  switch (axis) {
    case ImageFlipAxis::Horizontal:
      return horizontal.at(static_cast<unsigned int>(orientation));
    case ImageFlipAxis::Vertical:
      return vertical.at(static_cast<unsigned int>(orientation));
  }
  throw std::invalid_argument("Unknown image flip axis");
}

}  // namespace alcedo

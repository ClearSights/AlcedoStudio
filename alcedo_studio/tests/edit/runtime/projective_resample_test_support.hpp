//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "../graph/test_camera_profile.hpp"
#include "../input/prepared_raw_test_support.hpp"
#include "edit/geometry/projective_transform.hpp"
#include "edit/geometry/render_geometry_resolver.hpp"
#include "edit/geometry/render_request.hpp"
#include "edit/geometry/source_geometry.hpp"
#include "edit/geometry/types.hpp"
#include "edit/graph/graph_ids.hpp"
#include "edit/graph/pipeline_document.hpp"
#include "edit/input/raw_input_loader.hpp"
#include "edit/runtime/execution_plan.hpp"
#include "edit/runtime/graph_compiler.hpp"
#include "edit/runtime/texture_format.hpp"

namespace alcedo::gpu_dag_test {

constexpr Extent2D kProjectiveExtent{35, 27};
constexpr double   kHorizontal = 62.0;
constexpr double   kVertical   = -48.0;
constexpr double   kScale      = 86.0;
constexpr double   kOffsetX    = 6.0;
constexpr double   kOffsetY    = -4.0;
using RgbaPixel                = std::array<float, 4>;
constexpr RgbaPixel kOpaqueBlack{0.0f, 0.0f, 0.0f, 1.0f};

/// Resolves an uncropped tilted image with both interior and uncovered output pixels.
inline auto         ResolveTiltedFrame(RenderQuality quality) -> ResolvedRenderGeometry {
  ImageGeometryParams image;
  image.horizontal     = kHorizontal;
  image.vertical       = kVertical;
  image.scale          = kScale;
  image.offset_x       = kOffsetX;
  image.offset_y       = kOffsetY;
  image.constrain_crop = false;
  return ResolveRenderGeometry(MakeSourceGeometry(kProjectiveExtent, kProjectiveExtent), image, {},
                               {.quality = quality}, {});
}

/// Creates two coordinate ramps and four different blue values marking the source corners.
inline auto MakeLabeledRamp() -> std::vector<RgbaPixel> {
  const auto [width, height] = kProjectiveExtent;
  std::vector<RgbaPixel> pixels(width * height);
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      float blue = 0.25f;
      if ((x < 5 || x >= width - 5) && (y < 5 || y >= height - 5)) {
        blue = 0.5f + (x >= width - 5 ? 0.125f : 0.0f) + (y >= height - 5 ? 0.25f : 0.0f);
      }
      pixels[y * width + x] = {(x + 0.5f) / width, (y + 0.5f) / height, blue, 1.0f};
    }
  }
  return pixels;
}

/// Solves the two scalar perspective equations directly, without using the resolver's matrices.
inline auto InverseTiltedPosition(double x, double y) -> Point2d {
  const auto [width, height] = kProjectiveExtent;
  const double q             = width / 2.0;
  const double plane_x       = ((x - width / 2.0) / q - 0.005 * kOffsetX) / (kScale / 100.0);
  const double plane_y       = ((y - height / 2.0) / q + 0.005 * kOffsetY) / (kScale / 100.0);
  const double h             = kHorizontal / 200.0;
  const double v             = kVertical / 200.0;
  const double normalization = 1.0 / std::sqrt(std::cos(h) * std::cos(v));
  const double z_from_x      = std::cos(v) * std::sin(h) / 1.5;
  const double z_from_y      = std::sin(v) / 1.5;
  const double a             = normalization * std::cos(h) + plane_x * z_from_x;
  const double b             = plane_x * z_from_y;
  const double c             = -normalization * std::sin(v) * std::sin(h) + plane_y * z_from_x;
  const double d             = normalization * std::cos(v) + plane_y * z_from_y;
  const double det           = a * d - b * c;
  return {width / 2.0 + q * (plane_x * d - b * plane_y) / det,
          height / 2.0 + q * (a * plane_y - c * plane_x) / det};
}

/// Returns the linear or Catmull–Rom weight of a tap at distance from the sample.
inline auto RampTapWeight(double distance, bool cubic) -> double {
  const double t = std::abs(distance);
  if (!cubic) {
    return t < 1.0 ? 1.0 - t : 0.0;
  }
  if (t < 1.0) {
    return 1.0 - 2.5 * t * t + 1.5 * t * t * t;
  }
  if (t < 2.0) {
    return 0.5 * (1.0 - t) * (2.0 - t) * (2.0 - t);
  }
  return 0.0;
}

/// Exercises the production backend's geometry pass and checks pixels before color rendering.
template <class Device, class Encode>
void VerifyProjectiveResample(Encode encode) {
  const auto [width, height] = kProjectiveExtent;
  const auto source          = MakeLabeledRamp();
  for (const auto quality : {RenderQuality::Preview, RenderQuality::Export}) {
    SCOPED_TRACE(static_cast<int>(quality));
    ExecutionPlan plan;
    plan.sensor_linear_output     = {NodeId{"develop"}, PortId{"sensor_linear"}};
    plan.geometry_output          = {NodeId{"geometry"}, PortId{"scene_source"}};
    plan.geometry                 = ResolveTiltedFrame(quality);
    plan.encode_geometry_resample = true;
    ASSERT_TRUE(plan.geometry.projective.has_value());
    ASSERT_EQ(plan.geometry.render_extent, kProjectiveExtent);

    Device device;
    device.BeginRender();
    auto& workspace = device.Workspace();
    auto& sensor    = workspace.AcquireImageForWrite(plan.sensor_linear_output,
                                                     {width, height, TextureFormat::Rgba32f});
    workspace.Device().UploadTexture2D(sensor.Texture(), std::as_bytes(std::span(source)),
                                       device.CommandContext());
    encode(device, plan);
    device.EndRender();
    device.WaitIdle();

    auto* output = workspace.Images().Find(plan.geometry_output);
    ASSERT_NE(output, nullptr);
    ASSERT_EQ(output->Texture().Width(), width);
    ASSERT_EQ(output->Texture().Height(), height);
    std::vector<RgbaPixel> pixels(width * height);
    workspace.Device().DownloadTexture2D(
        output->Texture(), std::as_writable_bytes(std::span(pixels)), device.CommandContext());

    const bool                             cubic           = quality == RenderQuality::Export;
    const int                              first           = cubic ? -1 : 0;
    const int                              last            = cubic ? 2 : 1;
    int                                    interior_pixels = 0;
    int                                    border_pixels   = 0;
    int                                    blended_pixels  = 0;
    constexpr std::array                   corner_labels{0.5, 0.625, 0.75, 0.875};
    std::array<bool, corner_labels.size()> sampled_corners{};
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        SCOPED_TRACE(::testing::Message() << "output pixel (" << x << ", " << y << ")");
        const auto [sx, sy]         = InverseTiltedPosition(x + 0.5, y + 0.5);
        const auto&           pixel = pixels[y * width + x];
        const double          tap_x = sx - 0.5;
        const double          tap_y = sy - 0.5;
        const int             left  = static_cast<int>(std::floor(tap_x));
        const int             top   = static_cast<int>(std::floor(tap_y));
        std::array<double, 4> expected{};
        int                   source_taps = 0;
        for (int j = top + first; j <= top + last; ++j) {
          for (int i = left + first; i <= left + last; ++i) {
            const bool inside =
                i >= 0 && i < static_cast<int>(width) && j >= 0 && j < static_cast<int>(height);
            const auto&  sample = inside ? source[j * width + i] : kOpaqueBlack;
            const double weight = RampTapWeight(tap_x - i, cubic) * RampTapWeight(tap_y - j, cubic);
            source_taps += inside ? 1 : 0;
            for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
              expected[channel] += sample[channel] * weight;
            }
          }
        }
        if (source_taps == 0) {
          EXPECT_EQ(pixel, kOpaqueBlack);
          ++border_pixels;
          continue;
        }
        if (source_taps == (last - first + 1) * (last - first + 1)) {
          EXPECT_NEAR(pixel[0] * width, sx, 1e-3);
          EXPECT_NEAR(pixel[1] * height, sy, 1e-3);
          ++interior_pixels;
        } else {
          ++blended_pixels;
        }
        for (std::size_t channel = 0; channel < pixel.size(); ++channel) {
          EXPECT_NEAR(pixel[channel], expected[channel], 2e-4);
        }
        for (std::size_t i = 0; i < corner_labels.size(); ++i) {
          if (std::abs(expected[2] - corner_labels[i]) < 1e-9) {
            sampled_corners[i] = true;
          }
        }
      }
    }
    EXPECT_GT(interior_pixels, 0);
    EXPECT_GT(border_pixels, 0);
    EXPECT_GT(blended_pixels, 0);
    EXPECT_EQ(sampled_corners, (std::array{true, true, true, true}));
  }
}

/// Forces the geometry target allocation to fail after Develop and checks the executor's result.
template <class Device>
void VerifyGeometryAllocationFailure() {
  const auto [width, height] = kProjectiveExtent;
  const auto input =
      RawInputLoader::FromDirectRgb(MakeF32RgbaPlane(width, height), FullSensor(width, height));
  auto document = CreateDefaultPipelineDocument();
  EnsureTestCameraProfile(document);
  auto plan     = GraphCompiler::Compile(document, input.CompileSource(), RenderRequest{});
  plan.geometry = ResolveTiltedFrame(RenderQuality::Preview);
  plan.encode_geometry_resample     = true;
  plan.geometry.render_extent.width = 0;

  std::string error;
  Device      device;
  device.SetErrorReporter([&error](std::string_view message) { error = message; });
  try {
    (void)device.Execute(plan, input, document);
    FAIL() << "Geometry allocation failed to stop the frame";
  } catch (const std::runtime_error& failure) {
    EXPECT_EQ(error, failure.what());
    EXPECT_NE(error.find("GraphImageCache::AcquireTextureForWrite: invalid size"),
              std::string::npos);
  }
  EXPECT_EQ(device.PassStats().sensor_develop_execute, 1U);
  EXPECT_EQ(device.PassStats().geometry_execute, 0U);
  EXPECT_FALSE(device.Workspace().IsRendering());
  EXPECT_EQ(device.Workspace().Images().PersistentPublishCount(), 0U);
  EXPECT_EQ(device.Workspace().Images().Find(plan.geometry_output), nullptr);
  EXPECT_EQ(device.Workspace().Images().Find(plan.display_output), nullptr);
}

}  // namespace alcedo::gpu_dag_test

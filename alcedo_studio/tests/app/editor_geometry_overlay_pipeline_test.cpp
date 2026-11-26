//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include <gtest/gtest.h>

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "edit/geometry/render_geometry_resolver.hpp"
#include "edit/graph/pipeline_document.hpp"
#include "edit/runtime/execution_plan.hpp"
#include "edit/runtime/graph_compiler.hpp"

#ifdef ALCEDO_TEST_CUDA_GEOMETRY
#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>

#include "edit/runtime/lens/cuda/cuda_geometry_ops.hpp"
#endif

namespace alcedo {
namespace {

constexpr Extent2D kSource{901, 603};

auto               MakePlan() -> ExecutionPlan {
  ExecutionPlan plan;
  plan.source.full_reference_extent = kSource;
  plan.source.develop_output_extent = kSource;
  return plan;
}

auto EditingRequest() -> RenderRequest {
  RenderRequest request;
  request.document_geometry    = DocumentGeometryUse::GeometryEditingSource;
  request.resolution.max_edge  = 487;
  request.final_frame_geometry = ImageGeometryParams{
      .crop_rect        = {0.28f, 0.19f, 0.44f, 0.49f},
      .rotation_degrees = 19.0f,
      .horizontal       = 55.0,
      .vertical         = -41.0,
      .aspect           = 45.0,
      .scale            = 87.0,
      .offset_x         = 6.0,
      .offset_y         = -8.0,
  };
  return request;
}

TEST(EditorGeometryOverlayPipeline, EditingCropBoundaryMapsBackToFinalOutputCorners) {
  const auto document = CreateDefaultPipelineDocument();
  const auto request  = EditingRequest();
  auto       editing  = MakePlan();
  GraphCompiler::BindFrameGeometry(editing, document, request);
  ASSERT_TRUE(editing.geometry.projective);
  ASSERT_TRUE(editing.geometry.editing_crop_plane);
  EXPECT_TRUE(editing.encode_geometry_resample);
  // Projecting the four source bounds with the specified perspective and scaling gives
  // unrounded spans of 760.2749 by 753.8385 reference pixels.
  EXPECT_EQ(editing.geometry.edit_extent, (Extent2D{760, 754}));
  EXPECT_EQ(editing.geometry.render_extent.width, 487u);

  auto product_request                = request;
  product_request.document_geometry   = DocumentGeometryUse::ApplyCropAndRotation;
  product_request.resolution.max_edge = 0;
  auto final                          = MakePlan();
  GraphCompiler::BindFrameGeometry(final, document, product_request);
  ASSERT_TRUE(final.geometry.projective);
  EXPECT_FALSE(final.geometry.editing_crop_plane);
  const auto& image = *request.final_frame_geometry;
  const auto  placement =
      ResolveEditingCropPlacement(editing.geometry, image.crop_rect, image.rotation_degrees);
  const auto final_corners =
      Rectangle2d{0.0, 0.0, static_cast<double>(final.geometry.render_extent.width),
                  static_cast<double>(final.geometry.render_extent.height)}
          .Corners();
  const auto uv_corners = Rectangle2d{0.0, 0.0, 1.0, 1.0}.Corners();
  for (std::size_t index = 0; index < uv_corners.size(); ++index) {
    const auto pixel     = placement.crop_uv_to_render.Map(uv_corners[index]);
    const auto reference = editing.geometry.RenderToReferenceMap().Map(pixel);
    const auto actual    = final.geometry.ReferenceToRenderMap().Map(reference);
    EXPECT_NEAR(actual.x, final_corners[index].x, 1.0e-4) << index;
    EXPECT_NEAR(actual.y, final_corners[index].y, 1.0e-4) << index;
  }

  auto no_crop_or_turn                                   = request;
  no_crop_or_turn.final_frame_geometry->crop_rect        = {};
  no_crop_or_turn.final_frame_geometry->rotation_degrees = 0.0f;
  no_crop_or_turn.final_frame_geometry->constrain_crop   = false;
  auto unchanged_editing                                 = MakePlan();
  GraphCompiler::BindFrameGeometry(unchanged_editing, document, no_crop_or_turn);
  EXPECT_EQ(unchanged_editing.geometry.edit_extent, editing.geometry.edit_extent);
  EXPECT_EQ(unchanged_editing.geometry.ReferenceToRenderMap(),
            editing.geometry.ReferenceToRenderMap());
}

TEST(EditorGeometryOverlayPipeline, InvalidFinalCropReportsErrorBeforeEditingGeometryIsBound) {
  auto request                        = EditingRequest();
  request.final_frame_geometry->scale = 0.0;
  auto plan                           = MakePlan();
  try {
    GraphCompiler::BindFrameGeometry(plan, CreateDefaultPipelineDocument(), request);
    FAIL() << "Expected the resolver to reject the manual scale";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string_view(error.what()).find("scale"), std::string_view::npos);
  }
  EXPECT_TRUE(plan.geometry.render_extent.Empty());
  EXPECT_FALSE(plan.geometry.editing_crop_plane);
}

TEST(EditorGeometryOverlayPipeline, ValidEditingBoundsCannotHideFinalCropHorizon) {
  auto request                 = EditingRequest();
  request.final_frame_geometry = ImageGeometryParams{
      .horizontal = 100.0, .scale = 50.0, .offset_x = 100.0, .constrain_crop = false};
  auto plan = MakePlan();
  try {
    GraphCompiler::BindFrameGeometry(plan, CreateDefaultPipelineDocument(), request);
    FAIL() << "Expected the final crop horizon to reject the editing request";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string_view(error.what()).find("horizon"), std::string_view::npos);
  }
  EXPECT_TRUE(plan.geometry.render_extent.Empty());
  EXPECT_FALSE(plan.geometry.editing_crop_plane);
}

TEST(EditorGeometryOverlayPipeline, NonfiniteCropIsRejectedBeforeAffineEditingFrameIsBound) {
  auto request                 = EditingRequest();
  request.final_frame_geometry = ImageGeometryParams{
      .crop_rect = {0.25f, 0.25f, std::numeric_limits<float>::infinity(), 0.25f}};
  auto plan = MakePlan();
  EXPECT_THROW(GraphCompiler::BindFrameGeometry(plan, CreateDefaultPipelineDocument(), request),
               std::runtime_error);
  EXPECT_TRUE(plan.geometry.render_extent.Empty());
  EXPECT_FALSE(plan.geometry.editing_crop_plane);
}

#ifdef ALCEDO_TEST_CUDA_GEOMETRY
auto EnsureCudaDevice() -> bool {
  const int device_count = cv::cuda::getCudaEnabledDeviceCount();
  if (device_count <= 0) {
    return false;
  }
  cv::cuda::setDevice(0);
  return true;
}

TEST(CudaGeometryOpsOverlayCases, ResizeLinearHandlesRoiAndEmptyWithoutAbort) {
  if (!EnsureCudaDevice()) {
    GTEST_SKIP() << "No CUDA device available.";
  }

  const cv::Mat host = cv::Mat::zeros(255, 511, CV_32FC3);
  cv::cuda::GpuMat src(host);
  cv::cuda::GpuMat roi = src(cv::Rect(3, 5, 127, 63));
  cv::cuda::GpuMat dst;
  EXPECT_NO_THROW(CUDA::ResizeLinear(roi, dst, cv::Size(41, 19)));
  EXPECT_EQ(dst.cols, 41);
  EXPECT_EQ(dst.rows, 19);

  cv::cuda::GpuMat empty;
  cv::cuda::GpuMat empty_dst;
  EXPECT_NO_THROW(CUDA::ResizeLinear(empty, empty_dst, cv::Size(16, 16)));
  EXPECT_TRUE(empty_dst.empty());
}

#endif

}  // namespace
}  // namespace alcedo

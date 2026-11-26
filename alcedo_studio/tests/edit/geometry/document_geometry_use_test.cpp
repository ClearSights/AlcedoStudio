//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include <gtest/gtest.h>

#include "edit/geometry/render_request.hpp"
#include "edit/geometry/resolved_render_geometry.hpp"
#include "edit/graph/pipeline_document.hpp"
#include "edit/runtime/execution_plan.hpp"
#include "edit/runtime/graph_compiler.hpp"

namespace alcedo {
namespace {

auto MakePlanWithSource() -> ExecutionPlan {
  ExecutionPlan plan;
  plan.source.develop_output_extent = {600, 400};
  plan.source.full_reference_extent = {600, 400};
  return plan;
}

auto BindGeometry(const PipelineDocument& document, DocumentGeometryUse use)
    -> ResolvedRenderGeometry {
  auto          plan = MakePlanWithSource();
  RenderRequest request;
  request.view.viewport_extent = {300, 200};
  request.resolution.max_edge  = 512;
  request.document_geometry    = use;
  GraphCompiler::BindFrameGeometry(plan, document, request);
  return plan.geometry;
}

void ExpectSameMatrix(const Matrix3x3& actual, const Matrix3x3& expected) {
  for (int i = 0; i < 9; ++i) {
    EXPECT_FLOAT_EQ(actual.m[i], expected.m[i]) << "matrix element " << i;
  }
}

}  // namespace

TEST(GpuDagGeometry, GeometryEditingSourceOmitsCropAndStraightening) {
  auto document = CreateDefaultPipelineDocument();
  document.Geometry().SetCropRect({0.2f, 0.1f, 0.5f, 0.6f});
  document.Geometry().SetRotationDegrees(7.0f);
  const auto before  = document.ToJson();

  const auto preview = BindGeometry(document, DocumentGeometryUse::GeometryEditingSource);
  EXPECT_EQ(preview.edit_extent, (Extent2D{600, 400}));
  ASSERT_TRUE(preview.editing_crop_plane);
  ExpectSameMatrix(preview.reference_to_edit, Matrix3x3::Identity());
  ExpectSameMatrix(preview.render_to_reference, Matrix3x3::Scale(2.0f, 2.0f));

  const auto applied = BindGeometry(document, DocumentGeometryUse::ApplyCropAndRotation);
  EXPECT_LT(applied.edit_extent.width, preview.edit_extent.width);
  EXPECT_FALSE(applied.editing_crop_plane);
  EXPECT_EQ(document.ToJson(), before);
}

TEST(GpuDagGeometry, ReferenceSourceRequestIgnoresCropAndRotation) {
  auto document = CreateDefaultPipelineDocument();
  document.Geometry().SetCropRect({0.2f, 0.1f, 0.5f, 0.6f});
  document.Geometry().SetRotationDegrees(7.0f);
  const auto    json_before = document.ToJson();

  // The AI Mask analysis request: the whole frame at a long edge of at most 4096.
  ExecutionPlan plan;
  plan.source.develop_output_extent = {6000, 4000};
  plan.source.full_reference_extent = {6000, 4000};
  RenderRequest request;
  request.resolution.max_edge = 4096;
  request.document_geometry   = DocumentGeometryUse::ReferenceSource;
  GraphCompiler::BindFrameGeometry(plan, document, request);
  const auto& geometry = plan.geometry;

  // EditSpace is ReferenceSpace: no crop and no rotation reach the frame.
  EXPECT_EQ(geometry.edit_extent, (Extent2D{6000, 4000}));
  ExpectSameMatrix(geometry.reference_to_edit, Matrix3x3::Identity());
  EXPECT_EQ(geometry.render_extent, (Extent2D{4096, 2731}));
  EXPECT_TRUE(CoversFullEditSpace(geometry));

  // render_to_reference is a pure scale, so normalized render coordinates are normalized
  // ReferenceSpace coordinates.
  EXPECT_FLOAT_EQ(geometry.render_to_reference.m[1], 0.0f);
  EXPECT_FLOAT_EQ(geometry.render_to_reference.m[2], 0.0f);
  EXPECT_FLOAT_EQ(geometry.render_to_reference.m[3], 0.0f);
  EXPECT_FLOAT_EQ(geometry.render_to_reference.m[5], 0.0f);
  for (const auto& uv : {Vector2{0.0f, 0.0f}, Vector2{0.25f, 0.75f}, Vector2{1.0f, 1.0f}}) {
    const auto reference =
        TransformPoint(geometry.render_to_reference,
                       Vector2{uv.x * static_cast<float>(geometry.render_extent.width),
                               uv.y * static_cast<float>(geometry.render_extent.height)});
    EXPECT_NEAR(reference.x / 6000.0f, uv.x, 1.0e-6f);
    EXPECT_NEAR(reference.y / 4000.0f, uv.y, 1.0e-6f);
  }
  EXPECT_EQ(document.ToJson(), json_before);
}

}  // namespace alcedo

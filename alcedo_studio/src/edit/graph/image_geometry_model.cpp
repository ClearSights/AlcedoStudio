//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "edit/graph/image_geometry_model.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>

#include "edit/geometry/projective_transform.hpp"
#include "edit/geometry/render_geometry_resolver.hpp"
#include "edit/geometry/source_geometry.hpp"

namespace alcedo {
namespace {

struct ManualTransformNumber {
  const char* key;
  double ImageGeometryParams::* value;
  std::optional<double> ImageGeometryUpdate::* update;
};

constexpr std::array kManualTransformNumbers{
    ManualTransformNumber{"horizontal", &ImageGeometryParams::horizontal,
                          &ImageGeometryUpdate::horizontal},
    ManualTransformNumber{"vertical", &ImageGeometryParams::vertical,
                          &ImageGeometryUpdate::vertical},
    ManualTransformNumber{"aspect", &ImageGeometryParams::aspect, &ImageGeometryUpdate::aspect},
    ManualTransformNumber{"scale", &ImageGeometryParams::scale, &ImageGeometryUpdate::scale},
    ManualTransformNumber{"offset_x", &ImageGeometryParams::offset_x,
                          &ImageGeometryUpdate::offset_x},
    ManualTransformNumber{"offset_y", &ImageGeometryParams::offset_y,
                          &ImageGeometryUpdate::offset_y},
};

}  // namespace

auto ImageGeometryModel::HasManualTransformSettings() const -> bool {
  return !HasIdentityManualTransform(render_params_) || !render_params_.constrain_crop;
}

auto ImageGeometryModel::RenderParamsForUpdate(const ImageGeometryUpdate& update) const
    -> ImageGeometryParams {
  // The resolver needs all mapping inputs at once. Assemble its request before publishing
  // values to the document. Aspect selection remains with the document owner.
  ImageGeometryParams params{
      .crop_rect        = update.crop_rect.value_or(CropRect()),
      .rotation_degrees = update.rotation_degrees.value_or(RotationDegrees()),
      .constrain_crop   = update.constrain_crop.value_or(render_params_.constrain_crop),
  };
  for (const auto& field : kManualTransformNumbers) {
    params.*field.value = (update.*field.update).value_or(render_params_.*field.value);
  }
  return params;
}

void ImageGeometryModel::StoreUpdate(const ImageGeometryUpdate& update,
                                     const ImageGeometryParams& accepted) {
  // Copy text first. Allocation failure must leave the pixel-changing fields untouched.
  if (update.aspect_preset) {
    aspect_preset_ = *update.aspect_preset;
  }
  aspect_ratio_  = update.aspect_ratio.value_or(aspect_ratio_);
  render_params_ = accepted;
}

void ImageGeometryModel::ApplyUpdate(const ImageGeometryUpdate& update) {
  const auto params = RenderParamsForUpdate(update);
  (void)ResolveCropPlaneTransform(params);
  StoreUpdate(update, params);
}

void ImageGeometryModel::ApplyResolvedUpdate(const ImageGeometryUpdate& update, Extent2D source) {
  auto params = RenderParamsForUpdate(update);
  (void)ResolveCropPlaneTransform(params);
  // Keep the legacy crop write path for identity transforms, including old crops whose
  // stored rectangle the affine renderer constrains at render time.
  if (params != render_params_ && (HasManualTransformSettings() ||
                                   !HasIdentityManualTransform(params) || !params.constrain_crop)) {
    if (source.Empty()) {
      throw std::invalid_argument("Geometry transform requires the source image dimensions");
    }
    const auto decoded = MakeSourceGeometry(source, source);
    const auto editing = ResolveGeometryEditingView(decoded, params, {}, {}, {});
    if (params.constrain_crop) {
      params.crop_rect = ResolveEditingCropRect(editing, params.crop_rect, params.rotation_degrees);
    }
    (void)ResolveRenderGeometry(decoded, params, {}, {}, {});
  }
  StoreUpdate(update, params);
}

auto ImageGeometryModel::ToJson(bool include_manual_transforms) const -> nlohmann::json {
  const auto     crop = CropRect();
  nlohmann::json json{
      {"crop_rect", nlohmann::json::array({crop.x, crop.y, crop.w, crop.h})},
      {"rotation_degrees", RotationDegrees()},
      {"aspect_preset", aspect_preset_},
      {"aspect_ratio", nlohmann::json::array({aspect_ratio_.width, aspect_ratio_.height})},
  };
  if (include_manual_transforms) {
    for (const auto& field : kManualTransformNumbers) {
      json[field.key] = render_params_.*field.value;
    }
    json["constrain_crop"] = render_params_.constrain_crop;
  }
  return json;
}

auto ImageGeometryModel::FromJson(const nlohmann::json& json, bool allow_manual_transforms)
    -> ImageGeometryModel {
  ImageGeometryModel model;
  if (json.contains("crop_rect") && json["crop_rect"].is_array() && json["crop_rect"].size() >= 4) {
    const auto& crop = json["crop_rect"];
    model.SetCropRect(
        {crop[0].get<float>(), crop[1].get<float>(), crop[2].get<float>(), crop[3].get<float>()});
  }
  if (json.contains("rotation_degrees") && json["rotation_degrees"].is_number()) {
    model.SetRotationDegrees(json["rotation_degrees"].get<float>());
  }
  if (json.contains("aspect_preset") && json["aspect_preset"].is_string()) {
    model.aspect_preset_ = json["aspect_preset"].get<std::string>();
  }
  if (json.contains("aspect_ratio") && json["aspect_ratio"].is_array() &&
      json["aspect_ratio"].size() >= 2) {
    model.aspect_ratio_ = {json["aspect_ratio"][0].get<float>(),
                           json["aspect_ratio"][1].get<float>()};
  }
  for (const auto& field : kManualTransformNumbers) {
    if (const auto it = json.find(field.key); it != json.end()) {
      if (!allow_manual_transforms || !it->is_number()) {
        throw std::invalid_argument(std::string{"Invalid stored geometry field "} + field.key);
      }
      model.render_params_.*field.value = it->get<double>();
    }
  }
  if (const auto it = json.find("constrain_crop"); it != json.end()) {
    if (!allow_manual_transforms || !it->is_boolean()) {
      throw std::invalid_argument("Invalid stored geometry field constrain_crop");
    }
    model.render_params_.constrain_crop = it->get<bool>();
  }
  (void)ResolveCropPlaneTransform(model.render_params_);
  return model;
}

}  // namespace alcedo

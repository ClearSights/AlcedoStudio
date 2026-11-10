// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <string>

#include "type/type.hpp"

namespace alcedo {
enum class SourceImageStatus { Readable, Missing, Error };
struct SourceImageStatusResult {
  SourceImageStatus status = SourceImageStatus::Error;
  std::string       reason;
};
/// Check volume availability and file status, then open/read one byte. No RAW decoding.
/// Errors are report-only; only file-not-found on an available volume is Missing.
auto CheckSourceImageStatus(const image_path_t& path) -> SourceImageStatusResult;
}  // namespace alcedo

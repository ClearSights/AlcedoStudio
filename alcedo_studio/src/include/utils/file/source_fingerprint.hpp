// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

#include "type/type.hpp"
#include "utils/import/import_error_code.hpp"

namespace alcedo {
/// Source identity only; never contains Image metadata or editing state.
struct SourceFingerprint {
  uint64_t    byte_size = 0;
  std::string sha256;
  auto        operator==(const SourceFingerprint&) const -> bool = default;
};

class SourceReadError : public std::runtime_error {
 public:
  SourceReadError(ImportErrorCode code, const std::string& message)
      : std::runtime_error(message), code(code) {}
  ImportErrorCode code;
};

/// A source read held through metadata extraction and checked persistence. On Windows the
/// handle denies writes and replacement. Elsewhere validation rechecks identity and the digest.
/// Owns its handle until destruction; all methods run on one import worker or submission owner.
class SourceFingerprintReader {
 public:
  explicit SourceFingerprintReader(const image_path_t& path);
  ~SourceFingerprintReader();
  SourceFingerprintReader(const SourceFingerprintReader&)                    = delete;
  auto operator=(const SourceFingerprintReader&) -> SourceFingerprintReader& = delete;
  /// Byte count before hashing, for rejecting a recorded size mismatch without a digest read.
  auto Size() const -> uint64_t { return size_; }
  /// Streams the entire source with a fixed 64 KiB buffer. Throws on read/change/cancel errors.
  auto Read(const std::function<bool()>& canceled = {}) -> SourceFingerprint;
  /// Rejects source changes since Read. The caller retains this object through publication.
  void Validate(const SourceFingerprint& expected);

 private:
  image_path_t path_;
  std::FILE*   file_                 = nullptr;
  uint64_t     size_                 = 0;
  uint64_t     device_               = 0;
  uint64_t     inode_                = 0;
  int64_t      modified_seconds_     = 0;
  int64_t      modified_nanoseconds_ = 0;
  int64_t      changed_seconds_      = 0;
  int64_t      changed_nanoseconds_  = 0;
};
}  // namespace alcedo

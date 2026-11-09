// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "storage/store/database.hpp"
#include "utils/file/source_fingerprint.hpp"

namespace alcedo {
struct RecordedSourceFingerprint {
  SourceFingerprint source;
  std::string       basis;
};

/// Optional source identity persistence. Acquires a short-lived locked connection per operation.
/// Throws the real database error; holds no Image, file handle, or history state.
class SourceFingerprintStore {
 public:
  explicit SourceFingerprintStore(Database& database) : database_(database) {}
  /// Returns live bound IDs only for requested normalized paths, with one scoped path read.
  /// Does not open any source file or load Image objects.
  auto FindBindings(const std::set<std::wstring>& paths)
      -> std::map<std::wstring, std::vector<image_id_t>>;
  auto Load(image_id_t id) -> std::optional<RecordedSourceFingerprint>;
  /// Insert a validated identity only for a persisted live FileImage binding. Never overwrites
  /// an accepted identity. The provenance must be import or legacy_current_source.
  void Insert(image_id_t id, const SourceFingerprint& source, const std::string& basis);
  /// Validate and insert a whole set in one transaction. Rejects all rows if any fails.
  void InsertBatch(std::span<const std::pair<image_id_t, SourceFingerprint>> sources,
                   const std::string&                                        basis);
  /// Removes identities of failed provisional imports; the caller owns the ID list.
  void Remove(std::span<const image_id_t> ids);

 private:
  Database& database_;
};
}  // namespace alcedo

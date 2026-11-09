// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#include "storage/store/image/source_fingerprint_store.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <variant>

#include "storage/mapper/duckorm/duckdb_orm.hpp"
#include "utils/file/source_image_path.hpp"
#include "utils/string/convert.hpp"

namespace alcedo {
namespace expr = duckorm::expr;

auto SourceFingerprintStore::FindBindings(const std::set<std::wstring>& paths)
    -> std::map<std::wstring, std::vector<image_id_t>> {
  std::map<std::wstring, std::vector<image_id_t>> matches;
  if (paths.empty()) return matches;
  auto                                        guard = database_.GetConnectionGuard();
  auto                                        lock  = guard.Lock();
  const std::array<duckorm::DuckFieldDesc, 2> fields{
      {{"id", duckorm::DuckDBType::INT64, 0}, {"image_path", duckorm::DuckDBType::STRING, 0}}};
  const auto rows = duckorm::select_by_query(
      guard.conn_, fields, fields.size(),
      "SELECT DISTINCT i.id, i.image_path FROM Image i JOIN FileImage f ON f.image_id = i.id "
      "JOIN PipelineRoot r ON r.element_id = f.file_id");
  for (const auto& row : rows) {
    const auto path = image_path_t(conv::FromBytes(duckorm::cell_text(row[1])));
    if (!path.is_absolute()) continue;
    auto normalized = NormalizeSourceImagePath(path);
    if (paths.contains(normalized)) {
      matches[normalized].push_back(static_cast<image_id_t>(std::get<int64_t>(row[0])));
    }
  }
  return matches;
}

auto SourceFingerprintStore::Load(image_id_t id) -> std::optional<RecordedSourceFingerprint> {
  auto                                        guard = database_.GetConnectionGuard();
  auto                                        lock  = guard.Lock();
  const std::array<duckorm::DuckFieldDesc, 3> fields{{{"byte_size", duckorm::DuckDBType::UINT64, 0},
                                                      {"sha256", duckorm::DuckDBType::STRING, 0},
                                                      {"basis", duckorm::DuckDBType::STRING, 0}}};
  auto                                        query =
      expr::raw("SELECT byte_size, sha256, basis FROM ImageSourceFingerprint WHERE image_id = ");
  query.append(expr::param(static_cast<int64_t>(id)));
  const auto rows = duckorm::select_by_query(guard.conn_, fields, fields.size(), query);
  if (rows.empty()) return std::nullopt;
  return RecordedSourceFingerprint{{std::get<uint64_t>(rows[0][0]), duckorm::cell_text(rows[0][1])},
                                   duckorm::cell_text(rows[0][2])};
}

void SourceFingerprintStore::Insert(image_id_t id, const SourceFingerprint& source,
                                    const std::string& basis) {
  const std::pair<image_id_t, SourceFingerprint> item{id, source};
  InsertBatch(std::span(&item, 1), basis);
}

void SourceFingerprintStore::InsertBatch(
    std::span<const std::pair<image_id_t, SourceFingerprint>> sources, const std::string& basis) {
  for (const auto& [id, source] : sources) {
    if (source.sha256.size() != 64 ||
        !std::all_of(source.sha256.begin(), source.sha256.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
        (basis != "import" && basis != "legacy_current_source")) {
      throw std::invalid_argument("Invalid source fingerprint or provenance");
    }
  }
  auto                 guard = database_.GetConnectionGuard();
  auto                 lock  = guard.Lock();
  duckorm::Transaction transaction(guard.conn_);
  for (const auto& [id, source] : sources) {
    auto query = expr::raw(
        "SELECT COUNT(*) FROM FileImage f JOIN Image i ON i.id = f.image_id WHERE f.image_id = ");
    query.append(expr::param(static_cast<int64_t>(id)));
    if (duckorm::select_int64(guard.conn_, query).value_or(0) == 0) {
      throw std::runtime_error("Source identity requires a live image binding");
    }
    query = expr::raw("INSERT INTO ImageSourceFingerprint VALUES (");
    query.append(expr::param(static_cast<int64_t>(id)));
    query.append(expr::raw(", "));
    query.append(expr::raw(std::to_string(source.byte_size)));
    query.append(expr::raw(", "));
    query.append(expr::param(source.sha256));
    query.append(expr::raw(", "));
    query.append(expr::param(basis));
    query.append(expr::raw(")"));
    duckorm::execute(guard.conn_, query);
  }
  transaction.commit();
}

void SourceFingerprintStore::Remove(std::span<const image_id_t> ids) {
  auto                 guard = database_.GetConnectionGuard();
  auto                 lock  = guard.Lock();
  duckorm::Transaction transaction(guard.conn_);
  for (auto id : ids) {
    auto query = expr::raw("DELETE FROM ImageSourceFingerprint WHERE image_id = ");
    query.append(expr::param(static_cast<int64_t>(id)));
    duckorm::execute(guard.conn_, query);
  }
  transaction.commit();
}
}  // namespace alcedo

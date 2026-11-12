// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "storage/store/sleeve/album_stack_query.hpp"

#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "storage/mapper/duckorm/duckdb_expr.hpp"
#include "storage/mapper/duckorm/duckdb_orm.hpp"
#include "storage/mapper/duckorm/duckdb_select.hpp"
#include "storage/store/sleeve/album_query_rows.hpp"
#include "storage/store/sleeve/album_query_sql.hpp"
#include "storage/store/sleeve/photo_stack_store.hpp"

namespace alcedo::album_stack_query {
namespace {
namespace expr = duckorm::expr;
namespace sql  = album_query_sql;
using duckorm::SqlFragment;

/// Decode the typed group key; the SQL uses VARCHAR only at this private row boundary.
auto GroupKey(AlbumGroupField field, const std::optional<std::string>& text) -> AlbumGroupKey {
  if (field == AlbumGroupField::kNone || !text) return std::monostate{};
  if (field != AlbumGroupField::kRating) return *text;
  int64_t    number = 0;
  const auto parsed = std::from_chars(text->data(), text->data() + text->size(), number);
  if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) {
    throw std::runtime_error("Album display: invalid numeric group key");
  }
  return number;
}

auto FileId(int64_t id) -> sl_element_id_t {
  if (id < 0 || static_cast<uint64_t>(id) > std::numeric_limits<sl_element_id_t>::max()) {
    throw std::runtime_error("Album display: File ID is outside the supported range");
  }
  return static_cast<sl_element_id_t>(id);
}

/// One SQL definition for display counts and pages. The raw occurrence rank retains every
/// existing group/sort/relevance rule. A unit takes its displayed cover's rank, so no second
/// photo comparator is needed. The relation holds IDs/counts, never whole Image objects.
auto Projection(const AlbumQueryOptions& options, const std::string& model, bool relevance,
                sql::EditStateProjection metadata) -> SqlFragment {
  auto query = sql::RelationsPrefix(options, model, metadata);
  query.sql_.append(query.empty() ? "WITH " : ", ");
  query.sql_.append("base_occurrences AS (SELECT s.file_id, ");
  if (options.group_field_ == AlbumGroupField::kNone) {
    query.sql_.append("CAST(NULL AS VARCHAR)");
  } else {
    query.sql_.append("CAST(");
    query.append(sql::GroupKeyExpression(options.group_field_));
    query.sql_.append(" AS VARCHAR)");
  }
  query.sql_.append(" AS group_key, ROW_NUMBER() OVER (");
  query.append(duckorm::clause::order_by(sql::OccurrenceOrderTerms(options, relevance)));
  query.sql_.append(") - 1 AS photo_position ");
  query.append(sql::OccurrenceSource(options, sql::EditStateProjection::kNotRead));
  query.sql_.append(
      "), query_stack_counts AS (SELECT m.stack_id, COUNT(*) AS query_count "
      "FROM PhotoStackMember m JOIN SearchMatchSet s ON s.file_id = m.file_id GROUP BY "
      "m.stack_id), "
      "stack_counts AS (SELECT m.stack_id, COUNT(*) AS total_count FROM PhotoStackMember m "
      "WHERE m.stack_id IN (SELECT stack_id FROM query_stack_counts) GROUP BY m.stack_id), "
      "unit_members AS (SELECT b.*, m.stack_id, m.member_order, h.cover_file_id, "
      "COALESCE(t.total_count, 1) AS total_count, COALESCE(q.query_count, 1) AS query_count, "
      "COUNT(*) OVER fragment AS fragment_count, "
      "FIRST_VALUE(b.file_id) OVER fragment AS displayed_cover, "
      "FIRST_VALUE(b.photo_position) OVER fragment AS unit_position "
      "FROM base_occurrences b LEFT JOIN PhotoStackMember m ON m.file_id = b.file_id "
      "LEFT JOIN PhotoStack h ON h.stack_id = m.stack_id "
      "LEFT JOIN stack_counts t ON t.stack_id = m.stack_id "
      "LEFT JOIN query_stack_counts q ON q.stack_id = m.stack_id "
      "WINDOW fragment AS (PARTITION BY b.group_key, m.stack_id, "
      "CASE WHEN m.stack_id IS NULL THEN b.file_id END "
      "ORDER BY (b.file_id = h.cover_file_id) DESC NULLS LAST, m.member_order, b.file_id "
      "ROWS BETWEEN UNBOUNDED PRECEDING AND UNBOUNDED FOLLOWING)), "
      "display AS (SELECT *, ROW_NUMBER() OVER (ORDER BY unit_position) - 1 AS display_position "
      "FROM unit_members WHERE file_id = displayed_cover) ");
  return query;
}

struct GroupRow {
  std::optional<std::string> key;
  int64_t                    count = 0;
  int64_t                    first = 0;
  using Columns = duckorm::RowColumns<&GroupRow::key, &GroupRow::count, &GroupRow::first>;
};

/// Shared photo columns followed by the required new display result scalars only.
struct PageRow : album_query_rows::SearchResultDbRow {
  std::optional<std::string> key;
  std::optional<uint64_t>    edited_at;
  std::optional<std::string> version;
  std::optional<std::string> stack;
  std::optional<int64_t>     saved_cover;
  int64_t                    total_count     = 0;
  int64_t                    query_count     = 0;
  int64_t                    fragment_count  = 0;
  int64_t                    displayed_cover = 0;
  using Columns =
      duckorm::AppendRowColumns<album_query_rows::SearchResultDbRow::Columns, &PageRow::key,
                                &PageRow::edited_at, &PageRow::version, &PageRow::stack,
                                &PageRow::saved_cover, &PageRow::total_count, &PageRow::query_count,
                                &PageRow::fragment_count, &PageRow::displayed_cover>;
};
}  // namespace

void ReadDisplay(duckorm::Connection connection, sl_element_id_t folder_id,
                 const AlbumQueryOptions& options, const std::string& model,
                 const AlbumQueryRead& read, bool has_relevance, AlbumQueryResult& result) {
  PhotoStackStore::ValidateFileStacks(connection, expr::raw("SELECT file_id FROM SearchMatchSet"));
  result.first_display = read.display_offset;
  auto counts = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead);
  if (read.read_groups_ && options.group_field_ != AlbumGroupField::kNone) {
    counts.sql_.append(
        "SELECT group_key, COUNT(*), MIN(display_position) FROM display "
        "GROUP BY group_key ORDER BY MIN(display_position)");
    const auto rows = duckorm::query<GroupRow>(connection, counts);
    if (rows.size() != result.groups_.size()) {
      throw std::runtime_error("Album display: photo and display groups disagree");
    }
    for (size_t i = 0; i < rows.size(); ++i) {
      auto& group = result.groups_[i];
      if (group.key_ != GroupKey(options.group_field_, rows[i].key)) {
        throw std::runtime_error("Album display: inconsistent typed group key");
      }
      group.display_count = rows[i].count;
      group.first_display = rows[i].first;
      result.display_count += rows[i].count;
    }
  } else {
    counts.sql_.append("SELECT COUNT(*) FROM display");
    result.display_count = duckorm::scalar<int64_t>(connection, counts).value();
  }
  if (read.limit_ == 0) return;
  auto page = Projection(options, model, has_relevance, sql::EditStateProjection::kRead);
  page.sql_.append("SELECT ");
  page.sql_.append(album_query_rows::kMatchSetResultColumns);
  page.sql_.append(
      ", d.group_key, es.edited_at_ns, es.version_name, d.stack_id, d.cover_file_id, "
      "d.total_count, d.query_count, d.fragment_count, d.displayed_cover "
      "FROM display d JOIN SearchMatchSet s ON s.file_id = d.file_id "
      "LEFT JOIN edit_state es ON es.file_id = d.file_id ORDER BY d.display_position");
  page.append(
      duckorm::clause::limit_offset(read.limit_, read.display_offset, kMaxAlbumQueryPageRows));
  for (auto& row : duckorm::query<PageRow>(connection, page)) {
    const auto      file = FileId(row.file_id);
    AlbumDisplayRow output;
    output.photo.photo_     = album_query_rows::ToSearchResultRow(row);
    output.photo.group_key_ = GroupKey(options.group_field_, row.key);
    if (row.edited_at) output.photo.edited_at_ns_ = static_cast<int64_t>(*row.edited_at);
    output.photo.edit_version_name_ = std::move(row.version).value_or(std::string{});
    output.identity.fragment        = {folder_id, options.group_field_, output.photo.group_key_,
                                       std::move(row.stack).value_or(std::string{})};
    const bool stacked              = !output.identity.fragment.stack_id.empty();
    output.identity.kind =
        stacked ? AlbumDisplayKind::kStackRepresentative : AlbumDisplayKind::kPhoto;
    output.identity.file_id     = stacked ? 0 : file;
    output.saved_cover_file_id  = row.saved_cover ? FileId(*row.saved_cover) : 0;
    output.stack_total          = row.total_count;
    output.query_match_count    = row.query_count;
    output.fragment_match_count = row.fragment_count;
    output.temporary_cover      = stacked && row.displayed_cover != *row.saved_cover;
    result.display_rows.push_back(std::move(output));
  }
}
}  // namespace alcedo::album_stack_query

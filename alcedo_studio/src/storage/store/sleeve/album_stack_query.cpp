// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "storage/store/sleeve/album_stack_query.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
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

/// Reject an out-of-range persisted ID before narrowing to the application File ID type.
auto FileId(int64_t id) -> sl_element_id_t {
  if (id < 0 || static_cast<uint64_t>(id) > std::numeric_limits<sl_element_id_t>::max()) {
    throw std::runtime_error("Album display: File ID is outside the supported range");
  }
  return static_cast<sl_element_id_t>(id);
}

/// Bind typed public keys at the private VARCHAR SQL boundary. User text is never SQL text.
auto KeyParameter(const AlbumGroupKey& key) -> SqlFragment {
  if (const auto* text = std::get_if<std::string>(&key)) return expr::param(*text);
  if (const auto* number = std::get_if<int64_t>(&key)) return expr::param(std::to_string(*number));
  return {"?", {std::monostate{}}};
}

/// Validate typed input, retaining NULL as the unknown group (or the tagged flat group).
void ValidateKey(AlbumGroupField field, const AlbumGroupKey& key) {
  if (std::holds_alternative<std::monostate>(key)) return;
  const bool valid = field != AlbumGroupField::kNone &&
                     (field == AlbumGroupField::kRating ? std::holds_alternative<int64_t>(key)
                                                        : std::holds_alternative<std::string>(key));
  if (!valid) throw std::invalid_argument("Album display: group key has the wrong type");
}

/// Construct the stable representative key or an actual expanded member/ordinary photo key.
auto Identity(sl_element_id_t folder, AlbumGroupField field, AlbumGroupKey group, std::string stack,
              sl_element_id_t file, sl_element_id_t cover) -> AlbumDisplayIdentity {
  const bool stacked        = !stack.empty();
  const bool representative = stacked && file == cover;
  return {{folder, field, std::move(group), std::move(stack)},
          !stacked         ? AlbumDisplayKind::kPhoto
          : representative ? AlbumDisplayKind::kStackRepresentative
                           : AlbumDisplayKind::kStackMember,
          representative ? 0 : file};
}

/// One SQL definition for display counts and pages. The raw occurrence rank retains every
/// existing group/sort/relevance rule. A unit takes its displayed cover's rank, so no second
/// photo comparator is needed. The relation holds IDs/counts, never whole Image objects.
auto Projection(const AlbumQueryOptions& options, const std::string& model, bool relevance,
                sql::EditStateProjection               metadata,
                std::span<const AlbumStackFragmentKey> expanded = {}) -> SqlFragment {
  auto query = sql::RelationsPrefix(options, model, metadata);
  query.sql_.append(query.empty() ? "WITH " : ", ");
  if (!expanded.empty()) {
    query.sql_.append("expanded_keys AS (SELECT DISTINCT * FROM (VALUES ");
    for (size_t i = 0; i < expanded.size(); ++i) {
      if (i) query.sql_.append(", ");
      query.sql_.append("(CAST(");
      query.append(KeyParameter(expanded[i].group_key));
      query.sql_.append(" AS VARCHAR), ");
      query.append(expr::param(expanded[i].stack_id));
      query.sql_.append(")");
    }
    query.sql_.append(") keys(group_key, stack_id)), ");
  }
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
      "expanded_members AS (SELECT u.*, ");
  query.sql_.append(expanded.empty()
                        ? "FALSE"
                        : "EXISTS (SELECT 1 FROM expanded_keys e WHERE e.stack_id = u.stack_id "
                          "AND e.group_key IS NOT DISTINCT FROM u.group_key)");
  query.sql_.append(
      " AS expanded FROM unit_members u), "
      "display AS (SELECT *, ROW_NUMBER() OVER (ORDER BY unit_position, "
      "CASE WHEN file_id = displayed_cover THEN 0 ELSE 1 END, member_order, file_id) "
      "- 1 AS display_position FROM expanded_members "
      "WHERE file_id = displayed_cover OR expanded) ");
  return query;
}

struct GroupRow {
  std::optional<std::string> key;
  int64_t                    count = 0;
  int64_t                    first = 0;
  using Columns = duckorm::RowColumns<&GroupRow::key, &GroupRow::count, &GroupRow::first>;
};

/// Scalar SQL output shared by bounded pages and metadata-free relocation reads.
struct ItemRow {
  std::optional<std::string> key;
  std::optional<std::string> stack;
  std::optional<int64_t>     saved_cover;
  int64_t                    total_count = 0, query_count = 0, fragment_count = 0;
  int64_t                    displayed_file = 0, displayed_cover = 0;
  bool                       expanded = false;
  using Columns =
      duckorm::RowColumns<&ItemRow::key, &ItemRow::stack, &ItemRow::saved_cover,
                          &ItemRow::total_count, &ItemRow::query_count, &ItemRow::fragment_count,
                          &ItemRow::displayed_file, &ItemRow::displayed_cover, &ItemRow::expanded>;
};
inline constexpr const char* kItemColumns =
    "d.group_key, d.stack_id, d.cover_file_id, d.total_count, d.query_count, "
    "d.fragment_count, d.file_id, d.displayed_cover, d.expanded";
/// One bounded metadata row plus the display item's scalar fields. Metadata does not leave
/// this result for full-stream reads; GUI owners receive the two components separately.
struct PageRow : ItemRow, album_query_rows::SearchResultDbRow {
  std::optional<uint64_t>    edited_at;
  std::optional<std::string> version;
  using PhotoColumns =
      duckorm::AppendRowColumns<album_query_rows::SearchResultDbRow::Columns, &PageRow::edited_at,
                                &PageRow::version, &PageRow::key, &PageRow::stack,
                                &PageRow::saved_cover, &PageRow::total_count, &PageRow::query_count,
                                &PageRow::fragment_count, &PageRow::displayed_file,
                                &PageRow::displayed_cover, &PageRow::expanded>;
  using Columns = PhotoColumns;
};
/// Convert only required scalar values, preserving the stable representative identity.
auto DisplayItem(const ItemRow& row, sl_element_id_t folder, AlbumGroupField group)
    -> AlbumDisplayItem {
  AlbumDisplayItem item;
  item.file_id  = FileId(row.displayed_file);
  item.identity = Identity(folder, group, GroupKey(group, row.key), row.stack.value_or(""),
                           item.file_id, FileId(row.displayed_cover));
  item.saved_cover_file_id  = row.saved_cover ? FileId(*row.saved_cover) : 0;
  item.stack_total          = row.total_count;
  item.query_match_count    = row.query_count;
  item.fragment_match_count = row.fragment_count;
  item.temporary_cover      = row.stack && row.displayed_cover != *row.saved_cover;
  item.expanded             = row.expanded;
  return item;
}
struct PositionRow {
  int64_t                    position = 0;
  std::optional<std::string> key;
  std::optional<std::string> stack;
  int64_t                    file   = 0;
  int64_t                    cover  = 0;
  bool                       hidden = false;
  using Columns =
      duckorm::RowColumns<&PositionRow::position, &PositionRow::key, &PositionRow::stack,
                          &PositionRow::file, &PositionRow::cover, &PositionRow::hidden>;
};
struct FileRow {
  int64_t file  = 0;
  using Columns = duckorm::RowColumns<&FileRow::file>;
};
/// Decode only ID outputs; no metadata or retained membership cache.
auto FileIds(duckorm::Connection connection, const SqlFragment& query)
    -> std::vector<sl_element_id_t> {
  std::vector<sl_element_id_t> result;
  for (const auto& row : duckorm::query<FileRow>(connection, query))
    result.push_back(FileId(row.file));
  return result;
}
/// Reuse the saved-stack owner's validation on the caller's read transaction.
void ValidateStacks(duckorm::Connection connection) {
  PhotoStackStore::ValidateFileStacks(connection, expr::raw("SELECT file_id FROM SearchMatchSet"));
}
}  // namespace

void ValidateExpansion(sl_element_id_t folder_id, const AlbumQueryOptions& options,
                       std::span<const AlbumStackFragmentKey> fragments) {
  ValidateAlbumQueryOptions(options);
  for (const auto& key : fragments) {
    if (key.folder_id != folder_id || key.group_field != options.group_field_ ||
        key.stack_id.empty()) {
      throw std::invalid_argument(
          "Album display: fragment does not belong to this query scope/group");
    }
    ValidateKey(options.group_field_, key.group_key);
  }
}
void ValidatePreferredGroup(AlbumGroupField field, const std::optional<AlbumGroupKey>& group) {
  if (group) ValidateKey(field, *group);
}

void ReadDisplay(duckorm::Connection connection, sl_element_id_t folder_id,
                 const AlbumQueryOptions& options, const std::string& model,
                 const AlbumQueryRead& read, bool has_relevance, AlbumQueryResult& result) {
  ValidateStacks(connection);
  result.first_display = read.display_offset;
  auto counts = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead,
                           read.expanded_fragments);
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
  if (read.read_display_items) {
    auto items = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead,
                            read.expanded_fragments);
    items.sql_.append("SELECT ");
    items.sql_.append(kItemColumns);
    items.sql_.append(" FROM display d ORDER BY d.display_position");
    for (const auto& row : duckorm::query<ItemRow>(connection, items)) {
      result.display_items.push_back(DisplayItem(row, folder_id, options.group_field_));
    }
  }
  if (read.limit_ == 0) return;
  auto page = Projection(options, model, has_relevance, sql::EditStateProjection::kRead,
                         read.expanded_fragments);
  page.sql_.append("SELECT ");
  page.sql_.append(album_query_rows::kMatchSetResultColumns);
  page.sql_.append(", es.edited_at_ns, es.version_name, ");
  page.sql_.append(kItemColumns);
  page.sql_.append(
      " FROM display d JOIN SearchMatchSet s ON s.file_id = d.file_id "
      "LEFT JOIN edit_state es ON es.file_id = d.file_id ORDER BY d.display_position");
  page.append(
      duckorm::clause::limit_offset(read.limit_, read.display_offset, kMaxAlbumQueryPageRows));
  for (auto& row : duckorm::query<PageRow>(connection, page)) {
    AlbumDisplayRow output;
    static_cast<AlbumDisplayItem&>(output) = DisplayItem(row, folder_id, options.group_field_);
    output.photo.photo_                    = album_query_rows::ToSearchResultRow(row);
    output.photo.group_key_ = GroupKey(options.group_field_, row.key);
    if (row.edited_at) output.photo.edited_at_ns_ = static_cast<int64_t>(*row.edited_at);
    output.photo.edit_version_name_ = std::move(row.version).value_or(std::string{});
    result.display_rows.push_back(std::move(output));
  }
}

auto ReadPosition(duckorm::Connection connection, sl_element_id_t folder_id,
                  const AlbumQueryOptions& options, const std::string& model,
                  std::span<const AlbumStackFragmentKey> expanded_fragments, bool has_relevance,
                  sl_element_id_t file_id, const std::optional<AlbumGroupKey>& preferred_group)
    -> std::optional<AlbumDisplayPosition> {
  ValidateStacks(connection);
  auto query = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead,
                          expanded_fragments);
  query.sql_.append(
      "SELECT d.display_position, d.group_key, d.stack_id, d.file_id, d.displayed_cover, "
      "(c.file_id <> d.file_id) FROM expanded_members c JOIN display d "
      "ON d.unit_position = c.unit_position AND d.file_id = "
      "CASE WHEN c.expanded THEN c.file_id ELSE c.displayed_cover END WHERE c.file_id = ");
  query.append(expr::param(static_cast<int64_t>(file_id)));
  query.sql_.append(" ORDER BY ");
  if (preferred_group) {
    query.sql_.append("(d.group_key IS NOT DISTINCT FROM CAST(");
    query.append(KeyParameter(*preferred_group));
    query.sql_.append(" AS VARCHAR)) DESC, ");
  }
  query.sql_.append("d.display_position LIMIT 1");
  const auto row = duckorm::query_one<PositionRow>(connection, query);
  if (!row) return std::nullopt;
  return AlbumDisplayPosition{
      row->position,
      Identity(folder_id, options.group_field_, GroupKey(options.group_field_, row->key),
               row->stack.value_or(std::string{}), FileId(row->file), FileId(row->cover)),
      row->hidden};
}

auto ReadFileIds(duckorm::Connection connection, const AlbumQueryOptions& options,
                 const std::string&                     model,
                 std::span<const AlbumStackFragmentKey> expanded_fragments, bool has_relevance,
                 int64_t begin, int64_t end) -> std::vector<sl_element_id_t> {
  ValidateStacks(connection);
  auto query = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead,
                          expanded_fragments);
  query.sql_.append("SELECT file_id FROM display WHERE display_position >= ");
  query.append(expr::param(begin));
  query.sql_.append(" AND display_position < ");
  query.append(expr::param(end));
  query.sql_.append(" GROUP BY file_id ORDER BY MIN(display_position)");
  return FileIds(connection, query);
}

auto ReadFragmentIds(duckorm::Connection connection, const AlbumQueryOptions& options,
                     const std::string& model, bool has_relevance,
                     const AlbumStackFragmentKey& fragment) -> std::vector<sl_element_id_t> {
  ValidateStacks(connection);
  auto query = Projection(options, model, has_relevance, sql::EditStateProjection::kNotRead);
  query.sql_.append("SELECT file_id FROM unit_members WHERE stack_id = ");
  query.append(expr::param(fragment.stack_id));
  query.sql_.append(" AND group_key IS NOT DISTINCT FROM CAST(");
  query.append(KeyParameter(fragment.group_key));
  query.sql_.append(
      " AS VARCHAR) ORDER BY (file_id = displayed_cover) DESC, member_order, file_id");
  return FileIds(connection, query);
}
}  // namespace alcedo::album_stack_query

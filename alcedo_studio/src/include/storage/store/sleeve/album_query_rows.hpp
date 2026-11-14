// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "storage/mapper/duckorm/duckdb_orm.hpp"
#include "storage/store/sleeve/element_store.hpp"
namespace alcedo::album_query_rows {
// Shared typed SQL output of photo and stack pages. It is decoded once into the existing
// SearchResultRow result; it is never retained or written back into live project state.
inline constexpr const char* kMatchSetResultColumns =
    "s.file_id, s.image_id, s.file_name, s.camera_model, s.lens, "
    "CAST(s.capture_date AS VARCHAR), s.rating, CAST(epoch(s.added_time) AS BIGINT), "
    "s.color_labels, s.delete_mark";

/// One row of kSearchResultColumns or kMatchSetResultColumns. The text, rating, and mark
/// columns are optional: databases written before their NOT NULL defaults (and the #768 mark
/// migration, which adds no NOT NULL constraint) can hold NULL there, and a NULL reads as the
/// empty text, rating 0, no color label, and no Delete mark.
struct SearchResultDbRow {
  int64_t                    file_id  = 0;
  int64_t                    image_id = 0;
  std::optional<std::string> file_name;
  std::optional<std::string> camera_model;
  std::optional<std::string> lens;
  std::optional<std::string> capture_date;
  std::optional<int32_t>     rating;
  std::optional<int64_t>     added_time;
  std::optional<uint8_t>     color_labels;
  std::optional<bool>        delete_mark;
  using Columns =
      duckorm::RowColumns<&SearchResultDbRow::file_id, &SearchResultDbRow::image_id,
                          &SearchResultDbRow::file_name, &SearchResultDbRow::camera_model,
                          &SearchResultDbRow::lens, &SearchResultDbRow::capture_date,
                          &SearchResultDbRow::rating, &SearchResultDbRow::added_time,
                          &SearchResultDbRow::color_labels, &SearchResultDbRow::delete_mark>;
};

inline auto ToSearchResultRow(const SearchResultDbRow& row) -> SearchResultRow {
  return SearchResultRow{.file_id_      = static_cast<sl_element_id_t>(row.file_id),
                         .image_id_     = static_cast<image_id_t>(row.image_id),
                         .file_name_    = row.file_name.value_or(std::string{}),
                         .camera_model_ = row.camera_model.value_or(std::string{}),
                         .lens_         = row.lens.value_or(std::string{}),
                         .capture_date_ = row.capture_date.value_or(std::string{}),
                         .rating_       = static_cast<int>(row.rating.value_or(0)),
                         .color_labels_ = static_cast<ColorLabelMask>(row.color_labels.value_or(0) &
                                                                      kColorLabelMaskAll),
                         .delete_mark_  = row.delete_mark.value_or(false),
                         .added_time_   = row.added_time};
}

}  // namespace alcedo::album_query_rows

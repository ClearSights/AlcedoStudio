// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#pragma once

#include <string>

#include "storage/mapper/duckorm/duckdb_types.hpp"
#include "storage/store/sleeve/element_store.hpp"

namespace alcedo::album_stack_query {

/// Populate display fields of the existing read result from SearchMatchSet. The caller owns
/// its connection, database lock, and read transaction. Photo counts/statistics remain those
/// of the match set. Only bounded display metadata leaves this operation. Throws on damaged
/// participating stacks or SQL failure; no membership, cover, or order is written.
void ReadDisplay(duckorm::Connection connection, sl_element_id_t folder_id,
                 const AlbumQueryOptions& options, const std::string& model,
                 const AlbumQueryRead& read, bool has_relevance, AlbumQueryResult& result);

}  // namespace alcedo::album_stack_query

// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "storage/mapper/duckorm/duckdb_types.hpp"
#include "storage/store/sleeve/element_store.hpp"

namespace alcedo::album_stack_query {

/// Read the exact no-membership precondition on the caller's locked transaction connection.
bool HasMemberships(duckorm::Connection connection);

/// Reject mismatched scope/group and incorrectly typed keys before SQL. Unknown keys use
/// monostate. Valid absent keys are retained by the presentation owner and match no fragment.
void ValidateExpansion(sl_element_id_t folder_id, const AlbumQueryOptions& options,
                       std::span<const AlbumStackFragmentKey> fragments);
/// Reject a preferred group of the wrong type. A missing preferred group selects first use.
void ValidatePreferredGroup(AlbumGroupField field, const std::optional<AlbumGroupKey>& group);

/// Populate display fields of the existing read result from SearchMatchSet. The caller owns
/// its connection, database lock, and read transaction. Photo counts/statistics remain those
/// of the match set. Only bounded display metadata leaves this operation. Throws on damaged
/// participating stacks or SQL failure; no membership, cover, or order is written.
void ReadDisplay(duckorm::Connection connection, sl_element_id_t folder_id,
                 const AlbumQueryOptions& options, const std::string& model,
                 const AlbumQueryRead& read, bool has_relevance, AlbumQueryResult& result);
/// Move a bounded photo query output into ordinary display occurrences after the transaction
/// proves no memberships exist. Retains photo/group counts and metadata without a second read.
void ProjectUnstackedPage(sl_element_id_t folder, const AlbumQueryOptions& options, int64_t offset,
                          AlbumQueryResult& result);

/// Read only the requested display location from the same projection as ReadDisplay.
/// Caller owns SearchMatchSet, lock, and transaction; throws on damaged data or SQL failure.
auto ReadPosition(duckorm::Connection connection, sl_element_id_t folder_id,
                  const AlbumQueryOptions& options, const std::string& model,
                  std::span<const AlbumStackFragmentKey> expanded_fragments, bool has_relevance,
                  sl_element_id_t file_id, const std::optional<AlbumGroupKey>& preferred_group)
    -> std::optional<AlbumDisplayPosition>;
/// Read unique represented File IDs in a validated display range, with no photo metadata.
/// Caller owns SearchMatchSet, lock, and transaction; throws on damaged data or SQL failure.
auto ReadFileIds(duckorm::Connection connection, const AlbumQueryOptions& options,
                 const std::string&                     model,
                 std::span<const AlbumStackFragmentKey> expanded_fragments, bool has_relevance,
                 int64_t begin, int64_t end) -> std::vector<sl_element_id_t>;
/// Read the exact fragment's matching IDs, cover first then saved order, independent of pages.
/// Caller validates the key and owns SearchMatchSet, lock, and transaction; throws on failure.
auto ReadFragmentIds(duckorm::Connection connection, const AlbumQueryOptions& options,
                     const std::string& model, bool has_relevance,
                     const AlbumStackFragmentKey& fragment) -> std::vector<sl_element_id_t>;

/// Read typed matching fragment keys only, under the caller's read transaction; throws on failure.
auto ReadFragmentKeys(duckorm::Connection connection, sl_element_id_t folder,
                      const AlbumQueryOptions& options, const std::string& model, bool relevance)
    -> std::vector<AlbumStackFragmentKey>;

}  // namespace alcedo::album_stack_query

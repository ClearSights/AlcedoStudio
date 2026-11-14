//  Copyright 2026 Alcedo Studio contributors
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <span>
#include <string>
#include <vector>

#include "sleeve/photo_stack.hpp"
#include "storage/mapper/duckorm/duckdb_expr.hpp"
#include "storage/store/database.hpp"

namespace alcedo {

/**
 * @brief Sole owner of persisted photo-stack membership, order, and saved cover.
 *
 * Each call opens a connection, takes the database lock, and uses one transaction.
 * Complete input and affected-stack validation precedes writes. Writes commit together,
 * including source-stack repair. Invalid input throws std::invalid_argument; damaged rows
 * or SQL failures throw std::runtime_error with the diagnostic. No failed write changes rows.
 * The Database must outlive this store. Results belong to callers; no stack state is cached.
 */
class PhotoStackStore {
 public:
  explicit PhotoStackStore(Database& database) : database_(database) {}
  /// Create from first-occurrence ordered IDs (at least two live Files). A complete existing
  /// stack is a no-op. Otherwise move the selected cover first, retain other input order,
  /// reassign only these Files, and repair their source stacks.
  auto CreateStack(const std::string& stack_id, std::span<const sl_element_id_t> file_ids,
                   sl_element_id_t cover_file_id) -> PhotoStackChange;
  /// Append only new members in input order, retaining the target cover and existing order.
  auto AddMembers(const std::string& stack_id, std::span<const sl_element_id_t> file_ids)
      -> PhotoStackChange;
  /// Remove only explicit memberships. Promote a removed cover by saved order; dissolve <2.
  auto RemoveMembers(std::span<const sl_element_id_t> file_ids) -> PhotoStackChange;
  /// Remove an entire stack's organization, preserving all photos and album links.
  auto UngroupStack(const std::string& stack_id) -> PhotoStackChange;
  /// Move a member to the front and persist it as cover, retaining the other members' order.
  /// Unchanged cover returns a no-op. Reject nonmembers; order and cover commit together.
  auto SetCover(const std::string& stack_id, sl_element_id_t file_id) -> PhotoStackChange;
  /// Swap a member with its neighbor; the first member becomes cover in the same transaction.
  /// An endpoint or unstacked File is a no-op.
  auto MoveMember(sl_element_id_t file_id, PhotoStackMove direction) -> PhotoStackChange;
  /// Split saved order before file_id. The left retains its ID/cover when possible; the right
  /// uses new_stack_id and file_id as cover. Singleton sides become ordinary photos. The first
  /// member is a no-op; invalid/nonmember input or SQL failure leaves both sides unchanged.
  auto        SplitStack(const std::string& stack_id, const std::string& new_stack_id,
                         sl_element_id_t file_id) -> PhotoStackChange;
  /// Validate explicit targets and compute menu availability without exposing stored rows.
  auto ReadActionOptions(std::span<const sl_element_id_t> ids, const std::string& stack,
                         sl_element_id_t clicked) -> PhotoStackActionOptions;
  /// Order validated explicit targets by stack UUID, saved position, then unstacked File ID.
  auto OrderCommandFiles(std::span<const sl_element_id_t> ids) -> std::vector<sl_element_id_t>;
  /// Read validated cover-first member order; accept older independent cover positions.
  /// Unknown stacks are errors, never empty successes. This read changes no stored rows.
  auto ReadMemberIds(const std::string& stack_id) -> std::vector<sl_element_id_t>;
  /// Validate every stored stack and detect orphan memberships under one read transaction.
  /// Project open calls this before exposing services. Throws the persisted-data diagnostic;
  /// never repairs damaged data or changes covers, memberships, or positions.
  void        ValidateAll();
  /// Validate the stacks touched by a File ID SELECT on the caller's locked read transaction.
  /// The prepared query returns one file_id column. Reads minimal IDs and checks the complete
  /// affected relations; opens no connection and changes no rows. Throws the actual diagnostic.
  static void ValidateFileStacks(duckdb_connection&          connection,
                                 const duckorm::SqlFragment& file_ids);
  /// Remove memberships before File deletion on the caller's locked transaction connection.
  /// Validate affected stacks, then repair covers or dissolve them. Opens no connection or
  /// transaction; the caller must retain live File bindings until this method returns.
  static void RemoveFileMemberships(duckdb_connection&               connection,
                                    std::span<const sl_element_id_t> file_ids);

 private:
  Database& database_;
};

}  // namespace alcedo

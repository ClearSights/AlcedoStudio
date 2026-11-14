//  Copyright 2026 Alcedo Studio contributors
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <memory>
#include <span>
#include <string>
#include <vector>

#include "sleeve/photo_stack.hpp"
#include "sleeve/storage.hpp"

namespace alcedo {

/**
 * @brief Application commands for project-wide photo organization.
 *
 * Holds project storage alive, generates UUIDs, and routes focused operations to its owner.
 * Calls are synchronous and serialized by storage. Results own only committed change facts.
 * Exceptions preserve validation/storage diagnostics. These commands do not save the package,
 * mutate image metadata, or add image-edit history. ProjectService owns the service lifetime.
 */
class PhotoStackService {
 public:
  /// Require non-null project storage; retain it for all operations and outstanding callers.
  explicit PhotoStackService(std::shared_ptr<Storage> storage);
  /// Create a stack from ordered live File IDs and a selected cover; deduplicate by first use.
  auto CreateStack(std::span<const sl_element_id_t> file_ids, sl_element_id_t cover_file_id)
      -> PhotoStackChange;
  /// Append explicit Files and atomically repair their former stacks.
  auto AddMembers(const std::string& stack_id, std::span<const sl_element_id_t> file_ids)
      -> PhotoStackChange;
  /// Remove explicit memberships and repair covers or dissolve stacks with fewer than two Files.
  auto RemoveMembers(std::span<const sl_element_id_t> file_ids) -> PhotoStackChange;
  /// Remove organization only; unknown stacks report an error.
  auto UngroupStack(const std::string& stack_id) -> PhotoStackChange;
  /// Move an existing member first and persist it as cover; a nonmember reports an error.
  auto SetCover(const std::string& stack_id, sl_element_id_t file_id) -> PhotoStackChange;
  /// Move relative to cover-first order and update the cover when the first member changes.
  /// An endpoint or unstacked File returns a no-op.
  auto MoveMember(sl_element_id_t file_id, PhotoStackMove direction) -> PhotoStackChange;
  /// Atomically split saved order before file_id; a one-photo side becomes unstacked.
  /// Generate the right stack's UUID here. Invalid input and storage errors propagate.
  auto SplitStack(const std::string& stack_id, sl_element_id_t file_id) -> PhotoStackChange;
  /// Validate explicit targets and compute menu availability without exposing stored rows.
  auto ReadActionOptions(std::span<const sl_element_id_t> ids, const std::string& stack,
                         sl_element_id_t clicked) -> PhotoStackActionOptions;
  /// Order validated explicit targets by stack UUID, saved position, then unstacked File ID.
  auto OrderCommandFiles(std::span<const sl_element_id_t> ids) -> std::vector<sl_element_id_t>;
  /// Return the caller-owned, validated member IDs in saved order.
  auto ReadMemberIds(const std::string& stack_id) -> std::vector<sl_element_id_t>;

 private:
  std::shared_ptr<Storage> storage_;
};

}  // namespace alcedo

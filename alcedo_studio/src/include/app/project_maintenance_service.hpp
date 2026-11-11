// Copyright 2026 Yurun Zi
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "storage/store/sleeve/project_maintenance_store.hpp"
#include "utils/file/source_image_status.hpp"

namespace alcedo {
enum class MaintenanceFindingKind { Duplicate, MissingSource, SourceProblem, ProjectProblem };
using MaintenanceSourceStatus = SourceImageStatus;
using MaintenanceSourceCheck  = SourceImageStatusResult;
/// Advisory derived finding, owned by the service until a new scan or project change.
struct MaintenanceFinding {
  MaintenanceEntryEvidence       evidence;
  MaintenanceFindingKind         kind = MaintenanceFindingKind::ProjectProblem;
  std::string                    reason;
  std::optional<sl_element_id_t> retained_id;
  bool                           removable = false;
};

/// Read-only full-project inspection. The caller serializes scan/clear and result reads.
/// Owns derived findings only; storage and live editor state remain with their owners.
class ProjectMaintenanceService {
 public:
  using SourceChecker = std::function<MaintenanceSourceCheck(const image_path_t&)>;
  /// Check the volume before classifying ENOENT, then open/read one byte without RAW decoding.
  static auto CheckSource(const image_path_t& path) -> MaintenanceSourceCheck;
  /// Enumerate bounded storage pages. Returns false on user cancellation; partial findings
  /// never become applicable. Other fatal owner failures throw. No project writes occur.
  auto Scan(ProjectMaintenanceStore& store, const std::filesystem::path& journal_directory = {},
            std::optional<sl_element_id_t>     active_entry = {},
            const std::function<bool()>&       canceled     = {},
            const std::function<void(size_t)>& progress = {}, SourceChecker checker = {}) -> bool;
  /// Release results when changing projects. Call after the scan worker has settled.
  void Clear();
  auto Findings() const -> const std::vector<MaintenanceFinding>& { return findings_; }
  auto Complete() const -> bool { return complete_; }
  /// Validate explicit IDs against completed findings and produce minimal change descriptions.
  /// Rejects empty, repeated, protected, or unreported IDs before any package operation.
  auto Selection(std::span<const sl_element_id_t> ids) const -> std::vector<MaintenanceRemoval>;

 private:
  std::vector<MaintenanceFinding> findings_;
  bool                            complete_ = false;
};
}  // namespace alcedo

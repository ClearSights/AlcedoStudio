// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "app/photo_stack_service.hpp"
#include "concurrency/thread_pool.hpp"
#include "ui/alcedo_main/album_backend/background_task_controller.hpp"
#include "ui/alcedo_main/album_backend/interaction_policy_controller.hpp"
#include "ui/alcedo_main/album_backend/library_module.hpp"
#include "ui/alcedo_main/album_backend/project_module.hpp"

namespace alcedo::ui {

/// Focused command input. Owned by its worker until completion; contains no UI or stack state.
struct PhotoStackCommand {
  enum class Operation { kCreate, kAdd, kRemove, kUngroup, kSetCover, kMove };
  Operation                    operation = Operation::kCreate;
  std::vector<sl_element_id_t> file_ids;
  std::string                  stack_id;
  sl_element_id_t              file_id   = 0;  ///< Cover for create/set-cover; member for move.
  PhotoStackMove               direction = PhotoStackMove::kEarlier;
};

/// Terminal result distinguishes transaction failure from failure to save committed rows.
struct PhotoStackCommandResult {
  enum class Outcome { kFailed, kUnchanged, kSaved, kUnsaved };
  Outcome          outcome = Outcome::kFailed;
  PhotoStackChange change;
  QString          error;
};

/// GUI command/task owner. Services and immutable IDs cross the worker boundary. No stack cache.
class PhotoStackController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool busy READ Busy NOTIFY BusyChanged)

 public:
  PhotoStackController(ProjectModule* project, LibraryModule* library,
                       BackgroundTaskController* tasks, InteractionPolicyController* policy,
                       QObject* parent = nullptr);
  /// Start on the GUI thread; refuse conflicting work before queuing. Error is returned on refusal.
  auto               Start(PhotoStackCommand command) -> QString;
  [[nodiscard]] auto Busy() const -> bool { return !task_id_.isEmpty(); }

 signals:
  void BusyChanged();
  /// Emitted on the GUI thread after refresh request and task completion.
  void Finished(const alcedo::ui::PhotoStackCommandResult& result);

 private:
  ProjectModule*               project_;
  LibraryModule*               library_;
  BackgroundTaskController*    tasks_;
  InteractionPolicyController* policy_;
  QString                      task_id_;
  ThreadPool worker_{1};  ///< Destroyed first, drains work before collaborators retire.
};
}  // namespace alcedo::ui

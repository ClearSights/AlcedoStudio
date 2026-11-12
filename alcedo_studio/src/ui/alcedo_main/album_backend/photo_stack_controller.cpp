// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/alcedo_main/album_backend/photo_stack_controller.hpp"

#include <QMetaObject>
#include <exception>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace alcedo::ui {
namespace {
/// Dispatch only explicit IDs; storage validates the complete transaction before mutation.
auto Execute(PhotoStackService& service, const PhotoStackCommand& command) -> PhotoStackChange {
  switch (command.operation) {
    case PhotoStackCommand::Operation::kCreate:
      return service.CreateStack(command.file_ids, command.file_id);
    case PhotoStackCommand::Operation::kAdd:
      return service.AddMembers(command.stack_id, command.file_ids);
    case PhotoStackCommand::Operation::kRemove:
      return service.RemoveMembers(command.file_ids);
    case PhotoStackCommand::Operation::kUngroup:
      return service.UngroupStack(command.stack_id);
    case PhotoStackCommand::Operation::kSetCover:
      return service.SetCover(command.stack_id, command.file_id);
    case PhotoStackCommand::Operation::kMove:
      return service.MoveMember(command.file_id, command.direction);
  }
  throw std::invalid_argument("Unknown photo stack command.");
}
}  // namespace

PhotoStackController::PhotoStackController(ProjectModule* project, LibraryModule* library,
                                           BackgroundTaskController*    tasks,
                                           InteractionPolicyController* policy, QObject* parent)
    : QObject(parent), project_(project), library_(library), tasks_(tasks), policy_(policy) {}

auto PhotoStackController::Start(PhotoStackCommand command) -> QString {
  if (Busy()) return tr("A photo stack command is running.");
  if (!project_ || !tasks_ || !project_->handler().project() || project_->ProjectLoading())
    return tr("No project is available.");
  if (policy_) {
    const auto answer = policy_->EvaluateEditPhotoStacks();
    if (!answer.value("allowed").toBool()) return answer.value("reason").toString();
  }
  if (const auto reason = project_->ProjectCloseBlockReason(); !reason.isEmpty()) return reason;

  const std::unordered_set<sl_element_id_t> ids(command.file_ids.begin(), command.file_ids.end());
  if (ids.contains(0)) return tr("A photo File ID is invalid.");
  switch (command.operation) {
    case PhotoStackCommand::Operation::kCreate:
      if (ids.size() < 2) return tr("Select at least two photos.");
      if (command.file_id == 0) command.file_id = command.file_ids.front();
      if (!ids.contains(command.file_id)) return tr("The stack cover must be selected.");
      break;
    case PhotoStackCommand::Operation::kAdd:
    case PhotoStackCommand::Operation::kRemove:
      if (ids.empty()) return tr("Select at least one photo.");
      break;
    case PhotoStackCommand::Operation::kSetCover:
    case PhotoStackCommand::Operation::kMove:
      if (command.file_id == 0) return tr("A photo File ID is invalid.");
      break;
    case PhotoStackCommand::Operation::kUngroup:
      break;
  }
  if ((command.operation == PhotoStackCommand::Operation::kAdd ||
       command.operation == PhotoStackCommand::Operation::kSetCover ||
       command.operation == PhotoStackCommand::Operation::kUngroup) &&
      command.stack_id.empty())
    return tr("A photo stack ID is required.");

  const auto             reason = tr("A photo stack command is running.");
  BackgroundTaskSnapshot task;
  task.kind_             = BackgroundTaskKind::PhotoStackUpdate;
  task.state_            = BackgroundTaskState::Running;
  task.title_            = tr("Updating photo stacks");
  task.progress_percent_ = -1;
  task.shutdown_policy_  = BackgroundTaskShutdownPolicy::WaitForFinish;
  // Source stack repair may affect unselected members, so deletion is locked project-wide.
  task.locks_            = {{InteractionCapability::EditPhotoStacks, 0, reason},
                            {InteractionCapability::DeleteImages, 0, reason},
                            {InteractionCapability::CloseProject, 0, reason}};
  task_id_               = tasks_->RegisterTask(task);
  emit BusyChanged();
  auto service = project_->handler().project()->GetPhotoStackService();
  auto save    = project_->handler().MakeSaveAndPackageJob();
  worker_.Submit([this, service = std::move(service), save = std::move(save),
                  command = std::move(command)] {
    PhotoStackCommandResult result;
    try {
      result.change  = Execute(*service, command);
      result.outcome = result.change.Changed() ? PhotoStackCommandResult::Outcome::kUnsaved
                                               : PhotoStackCommandResult::Outcome::kUnchanged;
      if (result.change.Changed() && save(&result.error))
        result.outcome = PhotoStackCommandResult::Outcome::kSaved;
    } catch (const std::exception& error) {
      result.error = QString::fromUtf8(error.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, result = std::move(result)] {
          tasks_->FinishTask(task_id_,
                             result.outcome == PhotoStackCommandResult::Outcome::kFailed ||
                                     result.outcome == PhotoStackCommandResult::Outcome::kUnsaved
                                 ? BackgroundTaskState::Failed
                                 : BackgroundTaskState::Succeeded,
                             result.error);
          if (result.outcome == PhotoStackCommandResult::Outcome::kUnsaved) {
            project_->SetStackSaveState(true, result.error);
            project_->SetServiceMessageForCurrentProject(i18n::MakeLocalizedText(
                ALCEDO_I18N_CONTEXT, "Stack changed, but project save failed: %1", result.error));
          } else if (result.outcome == PhotoStackCommandResult::Outcome::kSaved) {
            project_->SetStackSaveState(false);
          }
          if (result.change.Changed() && library_) library_->RequestLibraryContentRefresh();
          task_id_.clear();
          emit BusyChanged();
          emit Finished(result);
        },
        Qt::QueuedConnection);
  });
  return {};
}
}  // namespace alcedo::ui

// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QObject>
#include <functional>
#include <memory>

#include "app/photo_stack_service.hpp"
#include "sleeve/album_query.hpp"
#include "ui/alcedo_main/album_backend/library_selection.hpp"
#include "ui/alcedo_main/album_backend/search_request_worker.hpp"

namespace alcedo::ui {
// LibraryModule defines this input and owns these operations. Including its header here would
// form a cycle; only immutable shared pointers appear in this API. The implementation includes it.
struct LibraryQueryInput;

/// GUI selection query owner, sharing the Library worker's replacement and teardown policy.
/// Keeps no membership or photo cache. Referenced selection and worker must outlive it.
class LibrarySelectionOperations final : public QObject {
 public:
  LibrarySelectionOperations(LibrarySelection& selection, SearchRequestWorker& worker,
                             std::function<void(const QString&)> error, QObject* parent);
  /// Read exact fragment or explicit whole-stack IDs on the worker, then replace selection.
  /// Failure retains selection and reports the actual diagnostic. No metadata pages are loaded.
  void SelectFragment(std::shared_ptr<const LibraryQueryInput> input,
                      const AlbumStackFragmentKey&             fragment);
  void SelectEntireStack(std::shared_ptr<const LibraryQueryInput> input,
                         std::shared_ptr<PhotoStackService> service, const std::string& stack);
  /// Intersect on a changed predicate/scope only; collapse only recomputes visibility counts.
  /// Reads query IDs on the worker and applies them to the live selection on the GUI thread.
  void RefreshMembership(std::shared_ptr<const LibraryQueryInput> input, bool intersect);

 private:
  void                 Select(std::shared_ptr<const LibraryQueryInput>      input,
                              std::function<std::vector<sl_element_id_t>()> read_ids);
  LibrarySelection&    selection_;
  SearchRequestWorker& worker_;
  std::function<void(const QString&)> error_;
  // A selection change may replace a queued visibility read after a filter replacement. Keep
  // its requested intersection until the worker accepts that read, rather than losing the action.
  bool                                intersect_pending_ = false;
};
}  // namespace alcedo::ui

// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include "ui/alcedo_main/album_backend/library_selection_operations.hpp"

#include <QMetaObject>
#include <exception>
#include <limits>
#include <utility>

#include "ui/alcedo_main/album_backend/library_module.hpp"

namespace alcedo::ui {
LibrarySelectionOperations::LibrarySelectionOperations(LibrarySelection&    selection,
                                                       SearchRequestWorker& worker,
                                                       std::function<void(const QString&)> error,
                                                       QObject*                            parent)
    : QObject(parent), selection_(selection), worker_(worker), error_(std::move(error)) {}

void LibrarySelectionOperations::SelectFragment(std::shared_ptr<const LibraryQueryInput> input,
                                                const AlbumStackFragmentKey&             fragment) {
  Select(input, [input, fragment] {
    return input->browse_->ReadAlbumMatchingFragmentIds(
        input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
        input->options_, input->active_model_key_, fragment, input->search_.semantic_query_);
  });
}

void LibrarySelectionOperations::SelectEntireStack(std::shared_ptr<const LibraryQueryInput> input,
                                                   std::shared_ptr<PhotoStackService>       service,
                                                   const std::string&                       stack) {
  Select(input, [service = std::move(service), stack] { return service->ReadMemberIds(stack); });
}

void LibrarySelectionOperations::Select(std::shared_ptr<const LibraryQueryInput>      input,
                                        std::function<std::vector<sl_element_id_t>()> read_ids) {
  worker_.Submit(SearchRequestKind::kSelectAll,
                 [this, browse = input->browse_, read_ids = std::move(read_ids)](auto generation) {
                   std::vector<LibrarySelection::SelectedImage> images;
                   QString                                      error;
                   try {
                     const auto ids = read_ids();
                     for (const auto& row : browse->ReadAlbumFileRows(ids))
                       images.push_back({row.file_id_, row.image_id_,
                                         QString::fromStdString(row.file_name_), false});
                   } catch (const std::exception& failure) {
                     error = QString::fromUtf8(failure.what());
                   }
                   QMetaObject::invokeMethod(
                       this,
                       [this, generation, images = std::move(images), error]() mutable {
                         if (!worker_.IsCurrent(SearchRequestKind::kSelectAll, generation)) return;
                         if (!error.isEmpty()) {
                           error_(error);
                           return;
                         }
                         selection_.Replace(std::move(images));
                       },
                       Qt::QueuedConnection);
                 });
}

void LibrarySelectionOperations::RefreshMembership(std::shared_ptr<const LibraryQueryInput> input,
                                                   bool intersect) {
  if (selection_.SelectedCount() == 0) {
    worker_.Invalidate(SearchRequestKind::kSelectionVisibility);
    intersect_pending_ = false;
    selection_.ApplyQueryMembership({}, {}, false, false);
    return;
  }
  intersect_pending_ = intersect_pending_ || intersect;
  worker_.Submit(
      SearchRequestKind::kSelectionVisibility, [this, input = std::move(input)](auto generation) {
        std::vector<sl_element_id_t> matching, displayed;
        QString                      error;
        try {
          const auto filter = MergeFilterNodes(input->stats_filter_, input->search_.filter_);
          matching          = input->browse_->ReadAlbumFileIds(
              input->folder_id_, filter, input->options_, input->active_model_key_, 0,
              std::numeric_limits<int64_t>::max(), input->search_.semantic_query_);
          displayed = input->read_.mode == AlbumReadMode::kDisplay
                          ? input->browse_->ReadAlbumDisplayFileIds(
                                input->folder_id_, filter, input->options_,
                                input->active_model_key_, input->read_.expanded_fragments, 0,
                                std::numeric_limits<int64_t>::max(), input->search_.semantic_query_)
                          : std::vector<sl_element_id_t>{};
        } catch (const std::exception& failure) {
          error = QString::fromUtf8(failure.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, display_mode = input->read_.mode == AlbumReadMode::kDisplay,
             matching = std::move(matching), displayed = std::move(displayed), error] {
              if (!worker_.IsCurrent(SearchRequestKind::kSelectionVisibility, generation)) return;
              if (!error.isEmpty()) {
                error_(error);
                return;
              }
              const bool intersect = std::exchange(intersect_pending_, false);
              selection_.ApplyQueryMembership(matching, displayed, intersect, display_mode);
            },
            Qt::QueuedConnection);
      });
}
}  // namespace alcedo::ui

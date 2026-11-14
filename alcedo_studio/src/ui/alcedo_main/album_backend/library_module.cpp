//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/alcedo_main/album_backend/library_module.hpp"

#include <QDate>
#include <QDateTime>
#include <QInputDialog>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimeZone>
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "app/project_service.hpp"
#include "image/image.hpp"
#include "sleeve/sleeve_filter/filter_combo.hpp"
#include "sleeve/storage.hpp"
#include "ui/alcedo_main/album_backend/folder_controller.hpp"
#include "ui/alcedo_main/album_backend/path_utils.hpp"
#include "ui/alcedo_main/album_backend/project_module.hpp"
#include "ui/alcedo_main/album_backend/search_controller.hpp"
#include "ui/alcedo_main/album_backend/semantic_generation_controller.hpp"
#include "ui/alcedo_main/album_backend/stats_engine.hpp"
#include "utils/string/convert.hpp"

namespace alcedo::ui {

using namespace album_util;
#define PL_TEXT(text, ...)                     \
  i18n::MakeLocalizedText(ALCEDO_I18N_CONTEXT, \
                          QT_TRANSLATE_NOOP(ALCEDO_I18N_CONTEXT, text) __VA_OPT__(, ) __VA_ARGS__)

namespace {
constexpr size_t kAlbumMetadataPageSize  = 1000;
constexpr size_t kSearchMetadataPageSize = 120;
/// Occurrence pages that a grouped view keeps loaded near its viewport.
constexpr size_t kMaxRetainedSectionPages = 3;

/// Compare the active values of Inspector-owned bucket trees (inactive fields are uninitialized).
auto             SameInspectorFilter(const FilterNode* left, const FilterNode* right) -> bool {
  if (!left || !right) return left == right;
  if (left->type_ != right->type_) return false;
  if (left->type_ == FilterNode::Type::StatsBucket) {
    const auto& a = left->stats_bucket_;
    const auto& b = right->stats_bucket_;
    return a && b && a->dimension == b->dimension && a->key == b->key &&
           a->time_zone == b->time_zone && a->semantic_model_key == b->semantic_model_key;
  }
  if (left->type_ != FilterNode::Type::Logical) return false;
  if (left->op_ != right->op_ || left->children_.size() != right->children_.size()) return false;
  for (size_t i = 0; i < left->children_.size(); ++i)
    if (!SameInspectorFilter(&left->children_[i], &right->children_[i])) return false;
  return true;
}

auto FormatCacheSize(size_t bytes) -> QString {
  if (bytes == 0) {
    return QStringLiteral("0 KiB");
  }
  if (bytes < 1024) {
    return QStringLiteral("< 1 KiB");
  }
  static constexpr const char* kUnits[] = {"KiB", "MiB", "GiB", "TiB"};
  double value = static_cast<double>(bytes) / 1024.0;
  int unit_idx = 0;
  while (value >= 1024.0 && unit_idx < 3) {
    value /= 1024.0;
    ++unit_idx;
  }
  const int decimals = value >= 10.0 ? 1 : 2;
  return QStringLiteral("%1 %2").arg(value, 0, 'f', decimals).arg(QLatin1String(kUnits[unit_idx]));
}

class ThumbnailModelLoadingGuard {
 public:
  explicit ThumbnailModelLoadingGuard(AlbumThumbnailModel& model) : model_(model) {
    model_.setLoading(true);
  }
  ~ThumbnailModelLoadingGuard() { model_.setLoading(false); }
  ThumbnailModelLoadingGuard(const ThumbnailModelLoadingGuard&) = delete;
  ThumbnailModelLoadingGuard& operator=(const ThumbnailModelLoadingGuard&) = delete;
 private:
  AlbumThumbnailModel& model_;
};
}  // namespace

LibraryModule::LibraryModule(ProjectModule* project, QObject* parent)
    : QObject(parent),
      project_(project),
      thumbs_(*this),
      query_worker_(std::make_unique<SearchRequestWorker>()) {
  selection_operations_ = std::make_unique<LibrarySelectionOperations>(
      selection_, *query_worker_, [this](const QString& error) { SetQueryError(error); }, this);
  LoadThumbnailDiskCacheSettings();
  connect(&display_model_, &LibraryDisplayModel::ExpansionChanged, this, [this] {
    if (stack_display_enabled_) RequestLibraryContentRefresh();
  });
  connect(&selection_, &LibrarySelection::SelectionChanged, this, [this]() {
    ScheduleSelectionMarksRead();
    RefreshSelectionMembership();
  });
}

void LibraryModule::SetStackDisplayEnabled(bool enabled) {
  if (stack_display_enabled_ == enabled) return;
  stack_display_enabled_ = enabled;
  RequestLibraryRefresh();
}
void LibraryModule::SetEditorPhotoView(bool active) {
  if (editor_photo_view_ == active) return;
  editor_photo_view_ = active;
  if (stack_display_enabled_) RequestLibraryRefresh();
}
void LibraryModule::ClearProjectPresentation() {
  for (auto kind :
       {SearchRequestKind::kApply, SearchRequestKind::kLibraryPage, SearchRequestKind::kLibraryIds,
        SearchRequestKind::kEditorList, SearchRequestKind::kSelectAll,
        SearchRequestKind::kSelectionVisibility, SearchRequestKind::kStatistics}) {
    query_worker_->Invalidate(kind);
  }
  ResetThumbnailWindow(true);
  intersect_selection_on_refresh_ = false;
  selection_.Clear();
  selection_.ApplyQueryMembership({}, {}, false, false);
}

LibraryModule::~LibraryModule() { query_worker_.reset(); }

void LibraryModule::BindCollaborators(FolderController* folders, SearchController* search,
                                       StatsEngine* stats) {
  folders_ = folders;
  search_ = search;
  stats_ = stats;
}

void LibraryModule::SetSemanticLabelProvider(
    std::function<QString(sl_element_id_t)> provider) {
  semantic_label_provider_ = std::move(provider);
}

void LibraryModule::NotifyThumbnailsChanged() {
  emit ThumbnailsChanged();
  emit thumbnailsChanged();
}

void LibraryModule::NotifyCountsChanged() { emit CountsChanged(); }

void LibraryModule::EmitThumbnailUpdated(uint elementId, const QString& thumbUrl, bool loading,
                                         bool missingSource, const QString& errorText) {
  emit ThumbnailUpdated(elementId, thumbUrl, loading, missingSource, errorText);
  emit thumbnailUpdated(elementId, thumbUrl, loading, missingSource, errorText);
}


int LibraryModule::TotalCount() const {
  return static_cast<int>(
      std::min<size_t>(view_state_.total_count_, std::numeric_limits<int>::max()));
}

QVariantList LibraryModule::Thumbnails() const {
  QVariantList rows;
  rows.reserve(static_cast<qsizetype>(thumbnail_model_.items().size()));
  int index = 0;
  for (const AlbumItem& image : thumbnail_model_.items()) {
    rows.push_back(stats_->MakeThumbMap(image, index++));
  }
  return rows;
}

// ── Q_INVOKABLE: Folder delegation ──────────────────────────────────────────


void LibraryModule::SetThumbnailVisible(uint elementId, uint imageId, bool visible, uint maxEdge) {
  thumbs().SetThumbnailVisible(elementId, imageId, visible, maxEdge);
}


void LibraryModule::SetThumbnailCacheHint(uint visibleCells, uint maxEdge) {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (!thumb_svc) {
    return;
  }

  // Cache by count, but cap high-resolution tiers aggressively because
  // thumbnails are stored as float RGBA ImageBuffers before QML conversion.
  const uint32_t scroll_buffer = std::max<uint32_t>(visibleCells * 3, visibleCells + 4);
  uint32_t       tier_cap      = 96;
  if (maxEdge > 1024) {
    tier_cap = 8;
  } else if (maxEdge > 512) {
    tier_cap = 16;
  } else if (maxEdge > 256) {
    tier_cap = 48;
  }
  const uint32_t desired = std::clamp<uint32_t>(scroll_buffer, 4, tier_cap);
  try {
    thumb_svc->ResizeCache(desired);
  } catch (...) {
  }
}


bool LibraryModule::LoadMoreThumbnails() {
  if (refresh_in_flight_ || page_in_flight_ || !thumbnail_model_.hasMore() || !accepted_input_) {
    return false;
  }
  if (accepted_options_.group_field_ != AlbumGroupField::kNone) {
    // Grouped mode: the page after the last loaded page.
    const auto next = section_model_.LoadedEnd();
    if (next >= display_model_.DisplayCount()) {
      return false;
    }
    requested_begin_ = next;
    requested_end_   = std::min(display_model_.DisplayCount(), next + accepted_input_->page_size_);
    SubmitPage(next);
    return true;
  }
  SubmitPage(loaded_display_end_);
  return true;
}

bool LibraryModule::LoadThumbnailsThroughIndex(int index) {
  if (index < 0) {
    return false;
  }
  load_through_ = std::max<int64_t>(load_through_, index);
  if (accepted_options_.group_field_ != AlbumGroupField::kNone ||
      (accepted_input_ && accepted_input_->read_.mode == AlbumReadMode::kDisplay)) {
    // Grouped mode: load the page that holds the occurrence, not every earlier page.
    if (accepted_input_ && index < display_model_.DisplayCount()) {
      const auto page  = accepted_input_->page_size_;
      requested_begin_ = (index / page) * page;
      requested_end_   = std::min(display_model_.DisplayCount(), requested_begin_ + page);
      ContinuePaging();
      return true;
    }
    return false;
  }
  if (loaded_display_end_ > index) {
    return false;
  }
  return LoadMoreThumbnails();
}

void LibraryModule::RefreshSelectionMembership(bool intersect) {
  if (auto input = AcceptedReadInput({}); input && !refresh_in_flight_)
    selection_operations_->RefreshMembership(std::move(input), intersect);
}

void LibraryModule::ToggleStack(const QString& key) {
  const auto* item = display_model_.ItemAt(display_model_.rowByKey(key));
  if (!item || item->identity.fragment.stack_id.empty()) return;
  display_model_.SetStackExpanded(item->identity.fragment, !item->expanded);
}
void LibraryModule::SelectStack(const QString& key, bool entire) {
  const auto* item = display_model_.ItemAt(display_model_.rowByKey(key));
  if (!item || item->identity.fragment.stack_id.empty()) return;
  if (entire)
    SelectEntireStack(item->identity.fragment.stack_id);
  else
    SelectMatchingStackPhotos(item->identity.fragment);
}
void LibraryModule::SetAllStacksExpanded(bool expanded) { ChangeStackExpansion(expanded); }
void LibraryModule::ToggleAllStacksExpanded() { ChangeStackExpansion(std::nullopt); }
void LibraryModule::ExpandSplitStacks(std::vector<std::string> stack_ids) {
  ChangeStackExpansion(true, std::move(stack_ids));
}
void LibraryModule::ChangeStackExpansion(std::optional<bool>      expanded,
                                         std::vector<std::string> stack_ids) {
  if (refresh_in_flight_) {
    RunAfterRefresh([this, expanded, stack_ids = std::move(stack_ids)]() mutable {
      ChangeStackExpansion(expanded, std::move(stack_ids));
    });
    return;
  }
  auto input = AcceptedReadInput({});
  if (!input) return;
  query_worker_->Submit(SearchRequestKind::kLibraryIds, [this, input, expanded,
                                                         stack_ids = std::move(stack_ids)](
                                                            auto generation) {
    std::vector<AlbumStackFragmentKey> keys;
    QString                            error;
    try {
      keys = input->browse_->ReadAlbumStackFragmentKeys(
          input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
          input->options_, input->active_model_key_, input->search_.semantic_query_);
      if (!stack_ids.empty())
        std::erase_if(keys, [&](const auto& key) {
          return std::find(stack_ids.begin(), stack_ids.end(), key.stack_id) == stack_ids.end();
        });
    } catch (const std::exception& failure) {
      error = QString::fromUtf8(failure.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, keys = std::move(keys), expanded, error] {
          if (!query_worker_->IsCurrent(SearchRequestKind::kLibraryIds, generation)) return;
          if (!error.isEmpty()) {
            SetQueryError(error);
            return;
          }
          // One content refresh after every choice is visible.
          const QSignalBlocker block(&display_model_);
          const bool           next =
              expanded.value_or(std::any_of(keys.begin(), keys.end(), [this](const auto& key) {
                return !display_model_.IsStackExpanded(key);
              }));
          for (const auto& key : keys) display_model_.SetStackExpanded(key, next);
          RequestLibraryContentRefresh();
        },
        Qt::QueuedConnection);
  });
}
auto LibraryModule::StackCommandOrder(std::vector<sl_element_id_t> ids) const
    -> std::function<std::vector<sl_element_id_t>()> {
  auto input   = AcceptedReadInput({});
  auto service = project_->handler().project()->GetPhotoStackService();
  return [input = std::move(input), service = std::move(service), ids = std::move(ids)] {
    auto outside = service->OrderCommandFiles(ids);
    if (!input) return outside;
    const auto displayed = input->browse_->ReadAlbumDisplayFileIds(
        input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
        input->options_, input->active_model_key_, input->read_.expanded_fragments, 0,
        std::numeric_limits<int64_t>::max(), input->search_.semantic_query_);
    std::unordered_set<sl_element_id_t> remaining(ids.begin(), ids.end());
    std::vector<sl_element_id_t>        ordered;
    for (auto file : displayed)
      if (remaining.erase(file)) ordered.push_back(file);
    for (auto file : outside)
      if (remaining.erase(file)) ordered.push_back(file);
    return ordered;
  };
}

void LibraryModule::SelectMatchingStackPhotos(const AlbumStackFragmentKey& fragment) {
  if (refresh_in_flight_) {
    RunAfterRefresh([this, fragment] { SelectMatchingStackPhotos(fragment); });
    return;
  }
  if (auto input = AcceptedReadInput({}))
    selection_operations_->SelectFragment(std::move(input), fragment);
}

void LibraryModule::SelectEntireStack(const std::string& stack_id) {
  if (refresh_in_flight_) {
    RunAfterRefresh([this, stack_id] { SelectEntireStack(stack_id); });
    return;
  }
  if (auto input = AcceptedReadInput({}))
    selection_operations_->SelectEntireStack(
        std::move(input), project_->handler().project()->GetPhotoStackService(), stack_id);
}

void LibraryModule::SelectAllInCurrentView() {
  if (refresh_in_flight_) {
    // The ids must come from the result that the next refresh accepts.
    RunAfterRefresh([this]() { SelectAllInCurrentView(); });
    return;
  }
  auto input = occurrence_count_ > 0 ? AcceptedReadInput({}) : nullptr;
  if (!input) {
    query_worker_->Invalidate(SearchRequestKind::kSelectAll);
    selection_.Clear();
    return;
  }
  // Every occurrence: all unique files of the query, also those of collapsed groups.
  const int64_t occurrence_end = occurrence_count_;
  query_worker_->Submit(SearchRequestKind::kSelectAll, [this, input,
                                                        occurrence_end](std::uint64_t generation) {
    std::vector<LibrarySelection::SelectedImage> images;
    QString                                      error;
    try {
      const auto filter = MergeFilterNodes(input->stats_filter_, input->search_.filter_);
      const auto ids  = input->browse_->ReadAlbumFileIds(input->folder_id_, filter, input->options_,
                                                         input->active_model_key_, 0, occurrence_end,
                                                         input->search_.semantic_query_);
      // Ids and names from typed columns, no thumbnail metadata.
      const auto rows = input->browse_->ReadAlbumFileRows(ids);
      images.reserve(rows.size());
      for (const auto& row : rows) {
        images.push_back(LibrarySelection::SelectedImage{
            row.file_id_, row.image_id_, QString::fromUtf8(row.file_name_.c_str()), false});
      }
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, images = std::move(images), error]() mutable {
          if (!query_worker_->IsCurrent(SearchRequestKind::kSelectAll, generation)) {
            return;
          }
          if (!error.isEmpty()) {
            SetQueryError(error);
            return;
          }
          selection_.Replace(std::move(images));
        },
        Qt::QueuedConnection);
  });
}

// ── Q_INVOKABLE: Project I/O ────────────────────────────────────────────────


void LibraryModule::ReloadFolderTree(const std::filesystem::path& preferredFolderPath) {
  auto proj = project_->handler().project();
  if (!proj) {
    folders_->ClearState();
    if (folders_) emit folders_->FoldersChanged();
    if (folders_) emit folders_->FolderSelectionChanged();
    if (folders_) emit folders_->folderSelectionChanged();
    return;
  }

  auto browse = proj->GetAlbumBrowseService();
  if (!browse) {
    return;
  }

  folders_->ReloadTree(preferredFolderPath.empty() ? folders_->current_folder_path()
                                                      : preferredFolderPath);
}

void LibraryModule::ReloadCurrentFolder() { RequestLibraryRefresh(); }

bool LibraryModule::LoadThumbnailWindow(const std::optional<FilterNode>& /*statsFilter*/,
                                        bool reset) {
  if (reset) {
    RequestLibraryRefresh();
    return true;
  }
  return LoadMoreThumbnails();
}

void LibraryModule::ResetThumbnailWindow(bool close_session) {
  thumbnail_model_.beginReplace();
  section_model_.BeginReplace();
  display_model_.BeginReplace();
  thumbs().ReleaseVisibleThumbnailPins();
  view_state_.total_count_ = 0;
  occurrence_count_ = loaded_display_end_ = 0;
  load_through_                           = -1;
  accepted_input_.reset();
  thumbnail_model_.installItems({}, 0, false);
  if (close_session)
    display_model_.ResetSession(false);
  else
    display_model_.Install(0, {});
  section_model_.InstallGroups({}, 0, 0, false, AlbumReadMode::kDisplay);
  if (stats_) stats_->ApplyFolderStats({}, false);
  thumbnail_model_.endReplace();
  section_model_.EndReplace();
  display_model_.EndReplace();
  emit CountsChanged();
}

// ── Library query ───────────────────────────────────────────────────────────

auto CurrentLocalDayTimeZone() -> std::string {
  return QTimeZone::systemTimeZoneId().toStdString();
}

auto InspectorFieldName(AlbumSortField field) -> QString {
  switch (field) {
    case AlbumSortField::kCaptureTime:
      return QStringLiteral("date");
    case AlbumSortField::kImportTime:
      return QStringLiteral("import");
    case AlbumSortField::kCameraModel:
      return QStringLiteral("camera");
    case AlbumSortField::kLens:
      return QStringLiteral("lens");
    case AlbumSortField::kRating:
      return QStringLiteral("rating");
    case AlbumSortField::kLabels:
      return QStringLiteral("label");
    case AlbumSortField::kEditTime:
      return QStringLiteral("edited");
    case AlbumSortField::kNone:
      break;
  }
  return {};
}

auto InspectorFieldName(AlbumGroupField field) -> QString {
  switch (field) {
    case AlbumGroupField::kCaptureDay:
      return QStringLiteral("date");
    case AlbumGroupField::kImportDay:
      return QStringLiteral("import");
    case AlbumGroupField::kCameraModel:
      return QStringLiteral("camera");
    case AlbumGroupField::kLens:
      return QStringLiteral("lens");
    case AlbumGroupField::kRating:
      return QStringLiteral("rating");
    case AlbumGroupField::kLabels:
      return QStringLiteral("label");
    case AlbumGroupField::kEditDay:
      return QStringLiteral("edited");
    case AlbumGroupField::kNone:
      break;
  }
  return {};
}

namespace {

auto SortFieldFromName(const QString& name) -> std::optional<AlbumSortField> {
  for (const auto field :
       {AlbumSortField::kCaptureTime, AlbumSortField::kImportTime, AlbumSortField::kCameraModel,
        AlbumSortField::kLens, AlbumSortField::kRating, AlbumSortField::kLabels,
        AlbumSortField::kEditTime}) {
    if (InspectorFieldName(field) == name) {
      return field;
    }
  }
  return std::nullopt;
}

auto GroupFieldFromName(const QString& name) -> std::optional<AlbumGroupField> {
  for (const auto field :
       {AlbumGroupField::kCaptureDay, AlbumGroupField::kImportDay, AlbumGroupField::kCameraModel,
        AlbumGroupField::kLens, AlbumGroupField::kRating, AlbumGroupField::kLabels,
        AlbumGroupField::kEditDay}) {
    if (InspectorFieldName(field) == name) {
      return field;
    }
  }
  return std::nullopt;
}

/// Runs on the query worker: the pending search WHERE build, then one library read.
/// Convert producer photo results into ordinary display items without retaining another
/// photo representation. Full-ID relocation output contains only identities and scalar IDs.
void NormalizePhotoResult(const LibraryQueryInput& input, AlbumQueryResult& result) {
  if (input.read_.mode != AlbumReadMode::kPhotos) return;
  result.display_count = result.occurrence_count_;
  result.first_display = result.first_occurrence_;
  for (auto& group : result.groups_) {
    group.display_count = group.photo_count_;
    group.first_display = group.first_occurrence_;
  }
  const auto ordinary = [&](sl_element_id_t file, const AlbumGroupKey& key) {
    AlbumDisplayItem item;
    item.file_id = item.identity.file_id = file;
    item.identity.fragment               = {input.folder_id_, input.options_.group_field_, key, {}};
    return item;
  };
  size_t group = 0;
  for (size_t i = 0; i < result.occurrence_file_ids_.size(); ++i) {
    while (group + 1 < result.groups_.size() &&
           result.groups_[group + 1].first_display <= static_cast<int64_t>(i))
      ++group;
    const AlbumGroupKey key = result.groups_.empty() ? AlbumGroupKey{} : result.groups_[group].key_;
    result.display_items.push_back(ordinary(result.occurrence_file_ids_[i], key));
  }
  result.occurrence_file_ids_.clear();
  for (auto& photo : result.rows_) {
    AlbumDisplayRow row;
    static_cast<AlbumDisplayItem&>(row) = ordinary(photo.photo_.file_id_, photo.group_key_);
    row.photo                           = std::move(photo);
    result.display_rows.push_back(std::move(row));
  }
  result.rows_.clear();
}

/// Fill output.row_display_ for the rows of output.result_ on the query worker: the image pool
/// fields that SQL does not return and the semantic label text. A cold image pool reads each
/// Image from the database, so doing this on the UI thread stalled the window for large folders.
void ReadRowDisplayValues(const LibraryQueryInput& input, LibraryQueryOutput& output) {
  const auto& rows = output.result_.display_rows;
  output.row_display_.assign(rows.size(), AlbumItem{});
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const auto& row     = rows[i].photo.photo_;
    auto&       display = output.row_display_[i];
    if (row.file_id_ == 0 || row.image_id_ == 0) {
      continue;
    }
    if (input.image_pool_) {
      try {
        input.image_pool_->Read<void>(row.image_id_, [&display](std::shared_ptr<Image> image) {
          if (!image) return;
          if (!image->image_path_.empty()) {
            display.extension = ExtensionUpper(image->image_path_);
          }
          const auto& exif     = image->exif_display_;
          display.iso          = static_cast<int>(exif.iso_);
          display.aperture     = static_cast<double>(exif.aperture_);
          display.focal_length = static_cast<double>(exif.focal_);
          display.is_hdr       = exif.is_hdr_;
          // The stored dimensions are those of the oriented image (see
          // SetDisplayDimensionsFromLibRaw and PopulateRasterDimensionsFromHeader).
          if (exif.width_ > 0 && exif.height_ > 0) {
            display.aspect_ratio =
                static_cast<double>(exif.width_) / static_cast<double>(exif.height_);
          }
        });
      } catch (...) {
      }
    }
    if (input.storage_ && !input.active_model_key_.empty()) {
      try {
        display.tags = ReadSemanticLabelDisplayText(*input.storage_, row.file_id_,
                                                    input.active_model_key_,
                                                    input.label_language_);
      } catch (...) {
      }
    }
  }
}

auto RunLibraryQuery(const LibraryQueryInput& input) -> LibraryQueryOutput {
  LibraryQueryOutput output;
  try {
    output.search_filter_  = input.search_.filter_;
    output.semantic_query_ = input.search_.semantic_query_;
    if (input.search_.pending_text_.has_value()) {
      if (!input.filter_service_) {
        throw std::runtime_error("Search requires the project filter service.");
      }
      output.search_filter_ = input.filter_service_->BuildFuzzySearchWhere(
          *input.search_.pending_text_, input.search_.field_mask_);
    }
    if (input.search_.semantic_text_.has_value()) {
      // The only library path that reaches the semantic provider. A failure is the error of
      // this read: the search does not change to field search.
      if (!input.filter_service_) {
        throw std::runtime_error("Search requires the project filter service.");
      }
      output.semantic_query_ =
          input.filter_service_->EmbedSemanticQuery(*input.search_.semantic_text_);
    }
    output.result_ = input.browse_->ReadAlbumQuery(
        input.folder_id_, MergeFilterNodes(input.stats_filter_, output.search_filter_),
        input.options_, input.active_model_key_, input.read_, output.semantic_query_);
    NormalizePhotoResult(input, output.result_);
    ReadRowDisplayValues(input, output);
  } catch (const std::exception& e) {
    output.error_ = QString::fromUtf8(e.what());
  } catch (...) {
    output.error_ = QStringLiteral("Unknown library query error.");
  }
  return output;
}

auto GroupKeyFromTitle(AlbumGroupField field, const QString& title, bool unknown)
    -> std::optional<AlbumGroupKey> {
  if (field == AlbumGroupField::kNone) {
    return std::nullopt;
  }
  if (unknown) {
    return AlbumGroupKey{std::monostate{}};
  }
  if (title.isEmpty()) {
    return std::nullopt;
  }
  if (field == AlbumGroupField::kRating) {
    bool       ok    = false;
    const auto value = title.toLongLong(&ok);
    return ok ? std::optional<AlbumGroupKey>(AlbumGroupKey{static_cast<int64_t>(value)})
              : std::nullopt;
  }
  return AlbumGroupKey{title.toStdString()};
}

/// True when @p node is a stats bucket of @p dimension.
auto IsStatsBucketOf(const FilterNode& node, StatsDimensionId dimension) -> bool {
  return node.stats_bucket_.has_value() && node.stats_bucket_->dimension == dimension;
}

/// True when @p node has a condition on the rating (a typed condition or a rating bucket).
auto FilterReadsRating(const FilterNode& node) -> bool {
  if (node.condition_.has_value() && node.condition_->field_ == FilterField::Rating) {
    return true;
  }
  if (IsStatsBucketOf(node, StatsDimensionId::kRating)) {
    return true;
  }
  return std::any_of(node.children_.begin(), node.children_.end(),
                     [](const FilterNode& child) { return FilterReadsRating(child); });
}

/// True when @p node reads a library mark (a color label or Delete mark bucket).
auto FilterReadsMarks(const FilterNode& node) -> bool {
  if (IsStatsBucketOf(node, StatsDimensionId::kColorLabel) ||
      IsStatsBucketOf(node, StatsDimensionId::kDeleteMark)) {
    return true;
  }
  return std::any_of(node.children_.begin(), node.children_.end(),
                     [](const FilterNode& child) { return FilterReadsMarks(child); });
}

auto OptionsReadRating(const AlbumQueryOptions& options) -> bool {
  return options.sort_field_ == AlbumSortField::kRating ||
         options.group_field_ == AlbumGroupField::kRating;
}

auto SamePresentation(const AlbumQueryOptions& left, const AlbumQueryOptions& right) -> bool {
  return left.sort_field_ == right.sort_field_ && left.sort_direction_ == right.sort_direction_ &&
         left.group_field_ == right.group_field_;
}

}  // namespace

auto LibraryModule::RequestedOptions() const -> AlbumQueryOptions {
  return pending_options_.value_or(accepted_options_);
}

void LibraryModule::ToggleInspectorSort(const QString& field, bool descending) {
  const auto sort_field = SortFieldFromName(field);
  if (!sort_field.has_value()) {
    SetQueryError(PL_TEXT("Unknown sort field: %1", field).Render());
    return;
  }
  auto       options   = RequestedOptions();
  const auto direction = descending ? SortDirection::kDescending : SortDirection::kAscending;
  if (options.sort_field_ == *sort_field && options.sort_direction_ == direction) {
    options.sort_field_     = AlbumSortField::kNone;
    options.sort_direction_ = SortDirection::kAscending;
  } else {
    options.sort_field_     = *sort_field;
    options.sort_direction_ = direction;
  }
  pending_options_ = options;
  RequestLibraryRefresh();
}

void LibraryModule::SetInspectorGrouping(const QString& field, bool enabled) {
  const auto group_field = GroupFieldFromName(field);
  if (!group_field.has_value()) {
    SetQueryError(PL_TEXT("Unknown group field: %1", field).Render());
    return;
  }
  auto options = RequestedOptions();
  if (enabled) {
    options.group_field_ = *group_field;
  } else if (options.group_field_ == *group_field) {
    options.group_field_ = AlbumGroupField::kNone;
  } else {
    return;
  }
  pending_options_ = options;
  RequestLibraryRefresh();
}

void LibraryModule::RetryLibraryQuery() { RequestLibraryRefresh(); }

void LibraryModule::SetQueryError(const QString& message) {
  query_error_ = message;
  emit QueryStateChanged();
}

void LibraryModule::RunAfterRefresh(std::function<void()> action) {
  after_refresh_.push_back(std::move(action));
}

void LibraryModule::RequestLibraryRefresh() { ScheduleRefresh(RefreshKind::kReplace); }

void LibraryModule::RequestLibraryContentRefresh() { ScheduleRefresh(RefreshKind::kInPlace); }

void LibraryModule::ScheduleRefresh(RefreshKind kind) {
  if (!refresh_in_flight_) {
    refresh_in_flight_ = true;
    emit QueryStateChanged();
  }
  if (refresh_scheduled_) {
    if (kind == RefreshKind::kReplace) {
      scheduled_refresh_kind_ = RefreshKind::kReplace;
    }
    return;
  }
  refresh_scheduled_      = true;
  scheduled_refresh_kind_ = kind;
  // Combine the requests of this event loop turn: a caller that clears a filter and then
  // reloads the folder gets one read with both changes.
  QMetaObject::invokeMethod(this, [this]() { SubmitPendingRefresh(); }, Qt::QueuedConnection);
}

void LibraryModule::NoteRatingsChanged() {
  RefreshAfterColumnWrite(PresentationReadsRating());
  ScheduleSelectionMarksRead();
}

void LibraryModule::NoteMarksChanged() {
  RefreshAfterColumnWrite(PresentationReadsMarks());
  ScheduleSelectionMarksRead();
}

void LibraryModule::ScheduleSelectionMarksRead() {
  if (selection_marks_read_scheduled_) {
    return;
  }
  selection_marks_read_scheduled_ = true;
  // A rubber band drag changes the selection many times in one turn; one read follows.
  QMetaObject::invokeMethod(
      this,
      [this]() {
        selection_marks_read_scheduled_ = false;
        SubmitSelectionMarksRead();
      },
      Qt::QueuedConnection);
}

void LibraryModule::SubmitSelectionMarksRead() {
  auto       proj   = project_ ? project_->handler().project() : nullptr;
  auto       browse = proj ? proj->GetAlbumBrowseService() : nullptr;
  const auto ids    = selection_.SelectedElementIds();
  if (ids.empty() || !browse) {
    query_worker_->Invalidate(SearchRequestKind::kSelectionMarks);
    selection_marks_.Clear();
    return;
  }
  const auto revision = selection_marks_.LocalChangeRevision();
  query_worker_->Submit(SearchRequestKind::kSelectionMarks, [this, browse, ids,
                                                             revision](std::uint64_t generation) {
    std::vector<SearchResultRow> rows;
    bool                         ok = true;
    try {
      rows = browse->ReadAlbumFileRows(ids);
    } catch (const std::exception&) {
      ok = false;
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, revision, ok, rows = std::move(rows)]() {
          if (!ok || !query_worker_->IsCurrent(SearchRequestKind::kSelectionMarks, generation) ||
              selection_marks_.LocalChangeRevision() != revision) {
            return;
          }
          selection_marks_.Replace(rows);
        },
        Qt::QueuedConnection);
  });
}

auto LibraryModule::PresentationReadsMarks() const -> bool {
  if (!accepted_input_) {
    return false;
  }
  const auto& stats_filter  = accepted_input_->stats_filter_;
  const auto& search_filter = accepted_input_->search_.filter_;
  return (stats_filter.has_value() && FilterReadsMarks(*stats_filter)) ||
         (search_filter.has_value() && FilterReadsMarks(*search_filter));
}

void LibraryModule::RefreshAfterColumnWrite(bool reads_changed_column) {
  if (!accepted_input_ || reads_changed_column) {
    RequestLibraryRefresh();
    return;
  }
  if (refresh_in_flight_ || refresh_scheduled_) {
    // The refresh after the running one reads the new statistics; the rows stay.
    RequestLibraryContentRefresh();
    return;
  }
  SubmitStatisticsRead();
}

auto LibraryModule::PresentationReadsRating() const -> bool {
  if (OptionsReadRating(accepted_options_) || OptionsReadRating(RequestedOptions())) {
    return true;
  }
  if (!accepted_input_) {
    return false;
  }
  const auto& stats_filter  = accepted_input_->stats_filter_;
  const auto& search_filter = accepted_input_->search_.filter_;
  return (stats_filter.has_value() && FilterReadsRating(*stats_filter)) ||
         (search_filter.has_value() && FilterReadsRating(*search_filter));
}

void LibraryModule::SubmitStatisticsRead() {
  auto input = AcceptedReadInput(AlbumQueryRead{.read_statistics_ = true});
  if (!input) {
    return;
  }
  query_worker_->Submit(SearchRequestKind::kStatistics, [this, input](std::uint64_t generation) {
    auto output = RunLibraryQuery(*input);
    QMetaObject::invokeMethod(
        this,
        [this, generation, output = std::move(output)]() {
          if (!query_worker_->IsCurrent(SearchRequestKind::kStatistics, generation) ||
              refresh_in_flight_) {
            // A newer read of the same query, or a refresh that brings its own statistics.
            return;
          }
          if (!output.error_.isEmpty()) {
            qWarning().noquote() << "Library statistics read failed:" << output.error_;
            SetQueryError(output.error_);
            return;
          }
          if (output.result_.statistics_.has_value()) {
            stats_->ApplyFolderStats(ToAlbumStatsView(*output.result_.statistics_), false);
            emit stats_->StatsChanged();
          }
        },
        Qt::QueuedConnection);
  });
}

void LibraryModule::SubmitPendingRefresh() {
  refresh_scheduled_   = false;
  const auto requested_kind = std::exchange(scheduled_refresh_kind_, RefreshKind::kInPlace);
  // This read sees every edit written so far.
  edit_order_stale_    = false;
  auto       proj      = project_ ? project_->handler().project() : nullptr;
  const auto folder_id = folders_ ? folders_->CurrentFolderElementId() : std::nullopt;
  auto       browse    = proj ? proj->GetAlbumBrowseService() : nullptr;
  if (!proj || !browse || !folder_id.has_value() || !search_ || !stats_) {
    // No scope: show no content and run no SQL.
    query_worker_->Invalidate(SearchRequestKind::kApply);
    query_worker_->Invalidate(SearchRequestKind::kLibraryPage);
    query_worker_->Invalidate(SearchRequestKind::kLibraryIds);
    query_worker_->Invalidate(SearchRequestKind::kEditorList);
    query_worker_->Invalidate(SearchRequestKind::kSelectAll);
    query_worker_->Invalidate(SearchRequestKind::kStatistics);
    ResetThumbnailWindow();
    if (stats_) {
      stats_->ClearStats();
    }
    pending_options_.reset();
    after_refresh_.clear();
    refresh_in_flight_   = false;
    unpublished_replace_ = false;
    query_error_.clear();
    emit QueryStateChanged();
    return;
  }

  auto input                            = std::make_shared<LibraryQueryInput>();
  input->folder_id_                     = folder_id.value();
  input->stats_filter_                  = stats_->BuildStatsFilterNode();
  input->search_                        = search_->LibrarySearch();
  input->options_                       = RequestedOptions();
  input->options_.local_day_time_zone_  = CurrentLocalDayTimeZone();
  input->active_model_key_              = stats_->ActiveSemanticModelKey();
  input->page_size_                     = static_cast<int64_t>(
      input->search_.pending_text_.has_value() || input->search_.semantic_text_.has_value() ||
              input->search_.filter_.has_value() || input->search_.semantic_query_.has_value()
          ? kSearchMetadataPageSize
          : kAlbumMetadataPageSize);
  input->read_      = AlbumQueryRead{
           .offset_ = 0, .limit_ = input->page_size_, .read_groups_ = true, .read_statistics_ = true};
  input->browse_         = std::move(browse);
  input->filter_service_ = proj->GetSleeveFilterService();
  input->image_pool_     = proj->GetImagePoolService();
  input->storage_        = proj->GetStorage();
  input->label_language_ = CurrentUiSemanticLabelLanguage();

  // In place only for the same query: the same scope, presentation, and semantic model, no
  // pending search text, and no query change that a superseded read carried. The Inspector
  // filter and the search change only through RequestLibraryRefresh (a replacement). An
  // in-place change installs exactly the new result in any case; the kind decides only whether
  // the views keep their position and animate.
  const bool in_place =
      requested_kind == RefreshKind::kInPlace && !unpublished_replace_ && accepted_input_ &&
      !pending_options_.has_value() &&
      accepted_input_->read_.mode == (stack_display_enabled_ && !editor_photo_view_
                                          ? AlbumReadMode::kDisplay
                                          : AlbumReadMode::kPhotos) &&
      accepted_input_->folder_id_ == input->folder_id_ &&
      SamePresentation(accepted_input_->options_, input->options_) &&
      accepted_input_->options_.local_day_time_zone_ == input->options_.local_day_time_zone_ &&
      accepted_input_->active_model_key_ == input->active_model_key_ &&
      accepted_input_->page_size_ == input->page_size_ &&
      !input->search_.pending_text_.has_value() && !input->search_.semantic_text_.has_value();
  if (in_place) {
    // Counts, groups, statistics, and the file id of every occurrence from one transaction;
    // the photo rows that the change needs are read after the change is planned.
    input->read_ = AlbumQueryRead{.offset_                   = 0,
                                  .limit_                    = 0,
                                  .read_groups_              = true,
                                  .read_statistics_          = true,
                                  .read_occurrence_file_ids_ = true};
  } else {
    unpublished_replace_ = true;
  }

  input->read_.mode = stack_display_enabled_ && !editor_photo_view_ ? AlbumReadMode::kDisplay
                                                                    : AlbumReadMode::kPhotos;
  if (input->read_.mode == AlbumReadMode::kDisplay) {
    input->read_.read_display_items        = in_place;
    input->read_.read_occurrence_file_ids_ = false;
    input->read_.expanded_fragments =
        display_model_.ExpandedFragments(input->folder_id_, input->options_.group_field_);
  }

  // A different scope must not show the previous scope's photos while the new read runs.
  if (accepted_input_ && accepted_input_->folder_id_ != input->folder_id_) {
    intersect_selection_on_refresh_ = true;
    ResetThumbnailWindow();
    stats_->ClearStats();
  }

  // Later reads of the old result must not publish after this refresh. The loaded pages and
  // rows stay as they are until this refresh publishes: no page read runs meanwhile
  // (refresh_in_flight_), so an in-place change is planned against the state it changes.
  query_worker_->Invalidate(SearchRequestKind::kLibraryPage);
  query_worker_->Invalidate(SearchRequestKind::kLibraryIds);
  query_worker_->Invalidate(SearchRequestKind::kSelectAll);
  query_worker_->Invalidate(SearchRequestKind::kSelectionVisibility);
  // An editor list check of the old result is dropped; the check asks again after this
  // refresh is accepted.
  query_worker_->Invalidate(SearchRequestKind::kEditorList);
  page_in_flight_                                    = false;
  std::shared_ptr<const LibraryQueryInput> submitted = input;
  query_worker_->Submit(
      SearchRequestKind::kApply, [this, submitted, in_place](std::uint64_t generation) {
        auto output = RunLibraryQuery(*submitted);
        QMetaObject::invokeMethod(
            this,
            [this, submitted, generation, in_place, output = std::move(output)]() mutable {
              if (!query_worker_->IsCurrent(SearchRequestKind::kApply, generation)) {
                return;
              }
              if (in_place) {
                PublishInPlaceRefresh(submitted, std::move(output));
              } else {
                PublishRefresh(*submitted, std::move(output));
              }
            },
            Qt::QueuedConnection);
      });
}

auto LibraryModule::ItemFromRow(const AlbumQueryRow& query_row, const AlbumItem& display,
                                sl_element_id_t folderId) -> AlbumItem {
  const auto&   row         = query_row.photo_;
  const QString scope_type  = folderId == 0 ? QStringLiteral("root") : QStringLiteral("album");
  const auto    folder_path = folders_ ? folders_->CurrentFolderFsPath() : std::filesystem::path{};
  const auto    file_name   = conv::FromBytes(row.file_name_);
  AlbumItem     item;
  item.element_id = item.file_id = row.file_id_;
  item.image_id                  = row.image_id_;
  item.folder_id                 = folderId;
  item.scope_type                = scope_type;
  item.file_path_                = folder_path / file_name;
  item.accent                    = AccentForIndex(static_cast<size_t>(row.file_id_));
  // The query worker read these from the image pool and the label store (row_display_).
  item.extension    = display.extension;
  item.iso          = display.iso;
  item.aperture     = display.aperture;
  item.focal_length = display.focal_length;
  item.is_hdr       = display.is_hdr;
  item.aspect_ratio = display.aspect_ratio;
  item.tags         = display.tags;
  // SQL supplies these display values.
  item.file_name    = QString::fromUtf8(row.file_name_.c_str());
  item.camera_model = QString::fromUtf8(row.camera_model_.c_str());
  item.lens         = QString::fromUtf8(row.lens_.c_str());
  item.rating       = row.rating_;
  item.color_labels = row.color_labels_;
  item.delete_mark  = row.delete_mark_;
  item.edited_at_ns      = query_row.edited_at_ns_;
  item.edit_version_name = query_row.edited_at_ns_.has_value()
                               ? QString::fromUtf8(query_row.edit_version_name_.c_str())
                               : QString{};
  item.capture_date = QDate::fromString(QString::fromUtf8(row.capture_date_.c_str()), Qt::ISODate);
  item.import_date =
      row.added_time_.has_value()
          ? QDateTime::fromSecsSinceEpoch(*row.added_time_, QTimeZone::utc()).toLocalTime().date()
          : QDate{};
  if (item.extension.isEmpty()) {
    item.extension = ExtensionFromFileName(item.file_name);
  }
  return item;
}

void LibraryModule::FailRefresh(const QString& error) {
  // The accepted content and settings stay; the pending changes stay for Retry.
  qWarning().noquote() << "Library query failed:" << error;
  refresh_in_flight_ = refresh_scheduled_;
  after_refresh_.clear();
  SetQueryError(error);
}

void LibraryModule::FinishRefresh(bool search_changed) {
  emit CountsChanged();
  emit PresentationChanged();
  emit QueryStateChanged();
  emit stats_->StatsChanged();
  emit stats_->StatsFilterChanged();
  if (search_changed) {
    emit search_->SearchStateChanged();
  }
  NotifyThumbnailsChanged();

  auto actions = std::move(after_refresh_);
  after_refresh_.clear();
  for (auto& action : actions) {
    action();
  }
}

void LibraryModule::AcceptQueryInput(const LibraryQueryInput&  input,
                                     const LibraryQueryOutput& output) {
  accepted_input_ = std::make_shared<const LibraryQueryInput>(LibraryQueryInput{
      .folder_id_        = input.folder_id_,
      .stats_filter_     = input.stats_filter_,
      .search_           = LibrarySearchInput{.display_query_  = input.search_.display_query_,
                                              .filter_         = output.search_filter_,
                                              .semantic_query_ = output.semantic_query_,
                                              .pending_        = false,
                                              .field_mask_     = input.search_.field_mask_},
      .options_          = input.options_,
      .active_model_key_ = input.active_model_key_,
      .read_             = AlbumQueryRead{.mode               = input.read_.mode,
                                          .expanded_fragments = input.read_.expanded_fragments},
      .page_size_        = input.page_size_});
}

void LibraryModule::PublishRefresh(const LibraryQueryInput& input, LibraryQueryOutput output) {
  if (!output.error_.isEmpty()) {
    FailRefresh(output.error_);
    return;
  }
  const bool intersect_selection =
      std::exchange(intersect_selection_on_refresh_, false) ||
      (accepted_input_ &&
       (accepted_input_->folder_id_ != input.folder_id_ ||
        !SameInspectorFilter(
            accepted_input_->stats_filter_ ? &*accepted_input_->stats_filter_ : nullptr,
            input.stats_filter_ ? &*input.stats_filter_ : nullptr) ||
        accepted_input_->search_.field_mask_ != input.search_.field_mask_ ||
        accepted_input_->search_.display_query_ != input.search_.display_query_ ||
        accepted_input_->active_model_key_ != input.active_model_key_ ||
        accepted_input_->search_.semantic_query_.has_value() !=
            output.semantic_query_.has_value()));
  auto&                  result        = output.result_;
  const bool             grouped       = input.options_.group_field_ != AlbumGroupField::kNone;
  const bool same_grouping = input.options_.group_field_ == accepted_options_.group_field_;
  std::vector<AlbumItem> items;
  std::vector<AlbumDisplayItem>       display_items;
  std::unordered_set<sl_element_id_t> seen;
  for (size_t i = 0; i < result.display_rows.size(); ++i) {
    auto& row = result.display_rows[i];
    if (seen.insert(row.file_id).second)
      items.push_back(ItemFromRow(row.photo, output.row_display_[i], input.folder_id_));
    display_items.push_back(std::move(static_cast<AlbumDisplayItem&>(row)));
  }
  thumbnail_model_.beginReplace();
  section_model_.BeginReplace();
  display_model_.BeginReplace();
  thumbs().ReleaseVisibleThumbnailPins();
  unpublished_replace_ = false;
  accepted_options_ = input.options_;
  // A change requested after this read was submitted stays pending for its own refresh.
  if (pending_options_.has_value() && SamePresentation(*pending_options_, input.options_)) {
    pending_options_.reset();
  }
  AcceptQueryInput(input, output);
  occurrence_count_        = result.occurrence_count_;
  loaded_display_end_      = static_cast<int64_t>(display_items.size());
  load_through_            = -1;
  view_state_.total_count_ = static_cast<size_t>(result.unique_file_count_);
  LibraryDisplayModel::PageMap pages;
  if (!display_items.empty()) pages.emplace(0, std::move(display_items));
  display_model_.Install(result.display_count, std::move(pages));
  thumbnail_model_.installItems(std::move(items), view_state_.total_count_,
                                loaded_display_end_ < result.display_count);
  section_model_.InstallGroups(std::move(result.groups_), result.unique_file_count_,
                               grouped ? result.display_count : 0, same_grouping,
                               AlbumReadMode::kDisplay);
  if (result.statistics_) stats_->ApplyFolderStats(ToAlbumStatsView(*result.statistics_), false);
  const bool search_changed =
      search_->AcceptLibrarySearch(input.search_, output.search_filter_, output.semantic_query_);
  query_error_.clear();
  refresh_in_flight_ = refresh_scheduled_;
  requested_begin_   = 0;
  requested_end_     = loaded_display_end_;
  thumbnail_model_.endReplace();
  section_model_.EndReplace();
  display_model_.EndReplace();
  FinishRefresh(search_changed);
  RefreshSelectionMembership(intersect_selection);
}

auto LibraryModule::PlanInPlaceChange(const LibraryQueryInput& input,
                                      LibraryQueryOutput&      output) const -> InPlaceChange {
  auto&         items = output.result_.display_items;
  InPlaceChange change;
  if (input.options_.group_field_ == AlbumGroupField::kNone &&
      input.read_.mode == AlbumReadMode::kPhotos) {
    int64_t end = 0;
    for (size_t i = 0; i < items.size(); ++i)
      if (thumbnail_model_.itemByFile(items[i].file_id)) end = static_cast<int64_t>(i + 1);
    if (end == 0 || loaded_display_end_ >= display_model_.DisplayCount())
      end = std::min<int64_t>(items.size(), end + input.page_size_);
    if (end > 0)
      change.pages_.emplace(
          0, std::vector<AlbumDisplayItem>(std::make_move_iterator(items.begin()),
                                           std::make_move_iterator(items.begin() + end)));
  } else
    change.pages_ = display_model_.RetainedPages(items, requested_begin_, input.page_size_,
                                                 kMaxRetainedSectionPages);
  if (change.pages_.empty() && !items.empty()) {
    const auto end = std::min<int64_t>(items.size(), input.page_size_);
    change.pages_.emplace(
        0, std::vector<AlbumDisplayItem>(std::make_move_iterator(items.begin()),
                                         std::make_move_iterator(items.begin() + end)));
  }
  std::unordered_set<sl_element_id_t> wanted, ordered;
  for (const auto& [start, page] : change.pages_)
    for (const auto& item : page) wanted.insert(item.file_id);
  if (input.options_.group_field_ != AlbumGroupField::kNone) {
    for (const auto& item : thumbnail_model_.items())
      if (wanted.contains(item.element_id)) {
        change.loaded_order_.push_back(item.element_id);
        ordered.insert(item.element_id);
      }
  }
  for (const auto& [start, page] : change.pages_)
    for (size_t i = 0; i < page.size(); ++i) {
      const auto id = page[i].file_id;
      if (ordered.insert(id).second) {
        change.loaded_order_.push_back(id);
        if (!thumbnail_model_.itemByFile(id))
          change.unread_occurrences_.push_back(start + static_cast<int64_t>(i));
      }
    }
  if (!change.pages_.empty())
    change.loaded_end_ =
        change.pages_.rbegin()->first + static_cast<int64_t>(change.pages_.rbegin()->second.size());
  std::vector<AlbumDisplayItem>().swap(
      items);  // Relocation no longer needs the full SQL ID stream.
  return change;
}

void LibraryModule::PublishInPlaceRefresh(std::shared_ptr<const LibraryQueryInput> input,
                                          LibraryQueryOutput                       output) {
  if (!output.error_.isEmpty()) {
    FailRefresh(output.error_);
    return;
  }
  auto change = PlanInPlaceChange(*input, output);
  if (change.unread_occurrences_.empty()) {
    ApplyInPlaceChange(*input, std::move(output), std::move(change), LibraryQueryOutput{});
    return;
  }

  // Step 2: read the rows of the unread photos, in ranges of at most one page. It runs as the
  // same refresh (kApply): a newer refresh request replaces it, and the pages and rows that the
  // change was planned against stay as they are until it publishes.
  std::vector<std::pair<int64_t, int64_t>> ranges;
  const auto                               limit =
      std::max<int64_t>(1, std::min<int64_t>(input->page_size_, kMaxAlbumQueryPageRows));
  for (const auto occurrence : change.unread_occurrences_) {
    if (ranges.empty() || occurrence >= ranges.back().first + limit) {
      ranges.emplace_back(occurrence, occurrence + 1);
    } else {
      ranges.back().second = occurrence + 1;
    }
  }
  query_worker_->Submit(
      SearchRequestKind::kApply, [this, input, ranges, output = std::move(output),
                                  change = std::move(change)](std::uint64_t generation) mutable {
        LibraryQueryOutput rows;
        for (const auto& [begin, end] : ranges) {
          auto range_input  = *input;
          range_input.read_ = AlbumQueryRead{
              .offset_            = input->read_.mode == AlbumReadMode::kPhotos ? begin : 0,
              .limit_             = end - begin,
              .mode               = input->read_.mode,
              .display_offset     = input->read_.mode == AlbumReadMode::kDisplay ? begin : 0,
              .expanded_fragments = input->read_.expanded_fragments};
          auto range_output = RunLibraryQuery(range_input);
          if (!range_output.error_.isEmpty()) {
            rows.error_ = range_output.error_;
            break;
          }
          std::move(range_output.result_.display_rows.begin(),
                    range_output.result_.display_rows.end(),
                    std::back_inserter(rows.result_.display_rows));
          std::move(range_output.row_display_.begin(), range_output.row_display_.end(),
                    std::back_inserter(rows.row_display_));
        }
        QMetaObject::invokeMethod(
            this,
            [this, input, generation, output = std::move(output), change = std::move(change),
             rows = std::move(rows)]() mutable {
              if (!query_worker_->IsCurrent(SearchRequestKind::kApply, generation)) {
                return;
              }
              if (!rows.error_.isEmpty()) {
                FailRefresh(rows.error_);
                return;
              }
              // The rows were read in a later transaction than the occurrence ids. A photo that
              // left between the two reads was removed by a write that requests its own refresh;
              // read the change again so it is planned against one state.
              std::unordered_set<sl_element_id_t> read_ids;
              for (const auto& row : rows.result_.display_rows) {
                read_ids.insert(row.photo.photo_.file_id_);
              }
              for (const auto occurrence : change.unread_occurrences_) {
                const auto* item = LibraryDisplayModel::ItemAt(change.pages_, occurrence);
                if (!item || !read_ids.contains(item->file_id)) {
                  RequestLibraryContentRefresh();
                  return;
                }
              }
              ApplyInPlaceChange(*input, std::move(output), std::move(change), rows);
            },
            Qt::QueuedConnection);
      });
}

void LibraryModule::ApplyInPlaceChange(const LibraryQueryInput& input, LibraryQueryOutput output,
                                       InPlaceChange change, const LibraryQueryOutput& rows) {
  auto&      result    = output.result_;
  const bool grouped   = accepted_options_.group_field_ != AlbumGroupField::kNone;
  unpublished_replace_ = false;

  // Views read the previous result here (scroll anchor, cell positions).
  emit                                           LibraryChangeAboutToApply();
  emit                                           libraryChangeAboutToApply();

  std::unordered_map<sl_element_id_t, AlbumItem> new_items;
  for (size_t index = 0; index < rows.result_.display_rows.size(); ++index) {
    const auto& row = rows.result_.display_rows[index];
    const auto  id  = row.photo.photo_.file_id_;
    if (id == 0 || row.photo.photo_.image_id_ == 0 || new_items.contains(id) ||
        thumbnail_model_.rowByElementId(id) >= 0) {
      continue;
    }
    new_items.emplace(id, ItemFromRow(row.photo, rows.row_display_[index], input.folder_id_));
  }
  // A row without an image has no item, as in a replacement; its cell shows no photo.
  std::erase_if(change.loaded_order_, [&](sl_element_id_t id) {
    return !new_items.contains(id) && thumbnail_model_.rowByElementId(id) < 0;
  });

  display_model_.BeginReplace();
  AcceptQueryInput(input, output);
  thumbnail_model_.applyOrder(
      change.loaded_order_, new_items, static_cast<size_t>(result.unique_file_count_),
      change.loaded_end_ < result.display_count, [&] {
        occurrence_count_        = result.occurrence_count_;
        view_state_.total_count_ = static_cast<size_t>(result.unique_file_count_);
        loaded_display_end_      = change.loaded_end_;
        load_through_            = -1;
        display_model_.Install(result.display_count, std::move(change.pages_));
        if (result.statistics_)
          stats_->ApplyFolderStats(ToAlbumStatsView(*result.statistics_), false);
        requested_begin_ = std::min(requested_begin_, result.display_count);
        requested_end_   = std::min(requested_end_, result.display_count);
        query_error_.clear();
        refresh_in_flight_ = refresh_scheduled_;
        if (grouped)
          section_model_.ApplyGroupResultChange(std::move(result.groups_),
                                                result.unique_file_count_, result.display_count);
      });
  thumbs().ReleaseUnloadedThumbnailPins();
  display_model_.EndReplace();

  emit LibraryChangeApplied();
  emit libraryChangeApplied();
  FinishRefresh(false);
  RefreshSelectionMembership();
  ContinuePaging();
}

auto LibraryModule::AcceptedReadInput(AlbumQueryRead read) const
    -> std::shared_ptr<const LibraryQueryInput> {
  auto proj = project_ ? project_->handler().project() : nullptr;
  if (!accepted_input_ || !proj) {
    return nullptr;
  }
  // The accepted input keeps no service: holding one would keep a closed project's database
  // open. Each read takes the services of the open project when it is submitted.
  auto input             = std::make_shared<LibraryQueryInput>(*accepted_input_);
  input->read_           = read;
  input->read_.mode               = accepted_input_->read_.mode;
  input->read_.expanded_fragments = accepted_input_->read_.expanded_fragments;
  if (input->read_.mode == AlbumReadMode::kDisplay) {
    input->read_.display_offset = input->read_.offset_;
    input->read_.offset_        = 0;
  }
  input->browse_         = proj->GetAlbumBrowseService();
  input->filter_service_ = proj->GetSleeveFilterService();
  input->image_pool_     = proj->GetImagePoolService();
  input->storage_        = proj->GetStorage();
  input->label_language_ = CurrentUiSemanticLabelLanguage();
  if (!input->browse_) {
    return nullptr;
  }
  return input;
}

void LibraryModule::SubmitPage(int64_t offset, int64_t limit) {
  if (refresh_in_flight_ || !accepted_input_ || offset < 0 ||
      offset >= display_model_.DisplayCount()) {
    return;
  }
  const auto page  = accepted_input_->page_size_;
  limit =
      std::min(limit > 0 ? std::min(limit, page) : page, display_model_.DisplayCount() - offset);
  auto input       = AcceptedReadInput(AlbumQueryRead{.offset_ = offset, .limit_ = limit});
  page_in_flight_  = true;
  thumbnail_model_.setLoading(true);
  query_worker_->Submit(SearchRequestKind::kLibraryPage, [this, input](std::uint64_t generation) {
    auto output = RunLibraryQuery(*input);
    QMetaObject::invokeMethod(
        this,
        [this, input, generation, output = std::move(output)]() mutable {
          if (!query_worker_->IsCurrent(SearchRequestKind::kLibraryPage, generation)) {
            return;
          }
          PublishPage(*input, std::move(output));
        },
        Qt::QueuedConnection);
  });
}

void LibraryModule::PublishPage(const LibraryQueryInput& input, LibraryQueryOutput output) {
  page_in_flight_ = false;
  thumbnail_model_.setLoading(false);
  if (!output.error_.isEmpty()) {
    SetQueryError(output.error_);
    return;
  }
  std::vector<AlbumItem>              photos;
  std::vector<AlbumDisplayItem>       items;
  std::unordered_set<sl_element_id_t> seen;
  for (size_t i = 0; i < output.result_.display_rows.size(); ++i) {
    auto& row = output.result_.display_rows[i];
    if (seen.insert(row.file_id).second)
      photos.push_back(ItemFromRow(row.photo, output.row_display_[i], input.folder_id_));
    items.push_back(std::move(static_cast<AlbumDisplayItem&>(row)));
  }
  const auto first    = output.result_.first_display;
  loaded_display_end_ = first + static_cast<int64_t>(items.size());
  const bool raw_flat = input.read_.mode == AlbumReadMode::kPhotos &&
                        input.options_.group_field_ == AlbumGroupField::kNone;
  display_model_.BeginReplace();
  if (raw_flat) {
    const auto install = [&] {
      display_model_.StorePage(first, std::move(items), std::numeric_limits<size_t>::max(), false);
    };
    if (!photos.empty())
      thumbnail_model_.appendPage(std::move(photos), install);
    else
      install();
    thumbnail_model_.setHasMore(loaded_display_end_ < display_model_.DisplayCount());
  } else {
    thumbnail_model_.beginReplace();
    display_model_.StorePage(first, std::move(items), kMaxRetainedSectionPages, false);
    thumbnail_model_.installPageItems(std::move(photos), display_model_.LoadedUniqueFileIds(),
                                      view_state_.total_count_,
                                      loaded_display_end_ < display_model_.DisplayCount());
    thumbs().ReleaseUnloadedThumbnailPins();
    thumbnail_model_.endReplace();
  }
  display_model_.EndReplace();
  emit CountsChanged();
  if (raw_flat && load_through_ >= loaded_display_end_ && thumbnail_model_.hasMore())
    LoadMoreThumbnails();
  if (!raw_flat) ContinuePaging();
}

void LibraryModule::ContinuePaging() {
  if (page_in_flight_ || refresh_in_flight_ || !accepted_input_ ||
      (accepted_options_.group_field_ == AlbumGroupField::kNone &&
       accepted_input_->read_.mode != AlbumReadMode::kDisplay)) {
    return;
  }
  // The first occurrence of the requested range (from its page boundary) that no page holds.
  // After an in-place change the loaded pages need not start at page boundaries, so the read
  // ends at the next loaded page.
  const auto page = accepted_input_->page_size_;
  const auto missing =
      section_model_.FirstUnloadedOccurrence((requested_begin_ / page) * page, requested_end_);
  if (missing < 0) {
    return;
  }
  SubmitPage(missing, section_model_.NextLoadedPageStart(missing) - missing);
}

void LibraryModule::RequestDisplayRange(qint64 begin, qint64 end) {
  if (refresh_in_flight_ || !accepted_input_ ||
      accepted_input_->read_.mode != AlbumReadMode::kDisplay)
    return;
  requested_begin_ = std::clamp<int64_t>(begin, 0, display_model_.DisplayCount());
  requested_end_   = std::clamp<int64_t>(end, requested_begin_, display_model_.DisplayCount());
  ContinuePaging();
}

void LibraryModule::RequestSectionRows(int firstRow, int lastRow) {
  const auto range = section_model_.OccurrenceRangeForRows(firstRow, lastRow);
  const auto begin = range.value(QStringLiteral("begin")).toLongLong();
  const auto end   = range.value(QStringLiteral("end")).toLongLong();
  if (begin >= end) {
    return;
  }
  requested_begin_ = begin;
  requested_end_   = end;
  ContinuePaging();
}

auto LibraryModule::RequestOrderedFileIds(const QVariantList& ranges) -> qulonglong {
  std::vector<std::pair<int64_t, int64_t>> occurrence_ranges;
  for (const auto& value : ranges) {
    const auto map   = value.toMap();
    const auto begin = map.value(QStringLiteral("begin")).toLongLong();
    const auto end   = map.value(QStringLiteral("end")).toLongLong();
    if (begin >= 0 && end > begin) {
      occurrence_ranges.emplace_back(begin, end);
    }
  }
  if (refresh_in_flight_) {
    // The ids must come from the result that the next refresh accepts.
    RunAfterRefresh([this, ranges]() { RequestOrderedFileIds(ranges); });
    return 0;
  }
  auto input = AcceptedReadInput({});
  if (!input) {
    return 0;
  }
  const auto request_id = query_worker_->Submit(
      SearchRequestKind::kLibraryIds, [this, input, occurrence_ranges](std::uint64_t generation) {
        QVariantList ids;
        QString      error;
        try {
          std::unordered_set<sl_element_id_t> seen;
          std::vector<sl_element_id_t>        ordered;
          const auto filter = MergeFilterNodes(input->stats_filter_, input->search_.filter_);
          const auto relevance = input->search_.semantic_query_;
          for (const auto& [begin, end] : occurrence_ranges) {
            const auto range_ids =
                input->read_.mode == AlbumReadMode::kDisplay
                    ? input->browse_->ReadAlbumDisplayFileIds(
                          input->folder_id_, filter, input->options_, input->active_model_key_,
                          input->read_.expanded_fragments, begin, end, relevance)
                    : input->browse_->ReadAlbumFileIds(input->folder_id_, filter, input->options_,
                                                       input->active_model_key_, begin, end,
                                                       relevance);
            for (const auto id : range_ids) {
              if (seen.insert(id).second) {
                ordered.push_back(id);
              }
            }
          }
          // Selection items: ids and names from typed columns, no thumbnail metadata.
          for (const auto& row : input->browse_->ReadAlbumFileRows(ordered)) {
            ids.push_back(QVariantMap{
                {QStringLiteral("elementId"), static_cast<uint>(row.file_id_)},
                {QStringLiteral("fileId"), static_cast<uint>(row.file_id_)},
                {QStringLiteral("imageId"), static_cast<uint>(row.image_id_)},
                {QStringLiteral("fileName"), QString::fromUtf8(row.file_name_.c_str())}});
          }
        } catch (const std::exception& e) {
          error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, ids = std::move(ids), error]() {
              if (!query_worker_->IsCurrent(SearchRequestKind::kLibraryIds, generation)) {
                return;
              }
              if (!error.isEmpty()) {
                SetQueryError(error);
                return;
              }
              emit OrderedFileIdsReady(static_cast<qulonglong>(generation), ids);
              emit orderedFileIdsReady(static_cast<qulonglong>(generation), ids);
            },
            Qt::QueuedConnection);
      });
  return static_cast<qulonglong>(request_id);
}

void LibraryModule::RequestFocusPosition(uint fileId, const QString& preferredGroupTitle,
                                         bool preferredGroupUnknown) {
  if (fileId == 0) {
    return;
  }
  if (refresh_in_flight_) {
    RunAfterRefresh([this, fileId, preferredGroupTitle, preferredGroupUnknown]() {
      RequestFocusPosition(fileId, preferredGroupTitle, preferredGroupUnknown);
    });
    return;
  }
  auto input = AcceptedReadInput({});
  if (!input) {
    return;
  }
  const auto preferred =
      GroupKeyFromTitle(input->options_.group_field_, preferredGroupTitle, preferredGroupUnknown);
  query_worker_->Submit(
      SearchRequestKind::kLibraryIds, [this, input, fileId, preferred, preferredGroupTitle,
                                       preferredGroupUnknown](std::uint64_t generation) {
        std::optional<AlbumFilePosition>    position;
        std::optional<AlbumDisplayPosition> display_position;
        QString                             error;
        try {
          if (input->read_.mode == AlbumReadMode::kDisplay) {
            display_position = input->browse_->ReadAlbumDisplayPosition(
                input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
                input->options_, input->active_model_key_, input->read_.expanded_fragments, fileId,
                preferred, input->search_.semantic_query_);
            if (display_position) position = AlbumFilePosition{};
          } else
            position = input->browse_->ReadAlbumFilePosition(
                input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
                input->options_, input->active_model_key_, static_cast<sl_element_id_t>(fileId),
                preferred, input->search_.semantic_query_);
        } catch (const std::exception& e) {
          error = QString::fromUtf8(e.what());
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, fileId, preferredGroupTitle, preferredGroupUnknown, position,
             display_position, error]() {
              if (!query_worker_->IsCurrent(SearchRequestKind::kLibraryIds, generation)) {
                return;
              }
              if (!error.isEmpty()) {
                SetQueryError(error);
                return;
              }
              if (SettlePendingReveal(fileId, position.has_value())) {
                return;
              }
              if (!position.has_value()) {
                SetQueryError(tr("The photo is outside the current Library result."));
                emit FocusPositionReady(fileId, -1, -1);
                emit focusPositionReady(fileId, -1, -1);
                return;
              }
              if (display_position && display_position->hidden_by_collapse) {
                display_model_.SetStackExpanded(display_position->identity.fragment, true);
                RequestFocusPosition(fileId, preferredGroupTitle, preferredGroupUnknown);
                return;
              }
              const auto occurrence =
                  display_position ? display_position->display_index : position->occurrence_index_;
              LoadThumbnailsThroughIndex(static_cast<int>(occurrence));
              const int row = section_model_.RowForOccurrence(occurrence);
              emit      FocusPositionReady(fileId, occurrence, row);
              emit      focusPositionReady(fileId, occurrence, row);
            },
            Qt::QueuedConnection);
      });
}

auto LibraryModule::RequestEditorListPosition(uint fileId, bool loadPage) -> qulonglong {
  if (fileId == 0) {
    return 0;
  }
  const qulonglong request_id = ++editor_list_request_id_;
  SubmitEditorListPosition(request_id, fileId, loadPage);
  return request_id;
}

void LibraryModule::SubmitEditorListPosition(qulonglong requestId, uint fileId, bool loadPage) {
  if (requestId != editor_list_request_id_) {
    return;
  }
  if (refresh_in_flight_) {
    RunAfterRefresh([this, requestId, fileId, loadPage]() {
      SubmitEditorListPosition(requestId, fileId, loadPage);
    });
    return;
  }
  auto input = AcceptedReadInput({});
  if (!input) {
    // No accepted result: the list holds no file. Answer on the next event, like a read.
    QMetaObject::invokeMethod(
        this,
        [this, requestId, fileId]() {
          if (requestId == editor_list_request_id_) {
            emit editorListPositionReady(requestId, fileId, -1);
          }
        },
        Qt::QueuedConnection);
    return;
  }
  query_worker_->Submit(SearchRequestKind::kEditorList, [this, input, requestId, fileId,
                                                         loadPage](std::uint64_t generation) {
    std::optional<AlbumFilePosition> position;
    QString                          error;
    try {
      position = input->browse_->ReadAlbumFilePosition(
          input->folder_id_, MergeFilterNodes(input->stats_filter_, input->search_.filter_),
          input->options_, input->active_model_key_, static_cast<sl_element_id_t>(fileId),
          std::nullopt, input->search_.semantic_query_);
    } catch (const std::exception& e) {
      error = QString::fromUtf8(e.what());
    }
    QMetaObject::invokeMethod(
        this,
        [this, generation, requestId, fileId, loadPage, position, error]() {
          if (!query_worker_->IsCurrent(SearchRequestKind::kEditorList, generation) ||
              requestId != editor_list_request_id_) {
            return;
          }
          if (!error.isEmpty()) {
            SetQueryError(error);
            emit editorListPositionReady(requestId, fileId, -2);
            return;
          }
          if (!position.has_value()) {
            emit editorListPositionReady(requestId, fileId, -1);
            return;
          }
          const auto occurrence = position->occurrence_index_;
          if (loadPage) {
            LoadThumbnailsThroughIndex(static_cast<int>(occurrence));
          }
          emit editorListPositionReady(requestId, fileId, occurrence);
        },
        Qt::QueuedConnection);
  });
}

void LibraryModule::SetOccurrenceThumbnailVisible(const QString& groupTitle, bool groupUnknown,
                                                  uint elementId, uint imageId, bool visible,
                                                  uint maxEdge) {
  // The view's group title (or the unknown flag) identifies the occurrence of the photo.
  thumbs().SetOccurrenceThumbnailVisible(
      groupUnknown ? std::string("u:") : "t:" + groupTitle.toStdString(), elementId, imageId,
      visible, maxEdge);
}

void LibraryModule::SetAlbumItemHdrFlag(sl_element_id_t elementId, image_id_t imageId, bool isHdr) {
  thumbnail_model_.updateHdrFlag(elementId, imageId, isHdr);
}

void LibraryModule::SetAlbumItemEditState(sl_element_id_t elementId, image_id_t imageId,
                                          std::optional<std::int64_t> editedAtNs,
                                          const QString&              versionName) {
  const QString name = editedAtNs.has_value() ? versionName : QString{};
  thumbnail_model_.updateEditState(elementId, imageId, editedAtNs, name);
}

void LibraryModule::PersistImageHdrFlag(sl_element_id_t elementId, image_id_t imageId, bool isHdr) {
  auto proj = project_->handler().project();
  if (!proj || imageId == 0) {
    return;
  }

  try {
    proj->GetImagePoolService()->Write_NoSync<void>(imageId,
                                                    [isHdr](const std::shared_ptr<Image>& image) {
                                                      if (image) {
                                                        image->SetHdrDisplayMetadata(isHdr);
                                                      }
                                                    });
    SetAlbumItemHdrFlag(elementId, imageId, isHdr);
  } catch (...) {
  }
}

auto LibraryModule::FindAlbumItem(sl_element_id_t elementId) const -> const AlbumItem* {
  return thumbnail_model_.itemByFile(elementId);
}

// ── Phase 4: Thumbnail disk cache settings ─────────────────────────────────


void LibraryModule::LoadThumbnailDiskCacheSettings() {
  QSettings settings;
  thumbnail_disk_cache_enabled_ =
      settings.value(QStringLiteral("thumbnailCache/enabled"), true).toBool();
  thumbnail_disk_cache_root_ =
      settings.value(QStringLiteral("thumbnailCache/rootPath"), QString{}).toString();
  thumbnail_disk_cache_max_entries_ =
      settings.value(QStringLiteral("thumbnailCache/maxEntries"), 10000).toInt();
  thumbnail_disk_cache_jpeg_quality_ =
      settings.value(QStringLiteral("thumbnailCache/jpegQuality"), 85).toInt();
}


void LibraryModule::SaveThumbnailDiskCacheSettings() {
  QSettings settings;
  settings.setValue(QStringLiteral("thumbnailCache/enabled"), thumbnail_disk_cache_enabled_);
  settings.setValue(QStringLiteral("thumbnailCache/rootPath"), thumbnail_disk_cache_root_);
  settings.setValue(QStringLiteral("thumbnailCache/maxEntries"), thumbnail_disk_cache_max_entries_);
  settings.setValue(QStringLiteral("thumbnailCache/jpegQuality"),
                    thumbnail_disk_cache_jpeg_quality_);
}


void LibraryModule::ApplyThumbnailDiskCacheSettingsToService() {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (!thumb_svc) return;

  thumb_svc->SetDiskCacheEnabled(thumbnail_disk_cache_enabled_);
  if (!thumbnail_disk_cache_root_.isEmpty()) {
    thumb_svc->SetDiskCacheRoot(std::filesystem::path(thumbnail_disk_cache_root_.toStdWString()));
  }
  thumb_svc->SetDiskCacheMaxEntries(static_cast<size_t>(thumbnail_disk_cache_max_entries_));
  thumb_svc->SetDiskCacheJpegQuality(thumbnail_disk_cache_jpeg_quality_);
}

bool    LibraryModule::ThumbnailDiskCacheEnabled() const { return thumbnail_disk_cache_enabled_; }


QString LibraryModule::ThumbnailDiskCacheRoot() const {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    const auto root = thumb_svc->GetDiskCacheRoot();
    if (!root.empty()) {
      return QString::fromStdWString(root.wstring());
    }
  }
  return thumbnail_disk_cache_root_;
}


int LibraryModule::ThumbnailDiskCacheMaxEntries() const {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    return static_cast<int>(thumb_svc->GetDiskCacheMaxEntries());
  }
  return thumbnail_disk_cache_max_entries_;
}


int LibraryModule::ThumbnailDiskCacheJpegQuality() const {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    return thumb_svc->GetDiskCacheJpegQuality();
  }
  return thumbnail_disk_cache_jpeg_quality_;
}


QString LibraryModule::ThumbnailDiskCacheStats() const {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (!thumb_svc) {
    return QStringLiteral("No thumbnail service.");
  }
  const auto stats = thumb_svc->GetDiskCacheStats();
  return QStringLiteral(
             "Enabled: %1\n"
             "Entries: %2\n"
             "Size: %3\n"
             "Max entries: %4\n"
             "Hits: %5 / Misses: %6\n"
             "Root: %7")
      .arg(stats.enabled ? QStringLiteral("Yes") : QStringLiteral("No"))
      .arg(stats.total_entries)
      .arg(FormatCacheSize(stats.total_size_bytes))
      .arg(stats.max_entries)
      .arg(stats.hit_count)
      .arg(stats.miss_count)
      .arg(QString::fromStdString(stats.cache_root_path));
}


void LibraryModule::SetThumbnailDiskCacheEnabled(bool enabled) {
  thumbnail_disk_cache_enabled_ = enabled;
  SaveThumbnailDiskCacheSettings();
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    thumb_svc->SetDiskCacheEnabled(enabled);
  }
  emit ThumbnailDiskCacheStateChanged();
}


void LibraryModule::SetThumbnailDiskCacheRoot(const QString& rootPath) {
  const auto    root_path_opt   = InputToPath(rootPath);
  const QString normalized_root = root_path_opt.has_value()
                                      ? PathToQString(root_path_opt.value().lexically_normal())
                                      : rootPath;
  thumbnail_disk_cache_root_    = normalized_root;
  SaveThumbnailDiskCacheSettings();
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc && !thumbnail_disk_cache_root_.isEmpty()) {
    thumb_svc->SetDiskCacheRoot(std::filesystem::path(thumbnail_disk_cache_root_.toStdWString()));
  }
  emit ThumbnailDiskCacheStateChanged();
}


void LibraryModule::SetThumbnailDiskCacheMaxEntries(int maxEntries) {
  thumbnail_disk_cache_max_entries_ = std::max(1, maxEntries);
  SaveThumbnailDiskCacheSettings();
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    thumb_svc->SetDiskCacheMaxEntries(static_cast<size_t>(thumbnail_disk_cache_max_entries_));
  }
  emit ThumbnailDiskCacheStateChanged();
}


void LibraryModule::SetThumbnailDiskCacheJpegQuality(int quality) {
  thumbnail_disk_cache_jpeg_quality_ = std::clamp(quality, 1, 100);
  SaveThumbnailDiskCacheSettings();
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    thumb_svc->SetDiskCacheJpegQuality(thumbnail_disk_cache_jpeg_quality_);
  }
  emit ThumbnailDiskCacheStateChanged();
}


void LibraryModule::ClearAllThumbnailDiskCache() {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    thumb_svc->ClearAllDiskCache();
  }
  emit ThumbnailDiskCacheStateChanged();
  project_->SetServiceMessageForCurrentProject(PL_TEXT("All thumbnail disk cache cleared."));
}


void LibraryModule::ClearProjectThumbnailDiskCache() {
  auto thumb_svc = project_->handler().thumbnail_service();
  if (thumb_svc) {
    thumb_svc->ClearProjectDiskCache();
  }
  emit ThumbnailDiskCacheStateChanged();
  project_->SetServiceMessageForCurrentProject(PL_TEXT("Current project thumbnail disk cache cleared."));
}


int LibraryModule::PromptForInt(const QString& title, const QString& label, int defaultValue,
                               int minValue, int maxValue) {
  bool accepted = false;
  int  value =
      QInputDialog::getInt(nullptr, title, label, defaultValue, minValue, maxValue, 1, &accepted);
  return accepted ? value : defaultValue;
}

}  // namespace alcedo::ui

#undef PL_TEXT

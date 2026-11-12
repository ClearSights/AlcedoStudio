//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include "ui/alcedo_main/album_backend/album_thumbnail_model.hpp"

#include <QDate>
#include <QDateTime>
#include <QTimeZone>
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace alcedo::ui {

namespace {
QString formatAperture(double v) {
  if (v <= 0.0) return QStringLiteral("--");
  return QString::number(v, 'f', 1);
}

QString formatFocalLength(double v) {
  if (v <= 0.0) return QStringLiteral("--");
  return QString::number(v, 'f', 0);
}

/// Local time of the head commit of an edited photo; an empty QVariant when unedited.
QVariant EditedAtValue(const AlbumItem& item) {
  if (!item.edited_at_ns.has_value()) {
    return {};
  }
  constexpr std::int64_t kNanosecondsPerMillisecond = 1'000'000;
  return QDateTime::fromMSecsSinceEpoch(*item.edited_at_ns / kNanosecondsPerMillisecond,
                                        QTimeZone::utc())
      .toLocalTime();
}
}  // namespace

AlbumThumbnailModel::AlbumThumbnailModel(QObject* parent) : QAbstractListModel(parent) {}

int AlbumThumbnailModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(rows_.size());
}

QVariant AlbumThumbnailModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size())) {
    return {};
  }
  return roleValue(rows_[static_cast<size_t>(index.row())], role);
}

QVariant AlbumThumbnailModel::roleValue(const AlbumItem& item, int role) const {
  switch (role) {
    case ElementId:
      return static_cast<uint>(item.element_id);
    case FileId:
      return static_cast<uint>(item.file_id);
    case ImageId:
      return static_cast<uint>(item.image_id);
    case FolderId:
      return static_cast<uint>(item.folder_id);
    case ScopeType:
      return item.scope_type;
    case FileName:
      return item.file_name.isEmpty() ? QStringLiteral("(unnamed)") : item.file_name;
    case CameraModel:
      return item.camera_model.isEmpty() ? QStringLiteral("Unknown") : item.camera_model;
    case Extension:
      return item.extension.isEmpty() ? QStringLiteral("--") : item.extension;
    case Iso:
      return item.iso;
    case Aperture:
      return formatAperture(item.aperture);
    case FocalLength:
      return formatFocalLength(item.focal_length);
    case CaptureDate:
      return item.capture_date.isValid() ? item.capture_date.toString(QStringLiteral("yyyy-MM-dd"))
                                         : QStringLiteral("--");
    case ImportDate:
      return item.import_date.isValid() ? item.import_date.toString(QStringLiteral("yyyy-MM-dd"))
                                        : QStringLiteral("--");
    case Rating:
      return item.rating;
    case IsHdr:
      return item.is_hdr;
    case Tags:
      return item.tags;
    case Accent:
      return item.accent;
    case ThumbUrl:
      return item.thumb_url;
    case ThumbLoading:
      return item.thumb_loading;
    case ThumbMissingSource:
      return item.thumb_missing_source;
    case ThumbErrorText:
      return item.thumb_error_text;
    case IsEdited:
      return item.edited_at_ns.has_value();
    case EditedAt:
      return EditedAtValue(item);
    case EditVersionName:
      return item.edit_version_name;
    case AspectRatio:
      return item.aspect_ratio;
    case ColorLabels:
      return static_cast<int>(item.color_labels);
    case DeleteMark:
      return item.delete_mark;
    default:
      return {};
  }
}

QHash<int, QByteArray> AlbumThumbnailModel::roleNames() const { return RoleNameTable(); }

auto AlbumThumbnailModel::RoleNameTable() -> QHash<int, QByteArray> {
  return {
      {ElementId, "elementId"},
      {FileId, "fileId"},
      {ImageId, "imageId"},
      {FolderId, "folderId"},
      {ScopeType, "scopeType"},
      {FileName, "fileName"},
      {CameraModel, "cameraModel"},
      {Extension, "extension"},
      {Iso, "iso"},
      {Aperture, "aperture"},
      {FocalLength, "focalLength"},
      {CaptureDate, "captureDate"},
      {ImportDate, "importDate"},
      {Rating, "rating"},
      {IsHdr, "isHdr"},
      {Tags, "tags"},
      {Accent, "accent"},
      {ThumbUrl, "thumbUrl"},
      {ThumbLoading, "thumbLoading"},
      {ThumbMissingSource, "thumbMissingSource"},
      {ThumbErrorText, "thumbErrorText"},
      {IsEdited, "isEdited"},
      {EditedAt, "editedAt"},
      {EditVersionName, "editVersionName"},
      {AspectRatio, "aspectRatio"},
      {ColorLabels, "colorLabels"},
      {DeleteMark, "deleteMark"},
  };
}

void AlbumThumbnailModel::setHasMore(bool v) {
  if (has_more_ == v) return;
  has_more_ = v;
  emit hasMoreChanged();
}

void AlbumThumbnailModel::setLoading(bool v) {
  if (loading_ == v) return;
  loading_ = v;
  emit loadingChanged();
}

void AlbumThumbnailModel::resetModel(const std::vector<AlbumItem>& items, size_t totalCount) {
  const bool total_count_changed = total_count_ != totalCount;
  beginResetModel();
  rows_ = items;
  total_count_ = totalCount;
  rebuildElementIdIndex();
  setHasMore(rows_.size() < total_count_);
  endResetModel();
  emit countChanged();
  if (total_count_changed) {
    emit totalCountChanged();
  }
}

void AlbumThumbnailModel::beginReplace() {
  if (replacing_) {
    return;
  }
  replacing_ = true;
  beginResetModel();
}

void AlbumThumbnailModel::installItems(std::vector<AlbumItem> items, size_t totalCount,
                                       bool hasMore) {
  preserveThumbnailState(items);
  installRows(std::move(items), totalCount, hasMore);
}
void AlbumThumbnailModel::preserveThumbnailState(std::vector<AlbumItem>& items) {
  for (auto& item : items) {
    const auto row = rowByElementId(item.element_id);
    if (row >= 0 && rows_[static_cast<size_t>(row)].image_id == item.image_id) {
      auto& previous            = rows_[static_cast<size_t>(row)];
      item.thumb_url            = std::move(previous.thumb_url);
      item.thumb_loading        = previous.thumb_loading;
      item.thumb_missing_source = previous.thumb_missing_source;
      item.thumb_error_text     = std::move(previous.thumb_error_text);
    }
  }
}
void AlbumThumbnailModel::installRows(std::vector<AlbumItem> items, size_t totalCount,
                                      bool hasMore) {
  replace_total_changed_ = total_count_ != totalCount;
  replace_more_changed_  = has_more_ != hasMore;
  rows_                  = std::move(items);
  total_count_           = totalCount;
  has_more_              = hasMore;
  rebuildElementIdIndex();
}
void AlbumThumbnailModel::installPageItems(std::vector<AlbumItem>              items,
                                           const std::vector<sl_element_id_t>& order,
                                           size_t totalCount, bool hasMore) {
  std::unordered_map<sl_element_id_t, size_t> supplied;
  for (size_t i = 0; i < items.size(); ++i) supplied.emplace(items[i].element_id, i);
  std::unordered_set<sl_element_id_t> unique;
  for (auto id : order) {
    if (!unique.insert(id).second || (!supplied.contains(id) && rowByElementId(id) < 0)) {
      throw std::invalid_argument("Retained photo values are missing or repeated");
    }
  }
  preserveThumbnailState(items);
  std::vector<AlbumItem> rows;
  rows.reserve(order.size());
  for (auto id : order) {
    const auto found = supplied.find(id);
    if (found != supplied.end())
      rows.push_back(std::move(items[found->second]));
    else {
      const auto old = rowByElementId(id);
      if (old < 0) throw std::invalid_argument("Retained photo values are missing");
      rows.push_back(std::move(rows_[static_cast<size_t>(old)]));
    }
  }
  installRows(std::move(rows), totalCount, hasMore);
}
void AlbumThumbnailModel::endReplace() {
  replacing_ = false;
  endResetModel();
  emit countChanged();
  if (replace_total_changed_) emit totalCountChanged();
  if (replace_more_changed_) emit hasMoreChanged();
}
void AlbumThumbnailModel::endReplace(std::vector<AlbumItem> items, size_t totalCount,
                                     bool hasMore) {
  beginReplace();
  installItems(std::move(items), totalCount, hasMore);
  endReplace();
}
auto AlbumThumbnailModel::itemByFile(sl_element_id_t id) const -> const AlbumItem* {
  const auto row = rowByElementId(id);
  return row < 0 ? nullptr : &rows_[static_cast<size_t>(row)];
}

void AlbumThumbnailModel::appendPage(std::vector<AlbumItem> newItems,
                                     std::function<void()>  install_other_owners) {
  if (newItems.empty()) return;
  const int oldCount = static_cast<int>(rows_.size());
  beginInsertRows(QModelIndex(), oldCount, oldCount + static_cast<int>(newItems.size()) - 1);
  rows_.insert(rows_.end(), std::make_move_iterator(newItems.begin()),
               std::make_move_iterator(newItems.end()));
  for (const auto& item : newItems) {
    if (item.element_id > 0) {
      element_id_to_row_[item.element_id] = oldCount + static_cast<int>(&item - newItems.data());
    }
  }
  if (install_other_owners) install_other_owners();
  setHasMore(rows_.size() < total_count_);
  endInsertRows();
  emit countChanged();
}

void AlbumThumbnailModel::applyOrder(const std::vector<sl_element_id_t>& elementIds,
                                     const std::unordered_map<sl_element_id_t, AlbumItem>& newItems,
                                     size_t totalCount, bool hasMore,
                                     std::function<void()> install_other_owners) {
  // The loaded rows that stay in place: in the new order, each one after the previous kept row.
  std::vector<bool> keep(rows_.size(), false);
  int               last_kept = -1;
  for (const auto id : elementIds) {
    const int row = rowByElementId(static_cast<uint>(id));
    if (row > last_kept) {
      keep[static_cast<size_t>(row)] = true;
      last_kept                      = row;
    }
  }
  // A row that changed its order is inserted again at its new place with its own item.
  std::unordered_map<sl_element_id_t, AlbumItem> moved;
  for (int end = static_cast<int>(rows_.size()); end > 0;) {
    if (keep[static_cast<size_t>(end) - 1]) {
      --end;
      continue;
    }
    int begin = end - 1;
    while (begin > 0 && !keep[static_cast<size_t>(begin) - 1]) {
      --begin;
    }
    beginRemoveRows(QModelIndex(), begin, end - 1);
    for (int row = begin; row < end; ++row) {
      moved.emplace(rows_[static_cast<size_t>(row)].element_id,
                    std::move(rows_[static_cast<size_t>(row)]));
    }
    rows_.erase(rows_.begin() + begin, rows_.begin() + end);
    endRemoveRows();
    end = begin;
  }
  // One index rebuild per phase: during the runs rowByElementId reports a shifted row as not
  // loaded, never as another photo.
  rebuildElementIdIndex();

  for (size_t position = 0; position < elementIds.size();) {
    if (position < rows_.size() && rows_[position].element_id == elementIds[position]) {
      ++position;
      continue;
    }
    // The run of photos that are not loaded at this place.
    size_t                 end = position;
    std::vector<AlbumItem> run;
    while (end < elementIds.size() &&
           !(position < rows_.size() && rows_[position].element_id == elementIds[end])) {
      const auto id   = elementIds[end];
      const auto from = moved.find(id);
      if (from != moved.end()) {
        run.push_back(std::move(from->second));
        moved.erase(from);
      } else {
        run.push_back(newItems.at(id));
      }
      ++end;
    }
    beginInsertRows(QModelIndex(), static_cast<int>(position), static_cast<int>(end) - 1);
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(position),
                 std::make_move_iterator(run.begin()), std::make_move_iterator(run.end()));
    endInsertRows();
    position = end;
  }
  rebuildElementIdIndex();

  const bool total_count_changed = total_count_ != totalCount;
  total_count_                   = totalCount;
  const bool more_changed        = has_more_ != hasMore;
  has_more_                      = hasMore;
  if (install_other_owners) install_other_owners();
  emit countChanged();
  if (total_count_changed) {
    emit totalCountChanged();
  }
  if (more_changed) emit hasMoreChanged();
}

void AlbumThumbnailModel::updateThumbnailState(sl_element_id_t elementId, const QString& thumbUrl,
                                               bool loading, bool missingSource,
                                               const QString& errorText) {
  const int row = rowByElementId(elementId);
  if (row < 0 || row >= static_cast<int>(rows_.size())) {
    // A stale element_id_to_row_ entry (queued update arriving after a
    // resetModel/appendPage that restructured rows_) would otherwise index out
    // of bounds and corrupt the heap. updateRating/updateHdrFlag already guard
    // this; mirror them here — this is exactly the line the 0xc0000374 crash
    // surfaced on.
    return;
  }

  auto& item = rows_[static_cast<size_t>(row)];
  if (item.thumb_url == thumbUrl && item.thumb_loading == loading &&
      item.thumb_missing_source == missingSource && item.thumb_error_text == errorText) {
    return;
  }

  item.thumb_url            = thumbUrl;
  item.thumb_loading = loading;
  item.thumb_missing_source = missingSource;
  item.thumb_error_text = errorText;

  if (replacing_) return;
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx, {ThumbUrl, ThumbLoading, ThumbMissingSource, ThumbErrorText});
  emit thumbnailUpdated(static_cast<uint>(elementId), thumbUrl, loading, missingSource, errorText);
}

bool AlbumThumbnailModel::updateRating(sl_element_id_t elementId, image_id_t imageId, int rating) {
  bool updated = false;

  auto update_row = [&](int row) {
    if (row < 0 || row >= static_cast<int>(rows_.size())) {
      return;
    }
    auto& item = rows_[static_cast<size_t>(row)];
    if (imageId != 0 && item.image_id != imageId) {
      return;
    }
    if (item.rating == rating) {
      return;
    }

    item.rating = rating;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, {Rating});
    updated = true;
  };

  if (elementId != 0) {
    update_row(rowByElementId(static_cast<uint>(elementId)));
    return updated;
  }

  if (imageId == 0) {
    return false;
  }
  for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
    update_row(row);
  }
  return updated;
}

bool AlbumThumbnailModel::updateMarks(sl_element_id_t elementId, image_id_t imageId,
                                      ColorLabelMask colorLabels, bool deleteMark) {
  const int row = rowByElementId(elementId);
  if (row < 0 || row >= static_cast<int>(rows_.size())) return false;

  auto& item = rows_[static_cast<size_t>(row)];
  if (imageId != 0 && item.image_id != imageId) return false;
  if (item.color_labels == colorLabels && item.delete_mark == deleteMark) return false;

  item.color_labels     = colorLabels;
  item.delete_mark      = deleteMark;
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx, {ColorLabels, DeleteMark});
  return true;
}

bool AlbumThumbnailModel::updateEditState(sl_element_id_t elementId, image_id_t imageId,
                                          std::optional<std::int64_t> editedAtNs,
                                          const QString&              versionName) {
  const int row = rowByElementId(elementId);
  if (row < 0 || row >= static_cast<int>(rows_.size())) return false;

  auto& item = rows_[static_cast<size_t>(row)];
  if (imageId != 0 && item.image_id != imageId) return false;
  const QString name = editedAtNs.has_value() ? versionName : QString{};
  if (item.edited_at_ns == editedAtNs && item.edit_version_name == name) return false;

  item.edited_at_ns      = editedAtNs;
  item.edit_version_name = name;
  const QModelIndex idx  = index(row);
  emit dataChanged(idx, idx, {IsEdited, EditedAt, EditVersionName});
  return true;
}

bool AlbumThumbnailModel::updateHdrFlag(sl_element_id_t elementId, image_id_t imageId,
                                        bool isHdr) {
  const int row = rowByElementId(elementId);
  if (row < 0) return false;

  auto& item = rows_[static_cast<size_t>(row)];
  if (imageId != 0 && item.image_id != imageId) return false;
  if (item.is_hdr == isHdr) return false;

  item.is_hdr = isHdr;
  const QModelIndex idx = index(row);
  emit dataChanged(idx, idx, {IsHdr});
  return true;
}

QVariantMap AlbumThumbnailModel::getItemAt(int idx) const {
  if (idx < 0 || idx >= static_cast<int>(rows_.size())) return {};
  const auto& item = rows_[static_cast<size_t>(idx)];
  return {
      {"elementId", static_cast<uint>(item.element_id)},
      {"fileId", static_cast<uint>(item.file_id)},
      {"imageId", static_cast<uint>(item.image_id)},
      {"folderId", static_cast<uint>(item.folder_id)},
      {"scopeType", item.scope_type},
      {"fileName", item.file_name.isEmpty() ? QStringLiteral("(unnamed)") : item.file_name},
      {"rating", item.rating},
      {"colorLabels", static_cast<int>(item.color_labels)},
      {"deleteMark", item.delete_mark},
      {"isHdr", item.is_hdr},
      {"captureDate", item.capture_date.isValid()
                          ? item.capture_date.toString(QStringLiteral("yyyy-MM-dd"))
                          : QStringLiteral("--")},
      {"isEdited", item.edited_at_ns.has_value()},
      {"editedAt", EditedAtValue(item)},
      {"editVersionName", item.edit_version_name},
  };
}

QVariantList AlbumThumbnailModel::getItemsInRange(int firstIndex, int lastIndex) const {
  QVariantList result;
  if (rows_.empty()) return result;

  const int first = std::clamp(std::min(firstIndex, lastIndex), 0, static_cast<int>(rows_.size()) - 1);
  const int last  = std::clamp(std::max(firstIndex, lastIndex), 0, static_cast<int>(rows_.size()) - 1);
  result.reserve(last - first + 1);
  for (int idx = first; idx <= last; ++idx) {
    result.push_back(getItemAt(idx));
  }
  return result;
}

QVariantList AlbumThumbnailModel::getThumbnailStatesInRange(int firstIndex, int lastIndex) const {
  QVariantList result;
  if (rows_.empty()) return result;

  const int first = std::clamp(std::min(firstIndex, lastIndex), 0, static_cast<int>(rows_.size()) - 1);
  const int last  = std::clamp(std::max(firstIndex, lastIndex), 0, static_cast<int>(rows_.size()) - 1);
  result.reserve(last - first + 1);
  for (int idx = first; idx <= last; ++idx) {
    const auto& item = rows_[static_cast<size_t>(idx)];
    result.push_back(QVariantMap{
        {"elementId", static_cast<uint>(item.element_id)},
        {"imageId", static_cast<uint>(item.image_id)},
        {"thumbUrl", item.thumb_url},
        {"thumbLoading", item.thumb_loading},
    });
  }
  return result;
}

int AlbumThumbnailModel::rowByElementId(uint elementId) const {
  const auto it = element_id_to_row_.find(elementId);
  if (it == element_id_to_row_.end() || it->second < 0 ||
      it->second >= static_cast<int>(rows_.size()) ||
      rows_[static_cast<size_t>(it->second)].element_id != elementId) {
    return -1;
  }
  return it->second;
}

void AlbumThumbnailModel::rebuildElementIdIndex() {
  element_id_to_row_.clear();
  for (size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].element_id > 0) {
      element_id_to_row_[rows_[i].element_id] = static_cast<int>(i);
    }
  }
}

}  // namespace alcedo::ui

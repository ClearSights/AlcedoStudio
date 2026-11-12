// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include "ui/alcedo_main/album_backend/library_display_model.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace alcedo::ui {
namespace {
/// Scalar ID conversion for the existing photo-mode page operations.
auto PhotoItems(const std::vector<sl_element_id_t>& ids) -> std::vector<AlbumDisplayItem> {
  std::vector<AlbumDisplayItem> items;
  items.reserve(ids.size());
  for (auto id : ids) {
    AlbumDisplayItem item;
    item.file_id = item.identity.file_id = id;
    items.push_back(std::move(item));
  }
  return items;
}
/// Reject malformed page ranges before changing the accepted owner.
void ValidatePages(int64_t total, const LibraryDisplayModel::PageMap& pages) {
  if (total < 0) throw std::invalid_argument("Display count is negative");
  int64_t end = 0;
  for (const auto& [first, items] : pages) {
    if (first < end || first > total || items.empty() ||
        static_cast<int64_t>(items.size()) > total - first) {
      throw std::invalid_argument("Invalid display page range");
    }
    end = first + static_cast<int64_t>(items.size());
  }
}
/// Typed loading roles for an accepted occurrence whose bounded page has not arrived yet.
/// Zero IDs prevent thumbnail pins and photo actions; no substitute photo is displayed.
auto LoadingValue(int role) -> QVariant {
  switch (role) {
    case AlbumThumbnailModel::ElementId:
    case AlbumThumbnailModel::FileId:
    case AlbumThumbnailModel::ImageId:
    case AlbumThumbnailModel::FolderId:
    case AlbumThumbnailModel::Iso:
    case AlbumThumbnailModel::Rating:
    case AlbumThumbnailModel::ColorLabels:
    case AlbumThumbnailModel::AspectRatio:
    case LibraryDisplayModel::SavedCoverId:
    case LibraryDisplayModel::StackTotal:
    case LibraryDisplayModel::QueryMatchCount:
    case LibraryDisplayModel::FragmentMatchCount:
    case LibraryDisplayModel::DisplayKind:
      return 0;
    case AlbumThumbnailModel::ThumbLoading:
      return true;
    case AlbumThumbnailModel::IsHdr:
    case AlbumThumbnailModel::IsEdited:
    case AlbumThumbnailModel::ThumbMissingSource:
    case AlbumThumbnailModel::DeleteMark:
    case LibraryDisplayModel::TemporaryCover:
    case LibraryDisplayModel::StackExpanded:
      return false;
    case AlbumThumbnailModel::EditedAt:
      return {};
    default:
      return AlbumThumbnailModel::RoleNameTable().contains(role) ||
                     role == LibraryDisplayModel::StackId ||
                     role == LibraryDisplayModel::OccurrenceKey
                 ? QVariant{QString{}}
                 : QVariant{};
  }
}
}  // namespace
LibraryDisplayModel::LibraryDisplayModel(const AlbumThumbnailModel* photos, QObject* parent)
    : QAbstractListModel(parent), photos_(photos) {
  if (photos_)
    connect(photos_, &AlbumThumbnailModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
              if (replacing_) return;
              for (const auto& [start, items] : pages_)
                for (size_t i = 0; i < items.size(); ++i) {
                  const auto row = photos_->rowByElementId(items[i].file_id);
                  if (row >= first.row() && row <= last.row()) {
                    const auto position = static_cast<int>(start + static_cast<int64_t>(i));
                    emit       dataChanged(index(position), index(position), roles);
                  }
                }
            });
}
int LibraryDisplayModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid()
             ? 0
             : static_cast<int>(std::min<int64_t>(total_, std::numeric_limits<int>::max()));
}
QHash<int, QByteArray> LibraryDisplayModel::roleNames() const {
  auto roles = AlbumThumbnailModel::RoleNameTable();
  roles.insert(StackId, "stackId");
  roles.insert(SavedCoverId, "savedCoverId");
  roles.insert(StackTotal, "stackTotal");
  roles.insert(QueryMatchCount, "queryMatchCount");
  roles.insert(FragmentMatchCount, "fragmentMatchCount");
  roles.insert(TemporaryCover, "temporaryCover");
  roles.insert(StackExpanded, "stackExpanded");
  roles.insert(DisplayKind, "displayKind");
  roles.insert(OccurrenceKey, "occurrenceKey");
  return roles;
}
QVariant LibraryDisplayModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
  const auto* item = ItemAt(index.row());
  if (!item) return LoadingValue(role);
  switch (role) {
    case OccurrenceKey:
      return Key(item->identity);
    case StackId:
      return QString::fromStdString(item->identity.fragment.stack_id);
    case SavedCoverId:
      return item->saved_cover_file_id;
    case StackTotal:
      return static_cast<qint64>(item->stack_total);
    case QueryMatchCount:
      return static_cast<qint64>(item->query_match_count);
    case FragmentMatchCount:
      return static_cast<qint64>(item->fragment_match_count);
    case TemporaryCover:
      return item->temporary_cover;
    case StackExpanded:
      return item->expanded;
    case DisplayKind:
      return static_cast<int>(item->identity.kind);
    case AlbumThumbnailModel::FileId:
      return item->file_id;
    default:
      const int row = photos_ ? photos_->rowByElementId(item->file_id) : -1;
      return row < 0 ? QVariant{} : photos_->data(photos_->index(row), role);
  }
}
QString LibraryDisplayModel::Key(const AlbumDisplayIdentity& identity) {
  const auto& fragment = identity.fragment;
  QJsonArray  parts{static_cast<qint64>(fragment.folder_id), static_cast<int>(fragment.group_field),
                    static_cast<int>(fragment.group_key.index())};
  if (const auto* text = std::get_if<std::string>(&fragment.group_key))
    parts.append(QString::fromStdString(*text));
  else if (const auto* number = std::get_if<int64_t>(&fragment.group_key))
    parts.append(QString::number(*number));
  else
    parts.append(QJsonValue::Null);
  parts.append(QString::fromStdString(fragment.stack_id));
  parts.append(static_cast<int>(identity.kind));
  parts.append(static_cast<qint64>(identity.file_id));
  return QString::fromUtf8(QJsonDocument(parts).toJson(QJsonDocument::Compact));
}
QVariantMap LibraryDisplayModel::getItemAt(int position) const {
  if (!ItemAt(position)) return {};
  QVariantMap result;
  for (auto roles = roleNames(); auto role : roles.keys())
    result.insert(QString::fromUtf8(roles.value(role)), data(index(position), role));
  // Existing input owners use elementId for the actual displayed File.
  result.insert(QStringLiteral("elementId"), FileIdAt(position));
  return result;
}
QVariantMap LibraryDisplayModel::getStackAt(int position) const {
  if (!ItemAt(position)) return {};
  QVariantMap result{{QStringLiteral("fileId"), FileIdAt(position)}};
  const auto  names = roleNames();
  for (int role = StackId; role <= OccurrenceKey; ++role)
    result.insert(QString::fromUtf8(names.value(role)), data(index(position), role));
  return result;
}
QVariantList LibraryDisplayModel::getItemsInRange(int first, int last) const {
  QVariantList result;
  for (int position = std::max(0, std::min(first, last));
       position <= std::min(rowCount() - 1, std::max(first, last)); ++position) {
    auto item = getItemAt(position);
    if (!item.isEmpty()) result.append(std::move(item));
  }
  return result;
}
int LibraryDisplayModel::rowByElementId(uint file) const {
  for (const auto& [first, items] : pages_)
    for (size_t i = 0; i < items.size(); ++i)
      if (items[i].file_id == file) return static_cast<int>(first + i);
  return -1;
}
int LibraryDisplayModel::rowByKey(const QString& key) const {
  for (const auto& [first, items] : pages_)
    for (size_t i = 0; i < items.size(); ++i)
      if (Key(items[i].identity) == key) return static_cast<int>(first + i);
  return -1;
}
void LibraryDisplayModel::BeginReplace() {
  if (!replacing_) {
    replacing_ = true;
    beginResetModel();
  }
}
void LibraryDisplayModel::Install(int64_t total, PageMap pages) {
  ValidatePages(total, pages);
  total_ = total;
  pages_ = std::move(pages);
}
void LibraryDisplayModel::EndReplace() {
  if (replacing_) {
    replacing_ = false;
    endResetModel();
  }
  emit PagesChanged();
}
void LibraryDisplayModel::StorePage(int64_t first, std::vector<AlbumDisplayItem> items,
                                    size_t max_pages, bool notify) {
  if (items.empty()) return;
  if (first < 0 || first > total_ || static_cast<int64_t>(items.size()) > total_ - first ||
      items.size() > static_cast<size_t>(kMaxAlbumQueryPageRows)) {
    throw std::invalid_argument("Invalid display page range");
  }
  int64_t    begin     = first;
  int64_t    end       = first + static_cast<int64_t>(items.size());
  const auto store_end = end;
  auto       it        = pages_.upper_bound(first);
  if (it != pages_.begin() &&
      std::prev(it)->first + static_cast<int64_t>(std::prev(it)->second.size()) > first)
    --it;
  std::vector<std::pair<int64_t, std::vector<AlbumDisplayItem>>> joined;
  while (it != pages_.end() && it->first < store_end) {
    begin = std::min(begin, it->first);
    end   = std::max(end, it->first + static_cast<int64_t>(it->second.size()));
    joined.emplace_back(it->first, std::move(it->second));
    it = pages_.erase(it);
  }
  std::vector<AlbumDisplayItem> merged(static_cast<size_t>(end - begin));
  for (auto& [start, page] : joined)
    std::move(page.begin(), page.end(), merged.begin() + start - begin);
  std::move(items.begin(), items.end(), merged.begin() + first - begin);
  pages_[begin] = std::move(merged);
  while (pages_.size() > std::max<size_t>(1, max_pages)) {
    const auto front = pages_.begin(), back = std::prev(pages_.end());
    pages_.erase(back->first - first > first - front->first ? back : front);
  }
  if (notify && !replacing_) {
    if (rowCount()) emit dataChanged(index(0), index(rowCount() - 1));
    emit PagesChanged();
  }
}
void LibraryDisplayModel::StorePhotoPage(int64_t first, const std::vector<sl_element_id_t>& ids,
                                         size_t max_pages) {
  StorePage(first, PhotoItems(ids), max_pages);
}
void LibraryDisplayModel::InstallPhotoPages(int64_t total, PhotoPageMap pages, bool notify) {
  PageMap converted;
  for (auto& [start, ids] : pages)
    if (!ids.empty()) converted.emplace(start, PhotoItems(ids));
  if (notify) BeginReplace();
  Install(total, std::move(converted));
  if (notify) EndReplace();
}
auto LibraryDisplayModel::RetainedPages(std::vector<AlbumDisplayItem>& result,
                                        int64_t anchor_position, int64_t page_size,
                                        size_t max_pages) const -> PageMap {
  const auto less = [](const AlbumDisplayIdentity& a, const AlbumDisplayIdentity& b) {
    const auto parts = [](const AlbumDisplayIdentity& id) {
      return std::tie(id.fragment.folder_id, id.fragment.group_field, id.fragment.group_key,
                      id.fragment.stack_id, id.kind, id.file_id);
    };
    return parts(a) < parts(b);
  };
  std::map<AlbumDisplayIdentity, int64_t, decltype(less)> positions(less);
  // A create/remove operation can change a loaded File's occurrence kind or stack UUID.
  // This scoped index maps that same displayed File in its typed group to its old position.
  // Only scalar identity/position inputs leave this loop; no photo metadata is copied.
  using FileOccurrence =
      std::tuple<sl_element_id_t, AlbumGroupField, AlbumGroupKey, sl_element_id_t>;
  const auto file_key = [](const AlbumDisplayItem& item) -> FileOccurrence {
    const auto& fragment = item.identity.fragment;
    return {fragment.folder_id, fragment.group_field, fragment.group_key, item.file_id};
  };
  std::map<FileOccurrence, int64_t> file_positions;
  std::set<int64_t>                 photo_pages;
  for (const auto& [start, items] : pages_) {
    if (std::all_of(items.begin(), items.end(), [](const auto& item) {
          return item.identity.kind == AlbumDisplayKind::kPhoto;
        }))
      photo_pages.insert(start);
    for (size_t i = 0; i < items.size(); ++i) {
      positions.try_emplace(items[i].identity, start + static_cast<int64_t>(i));
      file_positions.try_emplace(file_key(items[i]), start + static_cast<int64_t>(i));
    }
  }
  std::vector<std::pair<int64_t, int64_t>> retained;
  for (size_t i = 0; i < result.size(); ++i) {
    const auto exact = positions.find(result[i].identity);
    const auto file  = file_positions.find(file_key(result[i]));
    if (exact != positions.end()) retained.emplace_back(exact->second, static_cast<int64_t>(i));
    if (file != file_positions.end()) retained.emplace_back(file->second, static_cast<int64_t>(i));
  }
  // Ordinary content shifts keep their existing page offsets and already-loaded metadata.
  // Stack pages, or an ordinary page spread by more than one page, use bounded aligned windows.
  std::map<int64_t, std::pair<int64_t, int64_t>> shifted_pages;
  for (const auto& [old_position, position] : retained) {
    const auto old_page = std::prev(pages_.upper_bound(old_position))->first;
    auto [at, inserted] = shifted_pages.try_emplace(old_page, position, position + 1);
    if (!inserted) {
      at->second.first  = std::min(at->second.first, position);
      at->second.second = std::max(at->second.second, position + 1);
    }
  }
  // Preserve the real viewport's nearest old occurrences. Separate bounded windows prevent a
  // large expansion or ungroup operation from loading every photo between two old cards.
  std::sort(retained.begin(), retained.end(), [anchor_position](const auto& a, const auto& b) {
    return std::tuple{std::abs(a.first - anchor_position), a.first, a.second} <
           std::tuple{std::abs(b.first - anchor_position), b.first, b.second};
  });
  page_size = std::clamp<int64_t>(page_size, 1, kMaxAlbumQueryPageRows);
  std::map<int64_t, int64_t> windows;
  for (const auto& [old_position, position] : retained) {
    const auto  old_page = std::prev(pages_.upper_bound(old_position))->first;
    const auto& shifted  = shifted_pages.at(old_page);
    const bool  keep_offset =
        photo_pages.contains(old_page) && shifted.second - shifted.first <= page_size;
    const auto begin = keep_offset ? shifted.first : (position / page_size) * page_size;
    const auto end =
        keep_offset ? shifted.second : std::min<int64_t>(result.size(), begin + page_size);
    auto [at, inserted] = windows.try_emplace(begin, end);
    if (!inserted) at->second = std::max(at->second, end);
    if (windows.size() >= std::max<size_t>(1, max_pages)) break;
  }
  const std::vector<std::pair<int64_t, int64_t>> ranges(windows.begin(), windows.end());
  PageMap pages;
  for (size_t i = 0; i < ranges.size();) {
    auto [begin, end] = ranges[i++];
    while (i < ranges.size() && ranges[i].first < end) end = std::max(end, ranges[i++].second);
    pages.emplace(begin,
                  std::vector<AlbumDisplayItem>(std::make_move_iterator(result.begin() + begin),
                                                std::make_move_iterator(result.begin() + end)));
  }
  return pages;
}

void LibraryDisplayModel::ResetSession(bool notify) {
  if (notify) BeginReplace();
  Install(0, {});
  expanded_.clear();
  if (notify) EndReplace();
}
auto LibraryDisplayModel::SetStackExpanded(const AlbumStackFragmentKey& key, bool expanded)
    -> bool {
  if (key.stack_id.empty()) throw std::invalid_argument("Expansion requires a stack fragment");
  auto it = std::find(expanded_.begin(), expanded_.end(), key);
  if (expanded == (it != expanded_.end())) return false;
  if (expanded)
    expanded_.push_back(key);
  else
    expanded_.erase(it);
  emit ExpansionChanged();
  return true;
}
auto LibraryDisplayModel::IsStackExpanded(const AlbumStackFragmentKey& key) const -> bool {
  return std::find(expanded_.begin(), expanded_.end(), key) != expanded_.end();
}
auto LibraryDisplayModel::ExpandedFragments(sl_element_id_t folder, AlbumGroupField group) const
    -> std::vector<AlbumStackFragmentKey> {
  std::vector<AlbumStackFragmentKey> keys;
  for (const auto& key : expanded_)
    if (key.folder_id == folder && key.group_field == group) keys.push_back(key);
  return keys;
}
auto LibraryDisplayModel::ItemAt(int64_t position) const -> const AlbumDisplayItem* {
  return ItemAt(pages_, position);
}
auto LibraryDisplayModel::ItemAt(const PageMap& pages, int64_t position)
    -> const AlbumDisplayItem* {
  auto it = pages.upper_bound(position);
  if (it == pages.begin()) return nullptr;
  --it;
  const auto offset = position - it->first;
  return offset < static_cast<int64_t>(it->second.size()) ? &it->second[static_cast<size_t>(offset)]
                                                          : nullptr;
}
auto LibraryDisplayModel::FileIdAt(int64_t position) const -> sl_element_id_t {
  const auto* item = ItemAt(position);
  return item ? item->file_id : 0;
}
auto LibraryDisplayModel::LoadedOccurrenceCount() const -> size_t {
  size_t count = 0;
  for (const auto& [start, items] : pages_) count += items.size();
  return count;
}
auto LibraryDisplayModel::LoadedPageStarts() const -> std::vector<int64_t> {
  std::vector<int64_t> starts;
  for (const auto& [start, items] : pages_) starts.push_back(start);
  return starts;
}
auto LibraryDisplayModel::LoadedUniqueFileIds() const -> std::vector<sl_element_id_t> {
  std::vector<sl_element_id_t>        ids;
  std::unordered_set<sl_element_id_t> seen;
  for (const auto& [start, items] : pages_)
    for (const auto& item : items)
      if (seen.insert(item.file_id).second) ids.push_back(item.file_id);
  return ids;
}
auto LibraryDisplayModel::FirstUnloadedOccurrence(int64_t begin, int64_t end) const -> int64_t {
  begin = std::max<int64_t>(0, begin);
  end   = std::min(end, total_);
  while (begin < end) {
    auto it = pages_.upper_bound(begin);
    if (it != pages_.begin()) {
      --it;
      const auto next = it->first + static_cast<int64_t>(it->second.size());
      if (begin < next) {
        begin = next;
        continue;
      }
    }
    return begin;
  }
  return -1;
}
auto LibraryDisplayModel::NextLoadedPageStart(int64_t position) const -> int64_t {
  const auto it = pages_.upper_bound(position);
  return it == pages_.end() ? total_ : it->first;
}
auto LibraryDisplayModel::LoadedEnd() const -> int64_t {
  return pages_.empty()
             ? 0
             : pages_.rbegin()->first + static_cast<int64_t>(pages_.rbegin()->second.size());
}
}  // namespace alcedo::ui

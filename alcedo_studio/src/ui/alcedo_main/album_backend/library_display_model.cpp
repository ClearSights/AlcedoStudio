// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include "ui/alcedo_main/album_backend/library_display_model.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
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
  return roles;
}
QVariant LibraryDisplayModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
  const auto* item = ItemAt(index.row());
  if (!item) return role == AlbumThumbnailModel::FileId ? QVariant(0U) : QVariant{};
  switch (role) {
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
auto LibraryDisplayModel::RetainedPages(std::vector<AlbumDisplayItem>& result) const -> PageMap {
  const auto less = [](const AlbumDisplayIdentity& a, const AlbumDisplayIdentity& b) {
    const auto parts = [](const AlbumDisplayIdentity& id) {
      return std::tie(id.fragment.folder_id, id.fragment.group_field, id.fragment.group_key,
                      id.fragment.stack_id, id.kind, id.file_id);
    };
    return parts(a) < parts(b);
  };
  std::map<AlbumDisplayIdentity, int64_t, decltype(less)> positions(less);
  for (const auto& [start, items] : pages_)
    for (const auto& item : items) positions.try_emplace(item.identity, -1);
  for (size_t i = 0; i < result.size(); ++i) {
    auto found = positions.find(result[i].identity);
    if (found != positions.end()) found->second = static_cast<int64_t>(i);
  }
  std::vector<std::pair<int64_t, int64_t>> ranges;
  for (const auto& [start, items] : pages_) {
    int64_t begin = -1, end = -1;
    for (const auto& item : items) {
      const auto at = positions.find(item.identity)->second;
      if (at >= 0) {
        begin = begin < 0 ? at : std::min(begin, at);
        end   = std::max(end, at + 1);
      }
    }
    if (begin >= 0) ranges.emplace_back(begin, end);
  }
  std::sort(ranges.begin(), ranges.end());
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

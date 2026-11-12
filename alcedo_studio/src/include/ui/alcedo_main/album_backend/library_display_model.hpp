// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#pragma once

#include <QAbstractListModel>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "storage/store/sleeve/element_store.hpp"
#include "ui/alcedo_main/album_backend/album_thumbnail_model.hpp"

namespace alcedo::ui {

/// Sole GUI-thread owner of ordered display pages and session fragment expansion. Photo
/// values stay in AlbumThumbnailModel, which must outlive this model when supplied. Pages
/// retain only IDs and stack scalars. No operation here performs SQL or changes saved stacks.
class LibraryDisplayModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(qint64 displayCount READ DisplayCount NOTIFY PagesChanged)
 public:
  using PageMap      = std::map<int64_t, std::vector<AlbumDisplayItem>>;
  using PhotoPageMap = std::map<int64_t, std::vector<sl_element_id_t>>;
  enum Roles {
    StackId = Qt::UserRole + 100,
    SavedCoverId,
    StackTotal,
    QueryMatchCount,
    FragmentMatchCount,
    TemporaryCover,
    StackExpanded,
    DisplayKind
  };
  explicit LibraryDisplayModel(const AlbumThumbnailModel* photos = nullptr,
                               QObject*                   parent = nullptr);
  int                    rowCount(const QModelIndex& parent = {}) const override;
  QVariant               data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;
  /// Begin while all accepted owners still expose the previous result.
  void                   BeginReplace();
  /// Move one validated result into the owner without notification. Page ranges must be
  /// nonnegative, disjoint, and within total. Joined retained ranges can exceed a query page.
  /// Throws before mutation on invalid input. The caller completes the other owners before
  /// EndReplace.
  void                   Install(int64_t total, PageMap pages);
  /// Publish only after all owners have installed the accepted result.
  void                   EndReplace();
  /// Join overlapping pages and retain the nearest max_pages. Existing entries move into
  /// algorithm output; no page copy is retained. notify=false supports complete publication.
  void StorePage(int64_t first, std::vector<AlbumDisplayItem> items, size_t max_pages,
                 bool notify = true);
  /// Existing photo-mode operation inputs; convert scalar IDs into ordinary display items.
  void StorePhotoPage(int64_t first, const std::vector<sl_element_id_t>& ids, size_t max_pages);
  void InstallPhotoPages(int64_t total, PhotoPageMap pages, bool notify = true);
  /// Relocate loaded ranges by stable identity, consuming only the selected new result items.
  /// The input is independent SQL algorithm output, not a live owner copy. Unselected entries
  /// are released with the result. The accepted pages remain unchanged until Install.
  [[nodiscard]] auto RetainedPages(std::vector<AlbumDisplayItem>& result) const -> PageMap;
  /// Release accepted pages and expansion choices when the project session ends.
  void               ResetSession(bool notify = true);
  /// Change only a session choice. Absent keys stay recorded across filters. Returns whether
  /// it changed; emits ExpansionChanged without changing accepted pages or their query input.
  auto SetStackExpanded(const AlbumStackFragmentKey& key, bool expanded) -> bool;
  [[nodiscard]] auto IsStackExpanded(const AlbumStackFragmentKey& key) const -> bool;
  /// Minimal request values for this exact scope/group, including currently absent fragments.
  [[nodiscard]] auto ExpandedFragments(sl_element_id_t folder, AlbumGroupField group) const
      -> std::vector<AlbumStackFragmentKey>;
  /// Scoped const reads, valid until the next GUI-thread mutation. Do not retain references.
  [[nodiscard]] auto pages() const -> const PageMap& { return pages_; }
  [[nodiscard]] auto ItemAt(int64_t position) const -> const AlbumDisplayItem*;
  /// The same scoped read for a pending page-relocation algorithm output.
  [[nodiscard]] static auto ItemAt(const PageMap& pages, int64_t position)
      -> const AlbumDisplayItem*;
  [[nodiscard]] auto FileIdAt(int64_t position) const -> sl_element_id_t;
  [[nodiscard]] auto DisplayCount() const -> int64_t { return total_; }
  [[nodiscard]] auto LoadedPageCount() const -> size_t { return pages_.size(); }
  [[nodiscard]] auto LoadedOccurrenceCount() const -> size_t;
  [[nodiscard]] auto LoadedPageStarts() const -> std::vector<int64_t>;
  [[nodiscard]] auto LoadedUniqueFileIds() const -> std::vector<sl_element_id_t>;
  [[nodiscard]] auto FirstUnloadedOccurrence(int64_t begin, int64_t end) const -> int64_t;
  [[nodiscard]] auto NextLoadedPageStart(int64_t position) const -> int64_t;
  [[nodiscard]] auto LoadedEnd() const -> int64_t;
 signals:
  void PagesChanged();
  void ExpansionChanged();

 private:
  const AlbumThumbnailModel*         photos_ = nullptr;
  PageMap                            pages_;
  std::vector<AlbumStackFragmentKey> expanded_;
  int64_t                            total_     = 0;
  bool                               replacing_ = false;
};
}  // namespace alcedo::ui

//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/album_browse_service.hpp"
#include "app/image_pool_service.hpp"
#include "app/sleeve_filter_service.hpp"
#include "sleeve/album_query.hpp"
#include "sleeve/storage.hpp"
#include "storage/store/semantic/semantic_label_config.hpp"
#include "storage/store/semantic/semantic_records.hpp"
#include "storage/store/sleeve/element_store.hpp"
#include "ui/alcedo_main/album_backend/album_catalog.hpp"
#include "ui/alcedo_main/album_backend/album_section_model.hpp"
#include "ui/alcedo_main/album_backend/album_thumbnail_model.hpp"
#include "ui/alcedo_main/album_backend/album_types.hpp"
#include "ui/alcedo_main/album_backend/folder_controller.hpp"
#include "ui/alcedo_main/album_backend/library_display_model.hpp"
#include "ui/alcedo_main/album_backend/library_selection.hpp"
#include "ui/alcedo_main/album_backend/library_selection_marks.hpp"
#include "ui/alcedo_main/album_backend/library_selection_operations.hpp"
#include "ui/alcedo_main/album_backend/project_module.hpp"
#include "ui/alcedo_main/album_backend/search_controller.hpp"
#include "ui/alcedo_main/album_backend/search_request_worker.hpp"
#include "ui/alcedo_main/album_backend/stats_engine.hpp"
#include "ui/alcedo_main/album_backend/thumbnail_manager.hpp"
#include "ui/alcedo_main/i18n.hpp"

namespace alcedo::ui {

/**
 * @brief Immutable input of one library read on the query worker.
 *
 * @details Captured on the UI thread when the read is submitted: the scope, the Inspector
 * filter tree, the search part, the presentation options, the active model key, the bounds,
 * and the services that keep the project alive. It is the message that crosses the thread
 * boundary, not a copy of the library state: it holds no photo, no QObject, and no reference
 * into a UI owner. LibraryModule keeps the accepted refresh input without its services, so
 * later pages, positions, and ids read the same query interpretation without keeping a closed
 * project open.
 */
struct LibraryQueryInput {
  sl_element_id_t                      folder_id_ = 0;
  std::optional<FilterNode>            stats_filter_{};
  LibrarySearchInput                   search_{};
  AlbumQueryOptions                    options_{};
  std::string                          active_model_key_{};
  /// Includes minimal expansion keys when display mode is requested. They travel with the
  /// immutable query message, and never retain an owner reference or member metadata.
  AlbumQueryRead                       read_{};
  int64_t                              page_size_ = 0;
  std::shared_ptr<AlbumBrowseService>  browse_{};
  std::shared_ptr<SleeveFilterService> filter_service_{};
  /// Read on the query worker for the display values of each row (see LibraryQueryOutput).
  std::shared_ptr<ImagePoolService>    image_pool_{};
  std::shared_ptr<Storage>             storage_{};
  SemanticLabelLanguage                label_language_ = SemanticLabelLanguage::kEnglish;
};

/// Result of one library read, moved once into the queued UI completion.
struct LibraryQueryOutput {
  AlbumQueryResult          result_{};
  /// Search filter the read used (built on the worker for a pending query text).
  std::optional<FilterNode> search_filter_{};
  /// Embedding of the natural-language search the read used: made on the worker for a pending
  /// natural-language text, else the accepted embedding of the input. Empty for other searches.
  std::optional<SemanticQueryEmbedding> semantic_query_{};
  QString                   error_{};
  /// Display values of result_.display_rows (same order) that SQL does not return: extension, ISO,
  /// aperture, focal length, HDR flag (from the image pool) and the semantic label text. The
  /// query worker reads them, so publishing a page runs no per-row database read on the UI
  /// thread.
  std::vector<AlbumItem>    row_display_{};
};

/// IANA id of the system time zone. Import-day filters, groups, and statistics and edit-day
/// groups use it.
[[nodiscard]] auto  CurrentLocalDayTimeZone() -> std::string;

/// Inspector field name ("date", "import", "camera", "lens", "rating", "label", "edited") of a
/// sort or group field; empty for kNone.
[[nodiscard]] auto  InspectorFieldName(AlbumSortField field) -> QString;
[[nodiscard]] auto  InspectorFieldName(AlbumGroupField field) -> QString;

/// Library/catalog module: thumbnail grid model, disk cache, windowed loads,
/// and IAlbumCatalog for sibling modules.
///
/// It owns the accepted library query: the presentation options (one optional group field and
/// one optional photo sort field), the accepted result's display, photo, and section owners, and
/// the query worker that runs every library read (refresh, page, position, and id reads) and the
/// search dialog previews. StatsEngine owns the Inspector filter values and SearchController owns
/// the search term; both request a refresh here instead of reading pages themselves.
class LibraryModule final : public QObject, public IAlbumCatalog {
  Q_OBJECT
  Q_PROPERTY(QVariantList thumbnails READ Thumbnails NOTIFY ThumbnailsChanged)
  Q_PROPERTY(QObject* thumbnailModel READ ThumbnailModel CONSTANT)
  Q_PROPERTY(LibraryDisplayModel* displayModel READ display_model CONSTANT)
  Q_PROPERTY(int shownCount READ ShownCount NOTIFY CountsChanged)
  Q_PROPERTY(int totalCount READ TotalCount NOTIFY CountsChanged)
  Q_PROPERTY(bool hasMoreThumbnails READ HasMoreThumbnails NOTIFY CountsChanged)
  Q_PROPERTY(bool thumbnailDiskCacheEnabled READ ThumbnailDiskCacheEnabled NOTIFY
                 ThumbnailDiskCacheStateChanged)
  Q_PROPERTY(QString thumbnailDiskCacheRoot READ ThumbnailDiskCacheRoot NOTIFY
                 ThumbnailDiskCacheStateChanged)
  Q_PROPERTY(int thumbnailDiskCacheMaxEntries READ ThumbnailDiskCacheMaxEntries NOTIFY
                 ThumbnailDiskCacheStateChanged)
  Q_PROPERTY(int thumbnailDiskCacheJpegQuality READ ThumbnailDiskCacheJpegQuality NOTIFY
                 ThumbnailDiskCacheStateChanged)
  Q_PROPERTY(QString thumbnailDiskCacheStats READ ThumbnailDiskCacheStats NOTIFY
                 ThumbnailDiskCacheStateChanged)
  Q_PROPERTY(QObject* sectionModel READ SectionModel CONSTANT)
  Q_PROPERTY(QString sortField READ SortFieldName NOTIFY PresentationChanged)
  Q_PROPERTY(bool sortDescending READ SortDescending NOTIFY PresentationChanged)
  Q_PROPERTY(QString groupField READ GroupFieldName NOTIFY PresentationChanged)
  Q_PROPERTY(bool grouped READ Grouped NOTIFY PresentationChanged)
  Q_PROPERTY(bool queryUpdating READ QueryUpdating NOTIFY QueryStateChanged)
  Q_PROPERTY(bool hasActiveFilter READ HasActiveFilter NOTIFY QueryStateChanged)
  Q_PROPERTY(QString queryError READ QueryError NOTIFY QueryStateChanged)
  Q_PROPERTY(uint pendingRevealFileId READ PendingRevealFileId NOTIFY PendingRevealChanged)
  /// The library image selection.
  Q_PROPERTY(LibrarySelection* selection READ selection CONSTANT)
  /// The rating and the marks of the selected photos (marks control, #772).
  Q_PROPERTY(LibrarySelectionMarks* selectionMarks READ selection_marks CONSTANT)

 public:
  explicit LibraryModule(ProjectModule* project, QObject* parent = nullptr);
  /// Joins the query worker first: a running job posts its result to this object.
  ~LibraryModule() override;

  void BindCollaborators(FolderController* folders, SearchController* search, StatsEngine* stats);
  void SetSemanticLabelProvider(std::function<QString(sl_element_id_t)> provider);

  [[nodiscard]] auto thumbs() -> ThumbnailManager& { return thumbs_; }
  [[nodiscard]] auto thumbs() const -> const ThumbnailManager& { return thumbs_; }
  [[nodiscard]] auto selection() -> LibrarySelection* { return &selection_; }
  [[nodiscard]] auto selection() const -> const LibrarySelection* { return &selection_; }
  [[nodiscard]] auto selection_marks() -> LibrarySelectionMarks* { return &selection_marks_; }
  [[nodiscard]] auto model() -> AlbumThumbnailModel& { return thumbnail_model_; }
  [[nodiscard]] auto model() const -> const AlbumThumbnailModel& { return thumbnail_model_; }
  [[nodiscard]] auto project() -> ProjectModule* { return project_; }
  [[nodiscard]] auto project() const -> const ProjectModule* { return project_; }
  /// The one worker of library reads and search previews.
  [[nodiscard]] auto query_worker() -> SearchRequestWorker& { return *query_worker_; }
  [[nodiscard]] auto section_model() -> AlbumSectionModel& { return section_model_; }
  [[nodiscard]] auto section_model() const -> const AlbumSectionModel& { return section_model_; }
  [[nodiscard]] auto display_model() -> LibraryDisplayModel* { return &display_model_; }
  [[nodiscard]] auto display_model() const -> const LibraryDisplayModel* { return &display_model_; }
  /// C++ composition setting only. Production keeps photo mode until every layout supports
  /// stacks. Changing it requests a replacement through the existing query worker.
  void               SetStackDisplayEnabled(bool enabled);
  /// Use raw photo reads while the editor filmstrip owns the thumbnail surface. GUI thread.
  void               SetEditorPhotoView(bool active);
  /// Project-close owner operation: invalidate reads and release photos, pages, and choices.
  void               ClearProjectPresentation();
  [[nodiscard]] auto accepted_options() const -> const AlbumQueryOptions& {
    return accepted_options_;
  }

  // ── Q_PROPERTY getters ─────────────────────────────────────────────────
  QVariantList Thumbnails() const;
  QObject*     ThumbnailModel() { return &thumbnail_model_; }
  int          ShownCount() const { return thumbnail_model_.count(); }
  int          TotalCount() const;
  bool         HasMoreThumbnails() const { return thumbnail_model_.hasMore(); }
  bool         ThumbnailDiskCacheEnabled() const;
  QString      ThumbnailDiskCacheRoot() const;
  int          ThumbnailDiskCacheMaxEntries() const;
  int          ThumbnailDiskCacheJpegQuality() const;
  QString      ThumbnailDiskCacheStats() const;
  QObject*     SectionModel() { return &section_model_; }
  QString      SortFieldName() const { return InspectorFieldName(accepted_options_.sort_field_); }
  bool         SortDescending() const {
    return accepted_options_.sort_field_ != AlbumSortField::kNone &&
           accepted_options_.sort_direction_ == SortDirection::kDescending;
  }
  QString GroupFieldName() const { return InspectorFieldName(accepted_options_.group_field_); }
  bool    Grouped() const { return accepted_options_.group_field_ != AlbumGroupField::kNone; }
  bool    QueryUpdating() const { return refresh_in_flight_; }
  /// Accepted search/Inspector predicates only; grouping and sort do not mark a query filtered.
  bool    HasActiveFilter() const {
    return accepted_input_ && (accepted_input_->stats_filter_.has_value() ||
                               accepted_input_->search_.filter_.has_value() ||
                               accepted_input_->search_.semantic_query_.has_value());
  }
  QString QueryError() const { return query_error_; }
  uint             PendingRevealFileId() const { return pending_reveal_file_id_; }

  // ── Q_INVOKABLE ────────────────────────────────────────────────────────
  Q_INVOKABLE void SetThumbnailVisible(uint elementId, uint imageId, bool visible,
                                       uint maxEdge = 1024);
  Q_INVOKABLE void SetThumbnailCacheHint(uint visibleCells, uint maxEdge = 1024);
  Q_INVOKABLE bool LoadMoreThumbnails();
  Q_INVOKABLE bool LoadThumbnailsThroughIndex(int index);
  /**
   * @brief Select All: selects every photo of the accepted library query.
   *
   * @details The query worker reads all file ids of the query, so the result does not depend on
   * the loaded pages or the scroll position; the selection changes when the read completes.
   * While a refresh runs, the read waits for the refreshed result. Clears the selection when the
   * view is empty. Every Ctrl+A path (library, section view, editor filmstrip) calls this.
   */
  Q_INVOKABLE void SelectAllInCurrentView();
  Q_INVOKABLE void SetThumbnailDiskCacheEnabled(bool enabled);
  Q_INVOKABLE void SetThumbnailDiskCacheRoot(const QString& rootPath);
  Q_INVOKABLE void SetThumbnailDiskCacheMaxEntries(int maxEntries);
  Q_INVOKABLE void SetThumbnailDiskCacheJpegQuality(int quality);
  Q_INVOKABLE void ClearAllThumbnailDiskCache();
  Q_INVOKABLE void ClearProjectThumbnailDiskCache();
  Q_INVOKABLE int  PromptForInt(const QString& title, const QString& label, int defaultValue,
                                int minValue, int maxValue);

  // ── Library query: presentation options, sections, ordered reads ───────
  /**
   * @brief Inspector sort action of @p field. Selects that field and direction as the one
   *        photo sort, or clears the explicit sort when that exact action is already selected.
   *        The group field and every filter stay. Unknown field names report a query error.
   */
  Q_INVOKABLE void       ToggleInspectorSort(const QString& field, bool descending);
  /**
   * @brief Inspector Group checkbox of @p field. Checking selects @p field as the one group
   *        field (the previous one is unchecked); unchecking the current field returns to the
   *        flat mode. The photo sort and every filter stay.
   */
  Q_INVOKABLE void       SetInspectorGrouping(const QString& field, bool enabled);
  /// Submit the failed refresh again with the same pending changes.
  Q_INVOKABLE void       RetryLibraryQuery();
  /// Load the occurrence pages that the section rows [@p firstRow, @p lastRow] show, with a
  /// margin; pages far from them are released (bounded retention).
  Q_INVOKABLE void       RequestSectionRows(int firstRow, int lastRow);
  /// Request a flat viewport's half-open display range, including holes in retained pages.
  /// GUI thread; query worker reads bounded pages and retains at most three nearby windows.
  Q_INVOKABLE void       RequestDisplayRange(qint64 begin, qint64 end);
  /// Read the ordered unique files of the occurrence ranges (`[{begin, end}]`) on the worker.
  /// They arrive with OrderedFileIdsReady as selection items `{elementId, fileId, imageId,
  /// fileName}`. Reads ids and typed columns only, no thumbnail metadata.
  Q_INVOKABLE qulonglong RequestOrderedFileIds(const QVariantList& ranges);
  /// Toggle exactly the loaded occurrence's fragment; refuse absent keys without changing
  /// selection.
  Q_INVOKABLE void       ToggleStack(const QString& occurrence_key);
  /// Read all matching fragment keys on the worker and change only their session choices.
  Q_INVOKABLE void       SetAllStacksExpanded(bool expanded);
  /// Expand all accepted matching fragments if any is collapsed, otherwise collapse them all.
  /// Read unloaded keys on the existing query worker; selection and saved rows are unchanged.
  Q_INVOKABLE void       ToggleAllStacksExpanded();
  /// Expand matching fragments of the split result, including unloaded groups, then refresh.
  /// GUI-thread entry; the query worker reads keys only. Selection and stored rows are unchanged.
  void                   ExpandSplitStacks(std::vector<std::string> stack_ids);
  /// Select exact fragment or entire saved stack from a loaded occurrence; no implicit selection.
  Q_INVOKABLE void       SelectStack(const QString& occurrence_key, bool entire);
  /// Capture minimal query input and explicit IDs for command ordering on a worker. No GUI SQL.
  auto                   StackCommandOrder(std::vector<sl_element_id_t> ids) const
      -> std::function<std::vector<sl_element_id_t>()>;
  /// Replace selection with this fragment's matching Files on the worker. GUI entry; failure
  /// keeps selection and reports the diagnostic through the query owner.
  void                   SelectMatchingStackPhotos(const AlbumStackFragmentKey& fragment);
  /// Explicitly select all saved members, including Files outside the result. GUI entry.
  void                   SelectEntireStack(const std::string& stack_id);
  /// Find @p fileId on the worker; expand its hidden fragment before loading its display page.
  /// FocusPositionReady reports -1 for a nonmatch, preserving the current filter and scope.
  Q_INVOKABLE void       RequestFocusPosition(uint fileId, const QString& preferredGroupTitle = {},
                                              bool preferredGroupUnknown = false);
  /**
   * @brief Editor list check: find @p fileId in the accepted result on the query worker.
   *
   * @details The answer arrives with editorListPositionReady(requestId, fileId, occurrence):
   * the occurrence index of the file, -1 when the accepted result does not hold it, or -2 when
   * the read failed. With @p loadPage the page that holds the file loads when it is found. A
   * newer request replaces an older one, and a library refresh drops a running request, so a
   * caller sends a new request after each accepted result. While a refresh runs, the read waits
   * for the refreshed result. Returns the request id (> 0), or 0 for @p fileId 0.
   */
  Q_INVOKABLE qulonglong RequestEditorListPosition(uint fileId, bool loadPage = false);
  /// Grouped-view visibility of one occurrence (see ThumbnailManager).
  Q_INVOKABLE void       SetOccurrenceThumbnailVisible(const QString& groupTitle, bool groupUnknown,
                                                       uint elementId, uint imageId, bool visible,
                                                       uint maxEdge = 1024);

  // ── Edit history (library_module_edit_state.cpp) ───────────────────────
  /**
   * @brief Read the project's last edited photo on the query worker and make it the pending
   *        reveal. The library view that shows the result calls RequestPendingReveal. Also
   *        emits LastEditedFileFound. Clears the previous pending reveal first. UI thread only.
   */
  void                   RevealLastEditedFile();
  /**
   * @brief Find the pending reveal in the accepted result. Expand its matching fragment when
   *        collapsed. A nonmatching File reports an error and leaves scope and filters intact.
   */
  Q_INVOKABLE void       RequestPendingReveal();
  /**
   * @brief An edit of one or more photos was written to storage. When the requested
   *        presentation reads the edit state (edit-time sort or edit-day groups), refresh it,
   *        or, while @p library_hidden, refresh it on the next RefreshStaleEditOrder.
   */
  void                   NoteEditHistoryChanged(bool library_hidden);
  /// Refresh once when an edit was written while the library was hidden (see above).
  void                   RefreshStaleEditOrder();

  /**
   * @brief The rating of loaded photos changed in storage, and their rows already show it
   *        (AlbumThumbnailModel::updateRating). When the shown order or the shown photos read
   *        the rating (rating sort, rating group, or a rating condition in the Inspector filter
   *        or the search), refresh the library. Otherwise read only the statistics of the
   *        accepted query, so the photo rows, their delegates, and the scroll position stay.
   *        UI thread only.
   */
  void                   NoteRatingsChanged();

  /**
   * @brief The color labels or the Delete mark of loaded photos changed in storage, and their
   *        rows already show it (AlbumThumbnailModel::updateMarks). When the Inspector filter or
   *        the search reads a mark, refresh the library; otherwise read only the statistics of
   *        the accepted query, as NoteRatingsChanged does. UI thread only.
   */
  void                   NoteMarksChanged();

  /// Read the rating and the marks of the selected photos into selectionMarks once in this
  /// event loop turn. Runs after each selection change and from NoteRatingsChanged and
  /// NoteMarksChanged. UI thread only.
  void                   ScheduleSelectionMarksRead();

  /// Request one refresh of the accepted library query. Requests of one event loop turn are
  /// combined: the refresh reads the filters, the search, and the presentation options when it
  /// is submitted to the worker. The new result replaces the shown one (a model reset). UI
  /// thread only.
  void                   RequestLibraryRefresh();
  /**
   * @brief The photos of the shown scope changed (an import, a delete, or photos added to the
   *        shown album) and the query did not: refresh it and change the shown result in place.
   *
   * @details No model reset. The section rows and the photo rows that stay keep their items;
   * rows of removed photos are removed, and photos that are new in the loaded range are read and
   * inserted at their sort positions. The retained section pages move to their new occurrences
   * without a page read. LibraryChangeAboutToApply and LibraryChangeApplied enclose the change,
   * so a view keeps its scroll anchor and animates the change.
   *
   * The change is applied in place only when every request of the same event loop turn was a
   * content refresh, and the scope, the options, and the semantic model key equal the accepted
   * ones; otherwise the refresh replaces the result. A caller that changes the Inspector filter
   * or the search calls RequestLibraryRefresh. UI thread only.
   */
  void                   RequestLibraryContentRefresh();

  // ── IAlbumCatalog ──────────────────────────────────────────────────────
  auto FindAlbumItem(sl_element_id_t elementId) const -> const AlbumItem* override;
  void SetAlbumItemHdrFlag(sl_element_id_t elementId, image_id_t imageId, bool isHdr) override;
  void SetAlbumItemEditState(sl_element_id_t elementId, image_id_t imageId,
                             std::optional<std::int64_t> editedAtNs,
                             const QString&              versionName) override;
  void PersistImageHdrFlag(sl_element_id_t elementId, image_id_t imageId, bool isHdr) override;
  auto view_state() -> AlbumViewState& override { return view_state_; }
  auto view_state() const -> const AlbumViewState& override { return view_state_; }
  void ReloadFolderTree(const std::filesystem::path& preferredFolderPath = {}) override;
  void ReloadCurrentFolder() override;
  bool LoadThumbnailWindow(const std::optional<FilterNode>& statsFilter, bool reset) override;

  void LoadThumbnailDiskCacheSettings();
  void ApplyThumbnailDiskCacheSettingsToService();
  void NotifyThumbnailsChanged();
  void NotifyCountsChanged();
  void EmitThumbnailUpdated(uint elementId, const QString& thumbUrl, bool loading,
                            bool missingSource, const QString& errorText);

 signals:
  void ThumbnailsChanged();
  void thumbnailsChanged();
  void ThumbnailUpdated(uint elementId, const QString& thumbUrl, bool loading, bool missingSource,
                        const QString& errorText);
  void thumbnailUpdated(uint elementId, const QString& thumbUrl, bool loading, bool missingSource,
                        const QString& errorText);
  void CountsChanged();
  void ThumbnailDiskCacheStateChanged();
  void PresentationChanged();
  void QueryStateChanged();
  void OrderedFileIdsReady(qulonglong requestId, const QVariantList& fileIds);
  void orderedFileIdsReady(qulonglong requestId, const QVariantList& fileIds);
  void FocusPositionReady(uint fileId, qint64 occurrence, int sectionRow);
  void focusPositionReady(uint fileId, qint64 occurrence, int sectionRow);
  /// Answer of RequestEditorListPosition.
  void editorListPositionReady(qulonglong requestId, uint fileId, qint64 occurrence);
  void PendingRevealChanged();
  void pendingRevealChanged();
  /// The project's last edited photo was read (see RevealLastEditedFile).
  void LastEditedFileFound(uint elementId, uint imageId);
  /// An in-place change (RequestLibraryContentRefresh) starts: the models still show the
  /// previous result. Emitted synchronously before the first model signal of the change.
  void LibraryChangeAboutToApply();
  void libraryChangeAboutToApply();
  /// The in-place change is complete in the thumbnail model and the section model.
  void LibraryChangeApplied();
  void libraryChangeApplied();

 private:
  /// Queue one complete expansion update. nullopt toggles from the live choices after key read.
  void ChangeStackExpansion(std::optional<bool> expanded, std::vector<std::string> stack_ids = {});
  /// How a submitted refresh changes the shown result.
  enum class RefreshKind : std::uint8_t {
    kReplace,  ///< A new result replaces the shown one (model reset).
    kInPlace,  ///< The same query after a content change: rows are inserted and removed.
  };
  /// The shown result moved into a new result of the same query: the retained section pages
  /// (grouped mode) or the loaded rows (flat mode) in the new occurrence order, and the photos
  /// among them whose rows the refresh must read before the change is applied.
  struct InPlaceChange {
    LibraryDisplayModel::PageMap pages_{};               ///< Retained display ranges.
    std::vector<sl_element_id_t> loaded_order_{};        ///< Photo rows of the thumbnail model.
    int64_t                      loaded_end_ = 0;        ///< Flat mode: loaded occurrence prefix.
    std::vector<int64_t>         unread_occurrences_{};  ///< One occurrence per unread photo.
  };

  void               ScheduleRefresh(RefreshKind kind);
  /// Store the published query and expansion choices without retaining project services.
  /// GUI thread only, during publication, before subsequent paging or selection reads.
  void AcceptQueryInput(const LibraryQueryInput& input, const LibraryQueryOutput& output);
  /// Consume the minimal display stream to plan retained ranges and unread photo positions.
  [[nodiscard]] auto PlanInPlaceChange(const LibraryQueryInput& input,
                                       LibraryQueryOutput&      output) const -> InPlaceChange;
  /// Step 1 of an in-place refresh: plan the change, then read the unread photo rows (step 2)
  /// or apply the change at once.
  void               PublishInPlaceRefresh(std::shared_ptr<const LibraryQueryInput> input,
                                           LibraryQueryOutput                       output);
  void               ApplyInPlaceChange(const LibraryQueryInput& input, LibraryQueryOutput output,
                                        InPlaceChange change, const LibraryQueryOutput& rows);
  /// A failed refresh: the accepted content stays and the pending changes stay for Retry.
  void               FailRefresh(const QString& error);
  /// Notify the fully installed accepted result, then run its deferred actions.
  void               FinishRefresh(bool search_changed);
  /// Update selection counts on the existing worker; predicate replacements also intersect.
  void               RefreshSelectionMembership(bool intersect = false);
  void SaveThumbnailDiskCacheSettings();
  /// Release the pins and empty the grid model, the sections, and the counts.
  void               ResetThumbnailWindow(bool close_session = false);
  /// Options of the next request: the pending change, else the accepted options.
  [[nodiscard]] auto RequestedOptions() const -> AlbumQueryOptions;
  void               SubmitPendingRefresh();
  /// Inputs of a read of the accepted result (page, position, ids), or empty without a result.
  [[nodiscard]] auto AcceptedReadInput(AlbumQueryRead read) const
      -> std::shared_ptr<const LibraryQueryInput>;
  void               PublishRefresh(const LibraryQueryInput& input, LibraryQueryOutput output);
  void               PublishPage(const LibraryQueryInput& input, LibraryQueryOutput output);
  /// Read the page of at most @p limit occurrences (0: one page size) at @p offset.
  void                SubmitPage(int64_t offset, int64_t limit = 0);
  /// True when the accepted or the requested presentation reads the rating (see
  /// NoteRatingsChanged).
  auto               PresentationReadsRating() const -> bool;
  /// True when the accepted Inspector filter or search reads a library mark (see
  /// NoteMarksChanged).
  auto               PresentationReadsMarks() const -> bool;
  /// Refresh the library when @p reads_changed_column, else read only the statistics.
  void               RefreshAfterColumnWrite(bool reads_changed_column);
  /// Read the rating and the marks of the selected photos on the worker into selection_marks_.
  /// A read that a local change (LibrarySelectionMarks::Apply*) passed while it ran is dropped;
  /// the write that made the change asks for the next read.
  void                  SubmitSelectionMarksRead();
  /// Read the statistics of the accepted query on the worker and publish them (no photo rows).
  void               SubmitStatisticsRead();
  /// Load the next missing page of the requested section rows or flat range, if any.
  void               ContinuePaging();
  /// Album item of one query row: SQL display values and edit state, plus the image pool
  /// fields SQL lacks.
  auto ItemFromRow(const AlbumQueryRow& row, const AlbumItem& display, sl_element_id_t folderId)
      -> AlbumItem;
  void               SetQueryError(const QString& message);
  void               RunAfterRefresh(std::function<void()> action);
  /// Run request @p requestId of RequestEditorListPosition unless a newer one replaced it.
  void                SubmitEditorListPosition(qulonglong requestId, uint fileId, bool loadPage);
  void                SetPendingReveal(uint fileId);
  /// Finish the pending reveal and report a miss without changing scope. Returns false so
  /// the caller always delivers this position result.
  auto                SettlePendingReveal(uint fileId, bool found) -> bool;

  ProjectModule*     project_ = nullptr;
  FolderController*  folders_ = nullptr;
  SearchController*  search_  = nullptr;
  StatsEngine*       stats_   = nullptr;

  ThumbnailManager    thumbs_;
  AlbumThumbnailModel thumbnail_model_{};
  AlbumViewState      view_state_{};
  LibrarySelection    selection_{this};
  LibrarySelectionMarks selection_marks_{};
  bool                  selection_marks_read_scheduled_    = false;

  bool    thumbnail_disk_cache_enabled_      = true;
  QString thumbnail_disk_cache_root_;
  int     thumbnail_disk_cache_max_entries_  = 10000;
  int     thumbnail_disk_cache_jpeg_quality_ = 85;
  std::function<QString(sl_element_id_t)> semantic_label_provider_{};

  // Accepted library query. The pending options are the change description only.
  LibraryDisplayModel                      display_model_{&thumbnail_model_};
  AlbumSectionModel                        section_model_{display_model_};
  bool                                     stack_display_enabled_ = false;
  bool                                     editor_photo_view_              = false;
  bool                                     intersect_selection_on_refresh_ = false;
  AlbumQueryOptions                        accepted_options_{};
  std::optional<AlbumQueryOptions>         pending_options_{};
  std::shared_ptr<const LibraryQueryInput> accepted_input_{};
  int64_t                                  occurrence_count_  = 0;
  int64_t loaded_display_end_ = 0;  ///< End of the most recently loaded display page.
  int64_t                              load_through_          = -1;
  int64_t                                  requested_begin_ = 0;  ///< Requested display range.
  int64_t                              requested_end_         = 0;
  bool                                 refresh_scheduled_     = false;
  /// Combined kind of the refresh requests since the last submit (kReplace wins).
  RefreshKind                              scheduled_refresh_kind_ = RefreshKind::kInPlace;
  /// A replacing refresh was submitted and no refresh was accepted since. A later request
  /// supersedes its read (latest wins), so that request must replace too: the superseded
  /// read carried a query change (filter, search, scope) that the accepted input lacks.
  bool                                     unpublished_replace_    = false;
  bool                                 refresh_in_flight_     = false;
  bool                                 page_in_flight_        = false;
  QString                              query_error_{};
  std::vector<std::function<void()>>   after_refresh_{};
  /// An edit was written while the library was hidden and the presentation reads edit state.
  bool                                     edit_order_stale_       = false;
  uint                                     pending_reveal_file_id_ = 0;

  /// Id of the newest RequestEditorListPosition request.
  qulonglong                               editor_list_request_id_ = 0;
  /// Last member: destroyed first, so no job outlives the state it posts to.
  std::unique_ptr<SearchRequestWorker> query_worker_;
  std::unique_ptr<LibrarySelectionOperations> selection_operations_;
};

}  // namespace alcedo::ui

// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>

#include "ui/alcedo_main/album_backend/adjustment_transfer_apply_coordinator.hpp"
#include "ui/photo_stack_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
class LibraryStackActionsTest : public PhotoStackProjectFixture {
 protected:
  std::string stack;
  void        SetUp() override {
    PhotoStackProjectFixture::SetUp();
    ASSERT_TRUE(host->photo_stacks()->Start(Create()).isEmpty());
    Wait();
    stack = result->change.stack_id;
    host->library()->SetStackDisplayEnabled(true);
    ASSERT_TRUE(WaitForLibraryQuery(*host));
  }
  auto Until(const std::function<bool()>& ready) -> bool {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 15000) ProcessEvents(10);
    return ready();
  }
  auto Selection() -> LibrarySelection* { return host->library()->selection(); }
  auto Rows() -> std::vector<SearchResultRow> {
    return host->project()->handler().project()->GetAlbumBrowseService()->ReadAlbumFileRows(files);
  }
  auto Item(size_t index) -> QVariantMap {
    const auto rows = Rows();
    return {{"elementId", files[index]},
            {"imageId", rows[index].image_id_},
            {"fileName", QString::fromStdString(rows[index].file_name_)}};
  }
  auto Fragment(const std::string& camera = "") -> AlbumStackFragmentKey {
    for (const auto& [offset, page] : host->library_display()->pages())
      for (const auto& item : page)
        if (item.identity.fragment.stack_id == stack &&
            (camera.empty() || item.identity.fragment.group_key == AlbumGroupKey{camera}))
          return item.identity.fragment;
    ADD_FAILURE() << "Missing stack fragment";
    return {};
  }
  void NikonOnly() {
    host->stats()->ToggleStatsFilter("camera", "Nikon");
    ASSERT_TRUE(WaitForLibraryQuery(*host));
    ASSERT_EQ(host->library()->TotalCount(), 2);
    ASSERT_EQ(host->library_display()->DisplayCount(), 1);
  }
};

TEST_F(LibraryStackActionsTest, CollapsedCoverRatingChangesOnlyTheExplicitlySelectedFile) {
  Selection()->Replace(QVariantList{Item(0)});
  const auto targets = host->library_mutations()->ResolveTargets(Item(0), true);
  ASSERT_EQ(targets.size(), 1);
  ASSERT_TRUE(host->library_mutations()->RateTargets(targets, 5).value("success").toBool());
  const auto rows = Rows();
  EXPECT_EQ(rows[0].rating_, 5);
  EXPECT_EQ(rows[1].rating_, 0);
  EXPECT_EQ(rows[2].rating_, 0);
}

TEST_F(LibraryStackActionsTest, SelectAllIncludesCollapsedMatchingMembersButExcludesNonMatches) {
  NikonOnly();
  EXPECT_EQ(host->library()->model().count(), 1);
  host->library()->SelectAllInCurrentView();
  ASSERT_TRUE(Until([&] { return Selection()->SelectedCount() == 2; }));
  EXPECT_EQ(Selection()->SelectedElementIds(), (std::vector{files[1], files[2]}));
  ASSERT_TRUE(Until([&] { return Selection()->HiddenSelectedCount() == 1; }));
  EXPECT_EQ(Selection()->OutsideResultCount(), 0);
}

TEST_F(LibraryStackActionsTest, WholeStackSelectionIsExplicitAndReportsOutsideResultCount) {
  NikonOnly();
  host->library()->SelectEntireStack(stack);
  ASSERT_TRUE(Until([&] {
    return Selection()->SelectedCount() == 3 && Selection()->OutsideResultCount() == 1 &&
           Selection()->HiddenSelectedCount() == 1;
  }));
  const auto targets = host->library_mutations()->ResolveTargets(Item(1), true);
  EXPECT_EQ(targets.size(), 3);
  host->export_queue()->AddTargets(targets);
  EXPECT_EQ(host->export_queue()->Count(), 3);
  host->library()->SelectMatchingStackPhotos(Fragment());
  ASSERT_TRUE(Until([&] { return Selection()->SelectedCount() == 2; }));
  EXPECT_EQ(Selection()->SelectedElementIds(), (std::vector{files[1], files[2]}));
}

TEST_F(LibraryStackActionsTest, RepeatedLabelOccurrencesProduceOneExportAndOneMutationPerFile) {
  // The active-model label schema currently permits one label per File. Repeated occurrence
  // maps still exercise the shared selection/action boundary without changing that schema.
  auto first = Item(1), second = Item(1);
  first.insert("groupTitle", "bird");
  second.insert("groupTitle", "flight");
  Selection()->Replace(QVariantList{first, second});
  const auto targets = host->library_mutations()->ResolveTargets(first, true);
  ASSERT_EQ(targets.size(), 1);
  host->export_queue()->AddTargets(targets);
  host->export_queue()->AddTargets(targets);
  EXPECT_EQ(host->export_queue()->Count(), 1);
  EXPECT_TRUE(host->library_mutations()->RateTargets(targets, 4).value("success").toBool());
  const auto rows = Rows();
  EXPECT_EQ(rows[1].rating_, 4);
  EXPECT_EQ(rows[0].rating_, 0);
  EXPECT_EQ(rows[2].rating_, 0);
}

TEST_F(LibraryStackActionsTest,
       FilterChangePrunesSelectionWithoutReplacingSelectedFileWithTemporaryCover) {
  Selection()->Replace(QVariantList{Item(0), Item(2), Item(3)});
  NikonOnly();
  ASSERT_TRUE(Until([&] { return Selection()->SelectedCount() == 1; }));
  EXPECT_EQ(Selection()->SelectedElementIds(), (std::vector{files[2]}));
  EXPECT_FALSE(Selection()->IsSelected(files[1]));
  const auto key = Fragment();
  host->library_display()->SetStackExpanded(key, true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  host->library_display()->SetStackExpanded(key, false);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(Selection()->SelectedElementIds(), (std::vector{files[2]}));
}

TEST_F(LibraryStackActionsTest, FilterReplacementStillIntersectsAfterACoverClickReplacesTheIdRead) {
  Selection()->Replace(QVariantList{Item(0), Item(2), Item(3)});
  ASSERT_TRUE(Until([&] { return Selection()->HiddenSelectedCount() == 1; }));
  struct PausedRead {
    std::atomic<bool>       entered{false};
    std::atomic<bool>       armed{true};
    std::atomic<bool>       timed_out{false};
    std::mutex              mutex;
    std::condition_variable condition;
    bool                    released = false;
  };
  const auto pause  = std::make_shared<PausedRead>();
  const auto browse = host->project()->handler().project()->GetAlbumBrowseService();
  browse->SetQueryThreadObserver([pause](std::string_view operation) {
    if (operation != "ReadAlbumFileIds" || !pause->armed.exchange(false)) return;
    std::unique_lock lock(pause->mutex);
    pause->entered = true;
    // Bounded on assertion failure, so fixture teardown can always join the real worker.
    if (!pause->condition.wait_for(lock, std::chrono::seconds(10), [&] { return pause->released; }))
      pause->timed_out = true;
  });
  NikonOnly();
  ASSERT_TRUE(Until([&] { return pause->entered.load(); }));
  const auto cover = Item(1);
  Selection()->SetImageSelected(files[1], cover.value("imageId").toUInt(), "cover", false, true);
  {
    std::lock_guard lock(pause->mutex);
    pause->released = true;
  }
  pause->condition.notify_all();
  ASSERT_TRUE(
      Until([&] { return Selection()->SelectedElementIds() == std::vector{files[1], files[2]}; }));
  EXPECT_FALSE(pause->timed_out);
  browse->SetQueryThreadObserver({});
}

TEST_F(LibraryStackActionsTest, DisplayRangeSelectsOnlyRepresentedFiles) {
  NikonOnly();
  QSignalSpy ready(host->library(), &LibraryModule::OrderedFileIdsReady);
  host->library()->RequestOrderedFileIds({QVariantMap{{"begin", 0}, {"end", 1}}});
  ASSERT_TRUE(WaitForSignal(ready));
  const auto rows = ready.front()[1].toList();
  ASSERT_EQ(rows.size(), 1);
  EXPECT_EQ(rows[0].toMap().value("elementId").toUInt(), files[1]);
}

TEST_F(LibraryStackActionsTest, FailedStackSelectionKeepsExplicitSelectionAndReportsDiagnostic) {
  Selection()->Replace(QVariantList{Item(0)});
  host->library()->SelectEntireStack("missing-stack");
  ASSERT_TRUE(Until([&] { return !host->library()->QueryError().isEmpty(); }));
  EXPECT_EQ(Selection()->SelectedElementIds(), (std::vector{files[0]}));
}

TEST_F(LibraryStackActionsTest, NonmatchingRevealKeepsTheFilterAndReportsTheRequestedFile) {
  NikonOnly();
  QSignalSpy ready(host->library(), &LibraryModule::FocusPositionReady);
  host->library()->RequestFocusPosition(files[0]);
  ASSERT_TRUE(WaitForSignal(ready));
  EXPECT_EQ(ready.front()[0].toUInt(), files[0]);
  EXPECT_EQ(ready.front()[1].toLongLong(), -1);
  EXPECT_EQ(host->library()->TotalCount(), 2);
  EXPECT_FALSE(host->library()->QueryError().isEmpty());
}

TEST_F(LibraryStackActionsTest, FilteredDeleteLeavesNonMatchingStackMembersAndRepairsCover) {
  host->stats()->ToggleStatsFilter("camera", "Canon");
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->TotalCount(), 1);
  Selection()->Replace(QVariantList{Item(0)});
  const auto image   = Item(0).value("imageId").toUInt();
  const auto package = host->project()->handler().package_path();
  ASSERT_TRUE(DeleteAndWait(*host, {{files[0], image}}));
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(stack),
            (std::vector{files[1], files[2]}));
  ASSERT_TRUE(host->project()->CloseProject(false));
  ASSERT_TRUE(LoadPackedProject(*host, package));
  host->library()->SetStackDisplayEnabled(true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(stack),
            (std::vector{files[1], files[2]}));
  for (const auto& [offset, page] : host->library_display()->pages())
    for (const auto& item : page)
      if (item.identity.fragment.stack_id == stack) EXPECT_EQ(item.saved_cover_file_id, files[1]);
}

TEST_F(LibraryStackActionsTest, CoverMarksAndAlbumLinkUseOnlyExplicitTargets) {
  Selection()->Replace(QVariantList{Item(0)});
  const auto targets = host->library_mutations()->ResolveTargets(Item(0), true);
  QSignalSpy marks(host->images(), &ImageController::ImageMarksFinished);
  ASSERT_TRUE(
      host->library_mutations()->SetDeleteMarkOfTargets(targets, true).value("started").toBool());
  ASSERT_TRUE(WaitForSignal(marks, 30000));
  auto rows = Rows();
  EXPECT_TRUE(rows[0].delete_mark_);
  EXPECT_FALSE(rows[1].delete_mark_);
  EXPECT_FALSE(rows[2].delete_mark_);
  marks.clear();
  ASSERT_TRUE(host->library_mutations()
                  ->SetColorLabelOfTargets(targets, 1, true)
                  .value("started")
                  .toBool());
  ASSERT_TRUE(WaitForSignal(marks, 30000));
  rows = Rows();
  EXPECT_NE(rows[0].color_labels_, 0);
  EXPECT_EQ(rows[1].color_labels_, 0);
  EXPECT_EQ(rows[2].color_labels_, 0);
  ASSERT_NE(host->folders()->CreateFolderIn(0, "Selected cover"), 0);
  const auto folder = FindFolderId(host->folders()->Folders(), "Selected cover");
  ASSERT_NE(folder, 0);
  const auto linked = host->images()->AddImagesToFolder(targets, folder);
  ASSERT_TRUE(linked.value("success").toBool());
  EXPECT_EQ(linked.value("addedCount").toInt(), 1);
  host->folders()->SelectFolder(folder);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->TotalCount(), 1);
  EXPECT_EQ(host->library_display()->FileIdAt(0), files[0]);
}

TEST_F(LibraryStackActionsTest, AdjustmentPasteUpdatesOnlyTheSelectedCover) {
  auto* transfer = host->adjustment_transfer();
  ASSERT_TRUE(transfer->PrepareCopy(files[3]).value("success").toBool());
  ASSERT_TRUE(transfer->CommitCopy().value("success").toBool());
  Selection()->Replace(QVariantList{Item(0)});
  const auto targets      = host->library_mutations()->ResolveTargets(Item(0), true);
  auto       pipeline     = host->project()->handler().pipeline_service();
  const auto first_count  = pipeline->LoadHistorySnapshot(files[1]).graph_->CommitCount();
  const auto second_count = pipeline->LoadHistorySnapshot(files[2]).graph_->CommitCount();
  QSignalSpy refreshed(transfer->apply_coordinator(),
                       &AdjustmentTransferApplyCoordinator::TargetRefreshed);
  QSignalSpy finished(transfer, &AdjustmentTransferController::PasteFinished);
  ASSERT_TRUE(transfer->Paste(targets, "paste").value("success").toBool());
  ASSERT_TRUE(WaitForSignal(finished, 60000));
  EXPECT_EQ(finished.front()[0].toMap().value("appliedCount").toInt(), 1);
  ASSERT_EQ(refreshed.count(), 1);
  EXPECT_EQ(refreshed.front()[0].toUInt(), files[0]);
  EXPECT_EQ(pipeline->LoadHistorySnapshot(files[1]).graph_->CommitCount(), first_count);
  EXPECT_EQ(pipeline->LoadHistorySnapshot(files[2]).graph_->CommitCount(), second_count);
}

TEST_F(LibraryStackActionsTest, EditorFilmstripKeepsAllMatchingFilesWhenLibraryStacksCollapse) {
  EXPECT_EQ(host->library_display()->DisplayCount(), 2);
  host->library()->SetEditorPhotoView(true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->model().count(), 4);
  QSignalSpy ready(host->library(), &LibraryModule::OrderedFileIdsReady);
  host->library()->RequestOrderedFileIds({QVariantMap{{"begin", 0}, {"end", 4}}});
  ASSERT_TRUE(WaitForSignal(ready));
  EXPECT_EQ(ready.front()[1].toList().size(), 4);
  QSignalSpy position(host->library(), &LibraryModule::editorListPositionReady);
  host->library()->RequestEditorListPosition(files[2]);
  ASSERT_TRUE(WaitForSignal(position));
  EXPECT_GE(position.front()[2].toLongLong(), 0);
  host->library()->SetEditorPhotoView(false);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library_display()->DisplayCount(), 2);
}

TEST_F(LibraryStackActionsTest, ReturningToHiddenMatchingMemberExpandsOnlyItsPreferredFragment) {
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto canon = Fragment("Canon"), nikon = Fragment("Nikon");
  QSignalSpy ready(host->library(), &LibraryModule::FocusPositionReady);
  host->library()->RequestFocusPosition(files[2], "Nikon");
  ASSERT_TRUE(WaitForSignal(ready, 15000));
  EXPECT_TRUE(host->library_display()->IsStackExpanded(nikon));
  EXPECT_FALSE(host->library_display()->IsStackExpanded(canon));
  EXPECT_EQ(ready.front()[0].toUInt(), files[2]);
  EXPECT_GE(ready.front()[1].toLongLong(), 0);
}
}  // namespace
}  // namespace alcedo::ui::test

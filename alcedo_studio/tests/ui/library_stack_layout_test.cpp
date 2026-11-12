// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QSignalSpy>
#include <algorithm>
#include <vector>

#include "ui/alcedo_main/album_backend/justified_layout_model.hpp"
#include "ui/alcedo_main/i18n.hpp"
#include "ui/photo_stack_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
class LibraryStackLayoutTest : public PhotoStackProjectFixture {
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
  /// Loaded matching occurrence identity, authored through the public scalar-role API.
  auto Key(const std::string& camera = "") -> QString {
    for (const auto& [start, page] : host->library_display()->pages())
      for (size_t i = 0; i < page.size(); ++i)
        if (page[i].identity.fragment.stack_id == stack &&
            (camera.empty() || page[i].identity.fragment.group_key == AlbumGroupKey(camera)))
          return host->library_display()
              ->getItemAt(static_cast<int>(start + i))
              .value("occurrenceKey")
              .toString();
    ADD_FAILURE() << "Expected a loaded stack occurrence";
    return {};
  }
};

TEST_F(LibraryStackLayoutTest, DisclosureTogglesOnlyTheClickedFragmentAndKeepsPhotoSelection) {
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto rows =
      host->project()->handler().project()->GetAlbumBrowseService()->ReadAlbumFileRows(files);
  host->library()->selection()->SetImageSelected(files[2], rows[2].image_id_, "selected", false,
                                                 true);
  const auto canon = Key("Canon"), nikon = Key("Nikon");
  host->library()->ToggleStack(nikon);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_TRUE(host->library_display()
                  ->getItemAt(host->library_display()->rowByKey(nikon))
                  .value("stackExpanded")
                  .toBool());
  EXPECT_FALSE(host->library_display()
                   ->getItemAt(host->library_display()->rowByKey(canon))
                   .value("stackExpanded")
                   .toBool());
  EXPECT_EQ(host->library()->selection()->SelectedElementIds(), std::vector{files[2]});
  host->library()->ToggleStack(nikon);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->selection()->SelectedElementIds(), std::vector{files[2]});
}

TEST_F(LibraryStackLayoutTest, GroupHeaderCollapseDoesNotChangeStackExpansionChoices) {
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto key = Key("Nikon");
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  auto&      sections = host->library()->section_model();
  const auto group    = sections.GroupIndexForTitle("Nikon", false);
  ASSERT_GE(group, 0);
  sections.SetGroupCollapsed(group, !sections.IsGroupCollapsed(group));
  EXPECT_TRUE(host->library_display()
                  ->getItemAt(host->library_display()->rowByKey(key))
                  .value("stackExpanded")
                  .toBool());
  sections.SetGroupCollapsed(group, !sections.IsGroupCollapsed(group));
  EXPECT_TRUE(host->library_display()
                  ->getItemAt(host->library_display()->rowByKey(key))
                  .value("stackExpanded")
                  .toBool());
}

TEST_F(LibraryStackLayoutTest, ExpandedFragmentUpdatesHiddenSelectionAndSubsequentDisplayReads) {
  auto* library = host->library();
  library->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  library->SelectEntireStack(stack);
  for (int i = 0; i < 200 && library->selection()->HiddenSelectedCount() != 1; ++i)
    ProcessEvents(20);
  ASSERT_EQ(library->selection()->SelectedCount(), 3);
  ASSERT_EQ(library->selection()->HiddenSelectedCount(), 1);
  const auto key = Key("Nikon");
  library->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  for (int i = 0; i < 200 && library->selection()->HiddenSelectedCount() != 0; ++i)
    ProcessEvents(20);
  EXPECT_EQ(library->selection()->HiddenSelectedCount(), 0);
  EXPECT_EQ(library->selection()->SelectedCount(), 3);
  QSignalSpy ordered(library, &LibraryModule::OrderedFileIdsReady);
  const auto request = library->RequestOrderedFileIds({QVariantMap{{"begin", 0}, {"end", 4}}});
  for (int i = 0; i < 200 && ordered.empty(); ++i) ProcessEvents(20);
  ASSERT_FALSE(ordered.empty());
  ASSERT_EQ(ordered.front()[0].toULongLong(), request);
  const auto ordered_photos = ordered.front()[1].toList();
  ASSERT_EQ(ordered_photos.size(), 4);
  for (const auto file : files) {
    EXPECT_TRUE(std::any_of(ordered_photos.begin(), ordered_photos.end(), [file](const auto& item) {
      return item.toMap().value("elementId").toUInt() == file;
    }));
  }
  // A later selection change must also read the newly accepted expansion choices.
  library->selection()->Clear();
  library->SelectEntireStack(stack);
  for (int i = 0; i < 200 && library->selection()->SelectedCount() != 3; ++i) ProcessEvents(20);
  ProcessEvents(100);
  EXPECT_EQ(library->selection()->HiddenSelectedCount(), 0);
  library->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  for (int i = 0; i < 200 && library->selection()->HiddenSelectedCount() != 1; ++i)
    ProcessEvents(20);
  EXPECT_EQ(library->selection()->HiddenSelectedCount(), 1);
}

TEST_F(LibraryStackLayoutTest, TemporaryCoverOpensItsOwnFileAndReportsMatchingCounts) {
  host->stats()->ToggleStatsFilter("camera", "Nikon");
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto item = host->library_display()->getItemAt(0);
  ASSERT_EQ(item.value("fileId").toUInt(), files[1]);
  EXPECT_TRUE(item.value("temporaryCover").toBool());
  EXPECT_EQ(item.value("fragmentMatchCount").toInt(), 2);
  EXPECT_EQ(item.value("queryMatchCount").toInt(), 2);
  EXPECT_EQ(item.value("stackTotal").toInt(), 3);
  ASSERT_TRUE(host->workspace_router()->openEditor(item.value("fileId").toUInt(),
                                                   item.value("imageId").toUInt()));
  EXPECT_EQ(host->workspace_router()->element_id(), files[1]);
  EXPECT_EQ(host->workspace_router()->image_id(), item.value("imageId").toUInt());
  host->workspace_router()->OpenLibrary();
}

TEST_F(LibraryStackLayoutTest, ExpandAllIncludesUnloadedFragmentsAndCollapseKeepsSelection) {
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  host->library()->SelectAllInCurrentView();
  for (int i = 0; i < 100 && host->library()->selection()->SelectedCount() != 4; ++i)
    ProcessEvents(20);
  host->library()->SetAllStacksExpanded(true);
  ProcessEvents(100);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  for (const auto& [start, page] : host->library_display()->pages())
    for (const auto& item : page)
      if (!item.identity.fragment.stack_id.empty()) EXPECT_TRUE(item.expanded);
  host->library()->SetAllStacksExpanded(false);
  ProcessEvents(100);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->selection()->SelectedCount(), 4);
}

TEST(LibraryStackGeometryTest,
     JustifiedTemporaryCoverChangeUsesItsAspectRatioAndKeepsFragmentIdentity) {
  AlbumThumbnailModel    photos;
  std::vector<AlbumItem> values(2);
  values[0].element_id = values[0].file_id = 1;
  values[0].aspect_ratio                   = 2.0;
  values[1].element_id = values[1].file_id = 2;
  values[1].aspect_ratio                   = 0.5;
  photos.resetModel(values, 2);
  LibraryDisplayModel display(&photos);
  AlbumDisplayItem    cover;
  cover.identity    = {{0, AlbumGroupField::kRating, int64_t{4}, "stack"},
                       AlbumDisplayKind::kStackRepresentative,
                       0};
  cover.file_id     = 1;
  cover.stack_total = 2;
  display.BeginReplace();
  display.Install(1, {{0, {cover}}});
  display.EndReplace();
  const auto           key = display.getItemAt(0).value("occurrenceKey");
  JustifiedLayoutModel layout;
  layout.setGeometry(1000, 200, 8, 22);
  layout.setViewportHeight(600);
  layout.setSource(&display);
  EXPECT_DOUBLE_EQ(layout.tileRect(0).width(), 400);
  cover.file_id         = 2;
  cover.temporary_cover = true;
  display.BeginReplace();
  display.Install(1, {{0, {cover}}});
  display.EndReplace();
  EXPECT_DOUBLE_EQ(layout.tileRect(0).width(), 100);
  EXPECT_EQ(display.getItemAt(0).value("occurrenceKey"), key);
}

TEST_F(LibraryStackLayoutTest, JustifiedExpansionPreservesTopFragmentAndShowsOnlyMatchingMembers) {
  host->stats()->ToggleStatsFilter("camera", "Nikon");
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto           key = Key();
  JustifiedLayoutModel layout;
  layout.setGeometry(700, 180, 8, 22);
  layout.setViewportHeight(300);
  layout.setSource(host->library_display());
  const auto before = layout.tileRect(host->library_display()->rowByKey(key));
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto index = host->library_display()->rowByKey(key);
  ASSERT_GE(index, 0);
  const auto viewport = layout.setGeometryAnchored(700, 180, 8, 22, index, 0, before.y());
  EXPECT_DOUBLE_EQ(layout.tileRect(index).y() - viewport, before.y());
  ASSERT_EQ(host->library_display()->rowCount(), 2);
  EXPECT_EQ(host->library_display()->FileIdAt(0), files[1]);
  EXPECT_EQ(host->library_display()->FileIdAt(1), files[2]);
}

TEST_F(LibraryStackLayoutTest, LanguageChangeKeepsTypedFragmentKeysAndSelection) {
  host->library()->SetInspectorGrouping("rating", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto key = Key();
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  host->library()->SelectStack(key, false);
  ProcessEvents(100);
  const auto selected = host->library()->selection()->SelectedElementIds();
  i18n::TranslationNotifier::Instance().NotifyLanguageChanged();
  ProcessEvents(100);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto index = host->library_display()->rowByKey(key);
  ASSERT_GE(index, 0);
  EXPECT_EQ(host->library()->selection()->SelectedElementIds(), selected);
  EXPECT_TRUE(host->library_display()->ItemAt(index)->expanded);
  EXPECT_TRUE(std::holds_alternative<int64_t>(
      host->library_display()->ItemAt(index)->identity.fragment.group_key));
}

TEST(LibraryStackGeometryTest,
     AppendedDisplayPageAddsJustifiedTilesWithoutChangingTheAcceptedCount) {
  AlbumThumbnailModel    photos;
  std::vector<AlbumItem> values(4);
  for (size_t i = 0; i < values.size(); ++i) {
    values[i].element_id = values[i].file_id = static_cast<uint>(i + 1);
    values[i].aspect_ratio                   = 1.5;
  }
  photos.resetModel(values, 4);
  LibraryDisplayModel display(&photos);
  AlbumDisplayItem    first, second;
  first.file_id = first.identity.file_id = 1;
  second.file_id = second.identity.file_id = 2;
  display.BeginReplace();
  display.Install(4, {{0, {first}}});
  display.EndReplace();
  JustifiedLayoutModel layout;
  layout.setGeometry(700, 180, 8, 22);
  layout.setViewportHeight(600);
  layout.setSource(&display);
  ASSERT_EQ(layout.layout().ItemCount(), 1);
  display.StorePage(1, {second}, 3);
  EXPECT_EQ(display.rowCount(), 4);
  EXPECT_EQ(layout.layout().ItemCount(), 2);
  EXPECT_GT(layout.tileRect(1).width(), 0);
}
TEST(LibraryStackGeometryTest,
     ExpansionAboveViewportKeepsTheActualTopTileAndBoundedMetadataWindows) {
  AlbumThumbnailModel photos;
  AlbumItem           cover, top, following;
  cover.element_id = cover.file_id = 1;
  cover.aspect_ratio               = 1.5;
  top.element_id = top.file_id = 10001;
  top.aspect_ratio             = 0.75;
  following.element_id = following.file_id = 10002;
  following.aspect_ratio                   = 1.5;
  photos.resetModel({cover, top, following}, 10002);
  LibraryDisplayModel display(&photos);
  AlbumDisplayItem    representative, last, after;
  representative.identity = {
      {0, AlbumGroupField::kNone, {}, "stack"}, AlbumDisplayKind::kStackRepresentative, 0};
  representative.file_id = 1;
  last.file_id = last.identity.file_id = 10001;
  after.file_id = after.identity.file_id = 10002;
  display.BeginReplace();
  display.Install(3, {{0, {representative, last, after}}});
  display.EndReplace();
  const auto           key = display.getItemAt(1).value("occurrenceKey").toString();
  JustifiedLayoutModel layout;
  layout.setGeometry(300, 160, 8, 22);
  layout.setViewportHeight(180);
  layout.setSource(&display);
  const auto                    before = layout.tileRect(1);
  std::vector<AlbumDisplayItem> expanded;
  expanded.push_back(representative);
  for (uint file = 2; file <= 10000; ++file) {
    AlbumDisplayItem member;
    member.identity = {
        {0, AlbumGroupField::kNone, {}, "stack"}, AlbumDisplayKind::kStackMember, file};
    member.file_id = file;
    expanded.push_back(std::move(member));
  }
  expanded.push_back(last);
  expanded.push_back(after);
  auto pages = display.RetainedPages(expanded, 1);
  ASSERT_LE(pages.size(), 3);
  ASSERT_TRUE(pages.contains(10000));
  display.BeginReplace();
  display.Install(10002, std::move(pages));
  display.EndReplace();
  EXPECT_LE(display.LoadedOccurrenceCount(), 3000);
  EXPECT_EQ(display.rowByKey(key), 10000);
  const auto viewport = layout.setGeometryAnchored(300, 160, 8, 22, 10000, 0, before.y());
  EXPECT_NEAR(layout.tileRect(10000).y() - viewport, before.y(), 0.001);
  EXPECT_EQ(photos.count(), 3);
}
}  // namespace
}  // namespace alcedo::ui::test

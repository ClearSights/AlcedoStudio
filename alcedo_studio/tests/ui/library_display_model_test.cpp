// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include "ui/alcedo_main/album_backend/library_display_model.hpp"

#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ui/alcedo_main/album_backend/album_section_model.hpp"

namespace alcedo::ui::test {
namespace {
auto Item(sl_element_id_t file, const std::string& stack = {}) -> AlbumDisplayItem {
  AlbumDisplayItem item;
  item.file_id          = file;
  item.identity.file_id = stack.empty() ? file : 0;
  item.identity.kind =
      stack.empty() ? AlbumDisplayKind::kPhoto : AlbumDisplayKind::kStackRepresentative;
  item.identity.fragment.stack_id = stack;
  return item;
}

TEST(LibraryDisplayModelTest, SectionGeometryReadsDisplayCountsButReportsMatchingPhotoCounts) {
  LibraryDisplayModel      display;
  AlbumSectionModel        sections(display);
  QAbstractItemModelTester checker(&sections,
                                   QAbstractItemModelTester::FailureReportingMode::Fatal);
  QAbstractItemModelTester display_checker(&display,
                                           QAbstractItemModelTester::FailureReportingMode::Fatal);
  display.BeginReplace();
  sections.BeginReplace();
  display.Install(3, {{0, {Item(1, "stack"), Item(4), Item(5, "stack")}}});
  sections.InstallGroups({{.key_              = std::string("Monday"),
                           .photo_count_      = 4,
                           .first_occurrence_ = 0,
                           .display_count     = 2,
                           .first_display     = 0},
                          {.key_              = std::string("Tuesday"),
                           .photo_count_      = 2,
                           .first_occurrence_ = 4,
                           .display_count     = 1,
                           .first_display     = 2}},
                         6, 3, false, AlbumReadMode::kDisplay);
  sections.EndReplace();
  display.EndReplace();
  sections.SetColumnCount(2);
  EXPECT_EQ(sections.rowCount(), 4);
  EXPECT_EQ(sections.OccurrenceTotal(), 3);
  EXPECT_EQ(sections.UniqueFileCount(), 6);
  EXPECT_EQ(sections.data(sections.index(0), AlbumSectionModel::PhotoCount).toLongLong(), 4);
  EXPECT_EQ(sections.RowAt(3).first_occurrence_, 2);
  EXPECT_EQ(sections.FileIdAt(2), 5);
  sections.SetGroupCollapsed(0, true);
  EXPECT_EQ(sections.rowCount(), 3);
  EXPECT_EQ(sections.RowForOccurrence(2), 2);
  sections.ExpandAll();
  EXPECT_EQ(sections.rowCount(), 4);
}

TEST(LibraryDisplayModelTest, DisplayPagesAreReadBySectionsWithoutAnotherPageCache) {
  LibraryDisplayModel display;
  AlbumSectionModel   sections(display);
  sections.ResetGroups({{.key_ = std::string("camera"), .photo_count_ = 8}}, 8, 8, false);
  display.BeginReplace();
  display.Install(8, {});
  display.EndReplace();
  QSignalSpy pages(&sections, &AlbumSectionModel::PagesChanged);
  display.StorePage(0, {Item(1), Item(2)}, 2);
  display.StorePage(4, {Item(5), Item(6)}, 2);
  EXPECT_EQ(sections.LoadedPageCount(), display.LoadedPageCount());
  EXPECT_EQ(sections.LoadedOccurrenceCount(), 4);
  EXPECT_EQ(sections.FileIdAt(4), 5);
  display.StorePage(6, {Item(7), Item(8)}, 2);
  EXPECT_EQ(sections.FileIdAt(0), 0);
  EXPECT_EQ(sections.LoadedUniqueFileIds(), (std::vector<sl_element_id_t>{5, 6, 7, 8}));
  EXPECT_EQ(sections.FirstUnloadedOccurrence(0, 8), 0);
  EXPECT_EQ(pages.count(), 3);
}

TEST(LibraryDisplayModelTest, TemporaryCoverChangesRetainTheRepresentativeKeyAndForwardPhotoRoles) {
  AlbumThumbnailModel photos;
  AlbumItem           first;
  first.element_id = first.file_id = 1;
  first.rating                     = 2;
  AlbumItem next;
  next.element_id = next.file_id = 2;
  next.rating                    = 4;
  photos.resetModel({first, next}, 2);
  LibraryDisplayModel display(&photos);
  auto                cover = Item(1, "stable-stack");
  cover.saved_cover_file_id = 1;
  cover.stack_total         = 4;
  display.BeginReplace();
  display.Install(1, {{0, {cover}}});
  display.EndReplace();
  const auto identity = display.ItemAt(0)->identity;
  EXPECT_EQ(display.data(display.index(0), AlbumThumbnailModel::Rating).toInt(), 2);
  cover.file_id         = 2;
  cover.temporary_cover = true;
  display.BeginReplace();
  display.Install(1, {{0, {cover}}});
  display.EndReplace();
  EXPECT_EQ(display.ItemAt(0)->identity, identity);
  EXPECT_EQ(display.data(display.index(0), AlbumThumbnailModel::Rating).toInt(), 4);
  EXPECT_TRUE(display.data(display.index(0), LibraryDisplayModel::TemporaryCover).toBool());
  QSignalSpy changed(&display, &QAbstractItemModel::dataChanged);
  photos.updateRating(2, 0, 5);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(display.data(display.index(0), AlbumThumbnailModel::Rating).toInt(), 5);
}

TEST(LibraryDisplayModelTest, ExpansionChoicesSurviveAbsentResultsAndStayWithinTheirSessionScope) {
  LibraryDisplayModel         display;
  const AlbumStackFragmentKey flat{1, AlbumGroupField::kNone, {}, "stable"};
  const AlbumStackFragmentKey day{1, AlbumGroupField::kCaptureDay, std::string("Tuesday"),
                                  "stable"};
  EXPECT_TRUE(display.SetStackExpanded(flat, true));
  EXPECT_TRUE(display.SetStackExpanded(day, true));
  EXPECT_FALSE(display.SetStackExpanded(day, true));
  display.BeginReplace();
  display.Install(0, {});
  display.EndReplace();
  EXPECT_TRUE(display.IsStackExpanded(flat));
  EXPECT_TRUE(display.IsStackExpanded(day));
  EXPECT_EQ(display.ExpandedFragments(1, AlbumGroupField::kCaptureDay), (std::vector{day}));
  EXPECT_TRUE(display.ExpandedFragments(2, AlbumGroupField::kCaptureDay).empty());
  display.ResetSession();
  EXPECT_FALSE(display.IsStackExpanded(flat));
  EXPECT_FALSE(display.IsStackExpanded(day));
}

TEST(LibraryDisplayModelTest, InvalidPageInstallationKeepsTheAcceptedPagesAndCount) {
  LibraryDisplayModel display;
  display.BeginReplace();
  display.Install(3, {{0, {Item(1), Item(2)}}});
  display.EndReplace();
  EXPECT_THROW(display.Install(-1, {}), std::invalid_argument);
  EXPECT_THROW(display.Install(3, {{-1, {Item(3)}}}), std::invalid_argument);
  EXPECT_THROW(display.Install(3, {{2, {Item(3), Item(4)}}}), std::invalid_argument);
  EXPECT_EQ(display.DisplayCount(), 3);
  EXPECT_EQ(display.FileIdAt(0), 1);
  EXPECT_EQ(display.FileIdAt(1), 2);
}
TEST(LibraryDisplayModelTest, InvalidRetainedPhotoOrderDoesNotMoveTheAcceptedThumbnailState) {
  AlbumThumbnailModel photos;
  AlbumItem           item;
  item.element_id = item.file_id = 1;
  item.image_id                  = 1;
  item.thumb_url                 = "image://accepted";
  photos.resetModel({item}, 1);
  EXPECT_THROW(photos.installPageItems({}, {1, 99}, 2, false), std::invalid_argument);
  ASSERT_NE(photos.itemByFile(1), nullptr);
  EXPECT_EQ(photos.itemByFile(1)->thumb_url, "image://accepted");
  EXPECT_EQ(photos.count(), 1);
}
}  // namespace
}  // namespace alcedo::ui::test

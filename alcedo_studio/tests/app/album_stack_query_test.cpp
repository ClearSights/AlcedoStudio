// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/album_stack_test_library.hpp"

namespace alcedo {
namespace {
class AlbumStackQueryTest : public test::AlbumStackTestLibrary {};

TEST_F(AlbumStackQueryTest, NonCoverMatchShowsStackAndChoosesTemporaryCover) {
  const auto before = StackRows();
  const auto out    = Read({}, sleeve_filter::BuildCaptureDateBucketFilter(L"2026-06-09"), scope);
  ASSERT_EQ(out.display_rows.size(), 1);
  const auto& row = out.display_rows[0];
  EXPECT_EQ(row.photo.photo_.file_id_, files[1]);
  EXPECT_EQ(row.saved_cover_file_id, files[0]);
  EXPECT_TRUE(row.temporary_cover);
  EXPECT_EQ(row.stack_total, 4);
  EXPECT_EQ(row.query_match_count, 2);
  EXPECT_EQ(row.fragment_match_count, 2);
  EXPECT_EQ(out.unique_file_count_, 2);
  EXPECT_EQ(out.display_count, 1);
  EXPECT_TRUE(out.rows_.empty());
  EXPECT_EQ(StackRows(), before);
}

TEST_F(AlbumStackQueryTest, DifferentMembersCannotSatisfyDifferentAndConditions) {
  const auto filter = MergeFilterNodes(sleeve_filter::BuildCaptureDateBucketFilter(L"2026-06-08"),
                                       sleeve_filter::BuildLensBucketFilter(L"Lens B"));
  const auto out    = Read({}, filter, scope);
  EXPECT_TRUE(out.display_rows.empty());
  EXPECT_EQ(out.unique_file_count_, 0);
  EXPECT_EQ(out.display_count, 0);
  EXPECT_TRUE(out.groups_.empty());
}

TEST_F(AlbumStackQueryTest, OneStackSpanningDaysHasOneFragmentInEachMatchingDay) {
  const auto out = Read({.group_field_ = AlbumGroupField::kCaptureDay}, {}, scope);
  EXPECT_EQ(DisplayIds(out), Files({3, 1, 0}));
  ASSERT_EQ(out.groups_.size(), 3);
  EXPECT_EQ(out.unique_file_count_, 4);
  EXPECT_EQ(out.occurrence_count_, 4);
  EXPECT_EQ(out.display_count, 3);
  EXPECT_EQ(out.groups_[1].photo_count_, 2);
  EXPECT_EQ(out.groups_[1].display_count, 1);
  EXPECT_EQ(out.groups_[1].first_display, 1);
  EXPECT_EQ(out.display_rows[1].fragment_match_count, 2);
  EXPECT_EQ(out.display_rows[1].query_match_count, 4);
  EXPECT_EQ(out.display_rows[1].identity.fragment.stack_id, stack);
}

TEST_F(AlbumStackQueryTest, LabelOccurrencesUseOneSavedStackWithoutDuplicatingPhotoTotals) {
  const auto out = Read({.group_field_ = AlbumGroupField::kLabels}, {}, scope);
  EXPECT_EQ(DisplayIds(out), Files({0, 2, 3}));
  EXPECT_EQ(out.unique_file_count_, 4);
  EXPECT_EQ(out.occurrence_count_, 4);
  ASSERT_EQ(out.display_rows.size(), 3);
  for (const auto& row : out.display_rows) {
    EXPECT_EQ(row.stack_total, 4);
    EXPECT_EQ(row.query_match_count, 4);
    EXPECT_EQ(row.identity.fragment.stack_id, stack);
  }
  EXPECT_EQ(out.display_rows[0].fragment_match_count, 2);
  EXPECT_EQ(out.display_rows[1].fragment_match_count, 1);
  EXPECT_EQ(out.display_rows[2].fragment_match_count, 1);
  EXPECT_EQ(out.display_rows[0].identity.fragment.group_key, AlbumGroupKey{std::string("bird")});
  EXPECT_EQ(out.display_rows[2].identity.fragment.group_key, AlbumGroupKey{std::monostate{}});
}

TEST_F(AlbumStackQueryTest, AlbumScopeCanUseATemporaryCoverWithoutChangingSavedCover) {
  const auto album = project->GetSleeveService()->CreateFolder(L"/", L"CD");
  ASSERT_TRUE(album.second.success_);
  for (auto id : Files({2, 3})) {
    ASSERT_TRUE(
        project->GetSleeveService()->LinkFileToFolder(id, album.first->element_id_).success_);
  }
  const auto before = StackRows();
  const auto out    = Read({}, {}, album.first->element_id_);
  EXPECT_EQ(DisplayIds(out), Files({2}));
  ASSERT_EQ(out.display_rows.size(), 1);
  EXPECT_TRUE(out.display_rows[0].temporary_cover);
  EXPECT_EQ(out.display_rows[0].query_match_count, 2);
  EXPECT_EQ(Cover(stack), files[0]);
  EXPECT_EQ(StackRows(), before);
}

TEST_F(AlbumStackQueryTest, ExplicitSortAndSemanticRelevanceOrderUseTheDisplayedCover) {
  // Independent expected comparator over the authored fixture values, not another SQL read.
  for (auto field :
       {AlbumSortField::kNone, AlbumSortField::kCaptureTime, AlbumSortField::kImportTime,
        AlbumSortField::kCameraModel, AlbumSortField::kLens, AlbumSortField::kRating,
        AlbumSortField::kLabels, AlbumSortField::kEditTime}) {
    for (auto direction : {SortDirection::kAscending, SortDirection::kDescending}) {
      SCOPED_TRACE(static_cast<int>(field));
      SCOPED_TRACE(static_cast<int>(direction));
      std::vector<size_t> expected{0, 4, 5};
      const auto          less = [&](size_t a, size_t b) {
        int        order     = 0;
        bool       missing_a = false, missing_b = false;
        const auto compare = [&](const auto& x, const auto& y) {
          return x < y ? -1 : x > y ? 1 : 0;
        };
        switch (field) {
          case AlbumSortField::kNone:
            break;
          case AlbumSortField::kCaptureTime:
            missing_a = dates[a].empty();
            missing_b = dates[b].empty();
            order     = compare(dates[a], dates[b]);
            break;
          case AlbumSortField::kImportTime:
            order = compare(a, b);
            break;
          case AlbumSortField::kCameraModel:
            missing_a = cameras[a].empty();
            missing_b = cameras[b].empty();
            order     = compare(cameras[a], cameras[b]);
            break;
          case AlbumSortField::kLens:
            missing_a = lenses[a].empty();
            missing_b = lenses[b].empty();
            order     = compare(lenses[a], lenses[b]);
            break;
          case AlbumSortField::kRating:
            order = compare(ratings[a], ratings[b]);
            break;
          case AlbumSortField::kLabels:
            missing_a = labels[a].empty();
            missing_b = labels[b].empty();
            order     = compare(labels[a], labels[b]);
            break;
          case AlbumSortField::kEditTime:
            missing_a = edits[a] == 0;
            missing_b = edits[b] == 0;
            order     = compare(edits[a], edits[b]);
            break;
        }
        if (missing_a != missing_b) return !missing_a;
        if (order == 0) return files[a] < files[b];
        return direction == SortDirection::kAscending ? order < 0 : order > 0;
      };
      std::sort(expected.begin(), expected.end(), less);
      std::vector<sl_element_id_t> ids;
      for (auto index : expected) ids.push_back(files[index]);
      EXPECT_EQ(DisplayIds(Read({.sort_field_ = field, .sort_direction_ = direction})), ids);
    }
  }
  const auto relevance = Relevance();
  const auto out       = project->GetAlbumBrowseService()->ReadAlbumQuery(
      0, {}, {}, model, {.limit_ = 1000, .mode = AlbumReadMode::kDisplay}, relevance);
  EXPECT_EQ(DisplayIds(out), Files({4, 1}));
  ASSERT_EQ(out.display_rows.size(), 2);
  EXPECT_TRUE(out.display_rows[1].temporary_cover);
  EXPECT_EQ(out.display_rows[1].query_match_count, 3);
  const auto sorted = project->GetAlbumBrowseService()->ReadAlbumQuery(
      0, {},
      {.sort_field_ = AlbumSortField::kRating, .sort_direction_ = SortDirection::kDescending},
      model, {.limit_ = 1000, .mode = AlbumReadMode::kDisplay}, relevance);
  EXPECT_EQ(DisplayIds(sorted), Files({1, 4}));
}

TEST_F(AlbumStackQueryTest, UnknownGroupAndFlatModeHaveDifferentFragmentKeys) {
  const auto second  = Create({4, 5}, 4);
  const auto flat    = Read();
  const auto unknown = Read({.group_field_ = AlbumGroupField::kCameraModel});
  ASSERT_FALSE(flat.display_rows.empty());
  EXPECT_EQ(DisplayIds(unknown), Files({0, 4, 1, 5}));
  const auto& key = flat.display_rows.back().identity.fragment;
  EXPECT_EQ(key.stack_id, second);
  EXPECT_EQ(unknown.display_rows.back().identity.fragment.stack_id, second);
  EXPECT_EQ(key.group_field, AlbumGroupField::kNone);
  EXPECT_EQ(key.group_key, AlbumGroupKey{std::monostate{}});
  ASSERT_FALSE(unknown.display_rows.empty());
  EXPECT_NE(key, unknown.display_rows.back().identity.fragment);
  const auto ratings_out = Read({.group_field_ = AlbumGroupField::kRating});
  for (const auto& row : ratings_out.display_rows) {
    EXPECT_TRUE(std::holds_alternative<int64_t>(row.identity.fragment.group_key));
  }
}

TEST_F(AlbumStackQueryTest, PhotoModeReturnsEveryMatchingFileRegardlessOfStackCollapse) {
  const auto out = project->GetAlbumBrowseService()->ReadAlbumQuery(
      scope, {}, {}, model,
      {.limit_ = 1000, .read_statistics_ = true, .read_occurrence_file_ids_ = true});
  std::vector<sl_element_id_t> ids;
  for (const auto& row : out.rows_) ids.push_back(row.photo_.file_id_);
  EXPECT_EQ(ids, Files({0, 1, 2, 3}));
  EXPECT_EQ(out.occurrence_file_ids_, ids);
  EXPECT_EQ(out.statistics_->total_photo_count_, 4);
  EXPECT_TRUE(out.display_rows.empty());
}

TEST_F(AlbumStackQueryTest, InvalidStoredCoverReportsErrorInsteadOfChoosingATemporaryCover) {
  Execute(std::format("UPDATE PhotoStack SET cover_file_id = {}", files[5]));
  const auto before = StackRows();
  EXPECT_THROW(Read({}, sleeve_filter::BuildCaptureDateBucketFilter(L"2026-06-09")),
               std::runtime_error);
  EXPECT_EQ(StackRows(), before);
  Execute(std::format("UPDATE PhotoStack SET cover_file_id = {}", files[0]));
}

TEST_F(AlbumStackQueryTest, PredicateRunsOnceAndReadsKeepStoredOrganizationAndMetadataUnchanged) {
  Execute("CREATE SEQUENCE stack_filter_calls START 1");
  const auto before   = OrganizationRows();
  const auto metadata = TextRows("SELECT to_json(t)::VARCHAR AS text FROM Image t ORDER BY id");
  const auto out      = project->GetStorage()->GetElementStore().ReadAlbumQuery(
      0, duckorm::expr::raw("nextval('stack_filter_calls') > 0"),
      {.group_field_ = AlbumGroupField::kCaptureDay}, model,
      {.limit_                    = 1000,
       .read_groups_              = true,
       .read_statistics_          = true,
       .read_occurrence_file_ids_ = true,
       .mode                      = AlbumReadMode::kDisplay});
  EXPECT_EQ(out.unique_file_count_, 6);
  EXPECT_EQ(Number(duckorm::expr::raw("SELECT currval('stack_filter_calls')")), 6);
  EXPECT_EQ(OrganizationRows(), before);
  EXPECT_EQ(TextRows("SELECT to_json(t)::VARCHAR AS text FROM Image t ORDER BY id"), metadata);
  const auto identity = Read({}, {}, scope).display_rows[0].identity;
  const auto filtered = Read({}, sleeve_filter::BuildCaptureDateBucketFilter(L"2026-06-09"), scope);
  EXPECT_EQ(filtered.display_rows[0].identity, identity);
  EXPECT_THROW(Read({}, {}, scope, -1), std::invalid_argument);
  EXPECT_THROW(Read({}, {}, scope, 0, 1001), std::invalid_argument);
}
}  // namespace
}  // namespace alcedo

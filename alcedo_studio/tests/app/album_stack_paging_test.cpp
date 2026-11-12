// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <future>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "sleeve/sleeve_filesystem.hpp"
#include "support/album_stack_test_library.hpp"

namespace alcedo {
namespace {
class AlbumStackPagingTest : public test::AlbumStackTestLibrary {
 protected:
  auto Page(const AlbumQueryOptions& options, std::span<const AlbumStackFragmentKey> expanded,
            sl_element_id_t folder = 0, int64_t offset = 0, int64_t limit = 1000,
            const std::optional<FilterNode>&             filter    = std::nullopt,
            const std::optional<SemanticQueryEmbedding>& relevance = std::nullopt)
      -> AlbumQueryResult {
    AlbumQueryRead read{.limit_                    = limit,
                        .read_groups_              = true,
                        .read_statistics_          = true,
                        .read_occurrence_file_ids_ = true,
                        .mode                      = AlbumReadMode::kDisplay,
                        .display_offset            = offset};
    read.expanded_fragments.assign(expanded.begin(), expanded.end());
    return project->GetAlbumBrowseService()->ReadAlbumQuery(folder, filter, options, model, read,
                                                            relevance);
  }

  auto Position(const AlbumQueryOptions& options, std::span<const AlbumStackFragmentKey> expanded,
                sl_element_id_t file, sl_element_id_t folder = 0,
                const std::optional<FilterNode>&    filter    = std::nullopt,
                const std::optional<AlbumGroupKey>& preferred = std::nullopt)
      -> std::optional<AlbumDisplayPosition> {
    return project->GetAlbumBrowseService()->ReadAlbumDisplayPosition(
        folder, filter, options, model, expanded, file, preferred);
  }
};

TEST_F(AlbumStackPagingTest, StackCrossingPageBoundariesKeepsOneRepresentativeAndExactMemberOrder) {
  ASSERT_TRUE(Stacks().SetCover(stack, files[2]).Changed());
  const AlbumQueryOptions options{.sort_field_ = AlbumSortField::kRating};
  const auto              collapsed = Page(options, {});
  const auto              key       = collapsed.display_rows[1].identity.fragment;
  EXPECT_EQ(DisplayIds(collapsed), Files({5, 2, 4}));
  const std::vector            expanded{key};
  std::vector<sl_element_id_t> actual;
  int                          representatives = 0;
  for (int64_t offset = 0; offset < 6; offset += 2) {
    const auto page = Page(options, expanded, 0, offset, 2);
    ASSERT_EQ(page.display_rows.size(), 2);
    EXPECT_EQ(page.display_count, 6);
    EXPECT_EQ(page.first_display, offset);
    EXPECT_TRUE(page.rows_.empty());
    for (const auto& row : page.display_rows) {
      actual.push_back(row.photo.photo_.file_id_);
      if (row.identity.fragment.stack_id.empty()) continue;
      EXPECT_TRUE(row.expanded);
      EXPECT_EQ(row.identity.fragment, key);
      if (row.identity.kind == AlbumDisplayKind::kStackRepresentative) {
        ++representatives;
        EXPECT_EQ(row.identity, collapsed.display_rows[1].identity);
        EXPECT_EQ(row.identity.file_id, 0);
      } else {
        EXPECT_EQ(row.identity.kind, AlbumDisplayKind::kStackMember);
        EXPECT_EQ(row.identity.file_id, row.photo.photo_.file_id_);
      }
    }
  }
  EXPECT_EQ(actual, Files({5, 2, 0, 1, 3, 4}));
  EXPECT_EQ(representatives, 1);
  EXPECT_TRUE(Page(options, expanded, 0, 6, 2).display_rows.empty());
}

TEST_F(AlbumStackPagingTest, LargeCollapsedStackLoadsOnlyItsRepresentativeMetadata) {
  std::vector<library_search_test::SyntheticImageSpec> specs;
  for (int i = 0; i < 9996; ++i) {
    specs.push_back({.file_name_   = std::format(L"large{}.jpg", i),
                     .image_path_  = std::format(L"D:/large/large{}.jpg", i),
                     .is_raw_file_ = false});
  }
  const auto added = library_search_test::SyntheticLibraryBuilder(*project).AddFiles(specs);
  ASSERT_EQ(added.size(), 9996);
  ASSERT_TRUE(Stacks().AddMembers(stack, added).Changed());
  ASSERT_TRUE(project->GetSleeveService()
                  ->Write<void>([&](FileSystem& fs) {
                    for (const auto file : added) fs.LinkFileToFolder(file, scope);
                  })
                  .success_);
  // A metadata-only page does not request the optional full raw ID list.
  auto           browse = project->GetAlbumBrowseService();
  AlbumQueryRead read{.limit_ = 1000, .mode = AlbumReadMode::kDisplay};
  const auto     collapsed = browse->ReadAlbumQuery(scope, {}, {}, model, read);
  EXPECT_EQ(collapsed.unique_file_count_, 10000);
  EXPECT_EQ(collapsed.display_count, 1);
  ASSERT_EQ(collapsed.display_rows.size(), 1);
  EXPECT_EQ(collapsed.display_rows[0].photo.photo_.file_id_, files[0]);
  EXPECT_EQ(collapsed.display_rows[0].stack_total, 10000);
  EXPECT_EQ(collapsed.display_rows[0].fragment_match_count, 10000);
  EXPECT_TRUE(collapsed.rows_.empty());
  EXPECT_TRUE(collapsed.occurrence_file_ids_.empty());
  const auto key          = collapsed.display_rows[0].identity.fragment;
  read.expanded_fragments = {key};
  read.display_offset     = 9000;
  const auto deep         = browse->ReadAlbumQuery(scope, {}, {}, model, read);
  EXPECT_EQ(deep.display_count, 10000);
  ASSERT_EQ(deep.display_rows.size(), 1000);
  EXPECT_EQ(deep.display_rows.front().photo.photo_.file_id_, added[8996]);
  EXPECT_EQ(deep.display_rows.back().photo.photo_.file_id_, added.back());
  EXPECT_TRUE(deep.rows_.empty());
  EXPECT_TRUE(deep.occurrence_file_ids_.empty());
  read.display_offset = 10000;
  EXPECT_TRUE(browse->ReadAlbumQuery(scope, {}, {}, model, read).display_rows.empty());
  read.limit_ = 1001;
  EXPECT_THROW((void)browse->ReadAlbumQuery(scope, {}, {}, model, read), std::invalid_argument);

  // First and ten warm samples of the same real service reads; output sizes are asserted.
  const auto measure = [&](const char* name, auto operation) {
    std::vector<double> times;
    for (int i = 0; i < 11; ++i) {
      const auto start = std::chrono::steady_clock::now();
      operation();
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      if (i)
        times.push_back(ms);
      else
        std::cout << name << " first_ms=" << ms << "\n";
    }
    std::sort(times.begin(), times.end());
    std::cout << name << " median_ms=" << times[5] << " p95_ms=" << times[9] << "\n";
  };
  measure("large collapsed page", [&] {
    EXPECT_EQ(browse
                  ->ReadAlbumQuery(scope, {}, {}, model,
                                   {.limit_ = 1000, .mode = AlbumReadMode::kDisplay})
                  .display_rows.size(),
              1);
  });
  read.limit_         = 1000;
  read.display_offset = 9000;
  measure("large expanded deep page", [&] {
    EXPECT_EQ(browse->ReadAlbumQuery(scope, {}, {}, model, read).display_rows.size(), 1000);
  });
  measure("large collapsed reveal", [&] {
    const auto result =
        browse->ReadAlbumDisplayPosition(scope, {}, {}, model, {}, added.back(), {});
    ASSERT_TRUE(result);
    EXPECT_EQ(result->display_index, 0);
    EXPECT_TRUE(result->hidden_by_collapse);
  });
  measure("large matching fragment ids", [&] {
    EXPECT_EQ(browse->ReadAlbumMatchingFragmentIds(scope, {}, {}, model, key).size(), 10000);
  });
}

TEST_F(AlbumStackPagingTest, DisplayPositionAndDisplayRangeIdsAgreeWithPagedRows) {
  auto browse = project->GetAlbumBrowseService();
  for (const auto group :
       {AlbumGroupField::kNone, AlbumGroupField::kCaptureDay, AlbumGroupField::kLabels,
        AlbumGroupField::kRating, AlbumGroupField::kImportDay, AlbumGroupField::kCameraModel,
        AlbumGroupField::kLens, AlbumGroupField::kEditDay}) {
    const AlbumQueryOptions            options{.sort_field_          = AlbumSortField::kRating,
                                               .sort_direction_      = SortDirection::kDescending,
                                               .group_field_         = group,
                                               .local_day_time_zone_ = "UTC"};
    const auto                         collapsed = Page(options, {});
    std::vector<AlbumStackFragmentKey> expanded;
    for (const auto& row : collapsed.display_rows) {
      if (!row.identity.fragment.stack_id.empty()) expanded.push_back(row.identity.fragment);
    }
    const auto all = Page(options, expanded);
    for (size_t i = 0; i < all.display_rows.size(); ++i) {
      const auto& row = all.display_rows[i];
      const auto  position =
          Position(options, expanded, row.photo.photo_.file_id_, 0, {}, row.photo.group_key_);
      ASSERT_TRUE(position);
      EXPECT_EQ(position->display_index, i);
      EXPECT_EQ(position->identity, row.identity);
      EXPECT_FALSE(position->hidden_by_collapse);
      EXPECT_EQ(browse->ReadAlbumDisplayFileIds(0, {}, options, model, expanded, i, i + 1),
                (std::vector{row.photo.photo_.file_id_}));
      EXPECT_EQ(DisplayIds(Page(options, expanded, 0, i, 1)),
                (std::vector{row.photo.photo_.file_id_}));
    }
    EXPECT_EQ(browse->ReadAlbumDisplayFileIds(0, {}, options, model, expanded, 0, 1000),
              DisplayIds(all));
  }
}

TEST_F(AlbumStackPagingTest, MatchingMemberRevealLocatesItsCollapsedFragment) {
  const auto filter    = sleeve_filter::BuildCaptureDateBucketFilter(L"2026-06-09");
  const auto collapsed = Page({}, {}, scope, 0, 1, filter);
  const auto key       = collapsed.display_rows[0].identity.fragment;
  const auto hidden    = Position({}, {}, files[2], scope, filter);
  ASSERT_TRUE(hidden);
  EXPECT_EQ(hidden->display_index, 0);
  EXPECT_EQ(hidden->identity, collapsed.display_rows[0].identity);
  EXPECT_TRUE(hidden->hidden_by_collapse);
  EXPECT_FALSE(Position({}, {}, files[0], scope, filter));
  const std::vector expanded{key};
  const auto        shown = Position({}, expanded, files[2], scope, filter);
  ASSERT_TRUE(shown);
  EXPECT_EQ(shown->display_index, 1);
  EXPECT_EQ(shown->identity.kind, AlbumDisplayKind::kStackMember);
  EXPECT_EQ(shown->identity.file_id, files[2]);
  EXPECT_FALSE(shown->hidden_by_collapse);
}

TEST_F(AlbumStackPagingTest, ExpandOneDayFragmentDoesNotExpandOtherDays) {
  const AlbumQueryOptions options{.group_field_ = AlbumGroupField::kCaptureDay};
  const auto              collapsed = Page(options, {}, scope);
  const auto              key       = collapsed.display_rows[1].identity.fragment;
  const std::vector       expanded{key};
  const auto              out = Page(options, expanded, scope);
  EXPECT_EQ(DisplayIds(out), Files({3, 1, 2, 0}));
  ASSERT_EQ(out.groups_.size(), 3);
  EXPECT_EQ(out.groups_[0].display_count, 1);
  EXPECT_EQ(out.groups_[1].display_count, 2);
  EXPECT_EQ(out.groups_[2].display_count, 1);
  EXPECT_EQ(out.groups_[2].first_display, 3);
  EXPECT_FALSE(out.display_rows.front().expanded);
  EXPECT_FALSE(out.display_rows.back().expanded);
  EXPECT_EQ(out.display_rows.front().identity, collapsed.display_rows.front().identity);
  EXPECT_EQ(out.display_rows.back().identity, collapsed.display_rows.back().identity);
}

TEST_F(AlbumStackPagingTest, FragmentSelectionIncludesUnloadedMatchesAndExcludesOtherGroups) {
  const AlbumQueryOptions options{.group_field_ = AlbumGroupField::kCaptureDay};
  const auto              first = Page(options, {}, scope, 1, 1);
  ASSERT_EQ(first.display_rows.size(), 1);
  const auto key = first.display_rows[0].identity.fragment;
  EXPECT_EQ(project->GetAlbumBrowseService()->ReadAlbumMatchingFragmentIds(scope, {}, options,
                                                                           model, key),
            Files({1, 2}));
  const std::vector expanded{key};
  EXPECT_EQ(Page(options, expanded, scope, 1, 1).display_rows.size(), 1);
  const auto filter = sleeve_filter::BuildCameraModelBucketFilter(L"Canon");
  EXPECT_EQ(project->GetAlbumBrowseService()->ReadAlbumMatchingFragmentIds(scope, filter, options,
                                                                           model, key),
            Files({2}));
  EXPECT_EQ(Cover(stack), files[0]);
}

TEST_F(AlbumStackPagingTest, RawSelectionAndStatisticsDoNotDependOnExpansion) {
  const AlbumQueryOptions            options{.group_field_ = AlbumGroupField::kCaptureDay};
  const auto                         before    = OrganizationRows();
  const auto                         collapsed = Page(options, {}, scope);
  const std::vector                  partial{collapsed.display_rows[1].identity.fragment};
  std::vector<AlbumStackFragmentKey> all;
  for (const auto& row : collapsed.display_rows) all.push_back(row.identity.fragment);
  for (const auto& expanded : {std::vector<AlbumStackFragmentKey>{}, partial, all}) {
    const auto out = Page(options, expanded, scope);
    EXPECT_EQ(out.unique_file_count_, 4);
    EXPECT_EQ(out.occurrence_count_, 4);
    EXPECT_EQ(out.occurrence_file_ids_, collapsed.occurrence_file_ids_);
    EXPECT_EQ(out.statistics_->total_photo_count_, 4);
    const auto buckets = [](const auto& values) {
      std::vector<std::pair<std::string, int>> result;
      for (const auto& value : values) result.emplace_back(value.label_, value.count_);
      return result;
    };
    EXPECT_EQ(buckets(out.statistics_->date_stats_), buckets(collapsed.statistics_->date_stats_));
    EXPECT_EQ(buckets(out.statistics_->camera_stats_),
              buckets(collapsed.statistics_->camera_stats_));
    EXPECT_EQ(buckets(out.statistics_->lens_stats_), buckets(collapsed.statistics_->lens_stats_));
    EXPECT_EQ(buckets(out.statistics_->label_stats_), buckets(collapsed.statistics_->label_stats_));
    EXPECT_EQ(buckets(out.statistics_->rating_stats_),
              buckets(collapsed.statistics_->rating_stats_));
    EXPECT_EQ(out.statistics_->color_label_counts_, collapsed.statistics_->color_label_counts_);
    EXPECT_EQ(out.statistics_->delete_mark_count_, collapsed.statistics_->delete_mark_count_);
    for (size_t i = 0; i < out.groups_.size(); ++i) {
      EXPECT_EQ(out.groups_[i].photo_count_, collapsed.groups_[i].photo_count_);
      EXPECT_EQ(out.groups_[i].first_occurrence_, collapsed.groups_[i].first_occurrence_);
    }
    EXPECT_EQ(
        project->GetAlbumBrowseService()->ReadAlbumFileIds(scope, {}, options, model, 0, 1000),
        collapsed.occurrence_file_ids_);
  }
  EXPECT_EQ(OrganizationRows(), before);
}

TEST_F(AlbumStackPagingTest, DuplicateAbsentAndUnknownExpansionKeysStayScopedAndTyped) {
  const auto              second = Create({4, 5}, 4);
  const AlbumQueryOptions options{.group_field_ = AlbumGroupField::kCameraModel};
  const auto              collapsed = Page(options, {});
  const auto              unknown   = collapsed.display_rows.back().identity.fragment;
  EXPECT_EQ(unknown.stack_id, second);
  EXPECT_EQ(unknown.group_key, AlbumGroupKey{std::monostate{}});
  auto absent      = unknown;
  absent.group_key = std::string("O'Brien");
  const std::vector expanded{unknown, unknown, absent};
  const auto        out = Page(options, expanded);
  EXPECT_EQ(DisplayIds(out), DisplayIds(collapsed));
  EXPECT_TRUE(out.display_rows.back().expanded);
  const auto position =
      Position(options, expanded, files[5], 0, {}, AlbumGroupKey{std::monostate{}});
  ASSERT_TRUE(position);
  EXPECT_EQ(position->identity, out.display_rows.back().identity);
  EXPECT_EQ(position->display_index, out.display_count - 1);
  auto wrong      = unknown;
  wrong.folder_id = scope;
  EXPECT_THROW((void)Page(options, std::vector{wrong}), std::invalid_argument);
  wrong             = unknown;
  wrong.group_field = AlbumGroupField::kNone;
  EXPECT_THROW((void)Page(options, std::vector{wrong}), std::invalid_argument);
  wrong           = unknown;
  wrong.group_key = int64_t{1};
  EXPECT_THROW((void)Page(options, std::vector{wrong}), std::invalid_argument);
  EXPECT_THROW((void)Position(options, {}, files[0], 0, {}, AlbumGroupKey{int64_t{1}}),
               std::invalid_argument);
  auto browse = project->GetAlbumBrowseService();
  EXPECT_THROW((void)browse->ReadAlbumDisplayFileIds(0, {}, options, model, {}, -1, 3),
               std::invalid_argument);
  EXPECT_THROW((void)browse->ReadAlbumDisplayFileIds(0, {}, options, model, {}, 3, 2),
               std::invalid_argument);
  EXPECT_TRUE(browse->ReadAlbumDisplayFileIds(0, {}, options, model, {}, 3, 3).empty());
  EXPECT_TRUE(browse->ReadAlbumMatchingFragmentIds(0, {}, options, model, absent).empty());
}

TEST_F(AlbumStackPagingTest, SemanticDisplayReadsUseOneKeptSetAndRunOnTheCallingWorker) {
  const auto        relevance = Relevance();
  auto              browse    = project->GetAlbumBrowseService();
  const auto        collapsed = Page({}, {}, 0, 0, 1000, {}, relevance);
  const std::vector expanded{collapsed.display_rows[1].identity.fragment};
  const auto        shown = Page({}, expanded, 0, 0, 1000, {}, relevance);
  EXPECT_EQ(DisplayIds(shown), Files({4, 1, 2, 3}));
  EXPECT_EQ(browse->ReadAlbumDisplayFileIds(0, {}, {}, model, expanded, 0, 1000, relevance),
            DisplayIds(shown));
  EXPECT_EQ(browse->ReadAlbumMatchingFragmentIds(0, {}, {}, model, expanded[0], relevance),
            Files({1, 2, 3}));
  EXPECT_FALSE(
      browse->ReadAlbumDisplayPosition(0, {}, {}, model, expanded, files[0], {}, relevance));
  const auto position =
      browse->ReadAlbumDisplayPosition(0, {}, {}, model, expanded, files[3], {}, relevance);
  ASSERT_TRUE(position);
  EXPECT_EQ(position->display_index, 3);
  std::vector<std::pair<std::string, std::thread::id>> observed;
  browse->SetQueryThreadObserver(
      [&](auto operation) { observed.emplace_back(operation, std::this_thread::get_id()); });
  const auto worker =
      std::async(std::launch::async, [&] {
        (void)browse->ReadAlbumDisplayPosition(0, {}, {}, model, expanded, files[1], {}, relevance);
        (void)browse->ReadAlbumDisplayFileIds(0, {}, {}, model, expanded, 0, 2, relevance);
        (void)browse->ReadAlbumMatchingFragmentIds(0, {}, {}, model, expanded[0], relevance);
        return std::this_thread::get_id();
      }).get();
  browse->SetQueryThreadObserver({});
  ASSERT_EQ(observed.size(), 3);
  EXPECT_EQ(observed[0].first, "ReadAlbumDisplayPosition");
  EXPECT_EQ(observed[1].first, "ReadAlbumDisplayFileIds");
  EXPECT_EQ(observed[2].first, "ReadAlbumMatchingFragmentIds");
  for (const auto& operation : observed) EXPECT_EQ(operation.second, worker);
  EXPECT_NE(worker, std::this_thread::get_id());
}

TEST_F(AlbumStackPagingTest, FailedDisplayReadsLeaveRowsUnchangedAndNextReadUsable) {
  auto       browse = project->GetAlbumBrowseService();
  const auto key    = Page({}, {}, scope).display_rows[0].identity.fragment;
  Execute(std::format("UPDATE PhotoStack SET cover_file_id = {} WHERE stack_id = '{}'", files[5],
                      stack));
  const auto before = OrganizationRows();
  EXPECT_THROW((void)Position({}, {}, files[2], scope), std::runtime_error);
  EXPECT_THROW((void)browse->ReadAlbumDisplayFileIds(scope, {}, {}, model, {}, 0, 1000),
               std::runtime_error);
  EXPECT_THROW((void)browse->ReadAlbumMatchingFragmentIds(scope, {}, {}, model, key),
               std::runtime_error);
  EXPECT_EQ(OrganizationRows(), before);
  Execute(std::format("UPDATE PhotoStack SET cover_file_id = {} WHERE stack_id = '{}'", files[0],
                      stack));
  EXPECT_EQ(DisplayIds(Page({}, {}, scope)), Files({0}));
  EXPECT_EQ(browse->ReadAlbumMatchingFragmentIds(scope, {}, {}, model, key), Files({0, 1, 2, 3}));
}
TEST_F(AlbumStackPagingTest, SqlFailureReleasesTheReadTransactionAndPreservesSavedRows) {
  const auto before = OrganizationRows();
  auto       browse = project->GetAlbumBrowseService();
  const auto key    = Page({}, {}, scope).display_rows[0].identity.fragment;
  // Remove one relation only for this fixture, so failure occurs after the match table exists.
  Execute("ALTER TABLE PhotoStack RENAME TO UnavailablePhotoStack");
  EXPECT_THROW((void)Page({}, {}, scope), std::runtime_error);
  EXPECT_THROW((void)Position({}, {}, files[2], scope), std::runtime_error);
  EXPECT_THROW((void)browse->ReadAlbumDisplayFileIds(scope, {}, {}, model, {}, 0, 10),
               std::runtime_error);
  EXPECT_THROW((void)browse->ReadAlbumMatchingFragmentIds(scope, {}, {}, model, key),
               std::runtime_error);
  Execute("ALTER TABLE UnavailablePhotoStack RENAME TO PhotoStack");
  EXPECT_EQ(OrganizationRows(), before);
  EXPECT_EQ(DisplayIds(Page({}, {}, scope)), Files({0}));
  EXPECT_EQ(browse->ReadAlbumMatchingFragmentIds(scope, {}, {}, model, key), Files({0, 1, 2, 3}));
}
TEST_F(AlbumStackPagingTest,
       RelocationReadsReturnOnlyMinimalItemsAndPreserveRepresentativeIdentity) {
  auto           browse = project->GetAlbumBrowseService();
  AlbumQueryRead read{.limit_ = 1, .read_display_items = true, .mode = AlbumReadMode::kDisplay};
  const auto     before = browse->ReadAlbumQuery(0, {}, {}, model, read);
  ASSERT_EQ(before.display_items.size(), 3);
  ASSERT_EQ(before.display_rows.size(), 1);
  const auto identity = before.display_items[0].identity;
  ASSERT_TRUE(Stacks().SetCover(stack, files[2]).Changed());
  read.expanded_fragments = {identity.fragment};
  const auto expanded     = browse->ReadAlbumQuery(0, {}, {}, model, read);
  ASSERT_EQ(expanded.display_items.size(), 6);
  EXPECT_EQ(expanded.display_rows.size(), 1);
  EXPECT_TRUE(expanded.rows_.empty());
  EXPECT_EQ(expanded.display_items[0].identity, identity);
  std::vector<sl_element_id_t> ids;
  for (const auto& item : expanded.display_items) ids.push_back(item.file_id);
  EXPECT_EQ(ids, Files({2, 0, 1, 3, 4, 5}));
  read.mode = AlbumReadMode::kPhotos;
  read.expanded_fragments.clear();
  EXPECT_THROW((void)browse->ReadAlbumQuery(0, {}, {}, model, read), std::invalid_argument);
}
}  // namespace
TEST_F(AlbumStackPagingTest,
       FiftyThousandPhotosWithLargeAndSmallStacksKeepEveryMetadataPageBounded) {
  ASSERT_TRUE(Stacks().UngroupStack(stack).Changed());
  std::vector<library_search_test::SyntheticImageSpec> specs;
  for (int i = 0; i < 49994; ++i)
    specs.push_back({.file_name_   = std::format(L"resource{}.jpg", i),
                     .image_path_  = std::format(L"D:/resource/resource{}.jpg", i),
                     .is_raw_file_ = false});
  auto added = library_search_test::SyntheticLibraryBuilder(*project).AddFiles(specs);
  std::vector<sl_element_id_t> all = files;
  all.insert(all.end(), added.begin(), added.end());
  std::sort(all.begin(), all.end());
  ASSERT_EQ(all.size(), 50000);
  const std::span<const sl_element_id_t> large(all.data(), 10000);
  const auto large_stack = Stacks().CreateStack(large, all.front()).stack_id;
  // Bulk performance fixture input for 20,000 independent two-photo stacks. Command
  // integration tests separately exercise every organization write through its production owner.
  const auto remaining   = std::format(
      "SELECT id, (ROW_NUMBER() OVER (ORDER BY id)-1) AS n FROM Element WHERE type = 0 AND id > {}",
      all[9999]);
  Execute("INSERT INTO PhotoStack (stack_id, cover_file_id) WITH ordered AS (" + remaining +
          ") SELECT printf('00000000-0000-4000-8000-%012d', (n//2)::BIGINT), MIN(id) FROM ordered "
          "GROUP BY n//2");
  Execute("INSERT INTO PhotoStackMember (file_id, stack_id, member_order) WITH ordered AS (" +
          remaining +
          ") SELECT id, printf('00000000-0000-4000-8000-%012d', (n//2)::BIGINT), n%2 FROM ordered");
  project->GetStorage()->GetPhotoStackStore().ValidateAll();
  const auto browse  = project->GetAlbumBrowseService();
  const auto measure = [&](const char* name, auto operation) {
    std::vector<double> times;
    for (int i = 0; i < 11; ++i) {
      const auto start = std::chrono::steady_clock::now();
      operation();
      const auto ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      if (i)
        times.push_back(ms);
      else
        std::cout << name << " first_ms=" << ms << "\n";
    }
    std::sort(times.begin(), times.end());
    std::cout << name << " median_ms=" << times[5] << " p95_ms=" << times[9] << "\n";
  };
  measure("50k mixed collapsed first page", [&] {
    const auto result =
        browse->ReadAlbumQuery(0, {}, {}, model, {.limit_ = 1000, .mode = AlbumReadMode::kDisplay});
    EXPECT_EQ(result.unique_file_count_, 50000);
    EXPECT_EQ(result.display_count, 20001);
    ASSERT_EQ(result.display_rows.size(), 1000);
    EXPECT_EQ(result.display_rows.front().stack_total, 10000);
    EXPECT_TRUE(result.rows_.empty());
    EXPECT_TRUE(result.occurrence_file_ids_.empty());
  });
  EXPECT_EQ(browse->ReadAlbumStackFragmentKeys(0, {}, {}, model).size(), 20001);
  const AlbumStackFragmentKey large_key{0, AlbumGroupField::kNone, {}, large_stack};
  measure("50k mixed large expanded deep page", [&] {
    const auto result = browse->ReadAlbumQuery(0, {}, {}, model,
                                               {.limit_             = 1000,
                                                .mode               = AlbumReadMode::kDisplay,
                                                .display_offset     = 9000,
                                                .expanded_fragments = {large_key}});
    EXPECT_EQ(result.display_count, 30000);
    ASSERT_EQ(result.display_rows.size(), 1000);
    EXPECT_EQ(result.display_rows.front().file_id, all[9000]);
    EXPECT_EQ(result.display_rows.back().file_id, all[9999]);
    EXPECT_TRUE(result.display_items.empty());
    EXPECT_TRUE(result.rows_.empty());
  });
}
}  // namespace alcedo

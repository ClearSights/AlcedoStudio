// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#include <gtest/gtest.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "app/library_search_test_support.hpp"
#include "storage/mapper/duckorm/duckdb_statement.hpp"
#include "support/storage_test_access.hpp"
#include "ui/album_backend_populated_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
class LibraryDisplayPublicationTests : public ApplicationModuleHostTestFixture {
 protected:
  std::unique_ptr<ApplicationModuleHost>    host;
  std::vector<sl_element_id_t>              files;
  std::string                               stack;
  std::unique_ptr<QAbstractItemModelTester> photosTester, displayTester, sectionsTester;
  void                                      SetUp() override {
    ApplicationModuleHostTestFixture::SetUp();
    host = std::make_unique<ApplicationModuleHost>();
    const std::vector<LibraryPhotoSpec> specs{
        {.model_ = "Canon", .date_time_ = "2026-06-08 09:00:00"},
        {.model_ = "Nikon", .date_time_ = "2026-06-09 08:00:00"},
        {.model_ = "Canon", .date_time_ = "2026-06-09 10:00:00"},
        {.model_ = "Nikon", .date_time_ = "2026-06-10 11:00:00"},
        {.model_ = "Fuji", .date_time_ = "2026-06-11 12:00:00"},
        {.model_ = "", .date_time_ = ""}};
    const auto packed = CreatePopulatedPackedProject(temp_dir_, {}, 0, specs);
    ASSERT_TRUE(packed);
    ASSERT_TRUE(LoadPackedProject(*host, packed->packed_path_));
    for (const auto& image : packed->images_) files.push_back(image.file_id_);
    const std::vector selected{files[0], files[1], files[2], files[3]};
    stack = host->project()
                ->handler()
                .project()
                ->GetPhotoStackService()
                ->CreateStack(selected, files[0])
                .stack_id;
    host->library()->SetStackDisplayEnabled(true);
    ASSERT_TRUE(WaitForLibraryQuery(*host));
    ASSERT_EQ(host->library_display()->DisplayCount(), 3);
    EXPECT_EQ(host->property("libraryDisplayModel").value<QObject*>(), host->library_display());
    photosTester = std::make_unique<QAbstractItemModelTester>(
        &host->library()->model(), QAbstractItemModelTester::FailureReportingMode::Fatal);
    displayTester = std::make_unique<QAbstractItemModelTester>(
        host->library_display(), QAbstractItemModelTester::FailureReportingMode::Fatal);
    sectionsTester = std::make_unique<QAbstractItemModelTester>(
        &host->library()->section_model(), QAbstractItemModelTester::FailureReportingMode::Fatal);
  }
  void TearDown() override {
    sectionsTester.reset();
    displayTester.reset();
    photosTester.reset();
    host.reset();
    ApplicationModuleHostTestFixture::TearDown();
  }
  auto Settle() -> bool { return WaitForLibraryQuery(*host); }
  auto DisplayIds() const -> std::vector<sl_element_id_t> {
    std::vector<sl_element_id_t> ids;
    for (const auto& [start, items] : host->library_display()->pages())
      for (const auto& item : items) ids.push_back(item.file_id);
    return ids;
  }
  auto Key(const AlbumGroupKey& group = {}) const -> AlbumStackFragmentKey {
    for (const auto& [start, items] : host->library_display()->pages())
      for (const auto& item : items)
        if (item.identity.fragment.stack_id == stack && item.identity.fragment.group_key == group)
          return item.identity.fragment;
    ADD_FAILURE() << "The expected matching fragment is absent";
    return {};
  }
  void CheckComplete() const {
    auto* library = host->library();
    auto* display = host->library_display();
    EXPECT_EQ(library->TotalCount(), host->stats()->TotalPhotoCount());
    EXPECT_EQ(library->TotalCount(), library->model().totalCountInt());
    EXPECT_EQ(display->LoadedUniqueFileIds().size(), library->model().items().size());
    for (const auto& [start, items] : display->pages())
      for (const auto& item : items) {
        const auto* photo = library->FindAlbumItem(item.file_id);
        EXPECT_NE(photo, nullptr);
        if (photo) EXPECT_EQ(photo->file_id, item.file_id);
      }
    if (library->Grouped()) {
      EXPECT_EQ(library->section_model().OccurrenceTotal(), display->DisplayCount());
      EXPECT_EQ(library->section_model().UniqueFileCount(), library->TotalCount());
    }
  }
  void SetStoredCover(sl_element_id_t file) {
    auto guard =
        alcedo::StorageTestAccess::GetDatabase(*host->project()->handler().project()->GetStorage())
            .GetConnectionGuard();
    auto               lock = guard.Lock();
    duckorm::Statement statement(
        guard.conn_,
        std::format("UPDATE PhotoStack SET cover_file_id = {} WHERE stack_id = '{}'", file, stack));
    statement.Execute();
  }
};

TEST_F(LibraryDisplayPublicationTests, PublicationObserversSeeOneCompletePhotoAndDisplayResult) {
  auto*                      library = host->library();
  auto*                      display = host->library_display();
  std::map<std::string, int> notifications;
  const auto                 check = [&](const char* signal) {
    ++notifications[signal];
    CheckComplete();
  };
  auto context = std::make_unique<QObject>();
  QObject::connect(&library->model(), &QAbstractItemModel::modelAboutToBeReset, context.get(),
                   [&] { check("photo about"); });
  QObject::connect(&library->model(), &QAbstractItemModel::modelReset, context.get(),
                   [&] { check("photo reset"); });
  QObject::connect(&library->model(), &AlbumThumbnailModel::countChanged, context.get(),
                   [&] { check("photo count"); });
  QObject::connect(&library->model(), &AlbumThumbnailModel::totalCountChanged, context.get(),
                   [&] { check("photo total"); });
  QObject::connect(display, &QAbstractItemModel::modelReset, context.get(),
                   [&] { check("display reset"); });
  QObject::connect(display, &LibraryDisplayModel::PagesChanged, context.get(),
                   [&] { check("display pages"); });
  QObject::connect(&library->section_model(), &AlbumSectionModel::SectionsChanged, context.get(),
                   [&] { check("sections"); });
  QObject::connect(&library->section_model(), &AlbumSectionModel::PagesChanged, context.get(),
                   [&] { check("section pages"); });
  QObject::connect(host->stats(), &StatsEngine::StatsChanged, context.get(),
                   [&] { check("statistics"); });
  QObject::connect(library, &LibraryModule::CountsChanged, context.get(), [&] { check("counts"); });
  QObject::connect(library, &LibraryModule::PresentationChanged, context.get(),
                   [&] { check("presentation"); });
  QObject::connect(library, &LibraryModule::ThumbnailsChanged, context.get(),
                   [&] { check("thumbnails"); });
  std::atomic<int> gui_reads{0};
  const auto       browse = host->project()->handler().project()->GetAlbumBrowseService();
  const auto       gui    = QCoreApplication::instance()->thread();
  browse->SetQueryThreadObserver([&](auto) {
    if (QThread::currentThread() == gui) ++gui_reads;
  });
  host->stats()->ToggleStatsFilter("date", "2026-06-09");
  ASSERT_TRUE(Settle());
  host->stats()->ClearStatsFilter();
  ASSERT_TRUE(Settle());
  library->SetInspectorGrouping("date", true);
  ASSERT_TRUE(Settle());
  browse->SetQueryThreadObserver({});
  EXPECT_EQ(gui_reads, 0);
  for (const auto& [name, count] : notifications) EXPECT_GT(count, 0) << name;
  EXPECT_GE(notifications.size(), 10);
}

TEST_F(LibraryDisplayPublicationTests,
       TemporaryCoverChangeKeepsFragmentIdentityWithoutSelectingTheNewCover) {
  auto*       library  = host->library();
  const auto  identity = host->library_display()->ItemAt(0)->identity;
  const auto* saved    = library->FindAlbumItem(files[0]);
  ASSERT_NE(saved, nullptr);
  library->selection()->SetImageSelected(files[0], saved->image_id, saved->file_name, false, true);
  host->stats()->ToggleStatsFilter("date", "2026-06-09");
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(), (std::vector{files[1]}));
  EXPECT_EQ(host->library_display()->ItemAt(0)->identity, identity);
  EXPECT_TRUE(host->library_display()->ItemAt(0)->temporary_cover);
  EXPECT_TRUE(library->selection()->IsSelected(files[0]));
  EXPECT_FALSE(library->selection()->IsSelected(files[1]));
}

TEST_F(LibraryDisplayPublicationTests, ClearingFilterRestoresSavedCoverAndPreviousExpansion) {
  auto*      display = host->library_display();
  const auto flat    = Key();
  display->SetStackExpanded(flat, true);
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(), files);
  host->stats()->ToggleStatsFilter("date", "2026-06-09");
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(), (std::vector{files[1], files[2]}));
  host->stats()->ClearStatsFilter();
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(), files);
  EXPECT_TRUE(display->IsStackExpanded(flat));
  host->library()->SetInspectorGrouping("date", true);
  ASSERT_TRUE(Settle());
  const auto tuesday = Key(std::string("2026-06-09"));
  display->SetStackExpanded(tuesday, true);
  ASSERT_TRUE(Settle());
  host->stats()->ToggleStatsFilter("date", "2026-06-08");
  ASSERT_TRUE(Settle());
  EXPECT_TRUE(display->IsStackExpanded(tuesday));
  EXPECT_EQ(DisplayIds(), (std::vector{files[0]}));
  host->stats()->ClearStatsFilter();
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(),
            (std::vector{files[4], files[3], files[1], files[2], files[0], files[5]}));
  EXPECT_TRUE(display->IsStackExpanded(tuesday));
}

TEST_F(LibraryDisplayPublicationTests, DisplayPagesHaveOneOwnerAndReleaseHiddenThumbnailPins) {
  auto*      library = host->library();
  const auto key     = Key();
  host->library_display()->SetStackExpanded(key, true);
  ASSERT_TRUE(Settle());
  const auto* member = library->FindAlbumItem(files[1]);
  ASSERT_NE(member, nullptr);
  library->SetOccurrenceThumbnailVisible({}, true, files[1], member->image_id, true, 256);
  EXPECT_TRUE(library->thumbs().IsThumbnailPinned(files[1]));
  host->library_display()->SetStackExpanded(key, false);
  ASSERT_TRUE(Settle());
  EXPECT_FALSE(library->thumbs().IsThumbnailPinned(files[1]));
  EXPECT_EQ(library->FindAlbumItem(files[1]), nullptr);
  EXPECT_EQ(library->section_model().LoadedPageCount(), host->library_display()->LoadedPageCount());
  EXPECT_EQ(host->library_display()->LoadedOccurrenceCount(), 3);
  EXPECT_EQ(library->model().items().size(), 3);
}

TEST_F(LibraryDisplayPublicationTests,
       QueryFailureRetainsAcceptedOwnersAndRetryPublishesTheValidResult) {
  auto*      library = host->library();
  const auto before  = DisplayIds();
  const auto key     = Key();
  SetStoredCover(files[5]);
  library->RequestLibraryRefresh();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  do {
    ProcessEvents(20);
  } while (library->QueryUpdating() && std::chrono::steady_clock::now() < deadline);
  EXPECT_FALSE(library->QueryError().isEmpty());
  EXPECT_EQ(DisplayIds(), before);
  CheckComplete();
  EXPECT_EQ(Key(), key);
  SetStoredCover(files[0]);
  library->RetryLibraryQuery();
  ASSERT_TRUE(Settle());
  EXPECT_TRUE(library->QueryError().isEmpty());
  EXPECT_EQ(DisplayIds(), before);
}

TEST_F(LibraryDisplayPublicationTests,
       InPlaceCoverChangeRelocatesTheRepresentativeAndLoadsOnlyItsPhotoValues) {
  const auto identity = host->library_display()->ItemAt(0)->identity;
  ASSERT_TRUE(host->project()
                  ->handler()
                  .project()
                  ->GetPhotoStackService()
                  ->SetCover(stack, files[1])
                  .Changed());
  host->library()->RequestLibraryContentRefresh();
  ASSERT_TRUE(Settle());
  EXPECT_EQ(DisplayIds(), (std::vector{files[1], files[4], files[5]}));
  EXPECT_EQ(host->library_display()->ItemAt(0)->identity, identity);
  EXPECT_NE(host->library()->FindAlbumItem(files[1]), nullptr);
  EXPECT_EQ(host->library()->FindAlbumItem(files[0]), nullptr);
  CheckComplete();
}

TEST_F(LibraryDisplayPublicationTests,
       ExpansionChangedAfterSubmissionSurvivesTheOlderWorkerResult) {
  struct Pause {
    std::mutex              mutex;
    std::condition_variable changed;
    bool                    armed = true, entered = false, released = false;
  };
  const auto pause  = std::make_shared<Pause>();
  const auto browse = host->project()->handler().project()->GetAlbumBrowseService();
  browse->SetQueryThreadObserver([pause](auto operation) {
    if (operation != "ReadAlbumQuery") return;
    std::unique_lock lock(pause->mutex);
    if (!std::exchange(pause->armed, false)) return;
    pause->entered = true;
    pause->changed.notify_all();
    pause->changed.wait(lock, [&] { return pause->released; });
  });
  struct Release {
    std::shared_ptr<Pause> pause;
    ~Release() {
      {
        std::lock_guard lock(pause->mutex);
        pause->released = true;
      }
      pause->changed.notify_all();
    }
  } release{pause};
  const auto key = Key();
  host->library()->RequestLibraryRefresh();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool       entered  = false;
  while (std::chrono::steady_clock::now() < deadline) {
    ProcessEvents(10);
    {
      std::lock_guard lock(pause->mutex);
      entered = pause->entered;
    }
    if (entered) break;
  }
  EXPECT_TRUE(entered);
  host->library_display()->SetStackExpanded(key, true);
  ProcessEvents(50);
  {
    std::lock_guard lock(pause->mutex);
    pause->released = true;
  }
  pause->changed.notify_all();
  ASSERT_TRUE(Settle());
  browse->SetQueryThreadObserver({});
  EXPECT_TRUE(host->library_display()->IsStackExpanded(key));
  EXPECT_EQ(DisplayIds(), files);
}
TEST_F(LibraryDisplayPublicationTests, DisplayPageEvictionReleasesOnlyTheUnloadedPhotoPins) {
  std::vector<library_search_test::SyntheticImageSpec> specs;
  for (int i = 0; i < 3500; ++i)
    specs.push_back({.file_name_   = std::format(L"paged{}.jpg", i),
                     .image_path_  = std::format(L"D:/paged/paged{}.jpg", i),
                     .is_raw_file_ = false});
  ASSERT_EQ(library_search_test::SyntheticLibraryBuilder(*host->project()->handler().project())
                .AddFiles(specs)
                .size(),
            3500);
  auto* library = host->library();
  library->RequestLibraryRefresh();
  ASSERT_TRUE(Settle());
  const auto* photo = library->FindAlbumItem(files[0]);
  ASSERT_NE(photo, nullptr);
  library->SetThumbnailVisible(files[0], photo->image_id, true, 256);
  EXPECT_TRUE(library->thumbs().IsThumbnailPinned(files[0]));
  for (int index : {1000, 2000, 3000}) {
    ASSERT_TRUE(library->LoadThumbnailsThroughIndex(index));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    do {
      ProcessEvents(20);
    } while (library->model().loading() && std::chrono::steady_clock::now() < deadline);
    ASSERT_FALSE(library->model().loading());
    EXPECT_NE(host->library_display()->ItemAt(index), nullptr);
  }
  EXPECT_EQ(host->library_display()->LoadedPageCount(), 3);
  EXPECT_LE(library->model().count(), 3000);
  EXPECT_EQ(host->library_display()->ItemAt(0), nullptr);
  EXPECT_FALSE(library->thumbs().IsThumbnailPinned(files[0]));
  CheckComplete();
}
TEST_F(LibraryDisplayPublicationTests,
       InPlaceCompletionNotificationsExposeTheNewCountsAndSectionGeometryTogether) {
  auto* library = host->library();
  library->SetInspectorGrouping("date", true);
  ASSERT_TRUE(Settle());
  auto       context       = std::make_unique<QObject>();
  int        notifications = 0;
  const auto check         = [&] {
    ++notifications;
    CheckComplete();
  };
  QObject::connect(&library->model(), &AlbumThumbnailModel::countChanged, context.get(), check);
  QObject::connect(&library->model(), &AlbumThumbnailModel::totalCountChanged, context.get(),
                   check);
  QObject::connect(&library->section_model(), &AlbumSectionModel::SectionsChanged, context.get(),
                   check);
  QObject::connect(host->library_display(), &LibraryDisplayModel::PagesChanged, context.get(),
                   check);
  QObject::connect(host->stats(), &StatsEngine::StatsChanged, context.get(), check);
  const std::vector<library_search_test::SyntheticImageSpec> added{
      {.file_name_   = L"new-day.jpg",
       .image_path_  = L"D:/photos/new-day.jpg",
       .date_time_   = "2026-06-12 12:00:00",
       .is_raw_file_ = false}};
  ASSERT_EQ(library_search_test::SyntheticLibraryBuilder(*host->project()->handler().project())
                .AddFiles(added)
                .size(),
            1);
  library->RequestLibraryContentRefresh();
  ASSERT_TRUE(Settle());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  do {
    ProcessEvents(10);
  } while (library->model().loading() && std::chrono::steady_clock::now() < deadline);
  EXPECT_EQ(library->TotalCount(), 7);
  EXPECT_EQ(library->section_model().GroupCount(), 6);
  EXPECT_GE(notifications, 4);
  CheckComplete();
}
}  // namespace
}  // namespace alcedo::ui::test

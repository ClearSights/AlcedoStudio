// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <QImage>
#include <QSignalSpy>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>

#include "app/library_search_test_support.hpp"
#include "storage/mapper/duckorm/duckdb_statement.hpp"
#include "support/storage_test_access.hpp"
#include "ui/photo_stack_project_fixture.hpp"

namespace alcedo::ui::test {
namespace {
/// Deterministic text vectors; storage and the real query worker compute every matching File.
class StackFilterSearchProvider final : public SemanticSearchProvider {
 public:
  auto EmbedQuery(const std::wstring&) -> SemanticQueryEmbedding override {
    SemanticQueryEmbedding query{.model_key_ = "stack-integration-model",
                                 .dimension_ = kSemanticEmbeddingDim,
                                 .query_     = std::vector<float>(kSemanticEmbeddingDim, 0),
                                 .neutral_   = std::vector<float>(kSemanticEmbeddingDim, 0)};
    query.query_.front()  = 1;
    query.neutral_.back() = 1;
    return query;
  }
};
class LibraryPhotoStackIntegrationTest : public PhotoStackProjectFixture {
 protected:
  std::string stack;
  void        SetUp() override {
    PhotoStackProjectFixture::SetUp();
    host->library()->SetStackDisplayEnabled(true);
    ASSERT_TRUE(WaitForLibraryQuery(*host));
    // Exercise the public command entry that the production context menu uses.
    host->library()->selection()->Replace(host->library_display()->getItemsInRange(0, 2));
    const auto key = host->library_display()->getItemAt(0).value("occurrenceKey").toString();
    host->photo_stacks()->RunAction("stack-group", key);
    Wait();
    ASSERT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kSaved);
    stack = result->change.stack_id;
    ASSERT_TRUE(WaitForLibraryQuery(*host));
  }
  /// Actual persisted scalar, read independently of the production cover evaluator.
  auto Cover() -> sl_element_id_t {
    auto guard =
        alcedo::StorageTestAccess::GetDatabase(*host->project()->handler().project()->GetStorage())
            .GetConnectionGuard();
    auto               lock = guard.Lock();
    duckorm::Statement statement(guard.conn_,
                                 "SELECT cover_file_id FROM PhotoStack WHERE stack_id = ?");
    statement.Bind(1, stack);
    return static_cast<sl_element_id_t>(statement.Execute().Get<int64_t>(0, 0));
  }
  auto Members() -> std::vector<sl_element_id_t> {
    return host->project()->handler().project()->GetPhotoStackService()->ReadMemberIds(stack);
  }
  auto Key() -> QString {
    for (int i = 0; i < host->library_display()->rowCount(); ++i) {
      const auto item = host->library_display()->getItemAt(i);
      if (item.value("stackId").toString().toStdString() == stack)
        return item.value("occurrenceKey").toString();
    }
    ADD_FAILURE() << "Expected matching stack";
    return {};
  }
  void NikonOnly() {
    host->stats()->ToggleStatsFilter("camera", "Nikon");
    ASSERT_TRUE(WaitForLibraryQuery(*host));
  }
};

TEST_F(LibraryPhotoStackIntegrationTest,
       FilterGroupSaveAndReopenPreserveSavedCoverAndExactStackMembership) {
  // Independent fixture evaluator: Nikon matches B,C; saved order is A,B,C, cover A.
  const std::array<std::string, 3> cameras{"Canon", "Nikon", "Nikon"};
  std::vector<sl_element_id_t>     matching;
  for (size_t i = 0; i < cameras.size(); ++i)
    if (cameras[i] == "Nikon") matching.push_back(files[i]);
  NikonOnly();
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto key = Key();
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  ASSERT_EQ(host->library_display()->rowCount(), matching.size());
  for (size_t i = 0; i < matching.size(); ++i)
    EXPECT_EQ(host->library_display()->FileIdAt(i), matching[i]);
  EXPECT_EQ(Cover(), files[0]);
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[2]}));
  const auto package = host->project()->handler().package_path();
  ASSERT_TRUE(host->project()->SaveProject());
  ASSERT_TRUE(host->project()->CloseProject(false));
  ASSERT_TRUE(LoadPackedProject(*host, package));
  EXPECT_EQ(Cover(), files[0]);
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[2]}));
  NikonOnly();
  ASSERT_EQ(host->library_display()->rowCount(), 1);
  EXPECT_EQ(host->library_display()->FileIdAt(0), matching.front());
  EXPECT_FALSE(host->library_display()->ItemAt(0)->expanded);
}

TEST_F(LibraryPhotoStackIntegrationTest, TemporaryCoverAndExpansionNeverWriteStackRows) {
  const auto package = Bytes();
  NikonOnly();
  const auto key = Key();
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(Cover(), files[0]);
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[2]}));
  EXPECT_EQ(Bytes(), package);
  EXPECT_FALSE(host->project()->HasUnsavedStackChanges());
}

TEST_F(LibraryPhotoStackIntegrationTest,
       DateCameraLensRatingMarksLabelsAndSemanticSearchUseIndividualMatchingPhotos) {
  const auto project = host->project()->handler().project();
  const auto rows    = project->GetAlbumBrowseService()->ReadAlbumFileRows(files);
  const std::array<std::string, 4> cameras{"Canon", "Nikon", "Nikon", "Fuji"};
  const std::array<std::string, 4> days{"2026-06-08", "2026-06-09", "2026-06-09", "2026-06-10"};
  const std::array<std::string, 4> lenses{"Long", "Short", "Short", "Wide"};
  for (size_t i = 0; i < files.size(); ++i)
    project->GetImagePoolService()->Write_NoSync<void>(rows[i].image_id_, [&](auto image) {
      ExifDisplayMetaData metadata;
      metadata.model_         = cameras[i];
      metadata.lens_          = lenses[i];
      metadata.date_time_str_ = days[i] + " 10:00:00";
      metadata.rating_        = i == 1 || i == 2 ? 4 : 0;
      image->SetExifDisplayMetaData(std::move(metadata));
    });
  ASSERT_TRUE(project->GetImagePoolService()->SyncWithStorage().failed_images_.empty());
  auto&       storage = *project->GetStorage();
  std::string error;
  ASSERT_TRUE(
      storage.GetSemanticModelRegistry().UpsertModel({.model_key_     = "stack-integration-model",
                                                      .model_id_      = "stack-integration-model",
                                                      .revision_      = "test",
                                                      .embedding_dim_ = kSemanticEmbeddingDim},
                                                     &error))
      << error;
  for (size_t i = 0; i < files.size(); ++i) {
    const float        score = i == 1 ? 0.6F : (i == 2 ? 0.8F : 0.0F);
    std::vector<float> embedding(kSemanticEmbeddingDim, 0);
    embedding[0]     = score;
    embedding[i + 1] = std::sqrt(1 - score * score);
    SemanticImageLabelRecord label{.file_id_   = files[i],
                                   .model_key_ = "stack-integration-model",
                                   .label_     = i == 1 || i == 2 ? "bird" : "portrait",
                                   .score_     = 0.9,
                                   .confident_ = true};
    ASSERT_TRUE(storage.GetSemanticEmbeddingStore().UpsertImageEmbeddingWithLabel(
        {.file_id_   = files[i],
         .image_id_  = rows[i].image_id_,
         .model_key_ = "stack-integration-model",
         .embedding_ = std::move(embedding)},
        &label, &error))
        << error;
  }
  host->library()->RequestLibraryContentRefresh();
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  for (const auto& [field, value] :
       {std::pair{"date", "2026-06-09"}, std::pair{"camera", "Nikon"}, std::pair{"lens", "Short"},
        std::pair{"rating", "4"}, std::pair{"label", "bird"}}) {
    SCOPED_TRACE(field);
    host->stats()->ToggleStatsFilter(field, value);
    ASSERT_TRUE(WaitForLibraryQuery(*host));
    const auto item = host->library_display()->getItemAt(0);
    ASSERT_EQ(host->library()->TotalCount(), 2);
    EXPECT_EQ(item.value("fileId").toUInt(), files[1]);
    EXPECT_EQ(item.value("queryMatchCount").toInt(), 2);
    EXPECT_TRUE(item.value("temporaryCover").toBool());
    host->stats()->ClearStatsFilter();
    ASSERT_TRUE(WaitForLibraryQuery(*host));
  }
  const auto target = host->library_display()->getItemsInRange(0, 0);
  QSignalSpy marked(host->images(), &ImageController::ImageMarksFinished);
  ASSERT_TRUE(
      host->library_mutations()->SetColorLabelOfTargets(target, 1, true).value("started").toBool());
  ASSERT_TRUE(WaitForSignal(marked));
  host->stats()->ToggleStatsFilter("colorLabel", "1");
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->TotalCount(), 1);
  EXPECT_EQ(host->library_display()->FileIdAt(0), files[0]);
  host->stats()->ClearStatsFilter();
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  marked.clear();
  ASSERT_TRUE(
      host->library_mutations()->SetDeleteMarkOfTargets(target, true).value("started").toBool());
  ASSERT_TRUE(WaitForSignal(marked));
  host->stats()->ToggleStatsFilter("deleted", "1");
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->TotalCount(), 1);
  EXPECT_EQ(host->library_display()->FileIdAt(0), files[0]);
  host->stats()->ClearStatsFilter();
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  project->GetSleeveFilterService()->SetSemanticSearchProvider(
      std::make_shared<StackFilterSearchProvider>());
  host->search()->SetNaturalLanguageSearchEnabled(true);
  ASSERT_TRUE(host->search()->ApplySemanticSearch("birds in flight").isEmpty());
  for (int i = 0; i < 300 && !host->search()->active_search_is_semantic(); ++i) ProcessEvents(10);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->TotalCount(), 2);
  EXPECT_EQ(host->library_display()->FileIdAt(0), files[1]);
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[2]}));
  EXPECT_EQ(Cover(), files[0]);
  host->search()->SetNaturalLanguageSearchEnabled(false);
}

TEST_F(LibraryPhotoStackIntegrationTest,
       MultipleGroupFragmentsSharePhotoSelectionWithoutDuplicatingActions) {
  host->library()->SetInspectorGrouping("camera", true);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  host->library()->SetAllStacksExpanded(true);
  ProcessEvents(100);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto key = Key();
  host->library()->SelectStack(key, true);
  for (int i = 0; i < 200 && host->library()->selection()->SelectedCount() != 3; ++i)
    ProcessEvents(10);
  ASSERT_EQ(host->library()->selection()->SelectedCount(), 3);
  const auto clicked = host->library_display()->getItemAt(host->library_display()->rowByKey(key));
  const auto targets = host->library_mutations()->ResolveTargets(clicked, true);
  ASSERT_EQ(targets.size(), 3);
  host->export_queue()->AddTargets(targets);
  host->export_queue()->AddTargets(targets);
  EXPECT_EQ(host->export_queue()->Count(), 3);
  QSignalSpy ratings(host->images(), &ImageController::ImageRatingsFinished);
  ASSERT_TRUE(host->library_mutations()->RateTargetsAsUser(targets, 4).value("started").toBool());
  ASSERT_TRUE(WaitForSignal(ratings));
  ASSERT_TRUE(ratings.front().front().toMap().value("success").toBool());
  const auto rows =
      host->project()->handler().project()->GetAlbumBrowseService()->ReadAlbumFileRows(files);
  for (size_t i = 0; i < rows.size(); ++i) EXPECT_EQ(rows[i].rating_, i < 3 ? 4 : 0);
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[2]}));
}

TEST_F(LibraryPhotoStackIntegrationTest,
       StackCommandsAndDeletionLeaveStorageInvariantsValidAfterEveryOperation) {
  auto*      controller = host->photo_stacks();
  const auto rows =
      host->project()->handler().project()->GetAlbumBrowseService()->ReadAlbumFileRows(files);
  ASSERT_TRUE(controller
                  ->Start({.operation = PhotoStackCommand::Operation::kAdd,
                           .file_ids  = {files[3], files[3]},
                           .stack_id  = stack})
                  .isEmpty());
  Wait();
  EXPECT_EQ(Members(), files);
  ASSERT_TRUE(controller
                  ->Start({.operation = PhotoStackCommand::Operation::kMove,
                           .file_id   = files[3],
                           .direction = PhotoStackMove::kEarlier})
                  .isEmpty());
  Wait();
  EXPECT_EQ(Members(), (std::vector{files[0], files[1], files[3], files[2]}));
  ASSERT_TRUE(DeleteAndWait(*host, {{files[0], rows[0].image_id_}}));
  EXPECT_EQ(Cover(), files[1]);
  EXPECT_EQ(Members(), (std::vector{files[1], files[3], files[2]}));
  const auto album = host->folders()->CreateFolderIn(0, "Subset");
  ASSERT_GT(album, 0);
  ASSERT_TRUE(host->project()
                  ->handler()
                  .project()
                  ->GetSleeveService()
                  ->LinkFileToFolder(files[1], album)
                  .success_);
  const auto folder = FindFolderId(host->folders()->Folders(), "Subset");
  ASSERT_GT(folder, 0);
  host->folders()->SelectFolder(folder);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  ASSERT_EQ(host->folders()->CurrentFolderElementId(), album);
  ASSERT_TRUE(DeleteAndWait(*host, {{files[1], rows[1].image_id_}}));
  EXPECT_EQ(Members(), (std::vector{files[1], files[3], files[2]}));
  host->folders()->SelectFolder(0);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  const auto source = temp_dir_ / "new_photo.png";
  QImage     image(64, 48, QImage::Format_RGB32);
  image.fill(Qt::gray);
  ASSERT_TRUE(image.save(QString::fromStdWString(source.wstring())));
  ASSERT_TRUE(ImportAndWait(*host, {source}));
  EXPECT_EQ(Members(), (std::vector{files[1], files[3], files[2]}));
  ASSERT_TRUE(host->project()->SaveProject());
  ExpectSavedProjectInvariants(temp_dir_);
}

TEST_F(LibraryPhotoStackIntegrationTest,
       InvalidCommandRollsBackAndPackageFailureRetainsCommittedOrderUntilRetry) {
  const auto before = Members();
  ASSERT_TRUE(host->photo_stacks()
                  ->Start({.operation = PhotoStackCommand::Operation::kAdd,
                           .file_ids  = {files[3], 9999999},
                           .stack_id  = stack})
                  .isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kFailed);
  EXPECT_EQ(Members(), before);
  const auto blocked =
      std::filesystem::path(host->project()->handler().package_path().wstring() + L".tmp");
  std::filesystem::create_directory(blocked);
  std::ofstream(blocked / "keep_directory").put('x');
  ASSERT_TRUE(host->photo_stacks()
                  ->Start({.operation = PhotoStackCommand::Operation::kMove,
                           .file_id   = files[0],
                           .direction = PhotoStackMove::kLater})
                  .isEmpty());
  Wait();
  EXPECT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kUnsaved);
  const std::vector expected{files[1], files[0], files[2]};
  EXPECT_EQ(Members(), expected);
  EXPECT_FALSE(host->project()->RetryStackSave());
  std::filesystem::remove(blocked / "keep_directory");
  std::filesystem::remove(blocked);
  ASSERT_TRUE(host->project()->RetryStackSave());
  const auto package = host->project()->handler().package_path();
  ASSERT_TRUE(host->project()->CloseProject(false));
  ASSERT_TRUE(LoadPackedProject(*host, package));
  EXPECT_EQ(Members(), expected);
}
TEST_F(LibraryPhotoStackIntegrationTest,
       LargeCollapsedStackKeepsMetadataPagesAndThumbnailPinsBounded) {
  const auto project = host->project()->handler().project();
  std::vector<library_search_test::SyntheticImageSpec> specs;
  for (int i = 0; i < 9996; ++i)
    specs.push_back({.file_name_   = L"resource" + std::to_wstring(i) + L".jpg",
                     .image_path_  = L"D:/resources/resource" + std::to_wstring(i) + L".jpg",
                     .is_raw_file_ = false});
  const auto added = library_search_test::SyntheticLibraryBuilder(*project).AddFiles(specs);
  ASSERT_EQ(added.size(), 9996);
  PipelineMgmtService           pipelines(project->GetStorage());
  std::vector<EncodedImageRoot> roots;
  for (const auto id : added)
    roots.push_back(pipelines.EncodeImageRoot(id, CreateDefaultPipelineDocument(), nullptr));
  pipelines.WriteImageRoots(std::move(roots));
  std::vector<sl_element_id_t> ids = files;
  ids.insert(ids.end(), added.begin(), added.end());
  ASSERT_TRUE(host->photo_stacks()
                  ->Start({.operation = PhotoStackCommand::Operation::kCreate,
                           .file_ids  = ids,
                           .file_id   = ids.front()})
                  .isEmpty());
  Wait();
  ASSERT_EQ(result->outcome, PhotoStackCommandResult::Outcome::kSaved);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  auto* display = host->library_display();
  EXPECT_EQ(display->DisplayCount(), 1);
  EXPECT_EQ(display->LoadedOccurrenceCount(), 1);
  EXPECT_EQ(host->library()->model().count(), 1);
  auto&      thumbs      = host->library()->thumbs();
  const auto cover       = display->getItemAt(0);
  const auto cover_file  = cover.value("fileId").toUInt(),
             cover_image = cover.value("imageId").toUInt();
  thumbs.SetOccurrenceThumbnailVisible("flat-cover", cover_file, cover_image, true, 256);
  thumbs.SetOccurrenceThumbnailVisible("flat-cover", cover_file, cover_image, true, 256);
  EXPECT_EQ(thumbs.VisibleOccurrenceCount(cover_file), 1);
  const auto key = display->getItemAt(0).value("occurrenceKey").toString();
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_LE(display->LoadedOccurrenceCount(), 1000);
  const auto member       = display->getItemAt(1);
  const auto member_file  = member.value("fileId").toUInt(),
             member_image = member.value("imageId").toUInt();
  thumbs.SetOccurrenceThumbnailVisible("flat-member", member_file, member_image, true, 256);
  EXPECT_TRUE(thumbs.IsThumbnailPinned(member_file));
  ASSERT_TRUE(host->library()->LoadThumbnailsThroughIndex(9000));
  ProcessEvents(100);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_LE(display->LoadedPageCount(), 3);
  EXPECT_LE(host->library()->model().count(), 3000);
  EXPECT_EQ(display->FirstUnloadedOccurrence(1000, 9000), 1000);
  host->library()->ToggleStack(key);
  ASSERT_TRUE(WaitForLibraryQuery(*host));
  EXPECT_EQ(host->library()->model().count(), 1);
  EXPECT_FALSE(thumbs.IsThumbnailPinned(member_file));
  thumbs.SetOccurrenceThumbnailVisible("flat-cover", cover_file, cover_image, false, 256);
  EXPECT_FALSE(thumbs.IsThumbnailPinned(cover_file));
  EXPECT_EQ(thumbs.VisibleOccurrenceCount(cover_file), 0);
}
TEST_F(ApplicationModuleHostTestFixture, ImportedRawPhotosCanBeGroupedSavedAndReopened) {
  const auto source_dir = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "resources/sample_images/ci_rawfiles";
  std::vector<std::filesystem::path> sources;
  int                                arw = 0, dng = 0;
  for (const auto& entry : std::filesystem::directory_iterator(source_dir)) {
    const auto extension = entry.path().extension().string();
    if ((extension == ".ARW" && arw++ < 2) || (extension == ".dng" && dng++ < 2))
      sources.push_back(entry.path());
  }
  ASSERT_EQ(sources.size(), 4);
  std::sort(sources.begin(), sources.end());
  ApplicationModuleHost backend;
  ASSERT_TRUE(CreateTestProject(backend, "stack_import_review"));
  backend.library()->SetStackDisplayEnabled(true);
  ASSERT_TRUE(ImportAndWait(backend, sources));
  ASSERT_EQ(backend.library()->TotalCount(), 4);
  backend.library()->selection()->Replace(backend.library_display()->getItemsInRange(0, 3));
  std::string saved_stack;
  QObject::connect(backend.photo_stacks(), &PhotoStackController::Finished, &backend,
                   [&](const PhotoStackCommandResult& result) {
                     EXPECT_EQ(result.outcome, PhotoStackCommandResult::Outcome::kSaved);
                     saved_stack = result.change.stack_id;
                   });
  QSignalSpy finished(backend.photo_stacks(), &PhotoStackController::BusyChanged);
  backend.photo_stacks()->RunAction(
      "stack-group", backend.library_display()->getItemAt(0).value("occurrenceKey").toString());
  while (backend.photo_stacks()->Busy()) ASSERT_TRUE(finished.wait(120000));
  ASSERT_FALSE(saved_stack.empty());
  ASSERT_TRUE(WaitForLibraryQuery(backend));
  const auto project = backend.project()->handler().project();
  const auto members = project->GetPhotoStackService()->ReadMemberIds(saved_stack);
  ASSERT_EQ(members.size(), 4);
  auto&       storage = *project->GetStorage();
  std::string error;
  ASSERT_TRUE(
      storage.GetSemanticModelRegistry().UpsertModel({.model_key_     = "stack-review-labels",
                                                      .model_id_      = "stack-review-labels",
                                                      .revision_      = "fixture",
                                                      .embedding_dim_ = kSemanticEmbeddingDim},
                                                     &error))
      << error;
  const auto rows = project->GetAlbumBrowseService()->ReadAlbumFileRows(members);
  for (size_t i = 0; i < rows.size(); ++i) {
    std::vector<float> vector(kSemanticEmbeddingDim, 0);
    vector[i] = 1;
    SemanticImageLabelRecord label{.file_id_   = rows[i].file_id_,
                                   .model_key_ = "stack-review-labels",
                                   .label_     = i == 1 || i == 2 ? "bird" : "portrait",
                                   .score_     = 0.9,
                                   .confident_ = true};
    ASSERT_TRUE(storage.GetSemanticEmbeddingStore().UpsertImageEmbeddingWithLabel(
        {.file_id_   = rows[i].file_id_,
         .image_id_  = rows[i].image_id_,
         .model_key_ = "stack-review-labels",
         .embedding_ = std::move(vector)},
        &label, &error))
        << error;
  }
  ASSERT_TRUE(backend.project()->SaveProject());
  const auto package = backend.project()->handler().package_path();
  // Optional independent package for native visual review. It captures a completed project
  // save only; source RAWs remain the committed test fixtures and no live state is written back.
  if (const auto* directory = std::getenv("ALCEDO_STACK_REVIEW_DIR")) {
    const std::filesystem::path destination(directory);
    std::filesystem::create_directories(destination);
    std::filesystem::copy_file(package, destination / "library_photo_stacks_review.alcd",
                               std::filesystem::copy_options::overwrite_existing);
  }
  ASSERT_TRUE(backend.project()->CloseProject(false));
  ASSERT_TRUE(LoadPackedProject(backend, package));
  EXPECT_EQ(
      backend.project()->handler().project()->GetPhotoStackService()->ReadMemberIds(saved_stack),
      members);
  EXPECT_EQ(backend.library_display()->DisplayCount(), 1);
  ASSERT_TRUE(backend.project()->CloseProject(false));
  ExpectSavedProjectInvariants(temp_dir_);
}
}  // namespace
}  // namespace alcedo::ui::test

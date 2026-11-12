// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GPLv3 section 7 applies; see the LICENSE file.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "image/image.hpp"
#include "image/metadata.hpp"
#include "sleeve/sleeve_filter/filter_factory.hpp"
#include "support/photo_stack_test_library.hpp"

namespace alcedo::test {

/// The approved four-photo example plus two unstacked photos. Expected data is authored
/// independently of SQL output; the base fixture supplies real project services and roots.
class AlbumStackTestLibrary : public PhotoStackProjectTest {
 protected:
  std::string                      stack;
  sl_element_id_t                  scope = 0;
  static constexpr const char*     model = "stack-query-model";
  const std::array<std::string, 6> dates{"2026-06-08 09:00:00", "2026-06-09 08:00:00",
                                         "2026-06-09 10:00:00", "2026-06-10 11:00:00",
                                         "2026-06-12 12:00:00", ""};
  const std::array<std::string, 6> cameras{"Canon", "Nikon", "Canon", "Nikon", "Fuji", ""};
  const std::array<std::string, 6> lenses{"Lens A", "Lens B", "Lens A", "Lens B", "Lens C", ""};
  const std::array<int, 6>         ratings{3, 5, 1, 4, 2, 0};
  const std::array<std::string, 6> labels{"bird", "bird", "flight", "", "portrait", ""};
  const std::array<int64_t, 6>     edits{30, 0, 20, 0, 10, 0};

  void                             SetUp() override {
    PhotoStackProjectTest::SetUp();
    const auto photos = project->GetAlbumBrowseService()->ReadAlbumFileRows(files);
    ASSERT_EQ(photos.size(), files.size());
    for (size_t i = 0; i < files.size(); ++i) {
      project->GetImagePoolService()->Write_NoSync<void>(photos[i].image_id_, [&](auto image) {
        ExifDisplayMetaData metadata;
        metadata.model_         = cameras[i];
        metadata.lens_          = lenses[i];
        metadata.date_time_str_ = dates[i];
        metadata.rating_        = ratings[i];
        image->SetExifDisplayMetaData(std::move(metadata));
      });
      Execute(std::format("UPDATE Element SET added_time = to_timestamp({}) WHERE id = {}",
                          (i + 1) * 1000, files[i]));
      if (!labels[i].empty()) Label(i, model, labels[i]);
      if (edits[i]) {
        Execute(
            std::format("INSERT INTO EditCommit SELECT 'query-edit-{}', root_id, NULL, NULL, "
                        "{}, 0, '{{}}' FROM PipelineRoot WHERE element_id = {}",
                        files[i], edits[i] * 1000000000, files[i]));
        Execute(
            std::format("UPDATE VersionRef SET head_commit_hash = 'query-edit-{}' "
                        "WHERE element_id = {}",
                        files[i], files[i]));
      }
    }
    ASSERT_TRUE(project->GetImagePoolService()->SyncWithStorage().failed_images_.empty());
    Label(3, "inactive-stack-model", "bird");
    stack            = Create({0, 1, 2, 3}, 0);
    const auto album = project->GetSleeveService()->CreateFolder(L"/", L"Example");
    ASSERT_TRUE(album.second.success_);
    scope = album.first->element_id_;
    for (auto id : Files({0, 1, 2, 3})) {
      ASSERT_TRUE(project->GetSleeveService()->LinkFileToFolder(id, scope).success_);
    }
  }

  void Label(size_t index, const std::string& key, const std::string& label) {
    Execute(std::format(
        "INSERT INTO SemanticImageLabel "
        "(file_id, model_key, label, score, confident) VALUES ({}, '{}', '{}', 0.9, TRUE)",
        files.at(index), key, label));
  }

  auto Read(const AlbumQueryOptions&         options = {},
            const std::optional<FilterNode>& filter = std::nullopt, sl_element_id_t folder = 0,
            int64_t offset = 0, int64_t limit = 1000) -> AlbumQueryResult {
    return project->GetAlbumBrowseService()->ReadAlbumQuery(folder, filter, options, model,
                                                            {.limit_           = limit,
                                                             .read_groups_     = true,
                                                             .read_statistics_ = true,
                                                             .mode = AlbumReadMode::kDisplay,
                                                             .display_offset = offset});
  }

  auto DisplayIds(const AlbumQueryResult& result) const -> std::vector<sl_element_id_t> {
    std::vector<sl_element_id_t> ids;
    for (const auto& row : result.display_rows) ids.push_back(row.photo.photo_.file_id_);
    return ids;
  }

  auto Relevance() -> SemanticQueryEmbedding {
    auto&       storage = *project->GetStorage();
    std::string error;
    EXPECT_TRUE(
        storage.GetSemanticModelRegistry().UpsertModel({.model_key_     = model,
                                                        .model_id_      = model,
                                                        .revision_      = "query-test",
                                                        .embedding_dim_ = kSemanticEmbeddingDim},
                                                       &error))
        << error;
    const auto rows = project->GetAlbumBrowseService()->ReadAlbumFileRows(files);
    constexpr std::array<float, 6> scores{0.01F, 0.35F, 0.60F, 0.40F, 0.50F, 0.05F};
    for (size_t i = 0; i < rows.size(); ++i) {
      std::vector<float> vector(kSemanticEmbeddingDim, 0);
      vector[0]     = scores[i];
      vector[i + 1] = std::sqrt(1 - scores[i] * scores[i]);
      EXPECT_TRUE(storage.GetSemanticEmbeddingStore().UpsertImageEmbedding(
          {.file_id_   = rows[i].file_id_,
           .image_id_  = rows[i].image_id_,
           .model_key_ = model,
           .embedding_ = std::move(vector)},
          &error))
          << error;
    }
    SemanticQueryEmbedding query{.model_key_ = model,
                                 .dimension_ = kSemanticEmbeddingDim,
                                 .query_     = std::vector<float>(kSemanticEmbeddingDim, 0),
                                 .neutral_   = std::vector<float>(kSemanticEmbeddingDim, 0)};
    query.query_[0]       = 1;
    query.neutral_.back() = 1;
    return query;
  }
};
}  // namespace alcedo::test

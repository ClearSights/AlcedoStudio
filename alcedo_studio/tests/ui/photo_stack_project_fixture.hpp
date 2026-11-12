// Copyright 2026 Alcedo Studio contributors
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QSignalSpy>
#include <fstream>
#include <iterator>
#include <optional>

#include "ui/album_backend_populated_project_fixture.hpp"
#include "ui/alcedo_main/album_backend/photo_stack_controller.hpp"
#include "ui/saved_project_invariants.hpp"
namespace alcedo::ui::test {
class PhotoStackProjectFixture : public ApplicationModuleHostTestFixture {
 protected:
  std::unique_ptr<ApplicationModuleHost> host;
  std::vector<sl_element_id_t>           files;
  std::optional<PhotoStackCommandResult> result;

  void                                   SetUp() override {
    ApplicationModuleHostTestFixture::SetUp();
    host               = std::make_unique<ApplicationModuleHost>();
    const auto project = CreatePopulatedPackedProject(
        temp_dir_, {}, 0,
        std::vector<LibraryPhotoSpec>{
            {.model_ = "Canon"}, {.model_ = "Nikon"}, {.model_ = "Nikon"}, {.model_ = "Fuji"}});
    ASSERT_TRUE(project);
    ASSERT_TRUE(LoadPackedProject(*host, project->packed_path_));
    for (const auto& image : project->images_) files.push_back(image.file_id_);
    QObject::connect(host->photo_stacks(), &PhotoStackController::Finished, host.get(),
                     [this](const PhotoStackCommandResult& value) { result = value; });
  }
  void TearDown() override {
    host.reset();
    ExpectSavedProjectInvariants(temp_dir_);
    ApplicationModuleHostTestFixture::TearDown();
  }
  auto Create() -> PhotoStackCommand {
    return {.operation = PhotoStackCommand::Operation::kCreate,
            .file_ids  = {files[0], files[1], files[2]},
            .file_id   = files[0]};
  }
  void Wait() {
    QSignalSpy spy(host->photo_stacks(), &PhotoStackController::BusyChanged);
    while (host->photo_stacks()->Busy()) ASSERT_TRUE(spy.wait(30000));
    ASSERT_TRUE(result);
  }
  auto Bytes() -> std::string {
    std::ifstream file(host->project()->handler().package_path(), std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
};

}  // namespace alcedo::ui::test

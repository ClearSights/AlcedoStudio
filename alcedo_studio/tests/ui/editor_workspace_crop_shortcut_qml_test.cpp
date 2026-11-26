//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariantList>
#include <QVariantMap>
#include <exception>
#include <memory>
#include <utility>

#include "app/editor_render_coordinator.hpp"
#include "edit/frame_presentation_types.hpp"
#include "edit/pipeline/pipeline_accelerator.hpp"
#include "support/harness_completing_pipeline_scheduler_port.hpp"
#include "ui/album_backend_populated_project_fixture.hpp"
#include "ui/album_backend_test_fixture.hpp"
#include "ui/alcedo_main/album_backend/editor_adjustment_models.hpp"
#include "ui/alcedo_main/album_backend/editor_behavior_preferences.hpp"
#include "ui/alcedo_main/album_backend/editor_session_controller.hpp"
#include "ui/alcedo_main/album_backend/export_recipe_builder.hpp"
#include "ui/alcedo_main/album_backend/import_export.hpp"
#include "ui/alcedo_main/album_backend/library_module.hpp"
#include "ui/alcedo_main/album_backend/workspace_router.hpp"
#include "ui/alcedo_main/application_module_qml_types.hpp"
#include "ui/alcedo_main/shortcut_registry.hpp"
#include "ui/edit_viewer/crop_guide_paths.hpp"
#include "ui/edit_viewer/frame_sink.hpp"
#include "ui/edit_viewer/mask_edit_geometry.hpp"
#include "ui/editor_rhi/editor_overlay_item.hpp"
#include "ui/main_qml_test_fixture.hpp"

namespace alcedo::ui::test {
namespace {

class EditorWorkspaceCropShortcutQmlTest : public MainQmlTestFixture {
 protected:
  /// Load the packaged workspace with an editable photograph and a presented frame extent.
  void OpenPhoto(std::unique_ptr<LoadedMainWindow>& loaded);
};

/// Find the input by the name prefix of the instantiated shared slider.
auto RotationValueInput(QQuickItem* slider) -> QQuickItem* {
  return slider->findChild<QQuickItem*>(slider->property("partName").toString() + "ValueInput");
}

void EditorWorkspaceCropShortcutQmlTest::OpenPhoto(std::unique_ptr<LoadedMainWindow>& loaded) {
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, PathToQString(temp_dir_));
  QCoreApplication::setOrganizationName(QStringLiteral("AlcedoTests"));
  RegisterApplicationModuleTypes();

  const auto photos = WritePngPhotos(temp_dir_, "geometry-shortcut-", 1);
  ASSERT_EQ(photos.size(), 1u);
  loaded = LoadMainWindowFromModule();
  ASSERT_NE(loaded->window, nullptr);
  ASSERT_TRUE(ImportAndWait(loaded->host, photos));
  const auto thumbnails = loaded->host.library()->Thumbnails();
  ASSERT_EQ(thumbnails.size(), 1);
  const auto photo      = thumbnails.front().toMap();
  const auto element_id = photo.value(QStringLiteral("elementId")).toUInt();
  const auto image_id   = photo.value(QStringLiteral("imageId")).toUInt();
  ASSERT_GT(element_id, 0u);
  ASSERT_GT(image_id, 0u);

  auto* session     = loaded->host.editor_session();
  auto* coordinator = loaded->host.editor_render_coordinator();
  ASSERT_NE(session, nullptr);
  ASSERT_NE(coordinator, nullptr);
  // The interaction test uses the real session and leaves pixel rendering to the render suites.
  auto scheduler = std::make_shared<alcedo::test::HarnessCompletingPipelineSchedulerPort>();
  scheduler->SetSinkResolver([session] { return session->presentation_frame_sink(); });
  scheduler->SetFrameProducer([](IFrameSink* sink, const EditorRenderRequest&) {
    const auto mapping = sink->MapResourceForWrite(FrameMemoryDomain::HostVisible);
    if (mapping) {
      sink->UnmapResource();
    }
    // WritePngPhotos creates a 48 by 32 photograph. Crop validation uses its full source extent.
    sink->NotifyFrameReady(FrameCompletionSubmission{
        .geometry = MaskEditGeometry::MakeIdentityPhotographGeometry({48, 32}),
    });
    return true;
  });
  coordinator->SetPipelineSchedulerPort(std::move(scheduler));

  loaded->host.workspace_router()->OpenEditor(element_id, image_id);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return session->element_id() == element_id && session->can_edit(); }, 15000));
}

TEST_F(EditorWorkspaceCropShortcutQmlTest, EnterKeysCommitNumericInputAndReturnFromViewport) {
  std::unique_ptr<LoadedMainWindow> loaded;
  ASSERT_NO_FATAL_FAILURE(OpenPhoto(loaded));
  auto* session   = loaded->host.editor_session();
  auto* window    = loaded->window;
  auto* workspace = window->findChild<QQuickItem*>(QStringLiteral("editorWorkspace"));
  ASSERT_NE(workspace, nullptr);
  auto* viewport =
      workspace->findChild<QQuickItem*>(QStringLiteral("editorViewportInteractionLayer"));
  auto* slider = workspace->findChild<QQuickItem*>(QStringLiteral("geometryRotationSlider"));
  auto* panel = workspace->findChild<QQuickItem*>(QStringLiteral("editorAdjustmentPanel_geometry"));
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(slider, nullptr);
  ASSERT_NE(panel, nullptr);
  auto* rotation =
      workspace->findChild<EditorAdjustmentValueModel*>(QStringLiteral("geometryRotationModel"));
  ASSERT_NE(rotation, nullptr);
  QSignalSpy settled(rotation, &EditorAdjustmentValueModel::settledCommitted);
  QSignalSpy entered(slider, SIGNAL(valueEntered(double)));
  ASSERT_TRUE(settled.isValid());
  ASSERT_TRUE(entered.isValid());

  const QString geometry   = QStringLiteral("geometry");
  const QString tone       = QStringLiteral("tone");
  const QString crop_scope = QLatin1String(shortcut_scope::kEditorCropEdit);
  for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
    SCOPED_TRACE(key == Qt::Key_Enter ? "Keypad Enter" : "Return");
    const auto   modifiers = key == Qt::Key_Enter ? Qt::KeypadModifier : Qt::NoModifier;
    const double angle     = key == Qt::Key_Enter ? 10.0 : 5.0;
    session->set_active_adjustment_panel(geometry);
    QQuickItem* numeric_input = nullptr;
    ASSERT_TRUE(QTest::qWaitFor(
        [&] {
          numeric_input = RotationValueInput(slider);
          return numeric_input && slider->isVisible() && slider->isEnabled() &&
                 panel->property("hasSourceSize").toBool();
        },
        10000))
        << "Numeric input found: " << (numeric_input != nullptr)
        << ", slider visible: " << slider->isVisible()
        << ", slider enabled: " << slider->isEnabled()
        << ", source extent available: " << panel->property("hasSourceSize").toBool();
    ASSERT_EQ(session->active_adjustment_panel(), geometry);

    numeric_input->forceActiveFocus();
    ASSERT_EQ(window->activeFocusItem(), numeric_input);
    ASSERT_TRUE(workspace->property("cropEditShortcutScope").toString().isEmpty());
    ASSERT_TRUE(numeric_input->setProperty("text", QString::number(angle)));
    QSignalSpy accepted(numeric_input, SIGNAL(accepted()));
    ASSERT_TRUE(accepted.isValid());
    entered.clear();
    settled.clear();
    QTest::keyClick(window, key, modifiers);
    ASSERT_EQ(accepted.size(), 1);
    ASSERT_EQ(entered.size(), 1);
    EXPECT_DOUBLE_EQ(entered.first().first().toDouble(), angle);
    EXPECT_EQ(settled.size(), 1);
    ASSERT_EQ(session->active_adjustment_panel(), geometry);
    EXPECT_NE(window->activeFocusItem(), numeric_input);

    viewport->forceActiveFocus();
    ASSERT_EQ(window->activeFocusItem(), viewport);
    ASSERT_EQ(workspace->property("cropEditShortcutScope").toString(), crop_scope);
    QTest::keyClick(window, key, modifiers);
    ASSERT_TRUE(QTest::qWaitFor([&] { return session->active_adjustment_panel() == tone; }, 2000));
    EXPECT_EQ(accepted.size(), 1);
    EXPECT_EQ(entered.size(), 1);
    EXPECT_EQ(settled.size(), 1);
  }

  session->Finalize(false);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !session->has_image(); }, 10000));
}

TEST_F(EditorWorkspaceCropShortcutQmlTest, GuidePreferenceChangesDoNotCreateHistoryEntries) {
  std::unique_ptr<LoadedMainWindow> loaded;
  ASSERT_NO_FATAL_FAILURE(OpenPhoto(loaded));
  auto* session     = loaded->host.editor_session();
  auto* preferences = loaded->host.editor_behavior();
  auto* window      = loaded->window;
  auto* workspace   = window->findChild<QQuickItem*>(QStringLiteral("editorWorkspace"));
  ASSERT_NE(workspace, nullptr);
  auto* viewport =
      workspace->findChild<QQuickItem*>(QStringLiteral("editorViewportInteractionLayer"));
  auto* overlay =
      workspace->findChild<editor_rhi::EditorOverlayItem*>(QStringLiteral("editorOverlayItem"));
  auto* panel = workspace->findChild<QQuickItem*>(QStringLiteral("editorAdjustmentPanel_geometry"));
  ASSERT_TRUE(viewport && overlay && panel);

  session->set_active_adjustment_panel(QStringLiteral("geometry"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return overlay->cropVisible(); }, 10000));
  viewport->forceActiveFocus();
  ASSERT_EQ(window->activeFocusItem(), viewport);
  ASSERT_EQ(workspace->property("cropEditShortcutScope").toString(),
            QLatin1String(shortcut_scope::kEditorCropEdit));
  const auto history_revision = session->history_revision();
  QSignalSpy history_changed(session, &EditorSessionController::HistoryChanged);
  ASSERT_TRUE(preferences->selectCropGuide(static_cast<int>(CropGuide::Ratio)));
  QTest::keyClick(window, Qt::Key_O);
  ASSERT_EQ(preferences->cropGuideIndex(), static_cast<int>(CropGuide::Spiral));
  EXPECT_EQ(overlay->cropGuideIndex(), static_cast<int>(CropGuide::Spiral));
  QTest::keyClick(window, Qt::Key_O, Qt::ShiftModifier);
  EXPECT_EQ(preferences->cropGuideOrientation(), 1);
  EXPECT_EQ(overlay->cropGuideOrientation(), 1);

  auto* show_guide = panel->findChild<QQuickItem*>(QStringLiteral("cropGuideVisibilitySelector"));
  ASSERT_NE(show_guide, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      show_guide, "activated", Q_ARG(int, static_cast<int>(CropGuideVisibility::DuringInput))));
  EXPECT_EQ(preferences->cropGuideVisibility(), static_cast<int>(CropGuideVisibility::DuringInput));
  ASSERT_TRUE(QTest::qWaitFor([&] { return overlay->lastSceneGeometry().grid_triangles.empty(); }));

  auto* slider = panel->findChild<QQuickItem*>(QStringLiteral("geometryRotationSlider"));
  ASSERT_NE(slider, nullptr);
  auto* numeric_input = RotationValueInput(slider);
  ASSERT_NE(numeric_input, nullptr);
  numeric_input->forceActiveFocus();
  ASSERT_EQ(window->activeFocusItem(), numeric_input);
  EXPECT_TRUE(workspace->property("cropEditShortcutScope").toString().isEmpty());
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !overlay->lastSceneGeometry().grid_triangles.empty(); }));
  QTest::keyClick(window, Qt::Key_O);
  EXPECT_EQ(preferences->cropGuideIndex(), static_cast<int>(CropGuide::Spiral));
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_TRUE(QTest::qWaitFor([&] { return overlay->lastSceneGeometry().grid_triangles.empty(); }));

  session->set_active_adjustment_panel(QStringLiteral("tone"));
  viewport->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_O);
  EXPECT_EQ(preferences->cropGuideIndex(), static_cast<int>(CropGuide::Spiral));
  EXPECT_EQ(session->history_revision(), history_revision);
  EXPECT_EQ(history_changed.count(), 0);
  session->Finalize(false);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !session->has_image(); }, 10000));
}

TEST_F(EditorWorkspaceCropShortcutQmlTest, CropGuideDoesNotChangeExportedPixels) {
  try {
    (void)ResolveAcceleratorBackend(AcceleratorBackendPreference::Auto);
  } catch (const std::exception& error) {
    GTEST_SKIP() << error.what();
  }
  std::unique_ptr<LoadedMainWindow> loaded;
  ASSERT_NO_FATAL_FAILURE(OpenPhoto(loaded));
  auto& host        = loaded->host;
  auto* session     = host.editor_session();
  auto* preferences = host.editor_behavior();
  auto* progress    = host.import_export();
  auto* overlay     = loaded->window->findChild<editor_rhi::EditorOverlayItem*>(
      QStringLiteral("editorOverlayItem"));
  ASSERT_NE(overlay, nullptr);
  session->set_active_adjustment_panel(QStringLiteral("geometry"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return overlay->cropVisible(); }, 10000));

  ExportRecipeOptions options;
  options.out_dir          = PathToQString(temp_dir_);
  options.format           = QStringLiteral("PNG");
  options.include_metadata = false;
  const QVariantList targets{
      QVariantMap{{"elementId", session->element_id()}, {"imageId", session->image_id()}}};
  QImage reference;
  for (const bool show_guide : {false, true}) {
    SCOPED_TRACE(show_guide);
    ASSERT_TRUE(preferences->selectCropGuide(
        static_cast<int>(show_guide ? CropGuide::Triangle : CropGuide::None)));
    ASSERT_TRUE(QTest::qWaitFor([&] {
      const bool guide_drawn = !overlay->lastSceneGeometry().grid_triangles.empty();
      return guide_drawn == show_guide;
    }));
    options.file_name_pattern =
        show_guide ? QStringLiteral("with-guide") : QStringLiteral("without-guide");
    const auto error = host.export_recipe()->Start(options, targets);
    ASSERT_FALSE(error.has_value()) << error->message.toStdString();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !progress->ExportInFlight(); }, 30000));
    ASSERT_EQ(progress->ExportSucceeded(), 1) << progress->ExportErrorSummary().toStdString();
    const QImage pixels(options.out_dir + "/" + options.file_name_pattern + ".png");
    ASSERT_EQ(pixels.size(), QSize(48, 32));
    if (show_guide) {
      EXPECT_EQ(pixels, reference);
    } else {
      reference = pixels;
    }
  }
  session->Finalize(false);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !session->has_image(); }, 10000));
}

}  // namespace
}  // namespace alcedo::ui::test

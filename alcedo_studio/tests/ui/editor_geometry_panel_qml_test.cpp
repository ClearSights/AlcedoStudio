//  Copyright 2026 Yurun Zi
//  SPDX-License-Identifier: GPL-3.0-only
//  Additional permission under GPLv3 section 7 applies; see the LICENSE file.

#include <gtest/gtest.h>

#include <QAccessible>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRectF>
#include <QVariantMap>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <variant>
#include <vector>

#include "ui/alcedo_main/album_backend/editor_adjustment_models.hpp"
#include "ui/alcedo_main/album_backend/editor_adjustment_submitter.hpp"
#include "ui/alcedo_main/app_theme.hpp"
#include "ui/edit_viewer/crop_geometry.hpp"

namespace alcedo::ui::test {
namespace {

class GeometrySession final : public QObject, public IEditorAdjustmentSubmitter {
  Q_OBJECT
  Q_PROPERTY(
      QVariantMap adjustmentSnapshot READ adjustmentSnapshot NOTIFY adjustmentSnapshotChanged)
  Q_PROPERTY(quint64 snapshotRevision READ snapshotRevision NOTIFY adjustmentSnapshotChanged)
  Q_PROPERTY(QString activeAdjustmentPanel READ activeAdjustmentPanel WRITE setActiveAdjustmentPanel
                 NOTIFY activeAdjustmentPanelChanged)
  Q_PROPERTY(bool canEdit READ canEdit NOTIFY canEditChanged)

 public:
  struct Call {
    QString field_key;
    QString params;
    bool    settled = false;
    alcedo::EditorParameterWrite write = alcedo::EditorScalarWrite{};
  };

  explicit GeometrySession(QVariantMap snapshot = {}, quint64 revision = 0)
      : snapshot_(std::move(snapshot)), revision_(revision) {}

  [[nodiscard]] auto adjustmentSnapshot() const -> QVariantMap { return snapshot_; }
  [[nodiscard]] auto snapshotRevision() const -> quint64 { return revision_; }
  [[nodiscard]] auto activeAdjustmentPanel() const -> QString { return active_panel_; }
  void               setActiveAdjustmentPanel(const QString& panel) {
    if (active_panel_ == panel) {
      return;
    }
    active_panel_ = panel;
    actions.push_back(QStringLiteral("panel:") + panel);
    emit activeAdjustmentPanelChanged();
  }
  [[nodiscard]] auto canEdit() const -> bool override { return can_edit_; }

  auto submitWrite(QString fieldKey, alcedo::EditorParameterWrite write, bool settled)
      -> bool override {
    actions.push_back(QStringLiteral("submit:") + fieldKey);
    calls.push_back({fieldKey, QString(), settled, std::move(write)});
    return can_edit_;
  }

  Q_INVOKABLE bool submitPatch(QString fieldKey, QString paramsJson, bool settled) override {
    nlohmann::json parsed;
    try {
      parsed = paramsJson.isEmpty() ? nlohmann::json::object()
                                    : nlohmann::json::parse(paramsJson.toStdString());
    } catch (const std::exception&) {
      return false;
    }
    std::string error;
    auto        write = alcedo::ParseEditorParameterWrite(fieldKey.toStdString(), parsed, &error);
    if (!write.has_value()) {
      return false;
    }
    const auto ok = submitWrite(fieldKey, std::move(*write), settled);
    if (ok && !calls.empty()) {
      calls.back().params = std::move(paramsJson);
    }
    return ok;
  }

  std::vector<Call> calls;
  QStringList       actions;
  QStringList         geometry_commands;

  Q_INVOKABLE QString submitGeometryCommand(const QString& command) {
    geometry_commands.push_back(command);
    return {};
  }

 signals:
  void adjustmentSnapshotChanged();
  void activeAdjustmentPanelChanged();
  void canEditChanged();

 private:
  QVariantMap snapshot_;
  quint64     revision_     = 0;
  QString     active_panel_ = QStringLiteral("geometry");
  bool        can_edit_     = true;
};

// Stands in for EditorInteractionController: the source size comes from the
// presented frame, crop setters only show values, and pointer edits arrive as
// cropFrameEdited. clampCropRect uses the production constraint.
class FakeGeometryInteraction final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QRectF cropRectNormalized READ cropRectNormalized WRITE setCropRectNormalized NOTIFY
                 cropChanged)
  Q_PROPERTY(float cropRotationDegrees READ cropRotationDegrees WRITE setCropRotationDegrees NOTIFY
                 cropChanged)
  Q_PROPERTY(int sourceImageWidth READ sourceImageWidth NOTIFY imageGeometryChanged)
  Q_PROPERTY(int sourceImageHeight READ sourceImageHeight NOTIFY imageGeometryChanged)

 public:
  [[nodiscard]] auto cropRectNormalized() const -> QRectF { return crop_rect_; }
  [[nodiscard]] auto cropRotationDegrees() const -> float { return rotation_degrees_; }
  [[nodiscard]] auto sourceImageWidth() const -> int { return source_width_; }
  [[nodiscard]] auto sourceImageHeight() const -> int { return source_height_; }
  [[nodiscard]] auto cropToolEnabled() const -> bool { return crop_tool_enabled_; }
  [[nodiscard]] auto cropOverlayVisible() const -> bool { return crop_overlay_visible_; }
  [[nodiscard]] auto aspectLocked() const -> bool { return aspect_locked_; }
  [[nodiscard]] auto aspectRatio() const -> float { return aspect_ratio_; }

  void               presentSource(int width, int height) {
    source_width_  = width;
    source_height_ = height;
    emit imageGeometryChanged();
  }
  Q_INVOKABLE void setCropToolEnabled(bool enabled) { crop_tool_enabled_ = enabled; }
  Q_INVOKABLE void setCropOverlayVisible(bool visible) { crop_overlay_visible_ = visible; }
  Q_INVOKABLE void setCropAspectLock(bool enabled, float aspectRatio) {
    aspect_locked_ = enabled;
    aspect_ratio_  = aspectRatio;
    emit cropChanged();
  }
  Q_INVOKABLE void setCropRectNormalized(const QRectF& rect) {
    crop_rect_ = rect;
    emit cropChanged();
  }
  Q_INVOKABLE void setCropRotationDegrees(float degrees) {
    rotation_degrees_ = degrees;
    emit cropChanged();
  }
  Q_INVOKABLE QRectF clampCropRect(const QRectF& rect, float degrees) const {
    if (source_width_ <= 0 || source_height_ <= 0) {
      return rect.normalized();
    }
    return CropGeometry::ClampCrop(rect, degrees,
                                   Extent2D{static_cast<std::uint32_t>(source_width_),
                                            static_cast<std::uint32_t>(source_height_)});
  }
  void editCropFrame(const QRectF& rect, float degrees, bool isFinal) {
    crop_rect_        = rect;
    rotation_degrees_ = degrees;
    emit cropChanged();
    emit cropFrameEdited(rect, degrees, isFinal);
  }

 signals:
  void cropChanged();
  void imageGeometryChanged();
  void cropFrameEdited(const QRectF& rect, float degrees, bool isFinal);

 private:
  QRectF crop_rect_{0.0, 0.0, 1.0, 1.0};
  float  rotation_degrees_     = 0.0F;
  float  aspect_ratio_         = 1.0F;
  bool   aspect_locked_        = false;
  bool   crop_tool_enabled_    = false;
  bool   crop_overlay_visible_ = false;
  int    source_width_         = 6000;
  int    source_height_        = 4000;
};

auto QmlDirectory() -> QString {
  return QString::fromStdString(
      (std::filesystem::path(ALCEDO_TEST_SRC_DIR) / "ui" / "alcedo_main" / "qml").string());
}

auto AdjustmentStackUrl() -> QUrl {
  return QUrl::fromLocalFile(QmlDirectory() + QStringLiteral("/EditorAdjustmentStack.qml"));
}

// The crop_rotate projection EditorPanelPresentation publishes.
auto MakeSnapshot(const QString& preset, double x, double y, double width, double height,
                  double angle, const QString& orientation = QStringLiteral("identity"))
    -> QVariantMap {
  QVariantMap cropRect;
  cropRect.insert(QStringLiteral("x"), x);
  cropRect.insert(QStringLiteral("y"), y);
  cropRect.insert(QStringLiteral("w"), width);
  cropRect.insert(QStringLiteral("h"), height);

  QVariantMap aspect;
  aspect.insert(QStringLiteral("width"), 16.0);
  aspect.insert(QStringLiteral("height"), 9.0);

  QVariantMap crop;
  crop.insert(QStringLiteral("crop_rect"), cropRect);
  crop.insert(QStringLiteral("angle_degrees"), angle);
  crop.insert(QStringLiteral("orientation"), orientation);
  crop.insert(QStringLiteral("aspect_ratio_preset"), preset);
  crop.insert(QStringLiteral("aspect_ratio"), aspect);
  QVariantMap cropWrapper;
  cropWrapper.insert(QStringLiteral("crop_rotate"), crop);

  QVariantMap snapshot;
  snapshot.insert(QStringLiteral("crop_rotate"), cropWrapper);
  return snapshot;
}

class AdjustmentStackHarness {
 public:
  AdjustmentStackHarness(GeometrySession* session, FakeGeometryInteraction* interaction) {
    static bool registered = false;
    if (!registered) {
      RegisterEditorAdjustmentQmlTypes();
      registered = true;
    }
    AppTheme::RegisterFonts();
    AppTheme::Instance().setReduceMotion(true);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    engine_.addImportPath(QStringLiteral("qrc:/"));
    engine_.addImportPath(QmlDirectory());
    engine_.rootContext()->setContextProperty(QStringLiteral("appTheme"), &AppTheme::Instance());

    QQmlComponent component(&engine_, AdjustmentStackUrl());
    if (component.isError()) {
      errors_ = component.errors();
      return;
    }

    QVariantMap initialProperties;
    initialProperties.insert(QStringLiteral("editorSession"),
                             QVariant::fromValue(static_cast<QObject*>(session)));
    initialProperties.insert(QStringLiteral("interaction"),
                             QVariant::fromValue(static_cast<QObject*>(interaction)));
    initialProperties.insert(QStringLiteral("controlsEnabled"), true);
    root_.reset(
        qobject_cast<QQuickItem*>(component.createWithInitialProperties(initialProperties)));
    if (!root_) {
      errors_ = component.errors();
      return;
    }
    root_->setWidth(400);
    root_->setHeight(700);
    root_->setParentItem(window_.contentItem());
    window_.resize(400, 700);
    window_.show();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
  }

  [[nodiscard]] auto root() const -> QQuickItem* { return root_.get(); }
  [[nodiscard]] auto errors() const -> QString {
    QStringList text;
    for (const auto& error : errors_) {
      text.push_back(error.toString());
    }
    return text.join('\n');
  }

  template <typename T>
  T* findObject(const QString& objectName) const {
    return root_ ? root_->findChild<T*>(objectName) : nullptr;
  }

 private:
  QQmlEngine                  engine_;
  QQuickWindow                window_;
  std::unique_ptr<QQuickItem> root_;
  QList<QQmlError>            errors_;
};

// The typed geometry write every submit carries (panel models submit typed
// writes; overlay edits submit JSON that the session parses to the same type).
auto GeometryOf(const GeometrySession::Call& call) -> alcedo::ImageGeometryUpdate {
  const auto* geometry = std::get_if<alcedo::ImageGeometryUpdate>(&call.write);
  EXPECT_NE(geometry, nullptr);
  return geometry != nullptr ? *geometry : alcedo::ImageGeometryUpdate{};
}

auto RectOf(const GeometrySession::Call& call) -> QRectF {
  const auto geometry = GeometryOf(call);
  EXPECT_TRUE(geometry.crop_rect.has_value());
  const auto rect = geometry.crop_rect.value_or(NormalizedRect{});
  return QRectF(rect.x, rect.y, rect.w, rect.h);
}

auto AngleOf(const GeometrySession::Call& call) -> double {
  return GeometryOf(call).rotation_degrees.value_or(0.0F);
}

auto PresetOf(const GeometrySession::Call& call) -> QString {
  return QString::fromStdString(GeometryOf(call).aspect_preset.value_or(std::string()));
}

}  // namespace

TEST(EditorGeometryPanelQmlTest, GeometryPanelExposesTypedModelsAndEnablesOverlay) {
  GeometrySession         session;
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);

  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();
  ASSERT_NE(harness.findObject<QObject>(QStringLiteral("editorAdjustmentPanel_geometry")), nullptr);
  ASSERT_NE(harness.findObject<QObject>(QStringLiteral("geometryAspectModel")), nullptr);
  ASSERT_NE(harness.findObject<QObject>(QStringLiteral("geometryRotationModel")), nullptr);
  EXPECT_EQ(harness.findObject<QObject>(QStringLiteral("geometryLensEnabledModel")), nullptr);
  EXPECT_EQ(harness.findObject<QObject>(QStringLiteral("editorAdjustmentGroupShell_geometry_lens")),
            nullptr);
  EXPECT_TRUE(interaction.cropToolEnabled());
  EXPECT_TRUE(interaction.cropOverlayVisible());
  EXPECT_TRUE(session.calls.empty());
}

TEST(EditorGeometryPanelQmlTest, SnapshotProjectsCropAndSourceSizeComesFromThePresentedFrame) {
  GeometrySession session(MakeSnapshot(QStringLiteral("ratio_16_9"), 0.1, 0.2, 0.7, 0.6, 12.5), 4);
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* geometryPanel =
      harness.findObject<QObject>(QStringLiteral("editorAdjustmentPanel_geometry"));
  auto* xModel   = harness.findObject<QObject>(QStringLiteral("geometryCropXModel"));
  auto* wModel   = harness.findObject<QObject>(QStringLiteral("geometryCropWidthModel"));
  auto* rotation = harness.findObject<QObject>(QStringLiteral("geometryRotationModel"));
  auto* aspect   = harness.findObject<QObject>(QStringLiteral("geometryAspectModel"));
  ASSERT_NE(geometryPanel, nullptr);
  ASSERT_NE(xModel, nullptr);
  ASSERT_NE(wModel, nullptr);
  ASSERT_NE(rotation, nullptr);
  ASSERT_NE(aspect, nullptr);

  EXPECT_NEAR(xModel->property("value").toDouble(), 0.1, 1e-6);
  EXPECT_NEAR(wModel->property("value").toDouble(), 0.7, 1e-6);
  EXPECT_DOUBLE_EQ(rotation->property("value").toDouble(), 12.5);
  EXPECT_EQ(aspect->property("currentValue").toString(), QStringLiteral("ratio_16_9"));
  EXPECT_EQ(geometryPanel->property("sourceImageWidth").toInt(), 6000);
  EXPECT_EQ(geometryPanel->property("sourceImageHeight").toInt(), 4000);
  EXPECT_NEAR(interaction.cropRectNormalized().x(), 0.1, 1e-6);
  EXPECT_NEAR(interaction.cropRotationDegrees(), 12.5f, 1e-5f);
  EXPECT_TRUE(interaction.aspectLocked());
  EXPECT_NEAR(interaction.aspectRatio(), 16.0 / 9.0, 1e-4);

  // Another image's frame changes the source size; the snapshot never carries it.
  interaction.presentSource(3000, 4500);
  EXPECT_EQ(geometryPanel->property("sourceImageWidth").toInt(), 3000);
  EXPECT_EQ(geometryPanel->property("sourceImageHeight").toInt(), 4500);
  // Showing a snapshot is not an edit.
  EXPECT_TRUE(session.calls.empty());
}

TEST(EditorGeometryPanelQmlTest, SliderDragSubmitsInteractivePatchesThenOneSettledPatch) {
  GeometrySession         session(MakeSnapshot(QStringLiteral("free"), 0.0, 0.0, 1.0, 1.0, 0.0));
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* widthModel = harness.findObject<QObject>(QStringLiteral("geometryCropWidthModel"));
  ASSERT_NE(widthModel, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(widthModel, "beginDrag"));
  ASSERT_TRUE(QMetaObject::invokeMethod(widthModel, "updateDrag", Q_ARG(double, 0.6)));
  ASSERT_TRUE(QMetaObject::invokeMethod(widthModel, "updateDrag", Q_ARG(double, 0.5)));
  ASSERT_TRUE(QMetaObject::invokeMethod(widthModel, "finishDrag"));

  ASSERT_EQ(session.calls.size(), 3u);
  EXPECT_FALSE(session.calls[0].settled);
  EXPECT_FALSE(session.calls[1].settled);
  EXPECT_TRUE(session.calls[2].settled);
  for (const auto& call : session.calls) {
    EXPECT_EQ(call.field_key, QStringLiteral("crop_rotate"));
    EXPECT_TRUE(GeometryOf(call).crop_rect.has_value());
  }
  EXPECT_NEAR(RectOf(session.calls.back()).width(), 0.5, 1e-6);
  const auto* geometry = std::get_if<alcedo::ImageGeometryUpdate>(&session.calls.back().write);
  ASSERT_NE(geometry, nullptr);
  ASSERT_TRUE(geometry->crop_rect.has_value());
  EXPECT_NEAR(geometry->crop_rect->w, 0.5f, 1e-5f);
  ASSERT_TRUE(geometry->aspect_preset.has_value());
  EXPECT_EQ(*geometry->aspect_preset, "free");
  // The overlay shows the value being edited.
  EXPECT_NEAR(interaction.cropRectNormalized().width(), 0.5, 1e-6);
}

TEST(EditorGeometryPanelQmlTest, OverlayDragSubmitsInteractivePatchesThenOneSettledPatch) {
  GeometrySession         session;
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* geometryPanel =
      harness.findObject<QObject>(QStringLiteral("editorAdjustmentPanel_geometry"));
  ASSERT_NE(geometryPanel, nullptr);
  interaction.editCropFrame(QRectF(0.2, 0.1, 0.4, 0.5), 3.0F, false);
  EXPECT_TRUE(geometryPanel->property("overlayInputActive").toBool());
  interaction.editCropFrame(QRectF(0.3, 0.15, 0.5, 0.7), 5.0F, true);
  EXPECT_FALSE(geometryPanel->property("overlayInputActive").toBool());

  ASSERT_EQ(session.calls.size(), 2u);
  EXPECT_FALSE(session.calls[0].settled);
  EXPECT_TRUE(session.calls[1].settled);
  EXPECT_NEAR(RectOf(session.calls[1]).x(), 0.3, 1e-6);
  EXPECT_NEAR(AngleOf(session.calls[1]), 5.0, 1e-6);
  auto* xModel        = harness.findObject<QObject>(QStringLiteral("geometryCropXModel"));
  auto* rotationModel = harness.findObject<QObject>(QStringLiteral("geometryRotationModel"));
  ASSERT_NE(xModel, nullptr);
  ASSERT_NE(rotationModel, nullptr);
  EXPECT_DOUBLE_EQ(xModel->property("value").toDouble(), 0.3);
  EXPECT_NEAR(rotationModel->property("value").toDouble(), 5.0, 1e-6);
}

TEST(EditorGeometryPanelQmlTest, RotationSliderShrinksTheCropToStayInsideTheSource) {
  GeometrySession         session(MakeSnapshot(QStringLiteral("free"), 0.0, 0.0, 1.0, 1.0, 0.0));
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* rotationModel = harness.findObject<QObject>(QStringLiteral("geometryRotationModel"));
  ASSERT_NE(rotationModel, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(rotationModel, "beginDrag"));
  ASSERT_TRUE(QMetaObject::invokeMethod(rotationModel, "updateDrag", Q_ARG(double, 10.0)));
  ASSERT_TRUE(QMetaObject::invokeMethod(rotationModel, "finishDrag"));

  ASSERT_EQ(session.calls.size(), 2u);
  const QRectF rect = RectOf(session.calls.back());
  EXPECT_LT(rect.width(), 1.0);
  EXPECT_NEAR(rect.center().x(), 0.5, 1e-5);
  EXPECT_NEAR(rect.center().y(), 0.5, 1e-5);
  EXPECT_NEAR(AngleOf(session.calls.back()), 10.0, 1e-6);
  const QRectF expected = interaction.clampCropRect(QRectF(0.0, 0.0, 1.0, 1.0), 10.0F);
  EXPECT_NEAR(rect.width(), expected.width(), 1e-5);
  EXPECT_NEAR(rect.height(), expected.height(), 1e-5);
}

TEST(EditorGeometryPanelQmlTest, SelectingAspectPresetSubmitsOneSettledCropOfThatShape) {
  GeometrySession         session(MakeSnapshot(QStringLiteral("free"), 0.0, 0.0, 1.0, 1.0, 0.0));
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* aspect = harness.findObject<QObject>(QStringLiteral("geometryAspectModel"));
  ASSERT_NE(aspect, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(aspect, "selectIndex", Q_ARG(int, 4)));  // 16:9

  ASSERT_EQ(session.calls.size(), 1u);
  EXPECT_TRUE(session.calls.back().settled);
  EXPECT_EQ(PresetOf(session.calls.back()), QStringLiteral("ratio_16_9"));
  const QRectF rect = RectOf(session.calls.back());
  EXPECT_NEAR((rect.width() * 6000.0) / (rect.height() * 4000.0), 16.0 / 9.0, 1e-4);
  EXPECT_NEAR(rect.center().x(), 0.5, 1e-4);
  EXPECT_TRUE(interaction.aspectLocked());
}

TEST(EditorGeometryPanelQmlTest, SelectingLandscapePresetOrientsCropForPortraitImage) {
  GeometrySession         session(MakeSnapshot(QStringLiteral("free"), 0.0, 0.0, 1.0, 1.0, 0.0));
  FakeGeometryInteraction interaction;
  interaction.presentSource(3000, 4000);
  AdjustmentStackHarness harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* aspect = harness.findObject<QObject>(QStringLiteral("geometryAspectModel"));
  ASSERT_NE(aspect, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(aspect, "selectIndex", Q_ARG(int, 4)));

  ASSERT_EQ(session.calls.size(), 1u);
  const QRectF rect = RectOf(session.calls.back());
  EXPECT_NEAR((rect.width() * 3000.0) / (rect.height() * 4000.0), 9.0 / 16.0, 1e-4);
  EXPECT_NEAR(rect.center().x(), 0.5, 1e-4);
}

TEST(EditorGeometryPanelQmlTest, CropResetUsesOneOwnerCommandWithoutSubmittingCropValues) {
  GeometrySession session(MakeSnapshot(QStringLiteral("ratio_16_9"), 0.1, 0.2, 0.5, 0.3, 6.0));
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  auto* reset = harness.findObject<QQuickItem>(QStringLiteral("geometryResetButton"));
  ASSERT_NE(reset, nullptr);
  ASSERT_TRUE(reset->isEnabled());
  ASSERT_TRUE(QMetaObject::invokeMethod(reset, "clicked"));
  EXPECT_EQ(session.geometry_commands, QStringList{QStringLiteral("geometry.reset_crop_rotate")});
  EXPECT_TRUE(session.calls.empty());
}

TEST(EditorGeometryPanelQmlTest, RotationButtonsWorkOnFullFrameAndRequireSourceSize) {
  GeometrySession         session;
  FakeGeometryInteraction interaction;
  interaction.presentSource(0, 0);
  AdjustmentStackHarness harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();
  auto* left  = harness.findObject<QQuickItem>(QStringLiteral("geometryRotateLeftButton"));
  auto* right = harness.findObject<QQuickItem>(QStringLiteral("geometryRotateRightButton"));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_FALSE(left->isEnabled());
  EXPECT_FALSE(right->isEnabled());
  EXPECT_EQ(left->property("actionName").toString(), QStringLiteral("Rotate Left"));
  EXPECT_EQ(right->property("actionName").toString(), QStringLiteral("Rotate Right"));
  EXPECT_TRUE(left->activeFocusOnTab());
  EXPECT_TRUE(right->activeFocusOnTab());
  ASSERT_TRUE(QMetaObject::invokeMethod(right, "clicked"));
  EXPECT_TRUE(session.geometry_commands.empty());

  interaction.presentSource(750, 500);
  EXPECT_TRUE(left->isEnabled());
  EXPECT_TRUE(right->isEnabled());
  EXPECT_EQ(interaction.cropRectNormalized(), QRectF(0.0, 0.0, 1.0, 1.0));
  ASSERT_TRUE(QMetaObject::invokeMethod(left, "clicked"));
  ASSERT_TRUE(QMetaObject::invokeMethod(right, "clicked"));
  EXPECT_EQ(session.geometry_commands, (QStringList{QStringLiteral("geometry.rotate_left"),
                                                    QStringLiteral("geometry.rotate_right")}));
  EXPECT_TRUE(session.calls.empty());
}

TEST(EditorGeometryPanelQmlTest, FlipButtonsExposeAccessibleNamesAndFollowTheRotationButtons) {
  GeometrySession         session;
  FakeGeometryInteraction interaction;
  interaction.presentSource(0, 0);
  AdjustmentStackHarness harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();
  auto* right      = harness.findObject<QQuickItem>(QStringLiteral("geometryRotateRightButton"));
  auto* horizontal = harness.findObject<QQuickItem>(QStringLiteral("geometryFlipHorizontalButton"));
  auto* vertical   = harness.findObject<QQuickItem>(QStringLiteral("geometryFlipVerticalButton"));
  ASSERT_NE(right, nullptr);
  ASSERT_NE(horizontal, nullptr);
  ASSERT_NE(vertical, nullptr);
  ASSERT_EQ(horizontal->parentItem(), right->parentItem());
  ASSERT_EQ(vertical->parentItem(), right->parentItem());
  const auto actions = right->parentItem()->childItems();
  EXPECT_LT(actions.indexOf(right), actions.indexOf(horizontal));
  EXPECT_LT(actions.indexOf(horizontal), actions.indexOf(vertical));
  for (auto* button : {horizontal, vertical}) {
    EXPECT_FALSE(button->isEnabled());
    EXPECT_TRUE(button->activeFocusOnTab());
    ASSERT_TRUE(QMetaObject::invokeMethod(button, "clicked"));
  }
  EXPECT_TRUE(session.geometry_commands.empty());

  interaction.presentSource(750, 500);
  EXPECT_EQ(interaction.cropRectNormalized(), QRectF(0.0, 0.0, 1.0, 1.0));
  ASSERT_TRUE(horizontal->isEnabled());
  ASSERT_TRUE(vertical->isEnabled());
  const auto* horizontal_accessible = QAccessible::queryAccessibleInterface(horizontal);
  const auto* vertical_accessible   = QAccessible::queryAccessibleInterface(vertical);
  ASSERT_NE(horizontal_accessible, nullptr);
  ASSERT_NE(vertical_accessible, nullptr);
  EXPECT_EQ(horizontal_accessible->text(QAccessible::Name), QStringLiteral("Flip Horizontal"));
  EXPECT_EQ(vertical_accessible->text(QAccessible::Name), QStringLiteral("Flip Vertical"));
  ASSERT_TRUE(QMetaObject::invokeMethod(horizontal, "clicked"));
  ASSERT_TRUE(QMetaObject::invokeMethod(vertical, "clicked"));
  EXPECT_EQ(session.geometry_commands, (QStringList{QStringLiteral("geometry.flip_horizontal"),
                                                    QStringLiteral("geometry.flip_vertical")}));
  EXPECT_TRUE(session.calls.empty());
}

TEST(EditorGeometryPanelQmlTest, RotatedCropDisplaysSwappedAxesAndKeepsOverlayInStoredAxes) {
  GeometrySession         session(MakeSnapshot(QStringLiteral("custom"), 0.1, 0.2, 0.5, 0.3, 6.0,
                                               QStringLiteral("rotate_90_cw")));
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();
  const auto value = [&](const QString& name) {
    auto* model = harness.findObject<QObject>(name);
    EXPECT_NE(model, nullptr);
    return model == nullptr ? 0.0 : model->property("value").toDouble();
  };
  EXPECT_NEAR(value(QStringLiteral("geometryCropWidthModel")), 0.3, 1e-6);
  EXPECT_NEAR(value(QStringLiteral("geometryCropHeightModel")), 0.5, 1e-6);
  EXPECT_NEAR(value(QStringLiteral("geometryAspectWidthModel")), 9.0, 1e-6);
  EXPECT_NEAR(value(QStringLiteral("geometryAspectHeightModel")), 16.0, 1e-6);
  EXPECT_NEAR(value(QStringLiteral("geometryRotationModel")), 6.0, 1e-6);
  EXPECT_NEAR(interaction.cropRectNormalized().x(), 0.1, 1e-6);
  EXPECT_NEAR(interaction.cropRectNormalized().width(), 0.5, 1e-6);
  EXPECT_TRUE(session.calls.empty());

  interaction.editCropFrame(QRectF(0.15, 0.2, 0.4, 0.3), 5.0f, true);
  ASSERT_EQ(session.calls.size(), 1u);
  EXPECT_NEAR(RectOf(session.calls.front()).width(), 0.4, 1e-6);
  EXPECT_NEAR(RectOf(session.calls.front()).height(), 0.3, 1e-6);
  EXPECT_FALSE(GeometryOf(session.calls.front()).orientation.has_value());
}

TEST(EditorGeometryPanelQmlTest, LeavingGeometryHidesTheOverlayWithoutSubmitting) {
  GeometrySession         session;
  FakeGeometryInteraction interaction;
  AdjustmentStackHarness  harness(&session, &interaction);
  ASSERT_NE(harness.root(), nullptr) << harness.errors().toStdString();

  ASSERT_TRUE(QMetaObject::invokeMethod(harness.root(), "returnFromGeometryToTone"));
  QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
  EXPECT_EQ(session.activeAdjustmentPanel(), QStringLiteral("tone"));
  EXPECT_TRUE(session.calls.empty());
  EXPECT_FALSE(interaction.cropToolEnabled());
  EXPECT_FALSE(interaction.cropOverlayVisible());
}

}  // namespace alcedo::ui::test

#include "editor_geometry_panel_qml_test.moc"

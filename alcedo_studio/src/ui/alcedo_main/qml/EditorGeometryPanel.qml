import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Alcedo.Main 1.0

// Geometry panel: manual transforms, crop and straightening of the whole image.
// The catalog supplies the control ranges. The document owner resolves any
// crop adjustment required by the accepted transform.
//
// Edits follow the path every adjustment uses: slider drags submit interactive
// patches and a settled patch on release; keyboard and field edits submit
// through the model's debounce; overlay drags submit interactive patches while
// moving and one settled patch on release. Each settled patch is one history
// commit. While the panel is open the viewport fits the editing image and the
// overlay shows the final crop and straightening.
Item {
    id: root
    objectName: "editorAdjustmentPanel_geometry"

    property var theme: null
    property var editorSession: null
    property var interaction: null
    property bool controlsEnabled: true
    property bool panelActive: false
    property bool restoring: false
    property bool useProjectiveCrop: false
    property bool overlayInputActive: false
    property var aspectEntries: []
    property string geometryError: ""

    readonly property color colText: theme ? theme.colText : appTheme.textColor
    readonly property color colMuted: theme ? theme.colTextMuted : appTheme.textMutedColor
    readonly property color colAccent: theme ? theme.colAccentPrimary : appTheme.accentColor
    readonly property color colCardSurface: theme ? theme.colCardSurface : appTheme.cardSurfaceColor
    readonly property color colCardBorder: theme ? theme.colCardBorder : appTheme.cardBorderColor
    readonly property color colHover: theme ? theme.colHover : appTheme.hoverColor
    readonly property bool canUseGeometry: root.controlsEnabled && root.interaction !== null
    // Uncropped source size of the presented frame. Zero until a frame arrives.
    readonly property int sourceImageWidth: root.interaction ? root.interaction.sourceImageWidth : 0
    readonly property int sourceImageHeight: root.interaction ? root.interaction.sourceImageHeight : 0
    readonly property bool hasSourceSize: root.sourceImageWidth > 0 && root.sourceImageHeight > 0
    readonly property double imageAspect: root.hasSourceSize
                                          ? root.sourceImageWidth / root.sourceImageHeight : 1.0
    readonly property var cropDefaults: parameterCatalog.uiDefault("crop_rotate")
    readonly property var cropSpecs: root.propertySpecs("crop_rotate")
    readonly property var transformDefaults: parameterCatalog.uiDefault("geometry_transform")
    readonly property var transformSpecs: root.propertySpecs("geometry_transform")
    readonly property bool canEditGeometry: root.canUseGeometry && root.hasSourceSize
                                            && !!root.editorSession && root.editorSession.canEdit
    readonly property int geometryImageId: root.editorSession
                                           ? Number(root.editorSession.imageId || 0) : 0
    readonly property bool transformDragActive: {
        for (let i = 0; i < transformSliders.count; ++i) {
            const slider = transformSliders.itemAt(i)
            if (slider && slider.model.dragActive)
                return true
        }
        return false
    }
    readonly property string transformInputError: {
        for (let i = 0; i < transformSliders.count; ++i) {
            const slider = transformSliders.itemAt(i)
            if (slider && !slider.model.valid)
                return slider.model.errorMessage
        }
        return ""
    }
    readonly property bool inputActive: root.overlayInputActive
                                      || root.transformDragActive
                                      || cropXModel.dragActive
                                      || cropYModel.dragActive
                                      || cropWidthModel.dragActive
                                      || cropHeightModel.dragActive
                                      || rotationModel.dragActive
                                      || aspectWidthModel.dragActive
                                      || aspectHeightModel.dragActive

    EditorParameterCatalog {
        id: parameterCatalog
        objectName: "geometryParameterCatalog"
    }

    /// Read the named property specs for a catalog object field.
    function propertySpecs(field) {
        var result = {}
        const properties = parameterCatalog.entry(field).properties || []
        for (var i = 0; i < properties.length; ++i)
            result[String(properties[i].name)] = properties[i]
        return result
    }

    /// Close any keyboard edit from a different slider before submitting a new action.
    function prepareGeometryEdit(except) {
        const cropModels = [cropXModel, cropYModel, cropWidthModel, cropHeightModel,
                            rotationModel, aspectWidthModel, aspectHeightModel]
        for (const model of cropModels) {
            if (model !== except)
                model.commitImmediately()
        }
        for (let i = 0; i < transformSliders.count; ++i) {
            const slider = transformSliders.itemAt(i)
            if (slider && slider.model !== except)
                slider.model.commitImmediately()
        }
        root.geometryError = ""
    }

    /// Build a write for the one changed transform field through the catalog.
    function transformParams(key, value) {
        const change = {}
        change[key] = value
        return parameterCatalog.modelParamsJson("geometry_transform", change)
    }

    /// Restore the projected values without submitting a change.
    function loadTransformSnapshot(snapshot) {
        const entry = snapshot ? snapshot["geometry_transform"] : undefined
        const values = entry ? parameterCatalog.uiValue("geometry_transform", entry)
                             : root.transformDefaults
        for (let i = 0; i < transformSliders.count; ++i) {
            const slider = transformSliders.itemAt(i)
            if (slider && !slider.model.hasPendingSettled())
                slider.model.value = values[slider.transformKey]
        }
        constrainCrop.checked = values.constrain_crop
    }

    function buildAspectEntries() {
        var result = []
        const presets = parameterCatalog.options("crop_rotate", "aspect_preset")
        for (var i = 0; i < presets.length; ++i)
            result.push({ value: String(presets[i].value), label: qsTr(String(presets[i].label)) })
        return result
    }

    function indexOfAspect(value) {
        for (var i = 0; i < aspectModel.entries.length; ++i) {
            if (String(aspectModel.entries[i].value) === String(value))
                return i
        }
        return -1
    }

    /// The whole crop state of the models in catalog UI units.
    function cropState() {
        return {
            x: Number(cropXModel.value),
            y: Number(cropYModel.value),
            width: Number(cropWidthModel.value),
            height: Number(cropHeightModel.value),
            angle_degrees: Number(rotationModel.value),
            aspect_preset: String(aspectModel.currentValue),
            aspect_width: Number(aspectWidthModel.value),
            aspect_height: Number(aspectHeightModel.value)
        }
    }

    /// Crop width / height in source pixels for a locked aspect, oriented to
    /// the source (16:9 becomes 9:16 for a portrait photograph). Undefined when
    /// the aspect is not locked.
    function lockedAspectRatio() {
        return parameterCatalog.cropLockedAspectRatio(root.cropState(), root.sourceImageWidth,
                                                      root.sourceImageHeight)
    }

    function rectFromModels() {
        return Qt.rect(cropXModel.value, cropYModel.value, cropWidthModel.value, cropHeightModel.value)
    }

    function setRectModels(rect) {
        const wasRestoring = root.restoring
        root.restoring = true
        cropXModel.value = Number(rect.x)
        cropYModel.value = Number(rect.y)
        cropWidthModel.value = Number(rect.width)
        cropHeightModel.value = Number(rect.height)
        root.restoring = wasRestoring
    }

    /// Write a constrained crop state to the models without a submit.
    function setStateModels(state) {
        const wasRestoring = root.restoring
        root.restoring = true
        root.setRectModels(Qt.rect(state.x, state.y, state.width, state.height))
        aspectWidthModel.value = Number(state.aspect_width)
        aspectHeightModel.value = Number(state.aspect_height)
        const presetIndex = root.indexOfAspect(state.aspect_preset)
        if (presetIndex >= 0)
            aspectModel.currentIndex = presetIndex
        root.restoring = wasRestoring
    }

    function syncOverlay() {
        if (!root.interaction)
            return
        const ratio = root.lockedAspectRatio()
        const locked = ratio !== undefined
        root.interaction.setCropAspectLock(locked, locked ? ratio : 1.0)
        root.interaction.setCropRotationDegrees(rotationModel.value)
        root.interaction.setCropRectNormalized(root.rectFromModels())
    }

    /// Apply the catalog crop constraint after the change @p driver (position,
    /// width, height, rotation, aspect_preset, aspect_size), then show the
    /// result on the overlay. A fixed preset sets the aspect size to its ratio;
    /// an aspect size edit selects the custom preset.
    function applyConstraints(driver) {
        const state = parameterCatalog.constrainCrop(root.cropState(), driver,
                                                     root.sourceImageWidth,
                                                     root.sourceImageHeight,
                                                     root.useProjectiveCrop)
        if (state.x !== undefined)
            root.setStateModels(state)
        root.wireEnabled()
        root.syncOverlay()
    }

    function buildCropParams() {
        return parameterCatalog.modelParamsJson("crop_rotate", root.cropState())
    }

    /// paramsBuilder for the models: constrain, then send the whole geometry.
    function paramsAfter(driver) {
        root.applyConstraints(driver)
        return root.buildCropParams()
    }

    function submitGeometry(settled) {
        if (!root.editorSession || typeof root.editorSession.submitPatch !== "function")
            return false
        return root.editorSession.submitPatch("crop_rotate", root.buildCropParams(), settled)
    }

    /// Enter / numpad Enter in the viewport: leave Geometry for Tone.
    function returnToTone() {
        if (root.editorSession)
            root.editorSession.activeAdjustmentPanel = "tone"
    }

    function resetModelsToDefaults() {
        root.restoring = true
        cropXModel.value = cropXModel.defaultValue
        cropYModel.value = cropYModel.defaultValue
        cropWidthModel.value = cropWidthModel.defaultValue
        cropHeightModel.value = cropHeightModel.defaultValue
        rotationModel.value = rotationModel.defaultValue
        aspectModel.currentIndex = aspectModel.defaultIndex
        aspectWidthModel.value = aspectWidthModel.defaultValue
        aspectHeightModel.value = aspectHeightModel.defaultValue
        root.restoring = false
    }

    /// Submit one scoped reset and wait for the owner's accepted projection.
    function resetSection(scope) {
        if (!root.canEditGeometry)
            return
        root.prepareGeometryEdit(null)
        root.editorSession.submitPatch("geometry_transform", JSON.stringify({reset: scope}), true)
    }

    function resetGeometry() {
        root.resetSection("crop_and_rotate")
    }

    function loadCropSnapshot(snapshot) {
        const raw = snapshot ? snapshot["crop_rotate"] : undefined
        const entry = raw && raw["crop_rotate"] !== undefined ? raw["crop_rotate"] : raw
        root.useProjectiveCrop = entry ? entry["use_projective_crop"] === true : false
        if (!entry) {
            root.resetModelsToDefaults()
            return
        }
        const rect = entry["crop_rect"] || {}
        const aspect = entry["aspect_ratio"] || {}
        root.restoring = true
        cropXModel.value = Number(rect["x"] !== undefined ? rect["x"] : 0.0)
        cropYModel.value = Number(rect["y"] !== undefined ? rect["y"] : 0.0)
        cropWidthModel.value = Number(rect["w"] !== undefined ? rect["w"] : 1.0)
        cropHeightModel.value = Number(rect["h"] !== undefined ? rect["h"] : 1.0)
        rotationModel.value = Number(entry["angle_degrees"] !== undefined ? entry["angle_degrees"] : 0.0)
        aspectWidthModel.value = Number(aspect["width"] !== undefined ? aspect["width"] : 1.0)
        aspectHeightModel.value = Number(aspect["height"] !== undefined ? aspect["height"] : 1.0)
        const presetIndex = root.indexOfAspect(entry["aspect_ratio_preset"] !== undefined
                                               ? entry["aspect_ratio_preset"] : "free")
        aspectModel.currentIndex = presetIndex >= 0 ? presetIndex : aspectModel.defaultIndex
        root.restoring = false
    }

    function loadFromSnapshot(snapshot) {
        if (snapshot === undefined || snapshot === null)
            return
        // An input sequence owns the models until its settled patch echoes back.
        if (root.inputActive)
            return
        if (!root.aspectEntries.length) {
            root.aspectEntries = root.buildAspectEntries()
            aspectModel.entries = root.aspectEntries
        }
        root.loadCropSnapshot(snapshot)
        root.loadTransformSnapshot(snapshot)
        root.wireEnabled()
        if (root.panelActive)
            root.syncOverlay()
    }

    function enterGeometryTool() {
        if (!root.panelActive || !root.canUseGeometry)
            return
        root.interaction.setCropToolEnabled(true)
        root.interaction.setCropOverlayVisible(true)
        root.syncOverlay()
    }

    function leaveGeometryTool() {
        if (!root.interaction)
            return
        root.interaction.setCropToolEnabled(false)
        root.interaction.setCropOverlayVisible(false)
        root.overlayInputActive = false
    }

    function wireEnabled() {
        const enabled = root.canEditGeometry
        cropXModel.enabled = enabled
        cropYModel.enabled = enabled
        cropWidthModel.enabled = enabled
        cropHeightModel.enabled = enabled
        rotationModel.enabled = enabled
        aspectModel.enabled = enabled
        aspectWidthModel.enabled = enabled && aspectModel.currentValue === "custom"
        aspectHeightModel.enabled = enabled && aspectModel.currentValue === "custom"
    }

    onControlsEnabledChanged: {
        root.wireEnabled()
        if (root.controlsEnabled)
            root.enterGeometryTool()
        else
            root.leaveGeometryTool()
    }
    onPanelActiveChanged: {
        if (root.panelActive)
            root.enterGeometryTool()
        else
            root.leaveGeometryTool()
    }
    onInteractionChanged: root.enterGeometryTool()
    onCanEditGeometryChanged: root.wireEnabled()
    onGeometryImageIdChanged: root.geometryError = ""
    onEditorSessionChanged: {
        root.geometryError = ""
        root.loadFromSnapshot(root.editorSession ? root.editorSession.adjustmentSnapshot : null)
    }

    Connections {
        target: root.editorSession
        ignoreUnknownSignals: true
        function onGeometryAdjustmentRejected(message) {
            root.geometryError = message
            root.loadFromSnapshot(root.editorSession.adjustmentSnapshot)
        }
    }

    EditorAdjustmentValueModel {
        id: cropXModel
        objectName: "geometryCropXModel"
        fieldKey: "crop_rotate"
        label: qsTr("Crop X")
        // With a rotation the unrotated rectangle of the frame may start left of
        // the source; the rotated-source constraint decides the valid range.
        minimum: root.cropSpecs.x.ui_min
        maximum: root.cropSpecs.x.ui_max
        defaultValue: root.cropDefaults.x
        step: root.cropSpecs.x.ui_step
        precision: root.cropSpecs.x.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("position") }
    }
    EditorAdjustmentValueModel {
        id: cropYModel
        objectName: "geometryCropYModel"
        fieldKey: "crop_rotate"
        label: qsTr("Crop Y")
        minimum: root.cropSpecs.y.ui_min
        maximum: root.cropSpecs.y.ui_max
        defaultValue: root.cropDefaults.y
        step: root.cropSpecs.y.ui_step
        precision: root.cropSpecs.y.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("position") }
    }
    EditorAdjustmentValueModel {
        id: cropWidthModel
        objectName: "geometryCropWidthModel"
        fieldKey: "crop_rotate"
        label: qsTr("Crop Width")
        minimum: root.cropSpecs.width.ui_min
        maximum: root.cropSpecs.width.ui_max
        defaultValue: root.cropDefaults.width
        step: root.cropSpecs.width.ui_step
        precision: root.cropSpecs.width.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("width") }
    }
    EditorAdjustmentValueModel {
        id: cropHeightModel
        objectName: "geometryCropHeightModel"
        fieldKey: "crop_rotate"
        label: qsTr("Crop Height")
        minimum: root.cropSpecs.height.ui_min
        maximum: root.cropSpecs.height.ui_max
        defaultValue: root.cropDefaults.height
        step: root.cropSpecs.height.ui_step
        precision: root.cropSpecs.height.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("height") }
    }
    EditorAdjustmentValueModel {
        id: rotationModel
        objectName: "geometryRotationModel"
        fieldKey: "crop_rotate"
        label: qsTr("Rotation")
        minimum: root.cropSpecs.angle_degrees.ui_min
        maximum: root.cropSpecs.angle_degrees.ui_max
        defaultValue: root.cropDefaults.angle_degrees
        step: root.cropSpecs.angle_degrees.ui_step
        precision: root.cropSpecs.angle_degrees.ui_decimals
        suffix: "°"
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("rotation") }
    }
    EditorAdjustmentEnumModel {
        id: aspectModel
        objectName: "geometryAspectModel"
        fieldKey: "crop_rotate"
        label: qsTr("Aspect Ratio")
        entries: root.aspectEntries
        defaultIndex: 0
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("aspect_preset") }
    }
    EditorAdjustmentValueModel {
        id: aspectWidthModel
        objectName: "geometryAspectWidthModel"
        fieldKey: "crop_rotate"
        label: qsTr("Aspect Width")
        minimum: root.cropSpecs.aspect_width.ui_min
        maximum: root.cropSpecs.aspect_width.ui_max
        defaultValue: root.cropDefaults.aspect_width
        step: root.cropSpecs.aspect_width.ui_step
        precision: root.cropSpecs.aspect_width.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("aspect_size") }
    }
    EditorAdjustmentValueModel {
        id: aspectHeightModel
        objectName: "geometryAspectHeightModel"
        fieldKey: "crop_rotate"
        label: qsTr("Aspect Height")
        minimum: root.cropSpecs.aspect_height.ui_min
        maximum: root.cropSpecs.aspect_height.ui_max
        defaultValue: root.cropDefaults.aspect_height
        step: root.cropSpecs.aspect_height.ui_step
        precision: root.cropSpecs.aspect_height.ui_decimals
        submitter: root.editorSession
        paramsBuilder: function () { return root.paramsAfter("aspect_size") }
    }

    Connections {
        target: root.interaction
        ignoreUnknownSignals: true
        // Overlay drag: interactive patches while moving, one settled patch on release.
        function onCropFrameEdited(rect, degrees, isFinal) {
            if (!root.panelActive || root.restoring || !root.canEditGeometry)
                return
            if (!root.overlayInputActive)
                root.prepareGeometryEdit(null)
            root.overlayInputActive = !isFinal
            root.setRectModels(rect)
            const wasRestoring = root.restoring
            root.restoring = true
            rotationModel.value = Number(degrees)
            root.restoring = wasRestoring
            root.submitGeometry(isFinal)
        }
        function onImageGeometryChanged() {
            // The source size arrived with a presented frame; redraw the frame
            // and its aspect lock against it.
            if (root.panelActive)
                root.syncOverlay()
        }
    }

    Flickable {
        id: scroller
        objectName: "editorGeometryPanelScroll"
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: panelColumn.implicitHeight + appTheme.spaceMd
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        property int inputLockCount: 0
        function beginInputLock() {
            if (inputLockCount === 0)
                interactive = false
            inputLockCount += 1
        }
        function endInputLock() {
            if (inputLockCount > 0)
                inputLockCount -= 1
            if (inputLockCount <= 0) {
                inputLockCount = 0
                interactive = true
            }
        }

        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            grabPermissions: PointerHandler.CanTakeOverFromItems
                             | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                             | PointerHandler.ApprovesTakeOverByAnything
            onWheel: function (event) {
                var step = event.pixelDelta.y !== 0
                           ? event.pixelDelta.y
                           : event.angleDelta.y / 120 * 48
                var maxY = Math.max(0, scroller.contentHeight - scroller.height)
                scroller.contentY = Math.max(0, Math.min(maxY, scroller.contentY - step))
                event.accepted = true
            }
        }

        ColumnLayout {
            id: panelColumn
            width: root.width
            spacing: appTheme.spaceSm

            RowLayout {
                Layout.fillWidth: true
                spacing: appTheme.spaceSm
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Geometry")
                    color: root.colText
                    font.pixelSize: appTheme.fontSizeTitle
                    font.weight: appTheme.fontWeightHeading
                }
                IconActionButton {
                    objectName: "geometryResetAllButton"
                    compact: true
                    enabled: root.canEditGeometry
                    iconSrc: "qrc:/panel_icons/reset.svg"
                    actionName: qsTr("Reset All Geometry")
                    onClicked: root.resetSection("all")
                }
            }

            Label {
                objectName: "editorGeometryWholeImageScope"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Geometry applies to the whole image.")
                color: root.colMuted
                font.pixelSize: appTheme.fontSizeCaption
            }

            Label {
                objectName: "editorGeometrySourceAspect"
                Layout.fillWidth: true
                text: root.hasSourceSize
                      ? qsTr("Source %1 × %2 (aspect %3)").arg(root.sourceImageWidth)
                                                         .arg(root.sourceImageHeight)
                                                         .arg(root.imageAspect.toFixed(3))
                      : qsTr("Source size is available after the image is shown.")
                color: root.colMuted
                font.pixelSize: appTheme.fontSizeCaption
                wrapMode: Text.Wrap
            }

            AlertBadge {
                objectName: "geometryError"
                text: root.geometryError || root.transformInputError
                    || (root.editorSession ? (root.editorSession.lastError || "") : "")
            }

            CollapsibleSection {
                objectName: "editorAdjustmentGroupShell_geometry_transform"
                title: qsTr("Transform")
                controlsEnabled: root.canEditGeometry
                bodyContentHeight: transformControls.implicitHeight
                headerActions: Component {
                    IconActionButton {
                        objectName: "geometryResetTransformButton"
                        compact: true
                        enabled: root.canEditGeometry
                        iconSrc: "qrc:/panel_icons/reset.svg"
                        actionName: qsTr("Reset Transform")
                        onClicked: root.resetSection("transform")
                    }
                }

                ColumnLayout {
                    id: transformControls
                    width: parent.width
                    spacing: appTheme.spaceSm

                    Repeater {
                        id: transformSliders
                        model: [
                            { key: "vertical", title: qsTr("Vertical"),
                                hint: qsTr("Adjust vertical perspective. Positive values widen the bottom.") },
                            { key: "horizontal", title: qsTr("Horizontal"),
                                hint: qsTr("Adjust horizontal perspective. Positive values widen the right.") },
                            { key: "aspect", title: qsTr("Aspect"),
                                hint: qsTr("Stretch image content. Crop Aspect Ratio sets the frame shape.") },
                            { key: "scale", title: qsTr("Scale"),
                                hint: qsTr("Scale the image about its center independently of viewport zoom.") },
                            { key: "offset_x", title: qsTr("X Offset"),
                                hint: qsTr("Move image content horizontally. Crop X moves the crop frame.") },
                            { key: "offset_y", title: qsTr("Y Offset"),
                                hint: qsTr("Move image content vertically. Crop Y moves the crop frame.") }
                        ]

                        delegate: AdjustmentSlider {
                            id: transformSlider
                            required property var modelData
                            readonly property string transformKey: modelData.key
                            readonly property var spec: root.transformSpecs[transformKey]
                            objectName: "geometryTransformSlider_" + transformKey
                            partName: objectName
                            flickable: scroller
                            infoText: modelData.hint
                            onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                            model: EditorAdjustmentValueModel {
                                fieldKey: "geometry_transform"
                                label: transformSlider.modelData.title
                                minimum: transformSlider.spec.ui_min
                                maximum: transformSlider.spec.ui_max
                                railMinimum: minimum
                                railMaximum: maximum
                                step: transformSlider.spec.ui_step
                                precision: transformSlider.spec.ui_decimals
                                defaultValue: root.transformDefaults[transformSlider.transformKey]
                                suffix: transformSlider.transformKey === "scale" ? "%" : ""
                                enabled: root.canEditGeometry
                                submitter: root.editorSession
                                paramsBuilder: function (value) {
                                    return root.transformParams(transformSlider.transformKey, value)
                                }
                            }
                        }
                    }

                    ThemeCheckBox {
                        id: constrainCrop
                        objectName: "geometryConstrainCrop"
                        text: qsTr("Constrain Crop")
                        enabled: root.canEditGeometry
                        Accessible.description: qsTr("Keep the crop inside the transformed image.")
                        ToolTip.visible: constrainHover.hovered
                        ToolTip.text: Accessible.description
                        HoverHandler { id: constrainHover }
                        onToggled: function (checked) {
                            root.prepareGeometryEdit(null)
                            root.editorSession.submitPatch("geometry_transform",
                                                          root.transformParams("constrain_crop", checked),
                                                          true)
                        }
                    }
                }
            }

            CollapsibleSection {
                id: cropSection
                objectName: "editorAdjustmentGroupShell_geometry_crop"
                Layout.fillWidth: true
                title: qsTr("Crop and Rotate")
                expanded: true
                controlsEnabled: root.canEditGeometry
                surfaceColor: root.colCardSurface
                disabledSurfaceColor: root.colCardSurface
                borderColor: root.colCardBorder
                textColor: root.colText
                mutedColor: root.colMuted
                hoverColor: root.colHover
                accentColor: root.colAccent
                bodyContentHeight: cropControls.implicitHeight
                headerActions: Component {
                    IconActionButton {
                        objectName: "geometryResetButton"
                        compact: true
                        enabled: root.canEditGeometry
                        iconSrc: "qrc:/panel_icons/reset.svg"
                        actionName: qsTr("Reset crop and rotation")
                        onClicked: root.resetGeometry()
                    }
                }

                ColumnLayout {
                    id: cropControls
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    spacing: appTheme.spaceSm

                    AdjustmentCombo {
                        objectName: "geometryAspectCombo"
                        Layout.fillWidth: true
                        model: aspectModel
                        infoText: qsTr("Set the shape of the output frame.")
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryAspectWidthSlider"
                        Layout.fillWidth: true
                        visible: aspectModel.currentValue === "custom"
                        model: aspectWidthModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryAspectHeightSlider"
                        Layout.fillWidth: true
                        visible: aspectModel.currentValue === "custom"
                        model: aspectHeightModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryCropXSlider"
                        Layout.fillWidth: true
                        model: cropXModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryCropYSlider"
                        Layout.fillWidth: true
                        model: cropYModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryCropWidthSlider"
                        Layout.fillWidth: true
                        model: cropWidthModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryCropHeightSlider"
                        Layout.fillWidth: true
                        model: cropHeightModel
                    }
                    AdjustmentSlider {
                        flickable: scroller
                        onEditStarting: function (model) { root.prepareGeometryEdit(model) }
                        objectName: "geometryRotationSlider"
                        Layout.fillWidth: true
                        model: rotationModel
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    }

    Component.onCompleted: {
        root.aspectEntries = root.buildAspectEntries()
        aspectModel.entries = root.aspectEntries
        root.resetModelsToDefaults()
        root.loadTransformSnapshot(null)
        root.wireEnabled()
        root.loadFromSnapshot(root.editorSession ? root.editorSession.adjustmentSnapshot : null)
        root.enterGeometryTool()
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "controls" as UI

ApplicationWindow {
    id: window
    objectName: "geometryDesignPreview"
    visible: true
    width: 1200
    height: 940
    minimumWidth: 720
    minimumHeight: 560
    title: qsTr("Alcedo Studio Geometry Preview")
    color: appTheme.bgCanvasColor
    font.family: inter.name
    font.pixelSize: appTheme.fontSizeBody
    palette.text: appTheme.textColor
    palette.windowText: appTheme.textColor
    palette.highlight: appTheme.editorListSelectedFillColor
    palette.highlightedText: appTheme.editorListSelectedInkColor

    // Window dimensions and schematic crop placement are preview composition,
    // not new product tokens. Shared chrome keeps its AppTheme dimensions.
    readonly property bool hasImage: previewState.currentValue !== "empty"
    readonly property bool aspectLocked: aspect.currentValue !== "free"
    property bool constrainCrop: true
    property string lastAction: qsTr("Static crop overlay. Image transforms are not connected.")
    property int triangleOrientation: 0
    property int spiralOrientation: 0
    property bool guidePreferenceInvalid: false

    FontLoader {
        id: inter
        source: "PREVIEW_CONFIG_URL/fonts/main_Inter.ttf"
    }
    FontLoader {
        source: "PREVIEW_CONFIG_URL/fonts/main_Manrope.ttf"
    }
    FontLoader {
        source: "PREVIEW_CONFIG_URL/fonts/main_IBM.ttf"
    }

    function resetCrop() {
        for (const value of [cropX, cropY, cropWidth, cropHeight, rotation, customWidth, customHeight])
            value.reset();
        aspect.reset();
    }
    function resetTransform() {
        for (const value of [vertical, horizontal, stretch, scale, offsetX, offsetY])
            value.reset();
        constrainCrop = true;
    }
    function setAspectLocked(locked) {
        if (!locked) {
            aspect.selectIndex(0);
        } else {
            // The schematic source is 3:2. Project only the sample menu and fields.
            customWidth.editValue(cropWidth.value * 3);
            customHeight.editValue(cropHeight.value * 2);
            aspect.selectIndex(1);
        }
    }
    function changeGuideOrientation() {
        const spiral = guide.currentValue === "spiral";
        if (spiral)
            spiralOrientation = (spiralOrientation + 1) % 4;
        else
            triangleOrientation = (triangleOrientation + 1) % 2;
        lastAction = qsTr("Guide orientation %1 selected. UI preview only.")
            .arg((spiral ? spiralOrientation : triangleOrientation) + 1);
    }
    function resetGuide() {
        guide.reset();
        guideVisibility.reset();
        triangleOrientation = 0;
        spiralOrientation = 0;
        previewState.selectIndex(0);
    }

    PreviewChoice {
        id: previewState
        label: qsTr("Preview state")
        entries: [
            {
                value: "editable",
                label: qsTr("Editable image")
            },
            {
                value: "empty",
                label: qsTr("No image")
            },
            {
                value: "error",
                label: qsTr("Validation error")
            },
            {
                value: "guide_error",
                label: qsTr("Guide preference error")
            }
        ]
        onCurrentValueChanged: window.guidePreferenceInvalid = currentValue === "guide_error"
    }
    PreviewChoice {
        id: aspect
        label: qsTr("Aspect")
        enabled: window.hasImage
        currentIndex: 2
        entries: [
            {
                value: "free",
                label: qsTr("Free")
            },
            {
                value: "custom",
                label: qsTr("Custom")
            },
            {
                value: "ratio_1_5_1_native_vistavision",
                label: qsTr("1.5:1 (native or VistaVision)")
            },
            {
                value: "ratio_2_35_1_35mm",
                label: qsTr("2.35:1 (35mm)")
            },
            {
                value: "ratio_1_1",
                label: qsTr("1:1")
            },
            {
                value: "ratio_16_9",
                label: qsTr("16:9")
            },
            {
                value: "ratio_1_9_1_imax",
                label: qsTr("1.9:1 (IMAX)")
            },
            {
                value: "ratio_1_85_1_dci",
                label: qsTr("1.85:1 (DCI)")
            },
            {
                value: "ratio_2_2_1_70mm",
                label: qsTr("2.2:1 (70mm)")
            },
            {
                value: "ratio_1_43_1_70mm_imax",
                label: qsTr("1.43:1 (70mm IMAX)")
            },
            {
                value: "ratio_4_3_35mm",
                label: qsTr("4:3 (35mm)")
            },
            {
                value: "ratio_2_76_1_panavision_ultra",
                label: qsTr("2.76:1 (Panavision Ultra)")
            }
        ]
    }
    PreviewChoice {
        id: guide
        label: qsTr("Guide")
        enabled: window.hasImage
        defaultIndex: 1
        currentIndex: defaultIndex
        entries: [
            {
                value: "none",
                label: qsTr("None")
            },
            {
                value: "thirds",
                label: qsTr("Thirds")
            },
            {
                value: "grid",
                label: qsTr("Grid")
            },
            {
                value: "ratio",
                label: qsTr("Golden Ratio")
            },
            {
                value: "spiral",
                label: qsTr("Golden Spiral")
            },
            {
                value: "diagonal",
                label: qsTr("Diagonal")
            },
            {
                value: "triangle",
                label: qsTr("Triangle")
            }
        ]
    }
    PreviewChoice {
        id: invalidGuide
        label: qsTr("Guide")
        enabled: false
        entries: [{ value: "invalid", label: qsTr("Unknown saved guide") }]
    }
    PreviewChoice {
        id: guideVisibility
        label: qsTr("Show guide")
        enabled: window.hasImage
        entries: [
            {
                value: "always",
                label: qsTr("Always")
            },
            {
                value: "input",
                label: qsTr("During input")
            },
            {
                value: "never",
                label: qsTr("Never")
            }
        ]
    }
    PreviewValue {
        id: customWidth
        label: qsTr("Custom width")
        minimum: 0.0001
        maximum: 100
        defaultValue: 1
        value: 3
        step: 0.01
        precision: 2
        enabled: window.hasImage
    }
    PreviewValue {
        id: customHeight
        label: qsTr("Custom height")
        minimum: 0.0001
        maximum: 100
        defaultValue: 1
        value: 2
        step: 0.01
        precision: 2
        enabled: window.hasImage
    }
    PreviewValue {
        id: cropX
        label: qsTr("Crop X")
        minimum: -1
        maximum: 1
        value: 0.08
        precision: 3
        step: 0.001
        enabled: window.hasImage
    }
    PreviewValue {
        id: cropY
        label: qsTr("Crop Y")
        minimum: -1
        maximum: 1
        value: 0.08
        precision: 3
        step: 0.001
        enabled: window.hasImage
    }
    PreviewValue {
        id: cropWidth
        label: qsTr("Crop width")
        minimum: 0.0001
        maximum: 1
        defaultValue: 1
        value: 0.84
        precision: 3
        step: 0.001
        enabled: window.hasImage
    }
    PreviewValue {
        id: cropHeight
        label: qsTr("Crop height")
        minimum: 0.0001
        maximum: 1
        defaultValue: 1
        value: 0.84
        precision: 3
        step: 0.001
        enabled: window.hasImage
    }
    PreviewValue {
        id: rotation
        label: qsTr("Rotation")
        minimum: -180
        maximum: 180
        suffix: "°"
        enabled: window.hasImage
    }
    PreviewValue {
        id: vertical
        label: qsTr("Vertical")
        enabled: window.hasImage
    }
    PreviewValue {
        id: horizontal
        label: qsTr("Horizontal")
        enabled: window.hasImage
    }
    PreviewValue {
        id: stretch
        label: qsTr("Aspect")
        enabled: window.hasImage
    }
    PreviewValue {
        id: scale
        label: qsTr("Scale")
        minimum: 50
        maximum: 150
        defaultValue: 100
        suffix: "%"
        enabled: window.hasImage
    }
    PreviewValue {
        id: offsetX
        label: qsTr("X Offset")
        enabled: window.hasImage
    }
    PreviewValue {
        id: offsetY
        label: qsTr("Y Offset")
        enabled: window.hasImage
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: appTheme.spaceLg
        spacing: appTheme.spaceMd

        RowLayout {
            Layout.fillWidth: true
            spacing: appTheme.spaceXl
            ColumnLayout {
                Layout.fillWidth: true
                spacing: appTheme.spaceXs
                Label {
                    text: qsTr("Geometry UI preview")
                    color: appTheme.textColor
                    font.pixelSize: appTheme.fontSizeSection
                    font.weight: appTheme.fontWeightHeading
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Inspect layout and sample controls. No image processing or document writes.")
                    color: appTheme.textMutedColor
                    font.pixelSize: appTheme.fontSizeCaption
                    wrapMode: Text.Wrap
                }
            }
            UI.AdjustmentCombo {
                Layout.preferredWidth: appTheme.editorSidePanelWidth
                Layout.fillWidth: false
                model: previewState
                showResetButton: false
                controlObjectName: "previewStateSelector"
            }
        }

        SplitView {
            id: split
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: Rectangle {
                implicitWidth: appTheme.spaceMd
                color: SplitHandle.hovered ? appTheme.hoverColor : appTheme.bgCanvasColor
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: appTheme.editorSidePanelWidthMin
                color: appTheme.cardSurfaceColor
                radius: appTheme.panelRadius

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: appTheme.spaceLg
                    spacing: appTheme.spaceMd
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Composition")
                            color: appTheme.textColor
                            font.pixelSize: appTheme.fontSizeTitle
                            font.weight: appTheme.fontWeightStrong
                        }
                        Label {
                            text: qsTr("Fit")
                            color: appTheme.textMutedColor
                            font.pixelSize: appTheme.fontSizeCaption
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Rectangle {
                            id: sourceFrame
                            anchors.centerIn: parent
                            width: Math.min(parent.width, parent.height * 1.5)
                            height: width / 1.5
                            color: appTheme.bgBaseColor
                            border.color: appTheme.cardBorderColor
                            visible: window.hasImage

                            Label {
                                anchors.centerIn: parent
                                text: qsTr("Image area")
                                color: appTheme.textMutedColor
                                font.pixelSize: appTheme.fontSizeHeadline
                                font.family: appTheme.headlineFontFamily
                            }
                            Rectangle {
                                id: cropFrame
                                x: parent.width * 0.08
                                y: parent.height * 0.08
                                width: parent.width * 0.84
                                height: parent.height * 0.84
                                color: "transparent"
                                border.width: 1
                                border.color: appTheme.textColor

                                Repeater {
                                    model: 2
                                    Rectangle {
                                        required property int index
                                        x: (index + 1) * cropFrame.width / 3
                                        width: 1
                                        height: cropFrame.height
                                        color: appTheme.textMutedColor
                                    }
                                }
                                Repeater {
                                    model: 2
                                    Rectangle {
                                        required property int index
                                        y: (index + 1) * cropFrame.height / 3
                                        width: cropFrame.width
                                        height: 1
                                        color: appTheme.textMutedColor
                                    }
                                }
                                Repeater {
                                    model: 4
                                    Rectangle {
                                        required property int index
                                        x: index % 2 ? cropFrame.width - width / 2 : -width / 2
                                        y: index >= 2 ? cropFrame.height - height / 2 : -height / 2
                                        width: appTheme.spaceSm
                                        height: appTheme.spaceSm
                                        color: appTheme.textColor
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !window.hasImage
                            text: qsTr("No editable image")
                            color: appTheme.textMutedColor
                            font.pixelSize: appTheme.fontSizeHeadline
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: window.lastAction
                        color: appTheme.textMutedColor
                        font.pixelSize: appTheme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Drag the divider to inspect the 260–460 px panel. Collapse Crop & Rotate to see Transform.")
                        color: appTheme.textMutedColor
                        font.pixelSize: appTheme.fontSizeCaption
                        wrapMode: Text.Wrap
                    }
                }
            }

            Rectangle {
                SplitView.preferredWidth: appTheme.editorSidePanelWidth
                SplitView.minimumWidth: appTheme.editorSidePanelWidthMin
                SplitView.maximumWidth: appTheme.editorSidePanelWidthMax
                color: appTheme.cardSurfaceColor
                radius: appTheme.panelRadius

                Flickable {
                    id: scroller
                    anchors.fill: parent
                    anchors.margins: appTheme.spaceMd
                    contentWidth: width
                    contentHeight: panel.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }

                    ColumnLayout {
                        id: panel
                        width: scroller.width
                        spacing: appTheme.spaceSm

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("Geometry")
                                color: appTheme.textColor
                                font.pixelSize: appTheme.fontSizeSection
                                font.weight: appTheme.fontWeightHeading
                            }
                            UI.IconActionButton {
                                compact: true
                                enabled: window.hasImage
                                iconSrc: "PREVIEW_CONFIG_URL/panel_icons/reset.svg"
                                actionName: qsTr("Reset All")
                                onClicked: {
                                    window.resetCrop();
                                    window.resetTransform();
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("Geometry applies to the whole image.")
                            color: appTheme.textMutedColor
                            font.pixelSize: appTheme.fontSizeCaption
                            wrapMode: Text.Wrap
                        }

                        UI.CollapsibleSection {
                            id: cropSection
                            objectName: "previewCropSection"
                            title: qsTr("Crop & Rotate")
                            bodyContentHeight: cropControls.implicitHeight
                            headerActions: UI.IconActionButton {
                                compact: true
                                enabled: window.hasImage
                                iconSrc: "PREVIEW_CONFIG_URL/panel_icons/reset.svg"
                                actionName: qsTr("Reset Crop & Rotate")
                                onClicked: window.resetCrop()
                            }

                            ColumnLayout {
                                id: cropControls
                                width: parent.width
                                spacing: appTheme.spaceSm

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: implicitHeight
                                    spacing: appTheme.spaceXs
                                    // Temporary typographic symbols. No new SVG artwork.
                                    Repeater {
                                        model: [
                                            {
                                                name: qsTr("Rotate Left"),
                                                symbol: "↶"
                                            },
                                            {
                                                name: qsTr("Rotate Right"),
                                                symbol: "↷"
                                            },
                                            {
                                                name: qsTr("Flip Horizontal"),
                                                symbol: "↔"
                                            },
                                            {
                                                name: qsTr("Flip Vertical"),
                                                symbol: "↕"
                                            },
                                            {
                                                name: qsTr("Swap Aspect"),
                                                symbol: "⇄"
                                            }
                                        ]
                                        UI.IconActionButton {
                                            required property var modelData
                                            compact: true
                                            enabled: window.hasImage
                                            actionName: modelData.name
                                            toolTipText: actionName === qsTr("Swap Aspect") ? qsTr("Swap Aspect (X)") : actionName
                                            onClicked: window.lastAction = qsTr("%1 selected. UI preview only.").arg(actionName)
                                            PreviewActionGlyph {
                                                anchors.centerIn: parent
                                                symbol: parent.modelData.symbol
                                            }
                                        }
                                    }
                                }
                                UI.AdjustmentCombo {
                                    model: aspect
                                    showResetButton: false
                                    controlObjectName: "previewAspectSelector"
                                    infoText: qsTr("Crop aspect selects the output frame shape. Transform Aspect stretches image content.")
                                }
                                UI.ThemeCheckBox {
                                    text: qsTr("Lock aspect ratio")
                                    enabled: window.hasImage
                                    checked: window.aspectLocked
                                    onToggled: function (checked) {
                                        window.setAspectLocked(checked);
                                    }
                                }
                                UI.AdjustmentSlider {
                                    model: customWidth
                                    flickable: scroller
                                    visible: aspect.currentValue === "custom"
                                }
                                UI.AdjustmentSlider {
                                    model: customHeight
                                    flickable: scroller
                                    visible: aspect.currentValue === "custom"
                                }
                                UI.AdjustmentSlider {
                                    model: cropX
                                    flickable: scroller
                                    infoText: qsTr("Position of the crop frame. X Offset in Transform moves image content.")
                                }
                                UI.AdjustmentSlider {
                                    model: cropY
                                    flickable: scroller
                                    infoText: qsTr("Position of the crop frame. Y Offset in Transform moves image content.")
                                }
                                UI.AdjustmentSlider {
                                    model: cropWidth
                                    flickable: scroller
                                }
                                UI.AdjustmentSlider {
                                    model: cropHeight
                                    flickable: scroller
                                }
                                UI.AdjustmentSlider {
                                    model: rotation
                                    flickable: scroller
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: appTheme.spaceXs
                                    UI.AdjustmentCombo {
                                        model: window.guidePreferenceInvalid ? invalidGuide : guide
                                        warning: window.guidePreferenceInvalid
                                        showResetButton: false
                                        controlObjectName: "previewGuideSelector"
                                    }
                                    UI.IconActionButton {
                                        compact: true
                                        actionName: qsTr("Change guide orientation")
                                        toolTipText: qsTr("Change guide orientation (Shift+O)")
                                        enabled: window.hasImage && !window.guidePreferenceInvalid
                                                 && ["spiral", "triangle"].indexOf(guide.currentValue) >= 0
                                        onClicked: window.changeGuideOrientation()
                                        PreviewActionGlyph {
                                            anchors.centerIn: parent
                                            symbol: "↷"
                                        }
                                    }
                                }
                                UI.AdjustmentCombo {
                                    model: guideVisibility
                                    showResetButton: false
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: window.guidePreferenceInvalid
                                    text: qsTr("The saved guide preference is invalid. Reset Guide to restore the default preferences.")
                                    color: appTheme.dangerColor
                                    font.pixelSize: appTheme.fontSizeCaption
                                    wrapMode: Text.Wrap
                                }
                                UI.DialogActionButton {
                                    objectName: "previewResetGuide"
                                    visible: window.guidePreferenceInvalid
                                    enabled: window.hasImage
                                    text: qsTr("Reset Guide")
                                    Accessible.name: text
                                    buttonHeight: appTheme.iconButtonHitSizeCompact
                                    font.pixelSize: appTheme.fontSizeBody
                                    onClicked: window.resetGuide()
                                }
                            }
                        }

                        UI.CollapsibleSection {
                            id: transformSection
                            objectName: "previewTransformSection"
                            title: qsTr("Transform")
                            bodyContentHeight: transformControls.implicitHeight
                            headerActions: UI.IconActionButton {
                                compact: true
                                enabled: window.hasImage
                                iconSrc: "PREVIEW_CONFIG_URL/panel_icons/reset.svg"
                                actionName: qsTr("Reset Transform")
                                onClicked: window.resetTransform()
                            }

                            ColumnLayout {
                                id: transformControls
                                width: parent.width
                                spacing: appTheme.spaceSm
                                UI.AdjustmentSlider {
                                    model: vertical
                                    flickable: scroller
                                }
                                UI.AdjustmentSlider {
                                    model: horizontal
                                    flickable: scroller
                                }
                                UI.AdjustmentSlider {
                                    model: stretch
                                    flickable: scroller
                                    infoText: qsTr("Stretch image content. Positive values stretch height and compress width.")
                                }
                                UI.AdjustmentSlider {
                                    model: scale
                                    flickable: scroller
                                    infoText: qsTr("Scale image content around its center. Viewport zoom is separate.")
                                }
                                UI.AdjustmentSlider {
                                    model: offsetX
                                    flickable: scroller
                                    infoText: qsTr("Move image content right with positive values. Crop X positions the frame.")
                                }
                                UI.AdjustmentSlider {
                                    model: offsetY
                                    flickable: scroller
                                    infoText: qsTr("Move image content up with positive values.")
                                }
                                UI.ThemeCheckBox {
                                    text: qsTr("Constrain Crop")
                                    enabled: window.hasImage
                                    checked: window.constrainCrop
                                    onToggled: function (checked) {
                                        window.constrainCrop = checked;
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: previewState.currentValue === "error"
                                    text: qsTr("This transform cannot keep the crop inside the image. The previous value is retained.")
                                    color: appTheme.dangerColor
                                    font.pixelSize: appTheme.fontSizeCaption
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

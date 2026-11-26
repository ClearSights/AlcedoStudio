import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property var preferences: null
    readonly property bool valid: !!root.preferences && root.preferences.cropGuideIndex >= 0
    visible: !!root.preferences
    spacing: appTheme.spaceSm

    RowLayout {
        Layout.fillWidth: true
        spacing: appTheme.spaceXs
        visible: root.valid

        AdjustmentCombo {
            controlObjectName: "cropGuideSelector"
            showResetButton: false
            model: QtObject {
                readonly property string label: qsTr("Guide")
                // The indices follow CropGuide in crop_guide_paths.hpp.
                readonly property var entries: [
                    { label: qsTr("None") },
                    { label: qsTr("Thirds") },
                    { label: qsTr("Grid") },
                    { label: qsTr("Golden Ratio") },
                    { label: qsTr("Golden Spiral") },
                    { label: qsTr("Diagonal") },
                    { label: qsTr("Triangle") }
                ]
                readonly property bool enabled: root.valid
                readonly property int currentIndex: root.valid ? root.preferences.cropGuideIndex : -1
                function selectIndex(index) {
                    root.preferences.selectCropGuide(index)
                }
            }
        }

        IconActionButton {
            objectName: "cropGuideOrientationButton"
            compact: true
            iconSrc: "qrc:/panel_icons/rotate-guide.svg"
            actionName: qsTr("Change guide orientation")
            enabled: root.valid && root.preferences.cropGuideOrientation >= 0
            onClicked: root.preferences.rotateCropGuide()
        }
    }

    AdjustmentCombo {
        controlObjectName: "cropGuideVisibilitySelector"
        visible: root.valid
        showResetButton: false
        model: QtObject {
            readonly property string label: qsTr("Show guide")
            // The indices follow CropGuideVisibility in crop_guide_paths.hpp.
            readonly property var entries: [
                { label: qsTr("Always") },
                { label: qsTr("During input") },
                { label: qsTr("Never") }
            ]
            readonly property bool enabled: root.valid
            readonly property int currentIndex: root.valid ? root.preferences.cropGuideVisibility : -1
            function selectIndex(index) {
                root.preferences.selectCropGuideVisibility(index)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: appTheme.spaceXs
        visible: !!root.preferences && !root.valid

        Label {
            objectName: "cropGuidePreferenceError"
            Layout.fillWidth: true
            text: qsTr("Saved crop guide preferences are invalid.")
            wrapMode: Text.WordWrap
            color: appTheme.dangerColor
            font.pixelSize: appTheme.fontSizeBody
        }

        IconActionButton {
            objectName: "cropGuideResetButton"
            compact: true
            iconSrc: "qrc:/panel_icons/reset.svg"
            actionName: qsTr("Reset Guide")
            onClicked: root.preferences.resetCropGuide()
        }
    }
}

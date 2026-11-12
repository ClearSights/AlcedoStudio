import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Owns menu presentation and explicit confirmation, never membership or selected member lists.
Item {
    id: root
    property string occurrenceKey: ""
    property bool active: true
    property int revision: 0
    readonly property var controller: appModules.photoStacks
    readonly property var library: appModules.library
    readonly property var clicked: {
        const changed = revision
        return library.displayModel.getStackAt(library.displayModel.rowByKey(occurrenceKey))
    }
    readonly property bool stacked: String(clicked.stackId || "").length > 0
    readonly property bool available: active && !controller.busy && !library.queryUpdating
        && appModules.interactionPolicy.canEditPhotoStacks
    readonly property var options: controller.menuOptions
    readonly property var actions: !active ? [] : [
        { id: "stack-group", label: qsTr("Group selected photos"), enabled: available && options.group === true },
        { id: "stack-add", label: qsTr("Add selected photos to this stack"), enabled: available && stacked && options.add === true },
        { id: "stack-remove", label: qsTr("Remove selected photos from stacks"), enabled: available && options.remove === true },
        { id: "stack-ungroup", label: qsTr("Ungroup entire stack"), enabled: available && stacked },
        { id: "stack-cover", label: qsTr("Set as stack cover"), tooltip: qsTr("Changes the saved cover throughout this project."), enabled: available && stacked && Number(clicked.savedCoverId) !== Number(clicked.fileId) },
        { id: "stack-earlier", label: qsTr("Move earlier in stack"), tooltip: qsTr("Moves this photo in saved stack order throughout this project."), enabled: available && options.earlier === true },
        { id: "stack-later", label: qsTr("Move later in stack"), tooltip: qsTr("Moves this photo in saved stack order throughout this project."), enabled: available && options.later === true },
        { id: "stack-select-matching", label: qsTr("Select matching photos in this stack"), enabled: available && stacked },
        { id: "stack-select-entire", label: qsTr("Select entire stack (%1 photos)").arg(Number(clicked.stackTotal || 0)), enabled: available && stacked },
        { id: "stack-toggle", label: clicked.stackExpanded === true ? qsTr("Collapse this stack") : qsTr("Expand this stack"), enabled: available && stacked },
        { id: "stack-expand-all", label: qsTr("Expand all matching stacks"), enabled: available },
        { id: "stack-collapse-all", label: qsTr("Collapse all matching stacks"), enabled: available }
    ]
    Connections {
        target: root.library.displayModel
        function onPagesChanged() { root.revision++ }
    }
    // Capture only the intended identity and action while the user reviews the total.
    property string pendingAction: ""
    property string pendingKey: ""
    property int pendingTotal: 0
    function request(action) {
        if (!available) return
        if (action === "stack-select-entire"
                || (action === "stack-ungroup" && Number(clicked.fragmentMatchCount) < Number(clicked.stackTotal))) {
            pendingAction = action
            pendingKey = occurrenceKey
            pendingTotal = Number(clicked.stackTotal)
            confirmation.open()
            return
        }
        apply(action, occurrenceKey)
    }
    function apply(action, key) {
        if (action === "stack-toggle") library.ToggleStack(key)
        else if (action === "stack-expand-all") library.SetAllStacksExpanded(true)
        else if (action === "stack-collapse-all") library.SetAllStacksExpanded(false)
        else if (action === "stack-select-matching") library.SelectStack(key, false)
        else if (action === "stack-select-entire") library.SelectStack(key, true)
        else controller.RunAction(action, key)
    }
    Dialog {
        id: confirmation
        objectName: "photoStackConfirmation"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: Math.min(parent ? parent.width - appTheme.spaceXl * 2 : 440, 440)
        padding: appTheme.spaceLg
        title: root.pendingAction === "stack-ungroup" ? qsTr("Ungroup entire stack") : qsTr("Select entire stack")
        background: Rectangle { color: appTheme.cardSurfaceColor; radius: appTheme.panelRadius }
        contentItem: ColumnLayout {
            spacing: appTheme.spaceMd
            Label {
                Layout.fillWidth: true
                text: root.pendingAction === "stack-ungroup"
                    ? qsTr("Ungroup all %1 photos? This changes every group and album. Photos outside the current filter are included.").arg(root.pendingTotal)
                    : qsTr("Select all %1 photos in this stack? Photos outside the current filter or album are included.").arg(root.pendingTotal)
                wrapMode: Text.Wrap
                color: appTheme.textColor
                font.family: appTheme.uiFontFamily
                font.pixelSize: appTheme.fontSizeBody
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: appTheme.spaceSm
                DialogActionButton { text: qsTr("Cancel"); onClicked: confirmation.close() }
                DialogActionButton {
                    text: qsTr("Confirm")
                    onClicked: { confirmation.close(); root.apply(root.pendingAction, root.pendingKey) }
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Scalar display roles only. The C++ owner maintains membership and expansion.
Item {
    id: root
    objectName: "photoStackDisclosure"
    property string occurrenceKey: ""
    property int matchingCount: 0
    property int queryCount: 0
    property int totalCount: 0
    property bool expanded: false
    property bool temporaryCover: false
    property bool compact: false
    property bool interactive: true
    property bool pointerInside: false
    property real availableWidth: parent ? parent.width : 0
    readonly property string actionText: expanded
        ? qsTr("Collapse stack, %1 matching photos").arg(matchingCount)
        : qsTr("Expand stack, %1 matching photos").arg(matchingCount)
    readonly property string description: qsTr("%1 matching here. %2 matching the query. %3 in stack.")
        .arg(matchingCount).arg(queryCount).arg(totalCount)
        + (temporaryCover ? " " + qsTr("Temporary cover. The saved cover is outside this group or filter.") : "")
    signal toggled()
    readonly property bool controlFocused: disclosure.activeFocus
    // Keep native keyboard focus on the same disclosure after its occurrence is republished.
    function focusControl() { disclosure.forceActiveFocus() }
    function activate() {
        if (!interactive) return
        focusControl()
        toggled()
    }
    visible: occurrenceKey.length > 0 && totalCount > 1
    implicitHeight: appTheme.inspectorHeaderActionSize
    implicitWidth: Math.min(Math.max(appTheme.inspectorHeaderActionSize,
        compact ? countLabel.implicitWidth + appTheme.spaceMd : fullTextWidth), availableWidth)
    readonly property real fullTextWidth: counts.implicitWidth
        + appTheme.inspectorHeaderActionSize + appTheme.spaceSm
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        anchors.fill: parent
        color: appTheme.cardSurfaceColor
    }
    ToolButton {
        id: disclosure
        objectName: "photoStackDisclosureButton"
        width: appTheme.inspectorHeaderActionSize
        height: width
        enabled: root.interactive
        focusPolicy: Qt.TabFocus
        text: root.expanded ? "▾" : "▸"
        font.family: appTheme.uiFontFamily
        font.pixelSize: appTheme.fontSizeBody
        contentItem: Text {
            text: disclosure.text
            color: appTheme.textColor
            font: disclosure.font
            verticalAlignment: Text.AlignVCenter
            leftPadding: root.compact ? appTheme.spaceXs : 0
            horizontalAlignment: root.compact ? Text.AlignLeft : Text.AlignHCenter
        }
        background: Rectangle {
            color: disclosure.hovered ? appTheme.hoverColor : "transparent"
            border.width: disclosure.activeFocus ? 1 : 0
            border.color: appTheme.textMutedColor
        }
        Accessible.name: root.actionText
        Accessible.description: root.description
        onClicked: root.activate()
        Keys.onSpacePressed: function(event) { root.activate(); event.accepted = true }
        Keys.onReturnPressed: function(event) { root.activate(); event.accepted = true }
        Keys.onEnterPressed: function(event) { root.activate(); event.accepted = true }
        ToolTip.visible: hovered || root.pointerInside
        ToolTip.text: root.description
    }
    Label {
        id: countLabel
        anchors.left: disclosure.right
        anchors.leftMargin: root.compact ? -appTheme.spaceMd : 0
        anchors.verticalCenter: parent.verticalCenter
        visible: root.compact
        text: root.matchingCount >= 1000
            ? qsTr("%1K").arg(Math.floor(root.matchingCount / 1000))
            : Number(root.matchingCount).toLocaleString(Qt.locale(), 'f', 0)
        color: appTheme.textColor
        font.family: appTheme.headlineFontFamily
        font.pixelSize: appTheme.fontSizeCaption
        font.features: { "tnum": 1 }
        Accessible.name: root.description
    }
    Row {
        id: counts
        anchors.left: disclosure.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: appTheme.spaceSm
        visible: !root.compact
        Label {
            text: qsTr("%1 matching here").arg(Number(root.matchingCount).toLocaleString(Qt.locale(), 'f', 0))
            color: appTheme.textColor
            font.family: appTheme.headlineFontFamily
            font.pixelSize: appTheme.fontSizeCaption
            font.features: { "tnum": 1 }
        }
        Label {
            text: qsTr("%1 in stack").arg(Number(root.totalCount).toLocaleString(Qt.locale(), 'f', 0))
            color: appTheme.textMutedColor
            font.family: appTheme.headlineFontFamily
            font.pixelSize: appTheme.fontSizeCaption
            font.features: { "tnum": 1 }
        }
    }
}

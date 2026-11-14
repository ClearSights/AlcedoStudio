import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl

// Shared photo overlay. Membership, matching positions, and expansion remain C++-owned.
Item {
    id: root
    objectName: "photoStackDisclosure"
    property string occurrenceKey: ""
    property int matchingCount: 0
    property int matchingPosition: 1
    property int queryCount: 0
    property int totalCount: 0
    property bool filtered: false
    property bool expanded: false
    property bool temporaryCover: false
    property bool compact: false
    property bool interactive: true
    property bool pointerInside: false
    property real availableWidth: parent ? parent.width : 0
    readonly property string actionText: expanded
        ? qsTr("Collapse stack, %1 matching photos").arg(matchingCount)
        : qsTr("Expand stack, %1 matching photos").arg(matchingCount)
    readonly property string fullRatioText: expanded
        ? exactNumber(matchingPosition) + "/" + exactNumber(matchingCount)
        : exactNumber(matchingCount)
    readonly property string fullBadgeText: filtered ? qsTr("%1 matched").arg(fullRatioText) : fullRatioText
    readonly property string ratioText: expanded
        ? displayNumber(matchingPosition) + "/" + displayNumber(matchingCount)
        : displayNumber(matchingCount)
    // The user approved keeping the ratio readable and moving matched status to the tooltip
    // when the complete label cannot fit a compact tile.
    readonly property bool showMatchedSuffix: filtered && fullTextWidth <= availableWidth
    readonly property string badgeText: showMatchedSuffix ? qsTr("%1 matched").arg(ratioText) : ratioText
    readonly property string description: fullBadgeText + ". " + (filtered
        ? qsTr("%1 matching here. %2 matching the query. %3 in stack.")
            .arg(matchingCount).arg(queryCount).arg(totalCount)
        : qsTr("%1 photos in this group. %2 in this view. %3 in stack.")
            .arg(matchingCount).arg(queryCount).arg(totalCount))
        + (temporaryCover ? " " + qsTr("Temporary cover. The saved cover is outside this group or filter.") : "")
    signal toggled()
    readonly property bool controlFocused: disclosure.activeFocus
    function exactNumber(value) { return Number(value).toLocaleString(Qt.locale(), 'f', 0) }
    function displayNumber(value) {
        return compact && value >= 1000 ? qsTr("%1K").arg(Math.floor(value / 1000)) : exactNumber(value)
    }
    // Keep native focus on the same disclosure after its occurrence is republished.
    function focusControl() { disclosure.forceActiveFocus() }
    function activate() {
        if (!interactive) return
        focusControl()
        toggled()
    }
    visible: occurrenceKey.length > 0 && totalCount > 1
    implicitHeight: appTheme.inspectorHeaderActionSize
    implicitWidth: Math.min(Math.max(appTheme.inspectorHeaderActionSize,
        stackIcon.width + badgeRow.spacing + badgeLabel.implicitWidth
        + appTheme.spaceXs * 2), availableWidth)
    // Independent of compact: the owning tile uses this value to choose its compact mode.
    readonly property real fullTextWidth: fullMetrics.advanceWidth
        + appTheme.iconOpticalSizeCompact + appTheme.spaceXs * 3
    width: implicitWidth
    height: implicitHeight
    TextMetrics {
        id: fullMetrics
        text: root.fullBadgeText
        font.family: appTheme.headlineFontFamily
        font.pixelSize: appTheme.fontSizeTitle
        font.weight: appTheme.fontWeightStrong
    }
    ToolButton {
        id: disclosure
        objectName: "photoStackDisclosureButton"
        anchors.fill: parent
        enabled: root.interactive
        focusPolicy: Qt.TabFocus
        leftPadding: appTheme.spaceXs
        rightPadding: appTheme.spaceXs
        topPadding: 0
        bottomPadding: 0
        contentItem: Row {
            id: badgeRow
            spacing: root.compact ? appTheme.spaceXs / 2 : appTheme.spaceXs
            ColorImage {
                id: stackIcon
                objectName: "photoStackIcon"
                width: root.compact ? appTheme.fontSizeBody : appTheme.iconOpticalSizeCompact
                height: width
                anchors.verticalCenter: parent.verticalCenter
                source: "qrc:/panel_icons/photo-stack.svg"
                color: appTheme.photoStackBadgeTextColor
                Accessible.ignored: true
            }
            Label {
                id: badgeLabel
                objectName: "photoStackBadgeText"
                width: Math.max(0, disclosure.availableWidth - stackIcon.width - badgeRow.spacing)
                anchors.verticalCenter: parent.verticalCenter
                text: root.badgeText
                elide: Text.ElideRight
                color: appTheme.photoStackBadgeTextColor
                font.family: appTheme.headlineFontFamily
                font.pixelSize: root.compact ? appTheme.fontSizeBody : appTheme.fontSizeTitle
                font.weight: appTheme.fontWeightStrong
                font.features: { "tnum": 1 }
                Accessible.ignored: true
            }
        }
        background: Rectangle {
            color: appTheme.photoStackBadgeFillColor
            radius: height / 2
            border.width: disclosure.activeFocus ? 1 : 0
            border.color: appTheme.photoStackBadgeTextColor
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
}

import QtQuick
import "controls" as UI

// Temporary text artwork fitted by painted bounds to the shared compact icon size.
Item {
    id: root
    property string symbol: ""

    implicitWidth: appTheme.iconOpticalSizeCompact
    implicitHeight: appTheme.iconOpticalSizeCompact

    TextMetrics {
        id: metrics
        text: root.symbol
        font.family: appTheme.uiFontFamily
        font.pixelSize: appTheme.iconSourceSizeCompact
    }

    Text {
        readonly property rect ink: metrics.tightBoundingRect
        readonly property real fitScale: Math.min(root.width, root.height)
                                         / Math.max(1, ink.width, ink.height)
        x: root.width / 2 - fitScale * (ink.x + ink.width / 2)
        y: root.height / 2 - fitScale * (baselineOffset + ink.y + ink.height / 2)
        text: root.symbol
        font: metrics.font
        color: root.enabled ? appTheme.iconColor : appTheme.textMutedColor
        transformOrigin: Item.TopLeft
        scale: fitScale
    }
}

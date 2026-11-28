import QtQuick

Item {
    id: root

    required property string workspace
    required property bool inputAvailable
    signal ratingRequested(int rating)

    readonly property string ratingScope: workspace === "library" || workspace === "editor"
                                          ? "workspace.photoRatings" : ""

    Repeater {
        model: 6
        delegate: RegisteredShortcut {
            required property int index

            commandId: index === 0 ? "photoRating.clear" : "photoRating.set" + index
            activeScope: root.ratingScope
            commandEnabled: root.inputAvailable
            onActivated: root.ratingRequested(index)
        }
    }
}

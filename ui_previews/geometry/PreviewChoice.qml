import QtQuick

// Local menu selection only. It cannot submit a document edit.
QtObject {
    property string label: ""
    property var entries: []
    property int currentIndex: 0
    property int defaultIndex: 0
    property bool enabled: true
    readonly property string currentValue: entries.length ? entries[currentIndex].value : ""

    function selectIndex(index) {
        currentIndex = index;
    }
    function reset() {
        currentIndex = defaultIndex;
    }
}

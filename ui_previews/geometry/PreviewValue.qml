import QtQuick

// Local sample values for the shared slider. No editor service or image state.
QtObject {
    property string label: ""
    property real minimum: -100
    property real maximum: 100
    readonly property real railMinimum: minimum
    readonly property real railMaximum: maximum
    property real defaultValue: 0
    property real value: defaultValue
    property real step: 0.1
    property int precision: 1
    property string suffix: ""
    property bool enabled: true
    property bool valid: true

    function editValue(number) {
        value = number;
        valid = true;
    }
    function beginDrag() {
    }
    function updateDrag(number) {
        editValue(number);
    }
    function finishDrag() {
    }
    function commitImmediately() {
    }
    function reset() {
        editValue(defaultValue);
    }
    function setInvalid(message) {
        valid = false;
    }
}

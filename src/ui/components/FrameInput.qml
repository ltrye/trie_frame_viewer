import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Controls.TextField {
    id: control
    required property int frameNumber
    required property int maximumFrame
    required property string label
    property int minimumFrame: 1
    property bool cancelling: false
    property bool modified: false
    readonly property string displayNumber: enabled ? String(frameNumber) : "—"
    signal frameSubmitted(int frame)

    implicitHeight: Theme.frameFieldHeight
    implicitWidth: Math.max(Theme.frameFieldMinWidth, metrics.width + leftPadding + rightPadding)
    leftPadding: Theme.spaceSm
    rightPadding: Theme.spaceSm
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.monoFont
    font.pixelSize: Theme.textBody
    color: Theme.ink
    selectionColor: Theme.accentSurface
    selectedTextColor: Theme.ink
    horizontalAlignment: TextInput.AlignHCenter
    verticalAlignment: TextInput.AlignVCenter
    inputMethodHints: Qt.ImhDigitsOnly
    opacity: enabled ? 1 : 0.4
    Accessible.name: label

    HoverHandler {
        id: hover
    }
    Controls.ToolTip.visible: hover.hovered
    Controls.ToolTip.text: label
    Controls.ToolTip.delay: Theme.tooltipDelay

    TextMetrics {
        id: metrics
        font: control.font
        text: String(control.maximumFrame)
    }
    validator: IntValidator {
        bottom: control.minimumFrame
        top: control.maximumFrame
        locale: "C"
    }
    Component.onCompleted: text = displayNumber
    onDisplayNumberChanged: {
        if (!activeFocus)
            text = displayNumber;
    }
    onEnabledChanged: {
        if (!enabled) {
            cancelling = true;
            modified = false;
            focus = false;
            text = displayNumber;
        }
    }
    background: Rectangle {
        color: Theme.elevated
        radius: Theme.radiusSm
        border.width: control.activeFocus ? 1 : 0
        border.color: control.acceptableInput ? Theme.accent : Theme.warning
    }
    onActiveFocusChanged: {
        if (activeFocus) {
            cancelling = false;
            modified = false;
            text = displayNumber;
        }
        Qt.callLater(() => {
            if (activeFocus)
                selectAll();
            else
                text = displayNumber;
        });
    }
    onTextEdited: modified = true
    onAccepted: focus = false
    onEditingFinished: {
        const value = Number(text);
        if (enabled && modified && !cancelling && acceptableInput && Number.isInteger(value) && value >= minimumFrame && value <= maximumFrame && value !== frameNumber)
            frameSubmitted(value);
        cancelling = false;
        modified = false;
        text = displayNumber;
    }
    Keys.onEscapePressed: event => {
        cancelling = true;
        text = displayNumber;
        focus = false;
        event.accepted = true;
    }
}

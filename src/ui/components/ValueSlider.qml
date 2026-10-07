import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Controls.Slider {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: 100
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.availableWidth
        height: 3
        radius: 2
        color: Theme.border
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.enabled ? Theme.accent : Theme.muted
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 10
        implicitHeight: 10
        radius: 5
        color: control.enabled ? Theme.ink : Theme.muted
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.accent
    }
}

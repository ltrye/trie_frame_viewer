import QtQuick
import Trie.FrameViewer

Rectangle {
    id: chip
    property string text: ""
    property color textColor: Theme.secondary
    implicitHeight: Theme.controlHeight - Theme.spaceSm
    implicitWidth: label.implicitWidth + Theme.spaceMd * 2
    radius: Theme.radiusSm
    color: Theme.elevated
    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        color: chip.textColor
        font.family: Theme.font
        font.pixelSize: Theme.textSmall
    }
}

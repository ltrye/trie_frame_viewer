import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Controls.AbstractButton {
    id: control
    property string iconName: "play"
    property string hint: ""
    property bool selected: false
    property bool primary: false
    implicitWidth: text.length > 0 ? contentItem.implicitWidth + Theme.spaceLg : Theme.controlHeight
    implicitHeight: Theme.controlHeight
    opacity: enabled ? 1 : 0.35
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: hint.length ? hint : text
    background: Rectangle {
        radius: Theme.radiusSm
        color: control.primary || control.selected ? Theme.accentSurface : control.down || control.hovered ? Theme.hover : Theme.transparent
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.accent
        Behavior on color {
            ColorAnimation {
                duration: Theme.transitionMs
            }
        }
    }
    contentItem: Item {
        implicitWidth: contents.implicitWidth
        implicitHeight: contents.implicitHeight
        Row {
            id: contents
            objectName: "buttonContents"
            anchors.centerIn: parent
            spacing: Theme.spaceSm
            Icon {
                name: control.iconName
                color: control.primary || control.selected ? Theme.accent : Theme.ink
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: control.text
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.textBody
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
            }
        }
    }
    Controls.ToolTip.visible: hovered && hint.length > 0
    Controls.ToolTip.text: hint
    Controls.ToolTip.delay: 550
}

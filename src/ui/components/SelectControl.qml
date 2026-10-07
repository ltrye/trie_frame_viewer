import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Controls.ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: 130
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceXl
    font.family: Theme.font
    font.pixelSize: Theme.textBody
    opacity: enabled ? 1 : 0.4
    background: Rectangle {
        color: control.hovered ? Theme.hover : Theme.elevated
        radius: Theme.radiusSm
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.accent
    }
    contentItem: Text {
        text: control.displayText
        color: Theme.ink
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        name: "chevron"
        color: Theme.secondary
        rotation: 90
        width: 12
        height: 12
        x: control.width - width - Theme.spaceSm
        y: (control.height - height) / 2
    }
    delegate: Controls.ItemDelegate {
        required property int index
        width: control.width
        height: Theme.controlHeight
        text: control.textAt(index)
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: parent.text
            color: Theme.ink
            font: control.font
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: parent.highlighted ? Theme.hover : Theme.transparent
            radius: Theme.radiusSm
        }
    }
    popup: Controls.Popup {
        y: control.height + Theme.spaceXs
        width: control.width
        padding: Theme.spaceXs
        implicitHeight: contentItem.implicitHeight + padding * 2
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 240)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            Controls.ScrollIndicator.vertical: Controls.ScrollIndicator {}
        }
        background: Rectangle {
            color: Theme.elevated
            border.color: Theme.border
            radius: Theme.radiusMd
        }
    }
}

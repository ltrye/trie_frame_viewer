import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Controls.AbstractButton {
    id: card
    required property var player
    required property int rowIndex
    required property string markColor
    required property int start
    required property int end
    required property bool range
    property bool compact: true
    readonly property string frameLabel: String(start + 1) + (range ? "–" + (end + 1) : "")
    readonly property bool active: player.currentFrame >= start && player.currentFrame <= end
    objectName: "bookmarkRow"
    implicitHeight: Theme.bookmarkRowHeight
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: range ? qsTr("Frames %1 to %2").arg(start + 1).arg(end + 1) : qsTr("Frame %1").arg(start + 1)
    onClicked: player.activateBookmark(rowIndex)
    background: Rectangle {
        radius: Theme.radiusSm
        color: card.active ? Theme.accentSurface : card.hovered ? Theme.hover : Theme.transparent
        border.width: card.activeFocus ? 1 : 0
        border.color: Theme.accent
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 2
            height: parent.height - Theme.spaceMd
            radius: 1
            color: card.markColor
        }
    }
    contentItem: Item {
        Text {
            anchors.fill: parent
            anchors.leftMargin: Theme.spaceXs
            text: card.frameLabel
            color: card.active ? Theme.accent : Theme.ink
            font.family: Theme.monoFont
            font.pixelSize: card.compact ? Theme.textSmall : Theme.textBody
            minimumPixelSize: Theme.textSmall - 2
            fontSizeMode: Text.Fit
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint, button) => menu.popup(eventPoint.position.x, eventPoint.position.y)
    }
    Keys.onMenuPressed: menu.popup()
    Controls.ToolTip.visible: hovered
    Controls.ToolTip.text: Accessible.name + qsTr(" · Right-click for options")
    Controls.ToolTip.delay: 550
    Controls.Menu {
        id: menu
        Controls.MenuItem {
            text: qsTr("Go to frame %1").arg(card.start + 1)
            onTriggered: player.activateBookmark(card.rowIndex)
        }
        Controls.MenuItem {
            text: qsTr("Delete bookmark")
            onTriggered: player.bookmarks.remove(card.rowIndex)
        }
    }
}

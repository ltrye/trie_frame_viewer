import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Rectangle {
    id: panel
    objectName: "bookmarkPanel"
    required property var player
    required property var preferences
    property bool collapsed: true
    signal toggleRequested
    signal addRequested(bool range)
    color: Theme.surface
    Rectangle {
        width: 1
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: Theme.border
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: panel.collapsed ? Theme.spaceSm : Theme.spaceMd
        spacing: Theme.spaceSm
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs
            Item {
                visible: panel.collapsed
                Layout.fillWidth: true
            }
            ToolButton {
                objectName: "bookmarkToggle"
                iconName: "bookmark"
                hint: (panel.collapsed ? qsTr("Expand bookmarks") : qsTr("Collapse bookmarks")) + " · " + preferences.shortcuts.toggleBookmarks
                selected: !panel.collapsed
                onClicked: panel.toggleRequested()
            }
            Text {
                visible: !panel.collapsed
                Layout.fillWidth: true
                text: qsTr("Bookmarks")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.textBody
                font.weight: Font.DemiBold
            }
            ToolButton {
                visible: !panel.collapsed
                iconName: "plus"
                hint: qsTr("Add bookmark")
                enabled: player.loaded
                onClicked: addMenu.open()
                Controls.Menu {
                    id: addMenu
                    Controls.MenuItem {
                        text: qsTr("Current frame · ") + preferences.shortcuts.bookmark
                        onTriggered: panel.addRequested(false)
                    }
                    Controls.MenuItem {
                        text: qsTr("Playback range · ") + preferences.shortcuts.rangeBookmark
                        onTriggered: panel.addRequested(true)
                    }
                }
            }
            Item {
                visible: panel.collapsed
                Layout.fillWidth: true
            }
        }
        Controls.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            ListView {
                id: list
                model: player.bookmarks
                spacing: Theme.spaceXs
                delegate: BookmarkCard {
                    required property int index
                    required property string bookmarkColor
                    required property int startFrame
                    required property int endFrame
                    required property bool isRange
                    width: list.width
                    player: panel.player
                    rowIndex: index
                    markColor: bookmarkColor
                    start: startFrame
                    end: endFrame
                    range: isRange
                    compact: panel.collapsed
                }
                Text {
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spaceLg
                    width: parent.width
                    visible: !panel.collapsed && player.bookmarks.count === 0
                    text: qsTr("%1 saves a frame.\n%2 saves a range.\n\nSave a .tfv project to keep them.").arg(preferences.shortcuts.bookmark).arg(preferences.shortcuts.rangeBookmark)
                    color: Theme.muted
                    font.family: Theme.font
                    font.pixelSize: Theme.textSmall
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    lineHeight: 1.5
                }
            }
        }
    }
}

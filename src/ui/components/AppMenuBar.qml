pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Controls.MenuBar {
    id: menuBar
    objectName: "appMenuBar"
    required property var player
    required property var preferences
    required property bool fullScreen
    required property bool alwaysOnTop
    required property bool cinemaMode
    signal actionRequested(string action)
    implicitHeight: Theme.menuBarHeight
    padding: 0
    spacing: 0
    font.family: Theme.font
    font.pixelSize: Theme.menuTextSize
    background: Rectangle {
        color: Theme.menuBar
    }
    delegate: Controls.MenuBarItem {
        id: menuTitle
        objectName: "menuTitle_" + text.replace(/&/g, "")
        implicitWidth: contentItem.implicitWidth + Theme.spaceMd
        implicitHeight: Theme.menuBarHeight
        padding: 0
        contentItem: Text {
            text: menuTitle.text.replace(/&/g, "")
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.menuTextSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: menuTitle.highlighted ? Theme.hover : Theme.transparent
        }
    }
    function actionLabel(id) {
        for (const action of preferences.actions)
            if (action.id === id)
                return action.label;
        return id;
    }
    component ReviewMenu: Controls.Menu {
        popupType: Controls.Popup.Item
        implicitWidth: Theme.menuWidth
        padding: Theme.spaceXs
        font.family: Theme.font
        font.pixelSize: Theme.menuTextSize
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusSm
            border.color: Theme.border
        }
    }
    component Command: Controls.MenuItem {
        id: command
        property string actionId: ""
        objectName: actionId.length ? "menu_" + actionId : "menuOption"
        text: menuBar.actionLabel(actionId)
        implicitHeight: Theme.menuItemHeight
        leftPadding: Theme.spaceSm
        rightPadding: Theme.spaceSm
        font.family: Theme.font
        font.pixelSize: Theme.menuTextSize
        onTriggered: {
            if (actionId.length)
                menuBar.actionRequested(actionId);
        }
        contentItem: RowLayout {
            spacing: Theme.spaceLg
            Item {
                visible: command.checkable
                Layout.preferredWidth: Theme.iconSize
            }
            Text {
                Layout.fillWidth: true
                text: command.text
                color: command.enabled ? Theme.ink : Theme.muted
                font: command.font
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                text: menuBar.preferences.shortcuts[command.actionId] || ""
                color: Theme.muted
                font: command.font
            }
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: command.highlighted || command.down ? Theme.hover : Theme.transparent
        }
    }
    ReviewMenu {
        title: qsTr("&File")
        Command {
            actionId: "openMedia"
        }
        Command {
            actionId: "openProject"
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "saveProject"
            enabled: player.loaded
        }
        Command {
            actionId: "saveProjectAs"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "close"
        }
    }
    ReviewMenu {
        title: qsTr("&Edit")
        Command {
            actionId: "rangeStart"
            enabled: player.loaded
        }
        Command {
            actionId: "rangeEnd"
            enabled: player.loaded
        }
        Command {
            actionId: "resetRange"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "settings"
        }
    }
    ReviewMenu {
        title: qsTr("&Playback")
        Command {
            actionId: "playPause"
            enabled: player.loaded
        }
        Command {
            actionId: "reverse"
            enabled: player.loaded
        }
        Command {
            actionId: "forward"
            enabled: player.loaded
        }
        Command {
            actionId: "pause"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "previousFrame"
            enabled: player.loaded
        }
        Command {
            actionId: "nextFrame"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            text: qsTr("Loop range")
            enabled: player.loaded
            checkable: true
            checked: player.loopMode === "loop"
            onTriggered: player.loopMode = "loop"
        }
        Command {
            text: qsTr("Ping-pong")
            enabled: player.loaded
            checkable: true
            checked: player.loopMode === "pingpong"
            onTriggered: player.loopMode = "pingpong"
        }
        Command {
            text: qsTr("Play once")
            enabled: player.loaded
            checkable: true
            checked: player.loopMode === "once"
            onTriggered: player.loopMode = "once"
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "cinemaMode"
            enabled: player.loaded
            checkable: true
            checked: menuBar.cinemaMode
        }
    }
    ReviewMenu {
        title: qsTr("&Audio")
        Command {
            actionId: "mute"
            enabled: player.audioAvailable
            checkable: true
            checked: preferences.volume === 0
        }
        Command {
            actionId: "settings"
            text: qsTr("Audio timing & scrubbing…")
        }
    }
    ReviewMenu {
        title: qsTr("&Video")
        Command {
            actionId: "resetView"
            enabled: player.loaded
        }
        Command {
            actionId: "zoomIn"
            enabled: player.loaded
        }
        Command {
            actionId: "zoomOut"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "flipHorizontal"
            enabled: player.loaded
            checkable: true
            checked: player.flipH
        }
        Command {
            actionId: "flipVertical"
            enabled: player.loaded
            checkable: true
            checked: player.flipV
        }
        Command {
            actionId: "rotate"
            enabled: player.loaded
        }
        Command {
            actionId: "rotateCounterclockwise"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "fullscreen"
            checkable: true
            checked: menuBar.fullScreen
        }
    }
    ReviewMenu {
        title: qsTr("&Bookmark")
        Command {
            actionId: "bookmark"
            enabled: player.loaded
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "previousBookmark"
            enabled: player.bookmarks.count > 0
        }
        Command {
            actionId: "nextBookmark"
            enabled: player.bookmarks.count > 0
        }
    }
    ReviewMenu {
        title: qsTr("&Tools")
        Command {
            actionId: "settings"
        }
        Command {
            actionId: "shortcuts"
        }
        Controls.MenuSeparator {}
        Command {
            actionId: "alwaysOnTop"
            checkable: true
            checked: menuBar.alwaysOnTop
        }
    }
    ReviewMenu {
        title: qsTr("&Help")
        Command {
            actionId: "shortcuts"
        }
        Command {
            actionId: "about"
        }
    }
}

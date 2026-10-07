import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Item {
    id: transport
    required property var player
    required property var preferences
    implicitHeight: Theme.toolbarHeight
    RowLayout {
        anchors {
            fill: parent
            leftMargin: Theme.spaceLg
            rightMargin: Theme.spaceLg
        }
        spacing: transport.width < 700 ? Theme.spaceSm : Theme.spaceMd
        ColumnLayout {
            Layout.preferredWidth: transport.width < 700 ? 100 : 140
            spacing: Theme.spaceXs
            Text {
                text: player.loaded ? String(player.currentFrame + 1).padStart(4, "0") + " / " + String(player.frameCount).padStart(4, "0") : "— / —"
                font.family: Theme.monoFont
                font.pixelSize: Theme.textBody
                color: Theme.ink
            }
            Text {
                text: qsTr("CURRENT FRAME")
                font.family: Theme.font
                font.pixelSize: Theme.textSmall - 1
                color: Theme.muted
                font.letterSpacing: 1
            }
        }
        Item {
            Layout.fillWidth: true
        }
        Row {
            spacing: Theme.spaceXs
            ToolButton {
                iconName: "previous"
                hint: qsTr("Previous frame · ") + preferences.shortcuts.previousFrame
                enabled: player.loaded
                onClicked: player.execute("previousFrame")
            }
            ToolButton {
                iconName: "reverse"
                hint: qsTr("Play backward · ") + preferences.shortcuts.reverse
                enabled: player.loaded
                selected: player.playing && player.direction < 0
                onClicked: player.execute("reverse")
            }
            ToolButton {
                iconName: player.playing ? "pause" : "play"
                hint: qsTr("Play / pause · ") + preferences.shortcuts.playPause
                primary: true
                enabled: player.loaded
                onClicked: player.execute("playPause")
            }
            ToolButton {
                iconName: "next"
                hint: qsTr("Next frame · ") + preferences.shortcuts.nextFrame
                enabled: player.loaded
                onClicked: player.execute("nextFrame")
            }
        }
        ToolButton {
            iconName: "loop"
            hint: qsTr("Playback mode")
            selected: player.loopMode !== "once"
            enabled: player.loaded
            onClicked: loopMenu.open()
            Controls.Menu {
                id: loopMenu
                Controls.MenuItem {
                    text: qsTr("Loop range")
                    checkable: true
                    checked: player.loopMode === "loop"
                    onTriggered: player.loopMode = "loop"
                }
                Controls.MenuItem {
                    text: qsTr("Ping-pong")
                    checkable: true
                    checked: player.loopMode === "pingpong"
                    onTriggered: player.loopMode = "pingpong"
                }
                Controls.MenuItem {
                    text: qsTr("Play once")
                    checkable: true
                    checked: player.loopMode === "once"
                    onTriggered: player.loopMode = "once"
                }
                Controls.MenuSeparator {}
                Controls.MenuItem {
                    text: qsTr("Reset playback range")
                    onTriggered: player.resetRange()
                }
            }
        }
        SelectControl {
            Layout.preferredWidth: 80
            enabled: player.loaded
            model: preferences.playbackSpeeds
            currentIndex: model.indexOf(player.speed)
            displayText: player.speed + "×"
            onActivated: player.speed = model[index]
        }
        Item {
            Layout.fillWidth: true
        }
        ToolButton {
            iconName: preferences.volume > 0 ? "volume" : "mute"
            visible: transport.width >= 700
            hint: qsTr("Mute / unmute")
            enabled: player.audioAvailable
            onClicked: preferences.toggleMute()
        }
        ValueSlider {
            visible: transport.width >= 700
            Layout.preferredWidth: 70
            from: 0
            to: 1
            value: preferences.volume
            enabled: player.audioAvailable
            onMoved: preferences.volume = value
        }
    }
}

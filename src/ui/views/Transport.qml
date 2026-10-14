import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Item {
    id: transport
    objectName: "playbackBar"
    required property var player
    required property var preferences
    readonly property bool popupOpen: loopMenu.visible || speedControl.popup.visible
    readonly property bool interacting: popupOpen || currentFrameInput.activeFocus || startFrameInput.activeFocus || endFrameInput.activeFocus
    function closePopups() {
        loopMenu.close();
        speedControl.popup.close();
    }
    implicitHeight: Theme.playbackBarHeight
    readonly property bool compact: width < frameFields.implicitWidth * 2 + playbackControls.implicitWidth + Theme.spaceLg * 2 + Theme.spaceMd * 2
    RowLayout {
        id: frameFields
        objectName: "playbackFrameFields"
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
            leftMargin: Theme.spaceLg
        }
        spacing: Theme.spaceSm
        FrameInput {
            id: currentFrameInput
            objectName: "currentFrameField"
            label: qsTr("Current frame")
            enabled: player.loaded
            frameNumber: player.currentFrame + 1
            maximumFrame: Math.max(1, player.frameCount)
            onFrameSubmitted: frame => {
                player.execute("pause");
                player.seek(frame - 1);
            }
        }
        Text {
            text: qsTr("Start")
            color: Theme.secondary
            font.family: Theme.font
            font.pixelSize: Theme.textBody
        }
        FrameInput {
            id: startFrameInput
            objectName: "playbackStartField"
            label: qsTr("Start frame")
            enabled: player.loaded
            frameNumber: player.rangeStart + 1
            maximumFrame: Math.max(1, player.rangeEnd + 1)
            onFrameSubmitted: frame => player.setRange(frame - 1, player.rangeEnd)
        }
        Text {
            text: "|"
            color: Theme.muted
            font.family: Theme.font
            font.pixelSize: Theme.textBody
        }
        Text {
            text: qsTr("End")
            color: Theme.secondary
            font.family: Theme.font
            font.pixelSize: Theme.textBody
        }
        FrameInput {
            id: endFrameInput
            objectName: "playbackEndField"
            label: qsTr("End frame")
            enabled: player.loaded
            frameNumber: player.rangeEnd + 1
            minimumFrame: player.rangeStart + 1
            maximumFrame: Math.max(1, player.frameCount)
            onFrameSubmitted: frame => player.setRange(player.rangeStart, frame - 1)
        }
    }
    RowLayout {
        id: playbackControls
        objectName: "playbackControls"
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(frameFields.x + frameFields.width + Theme.spaceMd, (transport.width - width) / 2)
        width: implicitWidth
        spacing: Theme.spaceSm
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
                objectName: "togglePlaybackButton"
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
            objectName: "playbackLoopButton"
            iconName: "loop"
            hint: qsTr("Playback mode")
            selected: player.loopMode !== "once"
            enabled: player.loaded
            onClicked: loopMenu.open()
            Controls.Menu {
                id: loopMenu
                objectName: "playbackLoopMenu"
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
            id: speedControl
            Layout.preferredWidth: 80
            enabled: player.loaded
            model: preferences.playbackSpeeds
            currentIndex: model.indexOf(player.speed)
            displayText: player.speed + "×"
            onActivated: player.speed = model[index]
        }
    }
    RowLayout {
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
            rightMargin: Theme.spaceLg
        }
        visible: !transport.compact
        spacing: Theme.spaceSm
        ToolButton {
            iconName: preferences.volume > 0 ? "volume" : "mute"
            hint: qsTr("Mute / unmute")
            enabled: player.audioAvailable
            onClicked: preferences.toggleMute()
        }
        ValueSlider {
            Layout.preferredWidth: 70
            from: 0
            to: 1
            value: preferences.volume
            enabled: player.audioAvailable
            onMoved: preferences.volume = value
        }
    }
}

import QtQuick
import QtQuick.Layouts
import Trie.FrameViewer

Item {
    id: stage
    objectName: "reviewStage"
    required property var player
    required property var preferences
    required property var resourceMonitor
    property bool cinemaMode: false
    readonly property bool editingBookmark: timeline.editingBookmark
    readonly property bool popupOpen: timeline.popupOpen || transport.popupOpen
    readonly property bool controlsVisible: !cinemaMode || controlsRevealed
    property bool controlsRevealed: false
    signal openRequested

    function revealControls() {
        if (!cinemaMode)
            return;
        controlsRevealed = true;
        hideControls.restart();
    }
    onCinemaModeChanged: {
        timeline.closePopups();
        transport.closePopups();
        controlsRevealed = false;
        hideControls.stop();
    }
    Timer {
        id: hideControls
        interval: stage.preferences.cinemaControlsTimeoutMs
        onTriggered: {
            if (controlsHover.hovered || pressedPointer.active || transport.interacting || stage.popupOpen || stage.editingBookmark)
                restart();
            else
                stage.controlsRevealed = false;
        }
    }
    HoverHandler {
        enabled: stage.cinemaMode
        blocking: false
        onPointChanged: stage.revealControls()
    }
    PointHandler {
        id: pressedPointer
        enabled: stage.cinemaMode
        acceptedButtons: Qt.AllButtons
        target: null
        onActiveChanged: {
            if (active)
                stage.revealControls();
        }
    }
    Viewer {
        id: viewer
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: stage.cinemaMode ? parent.bottom : playbackPanel.top
        }
        player: stage.player
        preferences: stage.preferences
        resourceMonitor: stage.resourceMonitor
        cinemaMode: stage.cinemaMode
        hideCursor: stage.cinemaMode && !stage.controlsVisible
        onOpenRequested: stage.openRequested()
    }
    Rectangle {
        id: playbackPanel
        objectName: "playbackPanel"
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        implicitHeight: panelContents.implicitHeight
        height: implicitHeight
        z: 1
        color: stage.cinemaMode ? Theme.cinemaControlsSurface : Theme.background
        opacity: stage.controlsVisible ? 1 : 0
        visible: opacity > 0
        enabled: stage.controlsVisible
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.transitionMs
            }
        }
        HoverHandler {
            id: controlsHover
            blocking: false
            onHoveredChanged: {
                if (!hovered && stage.cinemaMode && stage.controlsRevealed)
                    hideControls.restart();
            }
        }
        ColumnLayout {
            id: panelContents
            anchors.fill: parent
            spacing: 0
            Timeline {
                id: timeline
                Layout.fillWidth: true
                player: stage.player
            }
            Transport {
                id: transport
                Layout.fillWidth: true
                player: stage.player
                preferences: stage.preferences
            }
        }
    }
}

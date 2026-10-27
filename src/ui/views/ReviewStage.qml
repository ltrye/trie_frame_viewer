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
    signal openRequested

    onCinemaModeChanged: {
        timeline.closePopups();
        transport.closePopups();
    }
    Viewer {
        id: viewer
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            bottom: playbackPanel.top
        }
        player: stage.player
        preferences: stage.preferences
        resourceMonitor: stage.resourceMonitor
        cinemaMode: stage.cinemaMode
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
        color: Theme.background
        ColumnLayout {
            id: panelContents
            anchors.fill: parent
            spacing: 0
            Timeline {
                id: timeline
                Layout.fillWidth: true
                player: stage.player
                debugMode: stage.preferences.debugMode
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

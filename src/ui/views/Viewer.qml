import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer
import Trie.Media

Item {
    id: viewer
    objectName: "mediaViewer"
    required property var player
    required property var preferences
    required property var resourceMonitor
    property bool cinemaMode: false
    property bool hideCursor: false
    signal openRequested
    clip: true
    Rectangle {
        anchors.fill: parent
        color: viewer.cinemaMode ? Theme.cinemaBackground : Theme.viewer
    }
    readonly property real fitMargin: cinemaMode ? 0 : Theme.spaceLg
    readonly property bool sideways: player.rotation % 180 !== 0
    readonly property real fitScale: Math.min((width - fitMargin * 2) / Math.max(1, sideways ? player.mediaHeight : player.mediaWidth), (height - fitMargin * 2) / Math.max(1, sideways ? player.mediaWidth : player.mediaHeight))
    VideoSurface {
        objectName: "videoSurface"
        gpuCacheMiB: preferences.gpuCacheMiB
        width: player.mediaWidth * viewer.fitScale * player.zoom
        height: player.mediaHeight * viewer.fitScale * player.zoom
        x: (viewer.width - width) / 2 + player.panX
        y: (viewer.height - height) / 2 + player.panY
        rotation: player.rotation
        transform: Scale {
            origin.x: player.mediaWidth * viewer.fitScale * player.zoom / 2
            origin.y: player.mediaHeight * viewer.fitScale * player.zoom / 2
            xScale: player.flipH ? -1 : 1
            yScale: player.flipV ? -1 : 1
        }
        visible: player.loaded
    }
    MouseArea {
        id: gestures
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton | Qt.RightButton
        enabled: player.loaded
        hoverEnabled: true
        property real lastX: 0
        property real lastY: 0
        property real originX: 0
        property real originY: 0
        property int originFrame: 0
        cursorShape: viewer.hideCursor ? Qt.BlankCursor : pressedButtons & Qt.MiddleButton ? Qt.ClosedHandCursor : pressedButtons & Qt.RightButton ? Qt.SizeHorCursor : Qt.ArrowCursor
        onPressed: mouse => {
            lastX = originX = mouse.x;
            lastY = originY = mouse.y;
            originFrame = player.currentFrame;
            player.execute("pause");
        }
        onPositionChanged: mouse => {
            if (pressedButtons & Qt.MiddleButton)
                player.pan(mouse.x - lastX, mouse.y - lastY);
            else if (pressedButtons & Qt.RightButton)
                player.seek(originFrame + Math.round((mouse.x - originX) / player.scrubSensitivity((mouse.modifiers & Qt.ShiftModifier) !== 0, mouse.y - originY)), true);
            lastX = mouse.x;
            lastY = mouse.y;
        }
        onReleased: mouse => {
            if (mouse.button === Qt.RightButton)
                player.finishScrub();
        }
        onWheel: wheel => {
            const oldZoom = player.zoom;
            player.zoom *= wheel.angleDelta.y > 0 ? preferences.zoomWheelFactor : 1 / preferences.zoomWheelFactor;
            const ratio = player.zoom / oldZoom;
            player.pan((wheel.x - viewer.width / 2 - player.panX) * (1 - ratio), (wheel.y - viewer.height / 2 - player.panY) * (1 - ratio));
            wheel.accepted = true;
        }
    }
    ToolButton {
        objectName: "openMediaButton"
        anchors.centerIn: parent
        visible: !player.loaded && !viewer.cinemaMode
        text: qsTr("Open Media")
        iconName: "folder"
        primary: true
        enabled: !player.loading
        onClicked: viewer.openRequested()
    }
    Row {
        objectName: "viewerTools"
        anchors {
            right: parent.right
            top: parent.top
            margins: Theme.spaceMd
        }
        spacing: Theme.spaceXs
        visible: player.loaded && !viewer.cinemaMode
        ToolButton {
            iconName: "fit"
            hint: qsTr("Fit and reset view · ") + preferences.shortcuts.resetView
            onClicked: player.resetView()
        }
        ToolButton {
            iconName: "flip"
            hint: qsTr("Flip horizontal · ") + preferences.shortcuts.flipHorizontal
            selected: player.flipH
            onClicked: player.flip(false)
        }
        ToolButton {
            iconName: "rotate"
            hint: qsTr("Rotate 90° · ") + preferences.shortcuts.rotate
            onClicked: player.rotate(90)
        }
        ToolButton {
            iconName: "more"
            hint: qsTr("View options")
            onClicked: viewMenu.open()
            Controls.Menu {
                id: viewMenu
                Controls.MenuItem {
                    text: qsTr("Flip vertical")
                    checkable: true
                    checked: player.flipV
                    onTriggered: player.flip(true)
                }
                Controls.MenuItem {
                    text: qsTr("Actual size")
                    onTriggered: player.zoom = 1 / viewer.fitScale
                }
                Controls.MenuItem {
                    text: qsTr("Rotate counterclockwise")
                    onTriggered: player.rotate(-90)
                }
            }
        }
    }
    Chip {
        objectName: "viewerZoom"
        anchors {
            left: parent.left
            bottom: parent.bottom
            margins: Theme.spaceMd
        }
        text: player.loaded ? Math.round(viewer.fitScale * player.zoom * 100) + "%" : ""
        visible: player.loaded && !viewer.cinemaMode
    }
    DebugOverlay {
        monitor: viewer.resourceMonitor
        visible: monitor.enabled && !viewer.cinemaMode
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: Theme.spaceMd
        }
    }
}

import QtQuick
import QtQuick.Controls
import Trie.FrameViewer

Item {
    id: timeline
    objectName: "timeline"
    required property var player
    readonly property bool editingBookmark: bookmarkEditor.visible
    implicitHeight: Theme.timelineHeight
    readonly property real trackLeft: Theme.spaceLg
    readonly property real trackWidth: Math.max(1, width - Theme.spaceLg * 2)
    function xFor(frame) {
        return trackLeft + frame / Math.max(1, player.frameCount - 1) * trackWidth;
    }
    function frameFor(x) {
        return Math.round(Math.max(0, Math.min(1, (x - trackLeft) / trackWidth)) * Math.max(0, player.frameCount - 1));
    }
    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 1
        color: Theme.border
    }
    Rectangle {
        x: timeline.trackLeft
        y: Theme.spaceXl
        width: timeline.trackWidth
        height: 4
        radius: 2
        color: Theme.elevated
    }
    Rectangle {
        x: timeline.xFor(player.rangeStart)
        y: Theme.spaceXl
        width: Math.max(4, timeline.xFor(player.rangeEnd) - x)
        height: 4
        color: Theme.accentSurface
    }
    Repeater {
        model: player.cachedRanges
        Rectangle {
            required property var modelData
            x: timeline.xFor(modelData.start)
            y: Theme.spaceXl + Theme.spaceSm
            width: Math.max(2, timeline.xFor(modelData.end) - x)
            height: 2
            color: Theme.accent
            opacity: 0.55
        }
    }
    MouseArea {
        x: timeline.trackLeft
        y: 0
        width: timeline.trackWidth
        height: Theme.spaceXl + Theme.spaceLg
        enabled: player.loaded
        cursorShape: Qt.PointingHandCursor
        onPressed: mouse => player.seek(timeline.frameFor(mouse.x + x), true)
        onReleased: player.finishScrub()
        onPositionChanged: mouse => {
            if (pressed)
                player.seek(timeline.frameFor(mouse.x + x), true);
        }
    }
    Repeater {
        model: ["start", "end"]
        Rectangle {
            id: handle
            required property string modelData
            x: timeline.xFor(modelData === "start" ? player.rangeStart : player.rangeEnd) - width / 2
            y: Theme.spaceXl - 5
            width: 7
            height: 14
            radius: 2
            color: Theme.secondary
            MouseArea {
                anchors.fill: parent
                anchors.margins: -5
                enabled: player.loaded
                cursorShape: Qt.SizeHorCursor
                onPositionChanged: mouse => {
                    if (!pressed)
                        return;
                    const point = mapToItem(timeline, mouse.x, mouse.y);
                    const f = timeline.frameFor(point.x);
                    if (handle.modelData === "start")
                        player.setRange(Math.min(f, player.rangeEnd), player.rangeEnd);
                    else
                        player.setRange(player.rangeStart, Math.max(f, player.rangeStart));
                }
            }
        }
    }
    Rectangle {
        x: timeline.xFor(player.currentFrame) - 1
        y: Theme.spaceLg + Theme.spaceXs
        width: 2
        height: Theme.spaceLg + Theme.spaceSm
        color: Theme.ink
        visible: player.loaded
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            y: -4
            width: 7
            height: 7
            radius: 2
            color: Theme.ink
        }
    }
    Repeater {
        model: player.bookmarks
        Item {
            id: marker
            required property string bookmarkId
            required property string bookmarkName
            required property int bookmarkFrame
            required property string bookmarkColor
            objectName: "bookmarkMarker_" + bookmarkId
            x: timeline.xFor(bookmarkFrame) - width / 2
            y: Theme.spaceLg - Theme.spaceXs - (height - Theme.bookmarkMarkerHeight) / 2
            width: Theme.bookmarkHitWidth
            height: Theme.bookmarkHitHeight
            z: 2
            Accessible.role: Accessible.Button
            Accessible.name: bookmarkName
            Accessible.onPressAction: player.activateBookmark(bookmarkId)
            Rectangle {
                anchors.centerIn: parent
                width: Theme.bookmarkMarkerWidth
                height: Theme.bookmarkMarkerHeight
                radius: 2
                color: marker.bookmarkColor
                opacity: pointer.containsMouse ? 1 : 0.85
            }
            MouseArea {
                id: pointer
                anchors.fill: parent
                enabled: player.loaded
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: Qt.PointingHandCursor
                property bool scrubbing: false
                onPressed: mouse => {
                    if (mouse.button === Qt.RightButton) {
                        const point = mapToItem(timeline, mouse.x, mouse.y);
                        bookmarkMenu.show(marker.bookmarkId, marker.bookmarkName, marker.bookmarkFrame, point);
                    } else {
                        scrubbing = true;
                        player.seek(marker.bookmarkFrame, true);
                    }
                }
                onPositionChanged: mouse => {
                    if (scrubbing) {
                        const point = mapToItem(timeline, mouse.x, mouse.y);
                        player.seek(timeline.frameFor(point.x), true);
                    }
                }
                onReleased: {
                    if (scrubbing)
                        player.finishScrub();
                    scrubbing = false;
                }
                onCanceled: {
                    if (scrubbing)
                        player.finishScrub();
                    scrubbing = false;
                }
            }
            ToolTip.visible: pointer.containsMouse && !pointer.pressed && !bookmarkMenu.visible && !bookmarkEditor.visible
            ToolTip.text: bookmarkName
            ToolTip.delay: Theme.tooltipDelay
        }
    }
    Menu {
        id: bookmarkMenu
        objectName: "bookmarkContextMenu"
        property string bookmarkId: ""
        property string bookmarkName: ""
        property int bookmarkFrame: 0
        popupType: Popup.Item
        implicitWidth: Theme.menuWidth
        padding: Theme.spaceXs
        font.family: Theme.font
        font.pixelSize: Theme.menuTextSize
        function show(id, name, frame, point) {
            bookmarkId = id;
            bookmarkName = name;
            bookmarkFrame = frame;
            popup(point.x, point.y);
        }
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusSm
            border.color: Theme.border
        }
        MenuItem {
            objectName: "bookmarkRenameAction"
            text: qsTr("Rename…")
            onTriggered: bookmarkEditor.edit(bookmarkMenu.bookmarkId, bookmarkMenu.bookmarkName, bookmarkMenu.bookmarkFrame, false)
        }
        MenuItem {
            objectName: "bookmarkFrameAction"
            text: qsTr("Change frame…")
            onTriggered: bookmarkEditor.edit(bookmarkMenu.bookmarkId, bookmarkMenu.bookmarkName, bookmarkMenu.bookmarkFrame, true)
        }
        MenuSeparator {}
        MenuItem {
            objectName: "bookmarkDeleteAction"
            text: qsTr("Delete bookmark")
            onTriggered: player.bookmarks.remove(bookmarkMenu.bookmarkId)
        }
    }
    BookmarkEditor {
        id: bookmarkEditor
        parent: Overlay.overlay
        player: timeline.player
    }
    Connections {
        target: player
        function onMediaChanged() {
            if (!player.loaded)
                bookmarkMenu.close();
        }
    }
}

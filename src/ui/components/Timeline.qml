import QtQuick
import QtQuick.Controls
import Trie.FrameViewer

Item {
    id: timeline
    required property var player
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
    Repeater {
        model: player.bookmarks
        Rectangle {
            required property int startFrame
            required property int endFrame
            required property bool isRange
            required property string bookmarkColor
            x: timeline.xFor(startFrame) - (isRange ? 0 : 3)
            y: Theme.spaceLg - Theme.spaceXs
            width: isRange ? Math.max(5, timeline.xFor(endFrame) - x) : 6
            height: isRange ? 4 : 9
            radius: 2
            color: bookmarkColor
            opacity: 0.85
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
    Row {
        x: timeline.trackLeft
        y: Theme.spaceXl + Theme.spaceLg + Theme.spaceSm
        spacing: Theme.spaceMd
        Text {
            text: player.loaded ? "1" : "—"
            color: Theme.muted
            font.family: Theme.monoFont
            font.pixelSize: Theme.textSmall
        }
        Text {
            text: player.loaded ? "IN " + (player.rangeStart + 1) + "   /   OUT " + (player.rangeEnd + 1) : "Playback range"
            color: Theme.secondary
            font.family: Theme.monoFont
            font.pixelSize: Theme.textSmall
        }
    }
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spaceLg
        y: Theme.spaceXl + Theme.spaceLg + Theme.spaceSm
        text: player.loaded ? player.frameCount : "—"
        color: Theme.muted
        font.family: Theme.monoFont
        font.pixelSize: Theme.textSmall
    }
}

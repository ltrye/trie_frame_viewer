import QtQuick
import Trie.FrameViewer

Canvas {
    id: icon
    property string name: "play"
    property color color: Theme.ink
    implicitWidth: Theme.iconSize
    implicitHeight: Theme.iconSize
    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        ctx.scale(width / 24, height / 24);
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.65;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        const paths = IconPaths.paths[name] || IconPaths.paths.more;
        for (const path of paths) {
            ctx.beginPath();
            ctx.moveTo(path[0][0], path[0][1]);
            for (let i = 1; i < path.length; ++i)
                ctx.lineTo(path[i][0], path[i][1]);
            ctx.stroke();
        }
    }
}

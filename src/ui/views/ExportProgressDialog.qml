import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Controls.Dialog {
    id: dialog
    objectName: "exportProgressDialog"
    required property var exporter
    property bool cancelled: false
    anchors.centerIn: parent
    width: Math.min(Theme.exportDialogWidth, parent.width - Theme.spaceXl * 2)
    title: exporter.busy ? qsTr("Exporting clip…") : exporter.error.length ? qsTr("Export failed") : cancelled ? qsTr("Export cancelled") : qsTr("Clip exported")
    modal: true
    closePolicy: exporter.busy ? Controls.Popup.NoAutoClose : Controls.Popup.CloseOnEscape
    standardButtons: exporter.busy ? Controls.Dialog.Cancel : Controls.Dialog.Close
    footer: Controls.DialogButtonBox {
        objectName: "exportDialogButtons"
        width: dialog.width
        standardButtons: dialog.standardButtons
        alignment: Qt.AlignRight
        delegate: Controls.Button {
            objectName: "exportDialogAction"
        }
        onAccepted: dialog.accept()
        onRejected: dialog.reject()
    }
    function begin() {
        cancelled = false;
        open();
    }
    contentItem: ColumnLayout {
        spacing: Theme.spaceMd
        Controls.ProgressBar {
            Layout.fillWidth: true
            visible: dialog.exporter.busy
            value: dialog.exporter.progress
        }
        Text {
            Layout.fillWidth: true
            text: dialog.exporter.busy ? qsTr("%1% — Encoding selected frames and source audio.").arg(Math.floor(dialog.exporter.progress * 100)) : dialog.exporter.error.length ? dialog.exporter.error : dialog.cancelled ? qsTr("No clip was saved.") : qsTr("Saved to %1").arg(dialog.exporter.outputPath)
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            color: dialog.exporter.error.length ? Theme.warning : Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.textBody
        }
    }
    onRejected: {
        if (exporter.busy)
            exporter.cancel();
    }
    Connections {
        target: dialog.exporter
        function onFinished(success, cancelled, output) {
            dialog.cancelled = cancelled;
        }
    }
}

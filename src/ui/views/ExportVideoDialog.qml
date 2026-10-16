import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Controls.Dialog {
    id: dialog
    objectName: "exportVideoOptionsDialog"
    required property var preferences
    required property var player
    signal resolutionChosen(string resolutionId)
    readonly property string resolutionId: resolution.currentValue || ""
    anchors.centerIn: parent
    width: Math.min(Theme.exportDialogWidth, parent.width - Theme.spaceXl * 2)
    title: qsTr("Export current video to MP4")
    modal: true
    function openForVideo() {
        const presets = preferences.videoExportPresets;
        resolution.currentIndex = presets.findIndex(preset => preset.id === preferences.defaultVideoExportPreset);
        open();
    }
    contentItem: ColumnLayout {
        spacing: Theme.spaceMd
        Text {
            Layout.fillWidth: true
            text: qsTr("Entire video · %1 frames · %2 fps").arg(dialog.player.frameCount).arg(Number(dialog.player.fps.toFixed(2)))
            color: Theme.secondary
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd
            Text {
                text: qsTr("Resolution")
                color: Theme.ink
                font.family: Theme.font
                font.pixelSize: Theme.textBody
            }
            SelectControl {
                id: resolution
                objectName: "videoExportResolution"
                Layout.fillWidth: true
                model: dialog.preferences.videoExportPresets
                textRole: "label"
                valueRole: "id"
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Keeps the original aspect ratio, with black bars when needed. Exports at source speed with source audio.")
            color: Theme.secondary
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            wrapMode: Text.Wrap
        }
    }
    footer: Controls.DialogButtonBox {
        width: dialog.width
        standardButtons: Controls.Dialog.Cancel
        alignment: Qt.AlignRight
        delegate: Controls.Button {}
        Controls.Button {
            objectName: "chooseVideoExportFile"
            text: qsTr("Choose file…")
            enabled: dialog.resolutionId.length > 0
            Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
        }
        onAccepted: dialog.accept()
        onRejected: dialog.reject()
    }
    onAccepted: resolutionChosen(resolutionId)
}

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Controls.Dialog {
    id: editor
    objectName: "bookmarkEditor"
    required property var player
    property string bookmarkId: ""
    property bool editFrame: false
    readonly property bool validInput: editFrame ? input.acceptableInput && Number.isInteger(Number(input.text)) && Number(input.text) >= 1 && Number(input.text) <= player.frameCount : input.text.trim().length > 0

    function edit(id, name, frame, changeFrame) {
        bookmarkId = id;
        editFrame = changeFrame;
        input.text = changeFrame ? String(frame + 1) : name;
        open();
    }

    title: editFrame ? qsTr("Change bookmark frame") : qsTr("Rename bookmark")
    modal: true
    popupType: Controls.Popup.Item
    anchors.centerIn: parent
    width: Math.min(Theme.bookmarkEditorWidth, parent.width - Theme.spaceXl * 2)
    standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
    onOpened: {
        input.forceActiveFocus();
        input.selectAll();
    }
    onAccepted: {
        if (!validInput)
            return;
        if (editFrame)
            player.setBookmarkFrame(bookmarkId, Number(input.text) - 1);
        else
            player.bookmarks.rename(bookmarkId, input.text);
    }
    Binding {
        target: editor.standardButton(Controls.Dialog.Ok)
        property: "enabled"
        value: editor.validInput
    }
    contentItem: ColumnLayout {
        spacing: Theme.spaceSm
        Text {
            text: editor.editFrame ? qsTr("Frame (1–%1)").arg(player.frameCount) : qsTr("Name")
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            color: Theme.secondary
        }
        Controls.TextField {
            id: input
            objectName: "bookmarkEditorInput"
            Layout.fillWidth: true
            font.family: editor.editFrame ? Theme.monoFont : Theme.font
            font.pixelSize: Theme.textBody
            color: Theme.ink
            selectionColor: Theme.accentSurface
            selectedTextColor: Theme.ink
            inputMethodHints: editor.editFrame ? Qt.ImhDigitsOnly : Qt.ImhNone
            validator: editor.editFrame ? frameValidator : null
            Accessible.name: editor.editFrame ? qsTr("Bookmark frame") : qsTr("Bookmark name")
            onAccepted: {
                if (editor.validInput)
                    editor.accept();
            }
            IntValidator {
                id: frameValidator
                bottom: 1
                top: Math.max(1, player.frameCount)
                locale: "C"
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: Theme.elevated
                border.color: editor.validInput ? Theme.accent : Theme.warning
            }
        }
    }
    Connections {
        target: player
        function onMediaChanged() {
            if (!player.loaded)
                editor.reject();
        }
    }
}

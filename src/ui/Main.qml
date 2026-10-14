import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQml.Models
import Trie.FrameViewer
import Trie.Window

Controls.ApplicationWindow {
    id: app
    required property var player
    required property var preferences
    required property var windowsIntegration
    width: Theme.defaultWidth
    height: Theme.defaultHeight
    minimumWidth: Theme.minimumWidth
    minimumHeight: Theme.minimumHeight
    visible: true
    title: (player.loaded ? player.fileName + (player.dirty ? " •" : "") + " — " : "") + "Trie FrameViewer"
    color: Theme.background
    font.family: Theme.font
    font.pixelSize: Theme.textBody
    palette.window: Theme.background
    palette.windowText: Theme.ink
    palette.base: Theme.surface
    palette.text: Theme.ink
    palette.button: Theme.elevated
    palette.buttonText: Theme.ink
    palette.highlight: Theme.accentSurface
    palette.highlightedText: Theme.ink
    palette.light: Theme.border
    palette.mid: Theme.border
    palette.dark: Theme.background
    readonly property bool alwaysOnTop: (flags & Qt.WindowStaysOnTopHint) !== 0
    property string pendingAction: ""
    property url pendingUrl
    property bool allowClose: false
    property bool savingForContinuation: false
    readonly property bool textInputActive: activeFocusItem !== null && typeof activeFocusItem.selectAll === "function"
    Binding {
        target: Theme
        property: "dark"
        value: preferences.theme === "dark"
    }

    WindowChrome {
        objectName: "windowChrome"
        window: app
        dark: Theme.dark
        captionColor: Theme.titleBar
        textColor: Theme.ink
        borderColor: Theme.titleBar
    }
    function actionLabel(id) {
        for (const action of preferences.actions)
            if (action.id === id)
                return action.label;
        return id;
    }
    function tooltip(id) {
        return actionLabel(id) + " · " + preferences.shortcut(id);
    }
    function bookmark() {
        player.addBookmark(Theme.bookmarkColors[player.bookmarks.count % Theme.bookmarkColors.length]);
    }
    function guarded(action) {
        if (player.dirty) {
            pendingAction = action;
            unsaved.open();
        } else
            continueAction(action);
    }
    function continueAction(action) {
        pendingAction = "";
        if (action === "openMedia")
            mediaDialog.open();
        else if (action === "openProject")
            projectDialog.open();
        else if (action === "close") {
            allowClose = true;
            app.close();
        } else if (action === "drop")
            openUrl(pendingUrl);
    }
    function openUrl(url) {
        if (String(url).toLowerCase().endsWith(".tfv") || String(url).toLowerCase().endsWith(".tfv.autosave"))
            player.openProject(url);
        else
            player.openMedia(url);
    }
    function dispatch(action) {
        if (action === "openMedia" || action === "openProject" || action === "close")
            guarded(action);
        else if (action === "saveProject" || action === "saveProjectAs") {
            if (!player.loaded)
                return;
            if (action === "saveProject" && player.projectPath.length)
                player.saveProject("");
            else
                saveDialog.openForMedia();
        } else if (action === "bookmark")
            bookmark();
        else if (action === "settings")
            settingsDialog.openTab(0);
        else if (action === "shortcuts")
            settingsDialog.openTab(1);
        else if (action === "about")
            aboutDialog.open();
        else if (action === "mute")
            preferences.toggleMute();
        else if (action === "resetRange")
            player.resetRange();
        else if (action === "flipVertical")
            player.flip(true);
        else if (action === "rotateCounterclockwise")
            player.rotate(-90);
        else if (action === "zoomIn")
            player.zoom *= preferences.zoomWheelFactor;
        else if (action === "zoomOut")
            player.zoom /= preferences.zoomWheelFactor;
        else if (action === "alwaysOnTop") {
            const wasVisible = app.visible;
            app.flags = alwaysOnTop ? app.flags & ~Qt.WindowStaysOnTopHint : app.flags | Qt.WindowStaysOnTopHint;
            app.visible = wasVisible;
        } else if (action === "fullscreen")
            visibility = visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen;
        else
            player.execute(action);
    }
    onClosing: close => {
        if (player.dirty && !allowClose) {
            close.accepted = false;
            pendingAction = "close";
            unsaved.open();
        }
    }
    Instantiator {
        model: preferences.actions
        delegate: Shortcut {
            required property var modelData
            sequence: modelData.shortcut
            enabled: modelData.shortcut.length > 0 && !app.textInputActive && !bookmarkTimeline.editingBookmark && !settingsDialog.visible && !unsaved.visible && !aboutDialog.visible
            onActivated: app.dispatch(modelData.id)
        }
    }
    FileDialog {
        id: mediaDialog
        title: qsTr("Open Media")
        nameFilters: [qsTr("Footage (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.png *.jpg *.jpeg *.bmp *.tif *.tiff)"), qsTr("All files (*)")]
        onAccepted: player.openMedia(selectedFile)
    }
    FileDialog {
        id: projectDialog
        title: qsTr("Open a FrameViewer project")
        nameFilters: [qsTr("FrameViewer projects (*.tfv *.tfv.autosave)")]
        onAccepted: player.openProject(selectedFile)
    }
    FileDialog {
        id: saveDialog
        objectName: "saveProjectDialog"
        function prepareForMedia() {
            const suggestion = String(player.suggestedProjectUrl);
            currentFolder = suggestion.slice(0, suggestion.lastIndexOf("/") + 1);
            selectedFile = player.suggestedProjectUrl;
        }
        function openForMedia() {
            prepareForMedia();
            open();
        }
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tfv"
        nameFilters: [qsTr("FrameViewer projects (*.tfv)")]
        onAccepted: {
            const success = player.saveProject(selectedFile);
            if (success && app.savingForContinuation)
                app.continueAction(app.pendingAction);
            app.savingForContinuation = false;
        }
        onRejected: app.savingForContinuation = false
    }
    FileDialog {
        id: relinkDialog
        title: qsTr("Locate the original footage")
        onAccepted: player.relinkMedia(selectedFile)
    }
    Connections {
        target: player
        function onRelinkRequired(missingPath) {
            relinkPrompt.text = qsTr("Locate the original footage to restore this project:\n") + missingPath;
            relinkPrompt.open();
        }
    }
    Controls.Dialog {
        id: relinkPrompt
        property string text: ""
        title: qsTr("Relink footage")
        modal: true
        anchors.centerIn: parent
        width: Math.min(480, app.width - Theme.spaceXl * 2)
        standardButtons: Controls.Dialog.Open | Controls.Dialog.Cancel
        contentItem: Text {
            text: relinkPrompt.text
            color: Theme.ink
            wrapMode: Text.Wrap
            font.family: Theme.font
            font.pixelSize: Theme.textBody
        }
        onAccepted: relinkDialog.open()
    }
    Controls.Dialog {
        id: unsaved
        title: qsTr("Save your project?")
        modal: true
        anchors.centerIn: parent
        width: 430
        standardButtons: Controls.Dialog.Save | Controls.Dialog.Discard | Controls.Dialog.Cancel
        contentItem: Text {
            text: qsTr("Your bookmarks and review settings have unsaved changes.")
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            wrapMode: Text.WordWrap
        }
        onAccepted: {
            if (player.projectPath.length) {
                if (player.saveProject(""))
                    app.continueAction(app.pendingAction);
            } else {
                app.savingForContinuation = true;
                saveDialog.openForMedia();
            }
        }
        onDiscarded: app.continueAction(app.pendingAction)
        onRejected: app.pendingAction = ""
    }
    Controls.Dialog {
        id: aboutDialog
        title: qsTr("Trie FrameViewer")
        modal: true
        anchors.centerIn: parent
        width: Math.min(400, app.width - Theme.spaceXl * 2)
        standardButtons: Controls.Dialog.Close
        contentItem: Text {
            text: qsTr("Version %1\n\nAnimation reference and playblast review.\nSave bookmarks and review settings in a .tfv project.").arg(Qt.application.version)
            color: Theme.ink
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            wrapMode: Text.WordWrap
        }
    }
    SettingsDialog {
        id: settingsDialog
        parent: Controls.Overlay.overlay
        preferences: app.preferences
        player: app.player
        windowsIntegration: app.windowsIntegration
    }
    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (drop.hasUrls && drop.urls.length) {
                app.pendingUrl = drop.urls[0];
                app.guarded("drop");
                drop.acceptProposedAction();
            }
        }
        Rectangle {
            anchors.fill: parent
            color: Theme.accentSurface
            opacity: 0.15
            visible: parent.containsDrag
            z: 10
        }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        AppMenuBar {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.menuBarHeight
            player: app.player
            preferences: app.preferences
            fullScreen: app.visibility === Window.FullScreen
            alwaysOnTop: app.alwaysOnTop
            onActionRequested: action => app.dispatch(action)
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.border
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
            Viewer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                player: app.player
                preferences: app.preferences
                onOpenRequested: app.dispatch("openMedia")
            }
            Timeline {
                id: bookmarkTimeline
                Layout.fillWidth: true
                player: app.player
            }
            Transport {
                Layout.fillWidth: true
                player: app.player
                preferences: app.preferences
            }
        }
    }
    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            margins: Theme.spaceLg
            topMargin: Theme.menuBarHeight + Theme.spaceMd
        }
        height: errorText.implicitHeight + Theme.spaceLg * 2
        radius: Theme.radiusMd
        color: Theme.warningSurface
        visible: player.error.length > 0
        z: 20
        Text {
            id: errorText
            anchors {
                left: parent.left
                right: dismiss.left
                verticalCenter: parent.verticalCenter
                margins: Theme.spaceLg
            }
            text: player.error
            color: Theme.warning
            font.family: Theme.font
            font.pixelSize: Theme.textBody
            wrapMode: Text.WordWrap
        }
        ToolButton {
            id: dismiss
            anchors {
                right: parent.right
                verticalCenter: parent.verticalCenter
                rightMargin: Theme.spaceSm
            }
            iconName: "close"
            hint: qsTr("Dismiss")
            onClicked: player.clearError()
        }
    }
}

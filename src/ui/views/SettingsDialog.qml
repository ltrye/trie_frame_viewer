import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Trie.FrameViewer

Controls.Dialog {
    id: settings
    required property var preferences
    required property var player
    function openTab(index) {
        tabs.currentIndex = index;
        open();
    }
    title: qsTr("Preferences")
    modal: true
    width: Math.min(600, parent.width - Theme.spaceXl * 2)
    height: Math.min(620, parent.height - Theme.spaceXl * 2)
    anchors.centerIn: parent
    standardButtons: Controls.Dialog.Close
    contentItem: ColumnLayout {
        spacing: Theme.spaceLg
        Controls.TabBar {
            id: tabs
            Layout.fillWidth: true
            Controls.TabButton {
                text: qsTr("Appearance & playback")
            }
            Controls.TabButton {
                text: qsTr("Keyboard shortcuts")
            }
        }
        Controls.ScrollView {
            id: playbackScroll
            visible: tabs.currentIndex === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: playbackScroll.availableWidth
                spacing: Theme.spaceLg
                SectionLabel {
                    text: qsTr("APPEARANCE")
                }
                RowLayout {
                    Text {
                        text: qsTr("Theme")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                    }
                    SelectControl {
                        model: [qsTr("Dark"), qsTr("Light")]
                        currentIndex: preferences.theme === "dark" ? 0 : 1
                        onActivated: preferences.theme = index === 0 ? "dark" : "light"
                    }
                }
                SectionLabel {
                    text: qsTr("RAM CACHE")
                }
                RowLayout {
                    Text {
                        text: qsTr("Video frame budget")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                    }
                    Controls.SpinBox {
                        from: preferences.cacheLimits.videoMin
                        to: preferences.cacheLimits.videoMax
                        stepSize: preferences.cacheLimits.videoStep
                        value: preferences.cacheMiB
                        editable: true
                        onValueModified: preferences.cacheMiB = value
                    }
                    Text {
                        text: "MiB"
                        color: Theme.secondary
                        font.family: Theme.font
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Frames cache in the background. Increasing this budget keeps more footage ready for immediate scrubbing.")
                    wrapMode: Text.WordWrap
                    color: Theme.secondary
                    font.family: Theme.font
                    font.pixelSize: Theme.textBody
                }
                RowLayout {
                    Text {
                        text: qsTr("GPU frame cache")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                    }
                    Controls.SpinBox {
                        from: preferences.cacheLimits.gpuMin
                        to: preferences.cacheLimits.gpuMax
                        stepSize: preferences.cacheLimits.gpuStep
                        value: preferences.gpuCacheMiB
                        editable: true
                        onValueModified: preferences.gpuCacheMiB = value
                    }
                    Text {
                        text: "MiB"
                        color: Theme.secondary
                        font.family: Theme.font
                    }
                }
                RowLayout {
                    Text {
                        text: qsTr("Video decoding")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                    }
                    SelectControl {
                        model: preferences.decodeOptions.map(option => qsTranslate("Preferences", option.label))
                        currentIndex: preferences.decodeOptions.findIndex(option => option.id === preferences.decodeMode)
                        onActivated: preferences.decodeMode = preferences.decodeOptions[index].id
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("The decoding choice takes effect when footage is opened again. GPU caching leaves more frames ready for display without uploading them again.")
                    wrapMode: Text.WordWrap
                    color: Theme.secondary
                    font.family: Theme.font
                    font.pixelSize: Theme.textBody
                }
                SectionLabel {
                    text: qsTr("AUDIO")
                }
                RowLayout {
                    Text {
                        text: qsTr("Audio offset")
                        color: Theme.ink
                        font.family: Theme.font
                        font.pixelSize: Theme.textBody
                        Layout.fillWidth: true
                    }
                    Controls.SpinBox {
                        from: -10000
                        to: 10000
                        stepSize: 10
                        value: player.audioOffsetMs
                        editable: true
                        enabled: player.loaded
                        onValueModified: player.audioOffsetMs = value
                    }
                    Text {
                        text: "ms"
                        color: Theme.secondary
                        font.family: Theme.font
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Right-drag to scrub. Move vertically or hold Shift for finer control. Middle-drag to pan; scroll to zoom.")
                    wrapMode: Text.WordWrap
                    color: Theme.secondary
                    font.family: Theme.font
                    font.pixelSize: Theme.textBody
                }
            }
        }
        Controls.ScrollView {
            visible: tabs.currentIndex === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: Theme.spaceSm
                Repeater {
                    model: preferences.actions
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Text {
                            text: modelData.label
                            color: Theme.ink
                            font.family: Theme.font
                            font.pixelSize: Theme.textBody
                            Layout.fillWidth: true
                        }
                        Controls.TextField {
                            text: modelData.shortcut
                            Layout.preferredWidth: 170
                            placeholderText: qsTr("No shortcut")
                            onEditingFinished: preferences.setShortcut(modelData.id, text)
                        }
                    }
                }
                Controls.Button {
                    text: qsTr("Reset shortcuts")
                    onClicked: preferences.resetShortcuts()
                }
            }
        }
        Item {
            Layout.fillHeight: true
            visible: tabs.currentIndex === 0
        }
    }
}

import QtQuick
import QtQuick.Controls as Controls
import Trie.FrameViewer

Rectangle {
    id: overlay
    objectName: "debugMemoryOverlay"
    required property var monitor
    visible: monitor.enabled
    implicitWidth: memoryText.implicitWidth + Theme.spaceMd * 2
    implicitHeight: memoryText.implicitHeight + Theme.spaceSm * 2
    width: implicitWidth
    height: implicitHeight
    color: Theme.surface
    radius: Theme.radiusSm
    border.color: Theme.border

    function amount(value, available) {
        return available ? value.toFixed(1) + " MiB" : qsTr("Unavailable");
    }
    Text {
        id: memoryText
        objectName: "debugMemoryText"
        anchors.centerIn: parent
        text: qsTr("RAM  %1\nVRAM  %2\nShared GPU  %3").arg(overlay.amount(monitor.ramMiB, monitor.ramAvailable)).arg(overlay.amount(monitor.vramMiB, monitor.gpuAvailable)).arg(overlay.amount(monitor.sharedGpuMiB, monitor.gpuAvailable))
        color: Theme.secondary
        font.family: Theme.monoFont
        font.pixelSize: Theme.textSmall
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
    }
    Controls.ToolTip.visible: hover.containsMouse
    Controls.ToolTip.delay: Theme.tooltipDelay
    Controls.ToolTip.text: qsTr("Memory used by this app. Updated every %1 seconds.\nRAM: resident working set. Private commit: %2 MiB.\nGPU coverage: %3.\nVRAM: dedicated/local GPU memory. Shared GPU: system memory used by the GPU.\nIntegrated graphics also use system RAM for local GPU memory.").arg((monitor.refreshIntervalMs / 1000).toFixed(1)).arg(monitor.privateRamMiB.toFixed(1)).arg(monitor.gpuAllAdapters ? qsTr("all GPUs") : qsTr("rendering GPU"))
}

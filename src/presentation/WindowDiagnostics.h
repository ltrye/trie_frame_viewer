#pragma once
class QQuickWindow;
namespace tfv {
class ResourceMonitor;
void connectWindowDiagnostics(QQuickWindow& window, ResourceMonitor& monitor);
} // namespace tfv

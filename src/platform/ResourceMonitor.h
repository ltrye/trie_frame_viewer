#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <memory>

namespace tfv {
// Samples this process only. GPU allocations include rendering and hardware decoding.
class ResourceMonitor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int refreshIntervalMs READ refreshIntervalMs CONSTANT)
    Q_PROPERTY(bool ramAvailable READ ramAvailable NOTIFY metricsChanged)
    Q_PROPERTY(bool gpuAvailable READ gpuAvailable NOTIFY metricsChanged)
    Q_PROPERTY(bool gpuAllAdapters READ gpuAllAdapters NOTIFY metricsChanged)
    Q_PROPERTY(double ramMiB READ ramMiB NOTIFY metricsChanged)
    Q_PROPERTY(double privateRamMiB READ privateRamMiB NOTIFY metricsChanged)
    Q_PROPERTY(double vramMiB READ vramMiB NOTIFY metricsChanged)
    Q_PROPERTY(double sharedGpuMiB READ sharedGpuMiB NOTIFY metricsChanged)

  public:
    explicit ResourceMonitor(int refreshIntervalMs, QObject* parent = nullptr);
    ~ResourceMonitor() override;
    bool enabled() const {
        return m_enabled;
    }
    int refreshIntervalMs() const {
        return m_timer.interval();
    }
    bool ramAvailable() const {
        return m_ramAvailable;
    }
    bool gpuAvailable() const {
        return m_gpuAvailable;
    }
    bool gpuAllAdapters() const {
        return m_gpuAllAdapters;
    }
    double ramMiB() const {
        return m_ramMiB;
    }
    double privateRamMiB() const {
        return m_privateRamMiB;
    }
    double vramMiB() const {
        return m_vramMiB;
    }
    double sharedGpuMiB() const {
        return m_sharedGpuMiB;
    }
    void setEnabled(bool enabled);
    // Called on the GUI thread with a native renderer snapshot, when Windows
    // performance counters are missing or omit this process.
    void updateRendererMemory(bool available, quint64 localBytes, quint64 sharedBytes);

  signals:
    void enabledChanged();
    void metricsChanged();
    void gpuSampleRequested();

  private:
    void sample();
    struct PlatformState;
    std::unique_ptr<PlatformState> m_platform;
    QTimer m_timer;
    QElapsedTimer m_rendererSampleAge;
    bool m_gpuAllAdapters = false, m_rendererAvailable = false;
    double m_rendererLocalMiB = 0, m_rendererSharedMiB = 0;
    bool m_enabled = false, m_ramAvailable = false, m_gpuAvailable = false;
    double m_ramMiB = 0, m_privateRamMiB = 0, m_vramMiB = 0, m_sharedGpuMiB = 0;
};
} // namespace tfv

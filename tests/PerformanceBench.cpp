#include "PerformanceBench.h"
#include "application/PlayerController.h"
#include "presentation/VideoItem.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <mutex>
#include <psapi.h>
#include <windows.h>

namespace {
double cpuSeconds() {
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0;
    const auto ticks = [](FILETIME time) {
        return (quint64(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    };
    return (ticks(kernel) + ticks(user)) / 1e7;
}
class Benchmark final : public QObject {
  public:
    Benchmark(QGuiApplication& app, QQuickWindow& window, tfv::PlayerController& player,
              tfv::Preferences& preferences, QString path)
        : QObject(&app), m_app(app), m_window(window), m_player(player), m_path(std::move(path)) {
        preferences.setVolume(0);
        window.resize(960, 640);
        m_total.start();
        m_cpuStart = cpuSeconds();
        connect(&player, &tfv::PlayerController::frameReady, this, [this](const auto& image) {
            if (image.isNull())
                return;
            ++m_frames;
            if (m_firstFrameMs < 0) {
                m_firstFrameMs = m_total.elapsed();
                QTimer::singleShot(3000, this, [this] {
                    m_warmCache = m_player.cacheUsedMiB();
                    m_phase = 1;
                    nextSeek();
                });
            }
            if (m_pending && m_player.currentFrame() == m_target)
                m_delivered = true;
            // Hidden windows do not swap. Force a native render/readback so this
            // workload stays reproducible without putting a window on the desktop.
            QTimer::singleShot(0, this, [this] {
                m_window.grabWindow();
                if (!m_pending || !m_delivered)
                    return;
                m_pending = m_delivered = false;
                if (m_phase == 1) {
                    m_latencies.append(m_request.nsecsElapsed() / 1e6);
                    QTimer::singleShot(0, this, [this] { nextSeek(); });
                } else if (m_phase == 3) {
                    m_finalSeekMs = m_request.nsecsElapsed() / 1e6;
                    QTimer::singleShot(500, this, [this] { finish(); });
                }
            });
        });
        connect(
            &window, &QQuickWindow::afterRendering, this,
            [this] {
                if (m_window.rendererInterface()->graphicsApi() != QSGRendererInterface::Direct3D11)
                    return;
                auto* device = static_cast<ID3D11Device*>(m_window.rendererInterface()->getResource(
                    &m_window, QSGRendererInterface::DeviceResource));
                if (!device)
                    return;
                IDXGIDevice* dxgi = nullptr;
                IDXGIAdapter* adapter = nullptr;
                IDXGIAdapter3* adapter3 = nullptr;
                if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))) {
                    if (SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
                        SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
                        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                        DXGI_ADAPTER_DESC description{};
                        if (SUCCEEDED(adapter->GetDesc(&description))) {
                            std::lock_guard lock(m_gpuMutex);
                            m_adapter = QString::fromWCharArray(description.Description);
                        }
                        if (SUCCEEDED(
                                adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
                            m_gpuLocal = std::max(m_gpuLocal.load(), info.CurrentUsage);
                        if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,
                                                                     &info)))
                            m_gpuShared = std::max(m_gpuShared.load(), info.CurrentUsage);
                        adapter3->Release();
                    }
                    if (adapter)
                        adapter->Release();
                    dxgi->Release();
                }
            },
            Qt::DirectConnection);
        m_sample.setInterval(50);
        connect(&m_sample, &QTimer::timeout, this, [this] {
            PROCESS_MEMORY_COUNTERS_EX counters{};
            if (GetProcessMemoryInfo(GetCurrentProcess(),
                                     reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                     sizeof(counters))) {
                m_peakWorking = std::max(m_peakWorking, quint64(counters.WorkingSetSize));
                m_peakPrivate = std::max(m_peakPrivate, quint64(counters.PrivateUsage));
            }
        });
        m_sample.start();
        m_scrub.setInterval(8);
        m_scrub.setTimerType(Qt::PreciseTimer);
        connect(&m_scrub, &QTimer::timeout, this, [this] {
            const int count = m_player.frameCount();
            const int index = int((quint64(m_scrubRequests++) * 104729 + 37) % quint64(count));
            m_player.seek(index);
            if (m_scrubTime.elapsed() >= 3000) {
                m_scrub.stop();
                m_phase = 3;
                request(count / 2);
            }
        });
        QTimer::singleShot(90000, this, [this] { finish(true); });
    }

  private:
    void request(int index) {
        m_target = index;
        m_pending = true;
        m_delivered = false;
        m_request.restart();
        m_player.seek(index);
    }
    void nextSeek() {
        if (m_seeks == 48) {
            m_phase = 2;
            m_scrubTime.start();
            m_scrub.start();
            return;
        }
        const int count = m_player.frameCount();
        const int index = m_seeks < 24 ? int(quint64(m_seeks) * 7919 % quint64(count))
                                       : int(quint64(47 - m_seeks) * 7919 % quint64(count));
        ++m_seeks;
        request(index);
    }
    void finish(bool timedOut = false) {
        if (m_finished)
            return;
        m_finished = true;
        m_sample.stop();
        m_scrub.stop();
        auto sorted = m_latencies;
        std::sort(sorted.begin(), sorted.end());
        const auto percentile = [&](double fraction) {
            return sorted.isEmpty() ? -1. : sorted[int((sorted.size() - 1) * fraction)];
        };
        std::lock_guard lock(m_gpuMutex);
        auto* surface = m_window.findChild<tfv::VideoItem*>();
        const QJsonObject report{{"frameCount", m_player.frameCount()},
                                 {"width", m_player.mediaWidth()},
                                 {"height", m_player.mediaHeight()},
                                 {"firstFrameMs", m_firstFrameMs},
                                 {"wallSeconds", m_total.elapsed() / 1000.},
                                 {"cpuSeconds", cpuSeconds() - m_cpuStart},
                                 {"peakWorkingSetMiB", m_peakWorking / 1048576.},
                                 {"peakPrivateMiB", m_peakPrivate / 1048576.},
                                 {"peakGpuLocalMiB", m_gpuLocal.load() / 1048576.},
                                 {"peakGpuNonLocalMiB", m_gpuShared.load() / 1048576.},
                                 {"warmCacheMiB", m_warmCache},
                                 {"finalCacheMiB", m_player.cacheUsedMiB()},
                                 {"serialSeekP50Ms", percentile(.5)},
                                 {"serialSeekP95Ms", percentile(.95)},
                                 {"serialSeeksCompleted", m_latencies.size()},
                                 {"rapidScrubRequests", m_scrubRequests},
                                 {"framesDelivered", m_frames},
                                 {"finalSeekMs", m_finalSeekMs},
                                 {"timedOut", timedOut},
                                 {"error", m_player.error()},
                                 {"audioEnabled", false},
                                 {"decodeBackend", m_player.decodeBackend()},
                                 {"gpuAdapter", m_adapter},
                                 {"gpuTextureCacheMiB", surface ? surface->gpuCacheUsedMiB() : -1.},
                                 {"graphicsBackend", int(m_window.rendererInterface()->graphicsApi())},
                                 {"latencyIncludesNativeReadback", true}};
        QFile file(m_path);
        const bool written =
            file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(report).toJson()) > 0;
        m_app.exit(written && !timedOut && m_player.error().isEmpty() ? 0 : 5);
    }
    QGuiApplication& m_app;
    QQuickWindow& m_window;
    tfv::PlayerController& m_player;
    QString m_path;
    QElapsedTimer m_total, m_request, m_scrubTime;
    QTimer m_sample, m_scrub;
    QList<double> m_latencies;
    std::atomic<quint64> m_gpuLocal{0}, m_gpuShared{0};
    std::mutex m_gpuMutex;
    QString m_adapter;
    quint64 m_peakWorking = 0, m_peakPrivate = 0;
    double m_cpuStart = 0, m_firstFrameMs = -1, m_finalSeekMs = -1, m_warmCache = 0;
    int m_target = 0, m_seeks = 0, m_phase = 0, m_frames = 0, m_scrubRequests = 0;
    bool m_pending = false, m_delivered = false, m_finished = false;
};
} // namespace
void startPerformanceBench(QGuiApplication& app, QQuickWindow& window, tfv::PlayerController& player,
                           tfv::Preferences& preferences, const QString& reportPath) {
    new Benchmark(app, window, player, preferences, reportPath);
}

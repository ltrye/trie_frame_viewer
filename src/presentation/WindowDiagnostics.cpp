#include "WindowDiagnostics.h"
#include "platform/ResourceMonitor.h"
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <atomic>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <memory>
#include <wrl/client.h>

namespace tfv {
void connectWindowDiagnostics(QQuickWindow& window, ResourceMonitor& monitor) {
    // Only service an explicit, throttled fallback request. Normal playback pays
    // for one atomic check, with no device queries or allocations per frame.
    auto requested = std::make_shared<std::atomic_bool>(monitor.enabled());
    QObject::connect(&monitor, &ResourceMonitor::enabledChanged, &window, [&monitor, requested] {
        if (!monitor.enabled())
            requested->store(false);
    });
    QObject::connect(&monitor, &ResourceMonitor::gpuSampleRequested, &window, [&window, requested] {
        requested->store(true);
        window.update();
    });
    QObject::connect(
        &window, &QQuickWindow::afterRendering, &monitor,
        [&window, &monitor, requested] {
            if (!requested->exchange(false))
                return;
            bool available = false;
            quint64 localBytes = 0, sharedBytes = 0;
            auto* renderer = window.rendererInterface();
            if (renderer->graphicsApi() == QSGRendererInterface::Direct3D11) {
                auto* device = static_cast<ID3D11Device*>(
                    renderer->getResource(&window, QSGRendererInterface::DeviceResource));
                Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
                Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
                Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter3;
                DXGI_QUERY_VIDEO_MEMORY_INFO local{}, shared{};
                available =
                    device && SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) &&
                    SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter.As(&adapter3)) &&
                    SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) &&
                    SUCCEEDED(
                        adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared));
                if (available) {
                    localBytes = local.CurrentUsage;
                    sharedBytes = shared.CurrentUsage;
                }
            }
            QMetaObject::invokeMethod(
                &monitor,
                [&monitor, available, localBytes, sharedBytes] {
                    monitor.updateRendererMemory(available, localBytes, sharedBytes);
                },
                Qt::QueuedConnection);
        },
        Qt::DirectConnection);
}
} // namespace tfv

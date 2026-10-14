#include "ResourceMonitor.h"
#include <algorithm>
#include <cstddef>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>

namespace tfv {
struct ResourceMonitor::PlatformState {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER dedicated = nullptr, shared = nullptr;
    std::vector<std::byte> dedicatedBuffer, sharedBuffer;
    const std::wstring processPrefix = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";

    PlatformState() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
            return;
        // English names are localized by PDH; instance names include PID, adapter LUID,
        // and physical GPU index. Filtering exact PID prefixes includes every used GPU.
        if (PdhAddEnglishCounterW(query, L"\\GPU Process Memory(*)\\Dedicated Usage", 0, &dedicated) !=
                ERROR_SUCCESS ||
            PdhAddEnglishCounterW(query, L"\\GPU Process Memory(*)\\Shared Usage", 0, &shared) !=
                ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
        }
    }
    ~PlatformState() {
        if (query)
            PdhCloseQuery(query);
    }
    bool readCounter(PDH_HCOUNTER counter, std::vector<std::byte>& buffer, double& bytes, bool& found) const {
        DWORD size = 0, count = 0;
        constexpr DWORD format = PDH_FMT_LARGE | PDH_FMT_NOSCALE;
        if (PdhGetFormattedCounterArrayW(counter, format, &size, &count, nullptr) != PDH_MORE_DATA)
            return false;
        buffer.resize(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, format, &size, &count, items) != ERROR_SUCCESS)
            return false;
        bytes = 0;
        for (DWORD index = 0; index < count; ++index) {
            const auto& item = items[index];
            if (!item.szName || std::wstring_view(item.szName).find(processPrefix) != 0)
                continue;
            found = true;
            if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
                return false;
            bytes += double(std::max<LONGLONG>(0, item.FmtValue.largeValue));
        }
        return true;
    }
    bool sampleGpu(double& dedicatedBytes, double& sharedBytes) {
        bool dedicatedFound = false, sharedFound = false;
        return query && PdhCollectQueryData(query) == ERROR_SUCCESS &&
               readCounter(dedicated, dedicatedBuffer, dedicatedBytes, dedicatedFound) &&
               readCounter(shared, sharedBuffer, sharedBytes, sharedFound) && (dedicatedFound || sharedFound);
    }
};

ResourceMonitor::ResourceMonitor(int refreshIntervalMs, QObject* parent) : QObject(parent) {
    m_timer.setInterval(std::clamp(refreshIntervalMs, 100, 60000));
    m_timer.setTimerType(Qt::CoarseTimer);
    connect(&m_timer, &QTimer::timeout, this, &ResourceMonitor::sample);
}
ResourceMonitor::~ResourceMonitor() = default;

void ResourceMonitor::setEnabled(bool enabled) {
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    if (enabled) {
        m_rendererSampleAge.invalidate();
        m_rendererAvailable = false;
        m_platform = std::make_unique<PlatformState>();
        sample();
        m_timer.start();
    } else {
        m_timer.stop();
        m_platform.reset();
    }
    emit enabledChanged();
}

void ResourceMonitor::updateRendererMemory(bool available, quint64 localBytes, quint64 sharedBytes) {
    if (!m_enabled)
        return;
    m_rendererAvailable = available;
    m_rendererLocalMiB = localBytes / 1048576.;
    m_rendererSharedMiB = sharedBytes / 1048576.;
    m_rendererSampleAge.start();
    if (!m_gpuAllAdapters) {
        m_gpuAvailable = available;
        m_vramMiB = available ? m_rendererLocalMiB : 0;
        m_sharedGpuMiB = available ? m_rendererSharedMiB : 0;
        emit metricsChanged();
    }
}

void ResourceMonitor::sample() {
    if (!m_enabled)
        return;
    constexpr double bytesPerMiB = 1048576.;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    m_ramAvailable = GetProcessMemoryInfo(
        GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters));
    m_ramMiB = m_ramAvailable ? counters.WorkingSetSize / bytesPerMiB : 0;
    m_privateRamMiB = m_ramAvailable ? counters.PrivateUsage / bytesPerMiB : 0;

    double dedicatedBytes = 0, sharedBytes = 0;
    // Some drivers expose a process instance but publish zero even while its
    // renderer has live allocations. Verify those empty readings through DXGI.
    m_gpuAllAdapters =
        m_platform->sampleGpu(dedicatedBytes, sharedBytes) && (dedicatedBytes > 0 || sharedBytes > 0);
    if (m_gpuAllAdapters) {
        m_gpuAvailable = true;
        m_vramMiB = dedicatedBytes / bytesPerMiB;
        m_sharedGpuMiB = sharedBytes / bytesPerMiB;
    } else {
        m_gpuAvailable = m_rendererAvailable && m_rendererSampleAge.isValid() &&
                         m_rendererSampleAge.elapsed() <= m_timer.interval() * 3;
        m_vramMiB = m_gpuAvailable ? m_rendererLocalMiB : 0;
        m_sharedGpuMiB = m_gpuAvailable ? m_rendererSharedMiB : 0;
        emit gpuSampleRequested();
    }
    emit metricsChanged();
}
} // namespace tfv

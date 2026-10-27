#pragma once
#include "FrameCache.h"
#include <memory>
#include <mutex>

namespace tfv {
// Shared cache metadata is locked briefly; pixels and codec execution are not.
class FrameRepository final {
  public:
    explicit FrameRepository(qint64 budget) : m_cache(std::make_unique<FrameCache>(budget)) {}
    void invalidate(quint64 generation) {
        std::lock_guard lock(m_mutex);
        m_generation = generation;
        m_ready = false;
    }
    void reset(quint64 generation, qint64 budget) {
        auto replacement = std::make_unique<FrameCache>(budget);
        {
            std::lock_guard lock(m_mutex);
            if (generation != m_generation)
                return;
            m_cache.swap(replacement);
            m_ready = true;
        } // Old generation is freed on the worker, outside the repository lock.
    }
    VideoFrame get(quint64 generation, int index) {
        std::lock_guard lock(m_mutex);
        return valid(generation) ? m_cache->get(index) : VideoFrame{};
    }
    bool contains(quint64 generation, int index) const {
        std::lock_guard lock(m_mutex);
        return valid(generation) && m_cache->contains(index);
    }
    bool insert(quint64 generation, int index, const VideoFrame& image, int anchor) {
        std::vector<VideoFrame> retired;
        bool kept = false;
        {
            std::lock_guard lock(m_mutex);
            if (valid(generation))
                kept = m_cache->insert(index, image, anchor, &retired);
        }
        return kept; // Last pixel references are destroyed after unlocking.
    }
    void setBudget(quint64 generation, qint64 budget, int anchor) {
        std::vector<VideoFrame> retired;
        {
            std::lock_guard lock(m_mutex);
            if (valid(generation))
                m_cache->setBudget(budget, anchor, &retired);
        }
    }
    void setWindow(quint64 generation, int first, int last, int wrapFirst = 0, int wrapLast = -1) {
        std::lock_guard lock(m_mutex);
        if (valid(generation)) {
            m_cache->setWindow(first, last);
            m_cache->setWrapWindow(wrapFirst, wrapLast);
        }
    }
    struct Snapshot {
        qint64 bytes = 0;
        quint64 revision = 0;
        QVariantList ranges;
    };
    Snapshot snapshot(quint64 generation, quint64 previousRevision) const {
        std::lock_guard lock(m_mutex);
        if (!valid(generation))
            return {};
        const auto revision = m_cache->revision();
        return {m_cache->bytes(), revision,
                revision == previousRevision ? QVariantList{} : m_cache->ranges()};
    }

  private:
    bool valid(quint64 generation) const {
        return m_ready && generation == m_generation;
    }
    mutable std::mutex m_mutex;
    std::unique_ptr<FrameCache> m_cache;
    quint64 m_generation = 0;
    bool m_ready = false;
};
} // namespace tfv

#pragma once
#include "VideoFrame.h"
#include <QVariantList>
#include <algorithm>
#include <list>
#include <map>

namespace tfv {
// Worker-owned LRU; the playhead window influences eviction under pressure.
class FrameCache {
  public:
    explicit FrameCache(qint64 budget) : m_budget(budget) {}
    void setBudget(qint64 bytes, int anchor) {
        m_budget = std::max<qint64>(1, bytes);
        evict(anchor);
    }
    void clear() {
        m_frames.clear();
        m_lru.clear();
        m_bytes = 0;
    }
    VideoFrame get(int index) {
        auto it = m_frames.find(index);
        if (it == m_frames.end())
            return {};
        m_lru.splice(m_lru.end(), m_lru, it->second.age);
        return it->second.image;
    }
    bool contains(int index) const {
        return m_frames.contains(index);
    }
    void setWindow(int first, int last) {
        m_first = first;
        m_last = last;
    }
    bool insert(int index, const VideoFrame& image, int anchor) {
        if (image.isNull() || image.sizeInBytes() > m_budget)
            return false;
        auto it = m_frames.find(index);
        if (it != m_frames.end()) {
            m_bytes -= it->second.image.sizeInBytes();
            it->second.image = image;
            m_lru.splice(m_lru.end(), m_lru, it->second.age);
        } else {
            m_lru.push_back(index);
            m_frames.emplace(index, Entry{image, std::prev(m_lru.end())});
        }
        m_bytes += image.sizeInBytes();
        evict(anchor);
        return contains(index);
    }
    qint64 bytes() const {
        return m_bytes;
    }
    bool nearlyFull(qint64 nextSize) const {
        return m_bytes + nextSize > m_budget;
    }
    QVariantList ranges() const {
        QVariantList result;
        int start = -1, end = -1;
        for (const auto& [index, image] : m_frames) {
            Q_UNUSED(image);
            if (start < 0)
                start = end = index;
            else if (index == end + 1)
                end = index;
            else {
                result.append(QVariantMap{{"start", start}, {"end", end}});
                start = end = index;
            }
        }
        if (start >= 0)
            result.append(QVariantMap{{"start", start}, {"end", end}});
        return result;
    }

  private:
    struct Entry {
        VideoFrame image;
        std::list<int>::iterator age;
    };
    void evict(int anchor) {
        while (m_bytes > m_budget && !m_lru.empty()) {
            auto victim = m_lru.end();
            for (auto it = m_lru.begin(); it != m_lru.end(); ++it) {
                if (*it != anchor && (*it < m_first || *it > m_last)) {
                    victim = it;
                    break;
                }
            }
            if (victim == m_lru.end()) {
                victim = m_lru.begin();
                if (*victim == anchor && m_lru.size() > 1)
                    ++victim;
            }
            const auto frame = m_frames.find(*victim);
            m_bytes -= frame->second.image.sizeInBytes();
            m_frames.erase(frame);
            m_lru.erase(victim);
        }
    }
    qint64 m_budget, m_bytes = 0;
    int m_first = -1, m_last = -1;
    std::map<int, Entry> m_frames;
    std::list<int> m_lru;
};
} // namespace tfv

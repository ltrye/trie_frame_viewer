#pragma once
#include "VideoFrame.h"
#include <QVariantList>
#include <algorithm>
#include <list>
#include <map>
#include <vector>

namespace tfv {
// LRU policy serialized by FrameRepository; playhead windows bias eviction.
class FrameCache {
  public:
    explicit FrameCache(qint64 budget) : m_budget(budget) {}
    void setBudget(qint64 bytes, int anchor, std::vector<VideoFrame>* retired = nullptr) {
        m_budget = std::max<qint64>(1, bytes);
        evict(anchor, retired);
    }
    void clear() {
        if (!m_frames.empty())
            ++m_revision;
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
    void setWrapWindow(int first, int last) {
        m_wrapFirst = first;
        m_wrapLast = last;
    }
    bool insert(int index, const VideoFrame& image, int anchor, std::vector<VideoFrame>* retired = nullptr) {
        if (image.isNull() || image.sizeInBytes() > m_budget)
            return false;
        auto it = m_frames.find(index);
        if (it != m_frames.end()) {
            if (it->second.image.identity() == image.identity()) {
                m_lru.splice(m_lru.end(), m_lru, it->second.age);
                return true;
            }
            if (retired)
                retired->push_back(it->second.image);
            m_bytes -= it->second.image.sizeInBytes();
            it->second.image = image;
            m_lru.splice(m_lru.end(), m_lru, it->second.age);
        } else {
            m_lru.push_back(index);
            m_frames.emplace(index, Entry{image, std::prev(m_lru.end())});
        }
        m_bytes += image.sizeInBytes();
        ++m_revision;
        evict(anchor, retired);
        return contains(index);
    }
    qint64 bytes() const {
        return m_bytes;
    }
    quint64 revision() const {
        return m_revision;
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
    void evict(int anchor, std::vector<VideoFrame>* retired = nullptr) {
        while (m_bytes > m_budget && !m_lru.empty()) {
            auto victim = m_lru.end();
            for (auto it = m_lru.begin(); it != m_lru.end(); ++it) {
                if (*it != anchor && (*it < m_first || *it > m_last) &&
                    (*it < m_wrapFirst || *it > m_wrapLast)) {
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
            if (retired)
                retired->push_back(frame->second.image);
            m_bytes -= frame->second.image.sizeInBytes();
            m_frames.erase(frame);
            m_lru.erase(victim);
            ++m_revision;
        }
    }
    qint64 m_budget, m_bytes = 0;
    quint64 m_revision = 0;
    int m_first = -1, m_last = -1;
    int m_wrapFirst = 0, m_wrapLast = -1;
    std::map<int, Entry> m_frames;
    std::list<int> m_lru;
};
} // namespace tfv

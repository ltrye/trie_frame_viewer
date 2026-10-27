#pragma once
#include <algorithm>
#include <array>

namespace tfv {
// Pure policy: useful intervals fit one frame-capacity estimate, including wrap.
class PrefetchPlanner final {
  public:
    struct Range {
        int first = 0, last = -1;
        bool contains(int frame) const {
            return frame >= first && frame <= last;
        }
    };
    void plan(int capacity, int anchor, int direction, int minimum, int maximum, bool loop) {
        capacity = std::clamp(capacity, 1, maximum - minimum + 1);
        const int distance = direction > 0 ? maximum - anchor : anchor - minimum;
        const int reserve =
            loop && maximum - minimum + 1 > capacity && capacity >= 4 && distance < capacity / 4
                ? std::max(1, capacity / 4)
                : 0;
        const int mainCapacity = capacity - reserve;
        const int behind = (mainCapacity - 1) / 4;
        const int offset = direction > 0 ? behind : mainCapacity - 1 - behind;
        main.first = std::clamp(anchor - offset, minimum, maximum - mainCapacity + 1);
        main.last = main.first + mainCapacity - 1;
        wrap = reserve ? (direction > 0 ? Range{minimum, minimum + reserve - 1}
                                        : Range{maximum - reserve + 1, maximum})
                       : Range{};
        const Range ahead{anchor + 1, main.last}, back{main.first, anchor - 1};
        m_segments = direction > 0 ? std::array{ahead, wrap, back} : std::array{back, wrap, ahead};
        m_segment = 0;
        m_cursor = m_segments[0].first;
    }
    template <typename Contains> int next(Contains contains) {
        while (m_segment < m_segments.size()) {
            const auto range = m_segments[m_segment];
            if (m_cursor > range.last) {
                ++m_segment;
                if (m_segment < m_segments.size())
                    m_cursor = m_segments[m_segment].first;
                continue;
            }
            const int frame = m_cursor++;
            if (!contains(frame))
                return frame;
        }
        return -1;
    }
    bool contains(int frame) const {
        return main.contains(frame) || wrap.contains(frame);
    }
    Range main, wrap;

  private:
    std::array<Range, 3> m_segments{};
    size_t m_segment = 3;
    int m_cursor = 0;
};
} // namespace tfv

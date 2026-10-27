#pragma once
#include <QElapsedTimer>
#include <QString>
#include <mutex>
#include <vector>

namespace tfv {
// Opt-in bounded developer trace. Disabled calls allocate nothing or take locks.
class PlaybackTrace final {
  public:
    explicit PlaybackTrace(QString path = {}, size_t capacity = 32768);
    ~PlaybackTrace();
    static PlaybackTrace& global();
    bool enabled() const {
        return !m_path.isEmpty();
    }
    qint64 timeUs() const {
        return enabled() ? m_clock.nsecsElapsed() / 1000 : -1;
    }
    void record(const char* stage, quint64 request = 0, int frame = -1, qint64 value = 0, qint64 atUs = -1);
    bool flush();

  private:
    struct Event {
        const char* stage;
        qint64 us;
        quint64 request;
        int frame;
        qint64 value;
    };
    QString m_path;
    QElapsedTimer m_clock;
    std::mutex m_mutex;
    std::vector<Event> m_events;
    size_t m_capacity;
    quint64 m_dropped = 0;
};
inline void tracePlayback(const char* stage, quint64 request = 0, int frame = -1, qint64 value = 0,
                          qint64 atUs = -1) {
    auto& trace = PlaybackTrace::global();
    if (trace.enabled())
        trace.record(stage, request, frame, value, atUs);
}
} // namespace tfv

#include "PlaybackTrace.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace tfv {
PlaybackTrace::PlaybackTrace(QString path, size_t capacity)
    : m_path(std::move(path)), m_capacity(std::clamp<size_t>(capacity, 1, 1000000)) {
    m_clock.start();
    if (enabled())
        m_events.reserve(m_capacity);
}
PlaybackTrace::~PlaybackTrace() {
    flush();
}
PlaybackTrace& PlaybackTrace::global() {
    static PlaybackTrace trace(qEnvironmentVariable("TFV_TRACE_PATH"));
    return trace;
}
void PlaybackTrace::record(const char* stage, quint64 request, int frame, qint64 value, qint64 atUs) {
    if (!enabled())
        return;
    const auto us = atUs >= 0 ? atUs : m_clock.nsecsElapsed() / 1000;
    std::lock_guard lock(m_mutex);
    if (m_events.size() == m_capacity) {
        ++m_dropped;
        return;
    }
    m_events.push_back({stage, us, request, frame, value});
}
bool PlaybackTrace::flush() {
    if (!enabled())
        return true;
    std::vector<Event> events;
    quint64 dropped;
    {
        std::lock_guard lock(m_mutex);
        events = m_events;
        dropped = m_dropped;
    }
    QJsonArray array;
    for (const auto& event : events)
        array.append(QJsonObject{{"stage", QString::fromLatin1(event.stage)},
                                 {"tUs", event.us},
                                 {"request", qint64(event.request)},
                                 {"frame", event.frame},
                                 {"value", event.value}});
    QSaveFile file(m_path);
    const auto data = QJsonDocument(QJsonObject{{"schema", 1},
                                                {"timeUnit", "microseconds"},
                                                {"droppedEvents", qint64(dropped)},
                                                {"events", array}})
                          .toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
} // namespace tfv

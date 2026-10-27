#include "MediaWorker.h"
#include "PlaybackTrace.h"
#include "PrefetchPlanner.h"
#include <QElapsedTimer>
#include <algorithm>
#include <limits>

namespace tfv {
MediaWorker::MediaWorker(QObject* parent) : QObject(parent) {
    qRegisterMetaType<VideoFrame>();
    m_thread = std::jthread([this](std::stop_token stop) { run(stop); });
}
MediaWorker::~MediaWorker() {
    m_thread.request_stop();
    m_revision.fetch_add(1);
    m_wake.notify_all();
    m_thread.join();
}
void MediaWorker::open(const QString& path, quint64 generation, qint64 budget, qint64 audioBudget) {
    {
        std::lock_guard lock(m_mutex);
        m_frames.invalidate(generation);
        m_path = path;
        m_generation = generation;
        m_openPending = true;
        m_requested = 0;
        m_cachedRequest = false;
        m_lastHint = 0;
        m_direction = 1;
        m_playing = false;
        m_rangeFirst = 0;
        m_rangeLast = -1;
        m_requestKind = RequestKind::Seek;
        m_epoch = 0;
        m_audioBudget = audioBudget;
        m_budget = budget;
        m_revision.fetch_add(1);
        m_requestRevision.fetch_add(1);
        m_speculationRevision.fetch_add(1);
    }
    tracePlayback("source.open", 0, -1, qint64(generation));
    m_wake.notify_one();
}
quint64 MediaWorker::reserveRequestSerial() {
    std::lock_guard lock(m_mutex);
    return ++m_serial;
}
quint64 MediaWorker::seek(int index, quint64 epoch, int direction, RequestKind kind, quint64 requestSerial) {
    VideoFrame cached;
    quint64 serial, generation;
    {
        std::lock_guard lock(m_mutex);
        serial = requestSerial ? requestSerial : ++m_serial;
        m_serial = std::max(m_serial, serial);
        generation = m_generation;
        tracePlayback("request", serial, index, int(kind));
        cached = m_frames.get(generation, index);
        if (cached.isNull() || (direction < 0 ? -1 : 1) != m_direction ||
            std::abs(index - m_lastHint) > m_retargetFrames)
            m_speculationRevision.fetch_add(1);
        m_lastHint = index;
        m_requested = index;
        m_requestedSerial = serial;
        m_cachedRequest = !cached.isNull();
        m_direction = direction < 0 ? -1 : 1;
        m_requestKind = kind;
        m_epoch = epoch;
        m_requestRevision.fetch_add(1);
    }
    tracePlayback(cached.isNull() ? "cache.miss" : "cache.hit", serial, index);
    if (!cached.isNull())
        emit decoded(generation, epoch, index, cached.forRequest(serial, index), serial);
    m_wake.notify_one();
    return serial;
}
void MediaWorker::setPlaybackRange(int first, int last, bool playing, bool loop) {
    {
        std::lock_guard lock(m_mutex);
        if (first == m_rangeFirst && last == m_rangeLast && playing == m_playing && loop == m_loop)
            return;
        m_rangeFirst = first;
        m_rangeLast = last;
        m_playing = playing;
        m_loop = loop;
        m_hintPending = true;
    }
    m_wake.notify_one();
}
void MediaWorker::setBudget(qint64 value) {
    m_budget = value;
    m_wake.notify_one();
}
void MediaWorker::setDecodeOptions(int threads, bool hardware, int minimumPixels, int prefetchDelayMs,
                                   int fineScrubFrames, int retargetFrames) {
    m_decoderThreads = std::clamp(threads, 1, 16);
    m_hardware = hardware;
    m_hardwareMinimumPixels = std::max(0, minimumPixels);
    m_prefetchDelayMs = std::clamp(prefetchDelayMs, 0, 500);
    m_fineScrubFrames = std::clamp(fineScrubFrames, 1, 256);
    m_retargetFrames = std::clamp(retargetFrames, 1, 1024);
}
void MediaWorker::run(std::stop_token stop) {
    Decoder decoder;
    PrefetchPlanner planner;
    std::jthread audioThread;
    quint64 generation = 0, sourceRevision = 0;
    quint64 publishedRevision = std::numeric_limits<quint64>::max();
    int anchor = 0, direction = 1, minimum = 0, maximum = -1;
    bool valid = false, prefilling = false, playing = false, loop = false;
    int quietDelay = 0;
    qint64 frameBytes = 0, appliedBudget = m_budget;
    QElapsedTimer coverageClock, quietClock;
    coverageClock.start();
    quietClock.start();
    auto publish = [&](bool force = false) {
        const auto snapshot =
            m_frames.snapshot(generation, force ? std::numeric_limits<quint64>::max() : publishedRevision);
        if (force || snapshot.revision != publishedRevision) {
            emit cacheUpdated(generation, snapshot.bytes, snapshot.ranges);
            tracePlayback("cache.bytes", 0, anchor, snapshot.bytes);
            publishedRevision = snapshot.revision;
        }
        coverageClock.restart();
    };
    auto planWindow = [&] {
        const int capacity = int(std::clamp<qint64>(appliedBudget / std::max<qint64>(1, frameBytes), 1,
                                                    decoder.info().frameCount));
        const bool inRange = playing && maximum >= minimum && anchor >= minimum && anchor <= maximum;
        planner.plan(capacity, anchor, direction, inRange ? minimum : 0,
                     inRange ? maximum : decoder.info().frameCount - 1, inRange && loop);
        m_frames.setWindow(generation, planner.main.first, planner.main.last, planner.wrap.first,
                           planner.wrap.last);
        prefilling = true;
    };
    while (!stop.stop_requested()) {
        QString path;
        bool opening, hint, cachedRequest;
        int requested;
        qint64 audioBudget = 0;
        quint64 epoch, requestRevision, serial;
        RequestKind kind;
        {
            std::unique_lock lock(m_mutex);
            if (!m_openPending && m_requested < 0 && !m_hintPending && (!prefilling || !valid))
                m_wake.wait(lock, [&] {
                    return stop.stop_requested() || m_openPending || m_requested >= 0 || m_hintPending ||
                           m_budget.load() != appliedBudget;
                });
            if (stop.stop_requested())
                break;
            opening = m_openPending;
            if (opening) {
                path = m_path;
                generation = m_generation;
                audioBudget = m_audioBudget;
                m_openPending = false;
                sourceRevision = m_revision;
            }
            requested = m_requested;
            m_requested = -1;
            cachedRequest = m_cachedRequest;
            serial = m_requestedSerial;
            hint = m_hintPending;
            m_hintPending = false;
            direction = m_direction;
            kind = m_requestKind;
            epoch = m_epoch;
            requestRevision = m_requestRevision;
            playing = m_playing;
            minimum = m_rangeFirst;
            maximum = m_rangeLast;
            loop = m_loop;
        }
        const bool budgetChanged = appliedBudget != m_budget;
        appliedBudget = m_budget;
        if (budgetChanged)
            m_frames.setBudget(generation, appliedBudget, anchor);
        auto sourceCancelled = [&] { return stop.stop_requested() || sourceRevision != m_revision; };
        if (opening) {
            valid = prefilling = false;
            m_frames.reset(generation, appliedBudget);
            publishedRevision = std::numeric_limits<quint64>::max();
            publish(true);
            QString error;
            if (!decoder.open(path, error, m_decoderThreads, m_hardware, m_hardwareMinimumPixels)) {
                if (!sourceCancelled())
                    emit failed(generation, error);
                continue;
            }
            if (sourceCancelled())
                continue;
            anchor = 0;
            auto image = decoder.videoFrame(0, error, sourceCancelled);
            if (image.isNull() && m_hardware && !sourceCancelled() &&
                decoder.open(path, error, m_decoderThreads, false))
                image = decoder.videoFrame(0, error, sourceCancelled);
            if (sourceCancelled())
                continue;
            if (image.isNull()) {
                emit failed(generation, error);
                continue;
            }
            valid = true;
            m_frames.insert(generation, 0, image, 0);
            emit opened(generation, decoder.info());
            frameBytes = image.sizeInBytes();
            emit decoded(generation, 0, 0, image.forRequest(0, 0), 0);
            if (decoder.info().variableRate)
                emit warning(generation, "Variable-rate footage detected. This preview uses the nominal "
                                         "frame rate; use a constant-rate playblast for precise bookmarks.");
            if (audioThread.joinable()) {
                audioThread.request_stop();
                audioThread.join();
            }
            audioThread = std::jthread(
                [this, path, audioBudget, generation, sourceRevision, stop](std::stop_token audioStop) {
                    auto cancelled = [&] {
                        return audioStop.stop_requested() || stop.stop_requested() ||
                               sourceRevision != m_revision;
                    };
                    QString warningText;
                    auto pcm = Decoder::decodeAudio(path, audioBudget, cancelled, warningText);
                    if (cancelled())
                        return;
                    emit audioReady(generation, pcm);
                    if (!warningText.isEmpty())
                        emit warning(generation, warningText);
                });
            quietDelay = 0;
            planWindow();
            publish(true);
            requested = -1;
        }
        if (!valid || sourceCancelled())
            continue;
        if (requested >= 0) {
            quietClock.restart();
            const int previousAnchor = anchor;
            anchor = std::clamp(requested, 0, decoder.info().frameCount - 1);
            quietDelay = playing || kind == RequestKind::Playback ||
                                 (kind == RequestKind::Preview &&
                                  std::abs(anchor - previousAnchor) <= m_fineScrubFrames)
                             ? 0
                             : m_prefetchDelayMs.load();
            planWindow();
            if (!cachedRequest) {
                tracePlayback("worker.begin", serial, anchor);
                auto cancelled = [&] { return sourceCancelled() || epoch != m_epoch; };
                QString error;
                auto image = m_frames.get(generation, anchor);
                if (image.isNull()) {
                    emit decodeStarted(serial, anchor);
                    if (kind == RequestKind::Playback && direction < 0)
                        image = decoder.videoFrame(
                            anchor, error, cancelled,
                            [&](int index, const VideoFrame& frame) {
                                if (!cancelled() && planner.contains(index))
                                    m_frames.insert(generation, index, frame, anchor);
                            },
                            [&](int index) {
                                return planner.contains(index) && !m_frames.contains(generation, index);
                            },
                            serial);
                    else
                        image = decoder.videoFrame(anchor, error, cancelled, {}, {}, serial);
                }
                if (cancelled()) {
                    tracePlayback("worker.cancelled", serial, anchor);
                    continue;
                }
                if (!image.isNull()) {
                    m_frames.insert(generation, anchor, image, anchor);
                    tracePlayback("worker.delivered", serial, anchor);
                    emit decoded(generation, epoch, anchor, image.forRequest(serial, anchor), serial);
                } else if (!error.isEmpty())
                    emit warning(generation, error);
            }
            if (coverageClock.elapsed() >= 100)
                publish();
        } else if ((budgetChanged || hint) && !opening) {
            planWindow();
            publish();
        }
        if (sourceCancelled() || requestRevision != m_requestRevision)
            continue;
        if (quietClock.elapsed() < quietDelay) {
            std::unique_lock lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(quietDelay - quietClock.elapsed()), [&] {
                return stop.stop_requested() || m_openPending || m_requested >= 0 || m_hintPending ||
                       m_budget != appliedBudget;
            });
            continue;
        }
        const int candidate = planner.next([&](int index) { return m_frames.contains(generation, index); });
        if (candidate >= 0) {
            const auto speculation = m_speculationRevision.load();
            auto cancelled = [&] { return sourceCancelled() || speculation != m_speculationRevision; };
            QString error;
            tracePlayback("prefetch.begin", 0, candidate);
            auto image = decoder.videoFrame(
                candidate, error, cancelled,
                [&](int index, const VideoFrame& frame) {
                    if (!cancelled() && planner.contains(index))
                        m_frames.insert(generation, index, frame, anchor);
                },
                [&](int index) { return planner.contains(index) && !m_frames.contains(generation, index); });
            if (cancelled())
                continue;
            if (image.isNull())
                prefilling = false;
            else
                m_frames.insert(generation, candidate, image, anchor);
            tracePlayback("prefetch.end", 0, candidate);
        } else
            prefilling = false;
        if (!prefilling || coverageClock.elapsed() >= 100)
            publish();
    }
}
} // namespace tfv

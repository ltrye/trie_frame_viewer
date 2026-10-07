#include "MediaWorker.h"
#include "FrameCache.h"
#include <QElapsedTimer>
#include <algorithm>

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
        m_path = path;
        m_generation = generation;
        m_openPending = true;
        m_requested = 0;
        m_direction = 1;
        m_epoch = 0;
        m_audioBudget = audioBudget;
        m_budget = budget;
        m_revision.fetch_add(1);
        m_requestRevision.fetch_add(1);
    }
    m_wake.notify_one();
}
void MediaWorker::seek(int index, quint64 epoch, int direction) {
    {
        std::lock_guard lock(m_mutex);
        m_requested = index;
        m_direction = direction < 0 ? -1 : 1;
        m_epoch = epoch;
        m_requestRevision.fetch_add(1);
    }
    m_wake.notify_one();
}
void MediaWorker::setBudget(qint64 value) {
    m_budget = value;
    m_wake.notify_one();
}
void MediaWorker::setDecodeOptions(int threads, bool hardware, int minimumPixels, int prefetchDelayMs) {
    m_decoderThreads = std::clamp(threads, 1, 16);
    m_hardware = hardware;
    m_hardwareMinimumPixels = std::max(0, minimumPixels);
    m_prefetchDelayMs = std::clamp(prefetchDelayMs, 0, 500);
}
void MediaWorker::run(std::stop_token stop) {
    Decoder decoder;
    FrameCache cache(m_budget);
    // PCM decoding must not occupy the video seek/prefetch worker.
    std::jthread audioThread;
    quint64 generation = 0, sourceRevision = 0;
    int anchor = 0, direction = 1, first = 0, last = 0, cursor = 0;
    bool valid = false, prefilling = false, backfill = false;
    qint64 frameBytes = 0, appliedBudget = m_budget;
    QElapsedTimer coverageClock;
    coverageClock.start();
    QElapsedTimer quietClock;
    quietClock.start();
    auto publish = [&]() {
        emit cacheUpdated(generation, cache.bytes(), cache.ranges());
        coverageClock.restart();
    };
    auto planWindow = [&]() {
        const int capacity = int(std::clamp<qint64>(appliedBudget / std::max<qint64>(1, frameBytes), 1,
                                                    decoder.info().frameCount));
        const int behind = (capacity - 1) / 4;
        const int offset = direction > 0 ? behind : capacity - 1 - behind;
        first = std::clamp(anchor - offset, 0, decoder.info().frameCount - capacity);
        last = first + capacity - 1;
        cache.setWindow(first, last);
        cursor = direction > 0 ? anchor + 1 : first;
        backfill = false;
        prefilling = true;
    };
    auto nextPrefetch = [&]() {
        while (prefilling) {
            if (cursor < first || cursor > last) {
                if (backfill) {
                    prefilling = false;
                    break;
                }
                backfill = true;
                cursor = direction > 0 ? first : anchor + 1;
                continue;
            }
            const int candidate = cursor;
            // Always decode batches in ascending order, including reverse
            // prefetch. Descending FFmpeg seeks repeatedly decode the same GOP.
            ++cursor;
            if ((!backfill && direction < 0 && candidate >= anchor) ||
                (backfill && direction > 0 && candidate >= anchor)) {
                if (backfill) {
                    prefilling = false;
                    break;
                }
                backfill = true;
                cursor = anchor + 1;
                continue;
            }
            if (!cache.contains(candidate))
                return candidate;
        }
        return -1;
    };
    while (!stop.stop_requested()) {
        QString path;
        bool opening = false;
        int requested = -1;
        qint64 audioBudget = 0;
        quint64 epoch = 0, requestRevision = 0;
        {
            std::unique_lock lock(m_mutex);
            if (!m_openPending && m_requested < 0 && (!prefilling || !valid))
                m_wake.wait(lock, [&] {
                    return stop.stop_requested() || m_openPending || m_requested >= 0 ||
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
            direction = m_direction;
            epoch = m_epoch;
            requestRevision = m_requestRevision;
        }
        const bool budgetChanged = appliedBudget != m_budget;
        appliedBudget = m_budget;
        cache.setBudget(appliedBudget, anchor);
        auto sourceCancelled = [&]() { return stop.stop_requested() || sourceRevision != m_revision; };
        if (opening) {
            valid = false;
            prefilling = false;
            cache.clear();
            publish();
            QString error;
            if (!decoder.open(path, error, m_decoderThreads, m_hardware, m_hardwareMinimumPixels)) {
                if (!sourceCancelled())
                    emit failed(generation, error);
                continue;
            }
            if (sourceCancelled())
                continue;
            valid = true;
            anchor = 0;
            auto image = decoder.videoFrame(0, error, sourceCancelled);
            if (image.isNull() && m_hardware && !sourceCancelled() &&
                decoder.open(path, error, m_decoderThreads, false))
                image = decoder.videoFrame(0, error, sourceCancelled);
            if (sourceCancelled())
                continue;
            if (image.isNull()) {
                valid = false;
                emit failed(generation, error);
                continue;
            }
            cache.insert(0, image, 0);
            emit opened(generation, decoder.info());
            frameBytes = image.sizeInBytes();
            emit decoded(generation, 0, 0, image);
            if (decoder.info().variableRate)
                emit warning(generation, "Variable-rate footage detected. This preview uses the nominal "
                                         "frame rate; use a constant-rate playblast for precise bookmarks.");
            if (audioThread.joinable()) {
                audioThread.request_stop();
                audioThread.join();
            }
            audioThread = std::jthread(
                [this, path, audioBudget, generation, sourceRevision, stop](std::stop_token audioStop) {
                    auto cancelled = [&]() {
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
            planWindow();
            publish();
            requested = -1;
        }
        if (!valid || sourceCancelled())
            continue;
        if (requested >= 0) {
            quietClock.restart();
            anchor = std::clamp(requested, 0, decoder.info().frameCount - 1);
            planWindow();
            auto cancelled = [&]() {
                // Playback requests share an epoch so a slightly late decoded
                // frame can still be presented. A new user seek cancels it.
                return sourceCancelled() || epoch != m_epoch;
            };
            QString error;
            auto image = cache.get(anchor);
            if (image.isNull())
                image = decoder.videoFrame(
                    anchor, error, cancelled,
                    [&](int index, const VideoFrame& frame) {
                        if (!cancelled() && index >= first && index <= last && !cache.contains(index))
                            cache.insert(index, frame, anchor);
                    },
                    [&](int index) { return index >= first && index <= last && !cache.contains(index); });
            if (cancelled())
                continue;
            if (!image.isNull()) {
                cache.insert(anchor, image, anchor);
                emit decoded(generation, epoch, anchor, image);
            } else if (!error.isEmpty())
                emit warning(generation, error);
            if (coverageClock.elapsed() >= 100)
                publish();
        } else if (budgetChanged && !opening) {
            planWindow();
            publish();
        }
        // A requested frame always takes precedence over speculative decoding.
        if (sourceCancelled() || requestRevision != m_requestRevision)
            continue;
        if (quietClock.elapsed() < m_prefetchDelayMs) {
            std::unique_lock lock(m_mutex);
            m_wake.wait_for(lock, std::chrono::milliseconds(m_prefetchDelayMs - quietClock.elapsed()), [&] {
                return stop.stop_requested() || m_openPending || m_requested >= 0 ||
                       m_budget != appliedBudget;
            });
            continue;
        }
        const int candidate = nextPrefetch();
        if (candidate >= 0) {
            auto cancelled = [&]() { return sourceCancelled() || requestRevision != m_requestRevision; };
            QString error;
            auto image = decoder.videoFrame(
                candidate, error, cancelled,
                [&](int index, const VideoFrame& frame) {
                    if (!cancelled() && index >= first && index <= last && !cache.contains(index))
                        cache.insert(index, frame, anchor);
                },
                [&](int index) { return index >= first && index <= last && !cache.contains(index); });
            if (cancelled())
                continue;
            if (image.isNull())
                prefilling = false;
            else {
                cache.insert(candidate, image, anchor);
            }
        }
        if (!prefilling || coverageClock.elapsed() >= 100)
            publish();
    }
    // audioThread joins here before this object's destruction completes.
}
} // namespace tfv

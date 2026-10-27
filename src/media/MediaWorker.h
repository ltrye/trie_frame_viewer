#pragma once
#include "Decoder.h"
#include "FrameRepository.h"
#include <QObject>
#include <QVariantList>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace tfv {
class MediaWorker final : public QObject {
    Q_OBJECT
  public:
    enum class RequestKind { Seek, Playback, Preview };
    explicit MediaWorker(QObject* parent = nullptr);
    ~MediaWorker() override;
    void open(const QString& path, quint64 generation, qint64 cacheBudget, qint64 audioBudget);
    quint64 reserveRequestSerial();
    quint64 seek(int index, quint64 epoch = 0, int direction = 1, RequestKind kind = RequestKind::Seek,
                 quint64 requestSerial = 0);
    void setPlaybackRange(int first, int last, bool playing, bool loop);
    void setBudget(qint64 budget);
    void setDecodeOptions(int threads, bool hardware, int minimumPixels = 0, int prefetchDelayMs = 80,
                          int fineScrubFrames = 4, int retargetFrames = 8);
  signals:
    void opened(quint64 generation, tfv::MediaInfo info);
    void decoded(quint64 generation, quint64 epoch, int index, tfv::VideoFrame image, quint64 requestSerial);
    void decodeStarted(quint64 requestSerial, int index);
    void cacheUpdated(quint64 generation, qint64 bytes, QVariantList ranges);
    void audioReady(quint64 generation, QByteArray pcm);
    void failed(quint64 generation, QString error);
    void warning(quint64 generation, QString message);

  private:
    void run(std::stop_token stop);
    FrameRepository m_frames{512 * 1024 * 1024};
    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    QString m_path;
    quint64 m_generation = 0;
    int m_requested = -1;
    int m_direction = 1;
    RequestKind m_requestKind = RequestKind::Seek;
    bool m_openPending = false;
    bool m_cachedRequest = false, m_hintPending = false;
    bool m_playing = false, m_loop = false;
    int m_rangeFirst = 0, m_rangeLast = -1;
    int m_lastHint = 0;
    quint64 m_serial = 0, m_requestedSerial = 0;
    qint64 m_audioBudget = 64 * 1024 * 1024;
    std::atomic<qint64> m_budget{512 * 1024 * 1024};
    std::atomic<quint64> m_revision{0};
    std::atomic<quint64> m_epoch{0}, m_requestRevision{0};
    std::atomic<quint64> m_speculationRevision{0};
    std::atomic<int> m_decoderThreads{2};
    std::atomic<bool> m_hardware{false};
    std::atomic<int> m_hardwareMinimumPixels{0}, m_prefetchDelayMs{80};
    std::atomic<int> m_fineScrubFrames{4}, m_retargetFrames{8};
};
} // namespace tfv

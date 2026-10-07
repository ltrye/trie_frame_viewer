#pragma once
#include "Decoder.h"
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
    explicit MediaWorker(QObject* parent = nullptr);
    ~MediaWorker() override;
    void open(const QString& path, quint64 generation, qint64 cacheBudget, qint64 audioBudget);
    void seek(int index, quint64 epoch = 0, int direction = 1);
    void setBudget(qint64 budget);
    void setDecodeOptions(int threads, bool hardware, int minimumPixels = 0, int prefetchDelayMs = 80);
  signals:
    void opened(quint64 generation, tfv::MediaInfo info);
    void decoded(quint64 generation, quint64 epoch, int index, tfv::VideoFrame image);
    void cacheUpdated(quint64 generation, qint64 bytes, QVariantList ranges);
    void audioReady(quint64 generation, QByteArray pcm);
    void failed(quint64 generation, QString error);
    void warning(quint64 generation, QString message);

  private:
    void run(std::stop_token stop);
    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    QString m_path;
    quint64 m_generation = 0;
    int m_requested = -1;
    int m_direction = 1;
    bool m_openPending = false;
    qint64 m_audioBudget = 64 * 1024 * 1024;
    std::atomic<qint64> m_budget{512 * 1024 * 1024};
    std::atomic<quint64> m_revision{0};
    std::atomic<quint64> m_epoch{0}, m_requestRevision{0};
    std::atomic<int> m_decoderThreads{2};
    std::atomic<bool> m_hardware{false};
    std::atomic<int> m_hardwareMinimumPixels{0}, m_prefetchDelayMs{80};
};
} // namespace tfv

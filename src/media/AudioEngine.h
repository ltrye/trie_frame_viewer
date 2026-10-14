#pragma once
#include <QAudioSink>
#include <QByteArray>
#include <QObject>
#include <QTimer>
#include <memory>

namespace tfv {
class AudioEngine final : public QObject {
    Q_OBJECT
  public:
    explicit AudioEngine(QObject* parent = nullptr);
    void load(QByteArray pcm);
    void play(double seconds, double speed, double volume);
    void scrub(double seconds, int milliseconds, double volume);
    void stop();
    void setVolume(double volume);
    bool available() const {
        return !m_pcm.isEmpty();
    }
    bool clockAvailable() const;
    double seconds() const;

  private:
    void start(double seconds, double speed, double volume, int limitMilliseconds);
    void feed();
    QByteArray m_pcm;
    std::unique_ptr<QAudioSink> m_sink;
    QIODevice* m_device = nullptr;
    QTimer m_feed;
    QTimer m_scrubTimer;
    double m_scrubSeconds = 0, m_scrubVolume = 0;
    int m_scrubMilliseconds = 0;
    double m_cursor = 0, m_originSeconds = 0, m_speed = 1;
    double m_volume = 0, m_targetVolume = 0;
    qint64 m_emitted = 0, m_limitSamples = -1;
    bool m_scrubbing = false;
};
} // namespace tfv

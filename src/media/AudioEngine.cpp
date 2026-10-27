#include "AudioEngine.h"
#include "PlaybackTrace.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>

namespace tfv {
AudioEngine::AudioEngine(QObject* parent) : QObject(parent) {
    m_feed.setInterval(8);
    m_feed.setTimerType(Qt::PreciseTimer);
    connect(&m_feed, &QTimer::timeout, this, &AudioEngine::feed);
    m_scrubTimer.setSingleShot(true);
    m_scrubTimer.setInterval(20);
    connect(&m_scrubTimer, &QTimer::timeout, this,
            [this] { start(m_scrubSeconds, 1, m_scrubVolume, m_scrubMilliseconds); });
}
void AudioEngine::load(QByteArray pcm) {
    stop();
    m_pcm = std::move(pcm);
}
void AudioEngine::stop() {
    tracePlayback("audio.stop.begin");
    const auto traceEnd = qScopeGuard([] { tracePlayback("audio.stop.end"); });
    m_feed.stop();
    m_scrubTimer.stop();
    if (m_sink)
        m_sink->stop();
    m_device = nullptr;
    // Qt 6.8's Windows backend leaves queued WASAPI buffers in a stopped
    // client. Reusing a full buffer cannot transition out of IdleState.
    // A fresh public QAudioSink also discards stale audio after a seek.
    m_sink.reset();
}
void AudioEngine::setVolume(double v) {
    if (!std::isfinite(v))
        return;
    m_scrubVolume = m_targetVolume = std::clamp(v, 0., 1.);
}
bool AudioEngine::clockAvailable() const {
    return m_sink && m_device && !m_scrubbing && m_sink->state() == QAudio::ActiveState;
}
double AudioEngine::seconds() const {
    return m_originSeconds + (m_sink ? m_sink->processedUSecs() / 1e6 : 0) * m_speed;
}
void AudioEngine::play(double seconds, double speed, double volume) {
    m_scrubTimer.stop();
    start(seconds, speed, volume, -1);
}
void AudioEngine::scrub(double seconds, int milliseconds, double volume) {
    m_scrubSeconds = seconds;
    m_scrubMilliseconds = milliseconds;
    m_scrubVolume = volume;
    if (!m_scrubTimer.isActive())
        m_scrubTimer.start();
}
void AudioEngine::start(double seconds, double speed, double volume, int limitMilliseconds) {
    tracePlayback("audio.setup.begin");
    const auto traceEnd = qScopeGuard([] { tracePlayback("audio.setup.end"); });
    stop();
    if (!std::isfinite(volume))
        volume = 0;
    // Continuous playback keeps its device and clock alive through mute/unmute.
    // Silent scrub snippets do not need to open an output device.
    if (m_pcm.isEmpty() || (volume <= 0 && limitMilliseconds >= 0) || seconds < 0 ||
        seconds * 48000 >= m_pcm.size() / 4)
        return;
    if (!m_sink) {
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);
        const auto output = QMediaDevices::defaultAudioOutput();
        if (output.isNull() || !output.isFormatSupported(format))
            return;
        m_sink = std::make_unique<QAudioSink>(output, format);
        m_sink->setBufferSize(48000 * 4 / 20);
    }
    m_cursor = seconds * 48000;
    m_originSeconds = seconds;
    m_speed = speed;
    m_emitted = 0;
    m_scrubbing = limitMilliseconds >= 0;
    m_limitSamples = m_scrubbing ? limitMilliseconds * 48 : -1;
    setVolume(volume);
    m_volume = 0;
    // Apply user gain in PCM, avoiding synchronous Windows volume updates for
    // every pointer movement. A short ramp suppresses discontinuities/clicks.
    m_sink->setVolume(1);
    m_device = m_sink->start();
    if (!m_device)
        return;
    feed();
    m_feed.start();
}
void AudioEngine::feed() {
    if (!m_device)
        return;
    const qint64 free = m_sink->bytesFree() / 4;
    const qint64 sourceSamples = m_pcm.size() / 4;
    qint64 samples = std::min<qint64>(free, 2048);
    if (m_limitSamples >= 0)
        samples = std::min(samples, m_limitSamples - m_emitted);
    samples =
        std::min<qint64>(samples, qint64(std::max(0., std::floor((sourceSamples - m_cursor - 1) / m_speed))));
    if (samples <= 0) {
        if (m_cursor >= sourceSamples - 2 || (m_limitSamples >= 0 && m_emitted >= m_limitSamples))
            m_feed.stop();
        return;
    }
    QByteArray bytes(samples * 4, Qt::Uninitialized);
    auto* output = reinterpret_cast<qint16*>(bytes.data());
    const auto* input = reinterpret_cast<const qint16*>(m_pcm.constData());
    constexpr double volumeStep = 1. / 240; // Full-scale ramp over 5 ms at 48 kHz.
    double volume = m_volume;
    for (qint64 n = 0; n < samples; ++n) {
        const double position = m_cursor + n * m_speed;
        const qint64 i = qint64(position);
        const double fraction = position - i;
        volume += std::clamp(m_targetVolume - volume, -volumeStep, volumeStep);
        double gain = volume;
        if (m_scrubbing)
            gain *= std::min({1., double(m_emitted + n) / 240, double(m_limitSamples - m_emitted - n) / 240});
        for (int ch = 0; ch < 2; ++ch)
            output[n * 2 + ch] =
                qint16((input[i * 2 + ch] * (1 - fraction) + input[(i + 1) * 2 + ch] * fraction) * gain);
    }
    const qint64 written = m_device->write(bytes);
    if (written <= 0)
        return;
    const qint64 accepted = written / 4;
    // Advance gain only for accepted samples if the output buffer filled.
    m_volume += std::clamp(m_targetVolume - m_volume, -accepted * volumeStep, accepted * volumeStep);
    m_cursor += accepted * m_speed;
    m_emitted += accepted;
}
} // namespace tfv

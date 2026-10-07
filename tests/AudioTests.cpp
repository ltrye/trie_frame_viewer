#include "media/AudioEngine.h"
#include <QGuiApplication>
#include <QMediaDevices>
#include <QtTest>
#include <cmath>

class AudioTests : public QObject {
    Q_OBJECT
  private:
    QByteArray pcm() const {
        QByteArray data(3 * 48000 * 4, Qt::Uninitialized);
        auto* samples = reinterpret_cast<qint16*>(data.data());
        for (int i = 0; i < 3 * 48000; ++i)
            samples[2 * i] = samples[2 * i + 1] =
                qint16(4000 * std::sin(i * 440. * 6.283185307179586 / 48000));
        return data;
    }
  private slots:
    void initTestCase() {
        if (QMediaDevices::defaultAudioOutput().isNull())
            QSKIP("No native audio output device");
    }
    void repeatedStopAndPlayKeepsProducingAudio() {
        tfv::AudioEngine audio;
        audio.load(pcm());
        for (int attempt = 0; attempt < 20; ++attempt) {
            const double origin = (attempt % 4) * 0.25;
            audio.play(origin, 1, 0.02);
            QTRY_VERIFY_WITH_TIMEOUT(audio.clockAvailable(), 800);
            QTRY_VERIFY_WITH_TIMEOUT(audio.seconds() > origin + 0.08, 800);
            QTest::qWait(7 + (attempt * 13) % 37);
            audio.stop();
        }
    }
    void replayAfterScrubbingAndEndOfAudio() {
        tfv::AudioEngine audio;
        audio.load(pcm());
        audio.play(2.9, 1, 0.02);
        QTest::qWait(220);
        for (int i = 0; i < 12; ++i) {
            audio.scrub((i * 7 % 20) / 10., 70, 0.02);
            QTest::qWait(9);
        }
        audio.play(0, 1, 0.02);
        QTRY_VERIFY_WITH_TIMEOUT(audio.clockAvailable(), 800);
        QTRY_VERIFY_WITH_TIMEOUT(audio.seconds() > 0.12, 800);
    }
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    AudioTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "AudioTests.moc"

#include "application/Preferences.h"
#include "media/ClipExporter.h"
#include "media/Decoder.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ExportTests : public QObject {
    Q_OBJECT
    tfv::ClipExportRequest request(const QString& source, int start, int end) {
        tfv::Decoder decoder;
        QString error;
        if (!decoder.open(source, error))
            return {};
        const auto info = decoder.info();
        return {source,          tfv::Decoder::imageSequenceFiles(source),
                start,           end,
                info.frameCount, info.width,
                info.height,     info.fps};
    }
    QByteArray contents(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    double imageDifference(const QImage& left, const QImage& right) {
        const auto a = left.convertToFormat(QImage::Format_RGB32);
        const auto b = right.convertToFormat(QImage::Format_RGB32);
        if (a.size() != b.size() || a.isNull())
            return 255;
        double total = 0;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x) {
                const auto first = a.pixel(x, y), second = b.pixel(x, y);
                total += qAbs(qRed(first) - qRed(second)) + qAbs(qGreen(first) - qGreen(second)) +
                         qAbs(qBlue(first) - qBlue(second));
            }
        return total / (a.width() * double(a.height()) * 3);
    }
  private slots:
    void initTestCase() {
        QVERIFY2(!qEnvironmentVariable("TFV_TEST_MEDIA").isEmpty(),
                 "Set TFV_TEST_MEDIA to the integration fixture");
    }
    void selectedRangeIncludesBothEndpointsAndAudio_data() {
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("end");
        QTest::newRow("non-keyframe-range") << 7 << 23;
        QTest::newRow("first-single-frame") << 0 << 0;
        const auto selection = request(qEnvironmentVariable("TFV_TEST_MEDIA"), 0, 0);
        QTest::newRow("last-single-frame") << selection.frameCount - 1 << selection.frameCount - 1;
    }
    void selectedRangeIncludesBothEndpointsAndAudio() {
        QFETCH(int, start);
        QFETCH(int, end);
        const int count = end - start + 1;
        const auto source = qEnvironmentVariable("TFV_TEST_MEDIA");
        const auto sourceHash = QCryptographicHash::hash(contents(source), QCryptographicHash::Sha256);
        tfv::Preferences preferences;
        tfv::ClipExporter exporter(preferences.exportOptions());
        QTemporaryDir folder;
        const auto output = folder.filePath(QString::fromUtf8("selected clip \xE6\xB5\x8B\xE8\xAF\x95.mp4"));
        QSignalSpy finished(&exporter, &tfv::ClipExporter::finished);
        {
            QFile old(output);
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("old clip");
        }
        QVERIFY(exporter.start(request(source, start, end), QUrl::fromLocalFile(output)));
        QVERIFY(exporter.busy());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY2(finished.first()[0].toBool(), qPrintable(exporter.error()));
        QVERIFY(!exporter.busy());
        QCOMPARE(exporter.progress(), 1.);
        tfv::Decoder original, clip;
        QString error;
        QVERIFY(original.open(source, error));
        QVERIFY2(clip.open(output, error), qPrintable(error));
        QCOMPARE(clip.info().codec, "h264");
        QCOMPARE(clip.info().frameCount, count);
        QVERIFY(qAbs(clip.info().fps - original.info().fps) < 0.001);
        QVERIFY(imageDifference(original.frame(start, error), clip.frame(0, error)) < 8);
        QVERIFY(imageDifference(original.frame(end, error), clip.frame(count - 1, error)) < 8);
        QString warning;
        const auto pcm = tfv::Decoder::decodeAudio(output, 4 * 1048576, [] { return false; }, warning);
        QVERIFY2(!pcm.isEmpty(), qPrintable(warning));
        QVERIFY(pcm.size() / (48000. * 4) >= count / original.info().fps - 0.05);
        QCOMPARE(QCryptographicHash::hash(contents(source), QCryptographicHash::Sha256), sourceHash);
        QVERIFY(QDir(folder.path()).entryList({".tfv-export-*"}, QDir::Files | QDir::Hidden).isEmpty());
    }
    void singleFrameAndGappedImageSequence() {
        QTemporaryDir folder;
        const QString prefix = folder.filePath("artist's shot_");
        for (const auto& entry : QList<QPair<int, QColor>>{{1, Qt::red}, {3, Qt::green}, {8, Qt::blue}}) {
            QImage image(161, 91, QImage::Format_RGB32);
            image.fill(entry.second);
            QVERIFY(image.save(prefix + QString::number(entry.first) + ".png"));
        }
        tfv::Preferences preferences;
        tfv::ClipExporter exporter(preferences.exportOptions());
        QSignalSpy finished(&exporter, &tfv::ClipExporter::finished);
        const QString output = folder.filePath("sequence.mp4");
        QVERIFY(exporter.start(request(prefix + "3.png", 1, 1), QUrl::fromLocalFile(output)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY2(finished.first()[0].toBool(), qPrintable(exporter.error()));
        tfv::Decoder clip;
        QString error;
        QVERIFY2(clip.open(output, error), qPrintable(error));
        QCOMPARE(clip.info().frameCount, 1);
        QCOMPARE(clip.info().width, 162);
        QCOMPARE(clip.info().height, 92);
        const QColor color = clip.frame(0, error).pixelColor(40, 40);
        QVERIFY(color.green() > 200 && color.red() < 20 && color.blue() < 20);
        const QString full = folder.filePath("full-sequence.mp4");
        QVERIFY(exporter.start(request(prefix + "3.png", 0, 2), QUrl::fromLocalFile(full)));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 15000);
        QVERIFY2(finished.last()[0].toBool(), qPrintable(exporter.error()));
        tfv::Decoder sequence;
        QVERIFY(sequence.open(full, error));
        QCOMPARE(sequence.info().frameCount, 3);
        QVERIFY(sequence.frame(0, error).pixelColor(40, 40).red() > 200);
        QVERIFY(sequence.frame(1, error).pixelColor(40, 40).green() > 200);
        QVERIFY(sequence.frame(2, error).pixelColor(40, 40).blue() > 200);
    }
    void cancellationAndFailurePreserveExistingOutput() {
        QTemporaryDir folder;
        const auto output = folder.filePath("keep.mp4");
        {
            QFile existing(output);
            QVERIFY(existing.open(QIODevice::WriteOnly));
            existing.write("keep this file");
        }
        tfv::Preferences preferences;
        tfv::ClipExporter exporter(preferences.exportOptions());
        QSignalSpy finished(&exporter, &tfv::ClipExporter::finished);
        const auto selection = request(qEnvironmentVariable("TFV_TEST_MEDIA"), 0, 30);
        QVERIFY(exporter.start(selection, QUrl::fromLocalFile(output)));
        QVERIFY(!exporter.start(selection, QUrl::fromLocalFile(folder.filePath("second.mp4"))));
        exporter.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first()[1].toBool());
        QCOMPARE(contents(output), QByteArray("keep this file"));
        auto invalidOptions = preferences.exportOptions();
        invalidOptions["videoEncoder"] = "missing-test-encoder";
        tfv::ClipExporter failing(invalidOptions);
        QSignalSpy failure(&failing, &tfv::ClipExporter::finished);
        QVERIFY(failing.start(selection, QUrl::fromLocalFile(output)));
        QTRY_COMPARE_WITH_TIMEOUT(failure.count(), 1, 5000);
        QVERIFY(!failure.first()[0].toBool());
        QVERIFY(!failing.error().isEmpty());
        QCOMPARE(contents(output), QByteArray("keep this file"));
        QVERIFY(QDir(folder.path()).entryList({".tfv-export-*"}, QDir::Files | QDir::Hidden).isEmpty());
        QVERIFY(!exporter.start(selection, QUrl::fromLocalFile(selection.source)));
        QVERIFY(exporter.error().contains("original"));
        auto invalid = selection;
        invalid.end = invalid.frameCount;
        QVERIFY(!exporter.start(invalid, QUrl::fromLocalFile(output)));
    }
};
QTEST_GUILESS_MAIN(ExportTests)
#include "ExportTests.moc"

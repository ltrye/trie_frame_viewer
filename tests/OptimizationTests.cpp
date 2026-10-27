#include "application/PlayerController.h"
#include "media/FrameRepository.h"
#include "media/MediaWorker.h"
#include "media/PlaybackTrace.h"
#include "media/PrefetchPlanner.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QtTest>
#include <set>

class OptimizationTests : public QObject {
    Q_OBJECT
  private slots:
    void repositoryInvalidatesSourcesAndKeepsSharedPixelsAlive() {
        tfv::FrameRepository repository(800);
        repository.invalidate(1);
        repository.reset(1, 800);
        QImage image(10, 10, QImage::Format_RGBA8888);
        image.fill(Qt::red);
        tfv::VideoFrame frame(image);
        QVERIFY(repository.insert(1, 0, frame, 0));
        const auto retained = repository.get(1, 0);
        repository.invalidate(2);
        QVERIFY(repository.get(1, 0).isNull());
        QVERIFY(repository.get(2, 0).isNull());
        repository.reset(1, 800); // A late old-source reset cannot revalidate it.
        QVERIFY(repository.get(1, 0).isNull());
        repository.reset(2, 800);
        QVERIFY(repository.insert(2, 1, frame, 1));
        repository.setBudget(2, 1, 1);
        QCOMPARE(repository.snapshot(2, 0).bytes, 0);
        QCOMPARE(retained.toImage().pixelColor(0, 0), QColor(Qt::red));
    }
    void plannerCoversBudgetedLoopBoundaryAndReversals() {
        tfv::PrefetchPlanner planner;
        for (const int direction : {1, -1}) {
            const int anchor = direction > 0 ? 98 : 1;
            planner.plan(20, anchor, direction, 0, 99, true);
            std::set<int> frames{anchor};
            int candidate;
            while ((candidate = planner.next([](int) { return false; })) >= 0)
                QVERIFY(frames.insert(candidate).second);
            QCOMPARE(frames.size(), size_t(20));
            QVERIFY(frames.contains(0) && frames.contains(99));
            for (const int frame : frames)
                QVERIFY(planner.contains(frame));
        }
        planner.plan(100, 25, 1, 24, 28, true);
        std::set<int> small{25};
        int frame;
        while ((frame = planner.next([](int) { return false; })) >= 0)
            small.insert(frame);
        QCOMPARE(small, (std::set<int>{24, 25, 26, 27, 28}));
    }
    void traceIsBoundedAndPreservesRequestIdentity() {
        QTemporaryDir directory;
        const auto path = directory.filePath("trace.json");
        tfv::PlaybackTrace trace(path, 2);
        trace.record("request", 7, 12);
        trace.record("cache.hit", 7, 12);
        trace.record("overflow", 8, 12);
        QVERIFY(trace.flush());
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(object["droppedEvents"].toInt(), 1);
        const auto events = object["events"].toArray();
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].toObject()["request"].toInt(), 7);
        QCOMPARE(events[1].toObject()["request"].toInt(), 7);
    }
    void cachedTargetArrivesWhileForegroundDecodeIsBlocked() {
        const auto path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Set TFV_TEST_MEDIA to the video fixture");
        tfv::MediaWorker worker;
        QSemaphore entered, resume;
        const auto release = qScopeGuard([&] { resume.release(); });
        QObject::connect(
            &worker, &tfv::MediaWorker::decodeStarted, &worker,
            [&](quint64, int index) {
                if (index == 80) {
                    entered.release();
                    resume.acquire();
                }
            },
            Qt::DirectConnection);
        QSignalSpy coverage(&worker, &tfv::MediaWorker::cacheUpdated);
        QSignalSpy decoded(&worker, &tfv::MediaWorker::decoded);
        worker.open(path, 1, 640 * 360 * 3 / 2 * 4, 0);
        QTRY_VERIFY_WITH_TIMEOUT(!coverage.isEmpty() && coverage.last()[1].toLongLong() > 0, 3000);
        worker.seek(80, 1);
        QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, 3000);
        const auto serial = worker.seek(0, 1);
        QVERIFY(!decoded.isEmpty());
        QCOMPARE(decoded.last()[2].toInt(), 0);
        QCOMPARE(decoded.last()[4].toULongLong(), serial);
        QCOMPARE(qvariant_cast<tfv::VideoFrame>(decoded.last()[3]).requestSerial(), serial);
        QVERIFY(resume.available() == 0); // The miss is still blocked, not merely fast.
        resume.release();
    }
    void playbackPrefetchDoesNotRequireAnIdleGap() {
        const auto path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Set TFV_TEST_MEDIA to the video fixture");
        tfv::MediaWorker worker;
        // A deliberately long quiet delay must not suppress playback warming.
        worker.setDecodeOptions(2, false, 0, 500);
        worker.open(path, 1, 640 * 360 * 3 / 2 * 12, 0);
        QSignalSpy opened(&worker, &tfv::MediaWorker::opened);
        QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 3000);
        worker.setPlaybackRange(0, 95, true, true);
        QSignalSpy coverage(&worker, &tfv::MediaWorker::cacheUpdated);
        QTimer input;
        connect(&input, &QTimer::timeout, &worker,
                [&] { worker.seek(40, 2, 1, tfv::MediaWorker::RequestKind::Playback); });
        input.start(10);
        const auto warmed = [&] {
            for (const auto& report : coverage)
                for (const auto& range : report[2].toList()) {
                    const auto values = range.toMap();
                    if (values["start"].toInt() <= 45 && values["end"].toInt() >= 45)
                        return true;
                }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(warmed(), 1500);
    }
    void olderPreviewCannotOverwriteANewerCachedFrame() {
        const auto path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Set TFV_TEST_MEDIA to the video fixture");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        preferences.setVolume(0);
        tfv::PlayerController player(preferences);
        auto* worker = player.findChild<tfv::MediaWorker*>();
        QVERIFY(worker);
        QSignalSpy delivered(worker, &tfv::MediaWorker::decoded);
        QSignalSpy frames(&player, &tfv::PlayerController::frameReady);
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 3000);
        player.seek(10, true);
        QTRY_VERIFY_WITH_TIMEOUT(player.presentedFrame() == 10, 1000);
        const auto oldFrame = qvariant_cast<tfv::VideoFrame>(frames.last().first());
        player.seek(20, true);
        QTRY_VERIFY_WITH_TIMEOUT(player.presentedFrame() == 20, 1000);
        const auto newer = delivered.last();
        QVERIFY(newer[4].toULongLong() > oldFrame.requestSerial());
        // Deliver a valid old-session preview after the cache hit, as a queued
        // result could arrive from an in-flight decode. Serial ordering wins.
        worker->decoded(newer[0].toULongLong(), newer[1].toULongLong(), 10, oldFrame,
                        oldFrame.requestSerial());
        QCOMPARE(player.presentedFrame(), 20);
        QCOMPARE(player.currentFrame(), 20);
        player.finishScrub();
        QCOMPARE(player.presentedFrame(), 20);
    }
};
QTEST_GUILESS_MAIN(OptimizationTests)
#include "OptimizationTests.moc"

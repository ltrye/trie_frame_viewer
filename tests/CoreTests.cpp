#include "application/PlayerController.h"
#include "domain/BookmarkModel.h"
#include "domain/ProjectDocument.h"
#include "media/Decoder.h"
#include "media/FrameCache.h"
#include "media/MediaWorker.h"
#include "platform/ResourceMonitor.h"
#include "platform/WindowsIntegration.h"
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void cinemaShortcutsMigrateTheOldFullscreenDefault() {
        const QList<QPair<QString, QString>> shortcuts{{"F11", ""}, {"F12", ""}, {"F11", "Ctrl+F11"}};
        for (const auto& [fullscreen, cinema] : shortcuts) {
            QTemporaryDir directory;
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
            QSettings saved;
            saved.setValue("shortcuts/fullscreen", fullscreen);
            if (!cinema.isEmpty())
                saved.setValue("shortcuts/cinemaMode", cinema);
            tfv::Preferences preferences;
            const bool migrate = fullscreen == "F11" && cinema.isEmpty();
            QCOMPARE(preferences.shortcut("cinemaMode"), cinema.isEmpty() ? QString("F11") : cinema);
            QCOMPARE(preferences.shortcut("fullscreen"), migrate ? QString{} : fullscreen);
            QCOMPARE(preferences.shortcut("exitCinemaMode"), QString("Esc"));
            QCOMPARE(preferences.cinemaControlsTimeoutMs(), 2000);
            tfv::Preferences reopened;
            QCOMPARE(reopened.shortcut("fullscreen"), preferences.shortcut("fullscreen"));
        }
    }
    void debugPreferencePersistsAndMonitoringStopsWhenDisabled() {
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        QVERIFY(!preferences.debugMode());
        preferences.setDebugMode(true);
        tfv::Preferences restored;
        QVERIFY(restored.debugMode());

        tfv::ResourceMonitor monitor(100);
        QSignalSpy samples(&monitor, &tfv::ResourceMonitor::metricsChanged);
        QTest::qWait(150);
        QVERIFY(samples.isEmpty());
        monitor.setEnabled(true);
        QVERIFY(monitor.ramAvailable());
        QVERIFY(monitor.ramMiB() > 0);
        QVERIFY(monitor.privateRamMiB() > 0);
        QVERIFY(monitor.vramMiB() >= 0 && monitor.sharedGpuMiB() >= 0);
        samples.clear();
        const double before = monitor.privateRamMiB();
        const QByteArray allocation(16 * 1048576, 'x');
        QTRY_VERIFY_WITH_TIMEOUT(monitor.privateRamMiB() >= before + 8, 1000);
        QVERIFY(!samples.isEmpty());
        QCOMPARE(allocation.back(), 'x');
        monitor.setEnabled(false);
        samples.clear();
        QTest::qWait(250);
        QVERIFY(samples.isEmpty());
        preferences.setDebugMode(false);
        tfv::Preferences disabled;
        QVERIFY(!disabled.debugMode());
    }
    void savePromptRequiresBookmarksAndIgnoresSpeedChanges() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        tfv::PlayerController player(preferences);
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 3000);
        player.setSpeed(2);
        player.setZoom(1.5);
        player.setRange(4, 36);
        QVERIFY(!player.dirty());
        player.addBookmark("#e6bf79");
        QVERIFY(player.dirty());
        const QUrl project = QUrl::fromLocalFile(settings.filePath("bookmarks.tfv"));
        QVERIFY(player.saveProject(project));
        QVERIFY(!player.dirty());
        player.setSpeed(0.5);
        QVERIFY(!player.dirty());
        auto* bookmarks = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
        const QString id = bookmarks->at(0)->id;
        bookmarks->rename(id, "Contact pose");
        QVERIFY(player.dirty());
        player.setSpeed(4);
        QVERIFY(player.dirty());
        QVERIFY(player.saveProject(project));
        player.pan(5, 0);
        QVERIFY(player.dirty());
        bookmarks->remove(id);
        QVERIFY(!player.dirty());
        player.addBookmark("#a8d5c2");
        QVERIFY(player.dirty());
    }
    void contextMenuUsesQuotedPathsAndPreservesOtherApplications() {
        const QString root = "HKEY_CURRENT_USER\\Software\\Trie\\FrameViewerTests\\" +
                             QUuid::createUuid().toString(QUuid::WithoutBraces);
        tfv::WindowsIntegration first("C:/Test apps/Trie FrameViewer/TrieFrameViewer.exe", root);
        QSettings registry(root, QSettings::NativeFormat);
        registry.setValue(".mp4/.", "Other.Player");
        registry.setValue(".mp4/OpenWithProgids/Other.Player", "");
        QVERIFY(first.registerContextMenu());
        QVERIFY(first.registered());
        QCOMPARE(
            registry.value("SystemFileAssociations/.mp4/shell/TrieFrameViewer.Open/command/.").toString(),
            QString("\"C:\\Test apps\\Trie FrameViewer\\TrieFrameViewer.exe\" \"%1\""));
        QCOMPARE(registry.value(".mp4/.").toString(), QString("Other.Player"));
        QVERIFY(first.unregisterContextMenu());
        QVERIFY(!first.registered());
        QVERIFY(!registry.contains("SystemFileAssociations/.mp4/shell/TrieFrameViewer.Open/command/."));
        QVERIFY(registry.contains(".mp4/OpenWithProgids/Other.Player"));
        QVERIFY(first.registerContextMenu());
        tfv::WindowsIntegration moved("C:/Another copy/TrieFrameViewer.exe", root);
        QVERIFY(moved.registerContextMenu());
        first.refresh();
        QVERIFY(!first.registered());
        QVERIFY(first.unregisterContextMenu());
        moved.refresh();
        QVERIFY(moved.registered());
        QVERIFY(moved.unregisterContextMenu());
        registry.remove(""); // Only the isolated test registry subtree.
        registry.sync();
    }
    void compactFrameStoresOddDimensionsAndColorMetadata() {
        const int width = 129, height = 65;
        QByteArray y(width * height, char(16)), u(65 * 33, char(128)), v(65 * 33, char(128));
        const uint8_t* data[] = {reinterpret_cast<const uint8_t*>(y.constData()),
                                 reinterpret_cast<const uint8_t*>(u.constData()),
                                 reinterpret_cast<const uint8_t*>(v.constData())};
        const int strides[] = {width, 65, 65};
        const auto frame =
            tfv::VideoFrame::copyYuv420(width, height, data, strides, tfv::VideoFrame::Matrix::Bt709, false);
        QCOMPARE(frame.sizeInBytes(), qint64(width * height + 2 * 65 * 33));
        QCOMPARE(frame.toImage().pixelColor(64, 32), QColor(Qt::black));
        const auto shared = frame;
        QCOMPARE(shared.identity(), frame.identity());
        y.fill(char(235));
        QCOMPARE(frame.toImage().pixelColor(64, 32), QColor(Qt::black));
    }
    void cacheKeepsVisitedRegionsUntilBudgetPressure() {
        tfv::FrameCache cache(1200);
        QImage frame(10, 10, QImage::Format_RGBA8888);
        cache.insert(1, frame, 1);
        cache.insert(2, frame, 2);
        cache.setWindow(50, 55);
        cache.insert(50, frame, 50);
        QVERIFY(cache.contains(1) && cache.contains(2));
        QVERIFY(!cache.get(1).isNull());
        cache.insert(51, frame, 51);
        QVERIFY(cache.contains(1));
        QVERIFY(!cache.contains(2));
        QVERIFY(cache.contains(51));
    }
    void coldSeekRetainsDecodedIntermediateFrames() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the fixture with scripts/verify.ps1");
        tfv::Decoder decoder;
        QString error;
        QVERIFY(decoder.open(path, error));
        QList<int> retained;
        const auto frame = decoder.videoFrame(
            40, error, {}, [&](int index, const auto&) { retained.append(index); },
            [](int index) { return index >= 30; });
        QVERIFY2(!frame.isNull(), qPrintable(error));
        QVERIFY(retained.contains(30) && retained.contains(39));
        QVERIFY(!retained.contains(29));
        QCOMPARE(frame.format(), tfv::VideoFrame::Format::Yuv420);
        QCOMPARE(frame.sizeInBytes(), qint64(640 * 360 * 3 / 2));
    }
    void hardwareDecodeMatchesSoftwareWhenAvailable() {
        const QString path = qEnvironmentVariable("TFV_STRESS_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the fixture with scripts/verify.ps1");
        tfv::Decoder hardware, software;
        QString error;
        QVERIFY(hardware.open(path, error, 2, true));
        QVERIFY(software.open(path, error));
        for (int index : {0, 120, 15, 359, 240}) {
            const auto gpu = hardware.videoFrame(index, error);
            const auto cpu = software.videoFrame(index, error);
            QVERIFY2(!gpu.isNull() && !cpu.isNull(), qPrintable(error));
            QCOMPARE(gpu.toImage(), cpu.toImage());
        }
        if (hardware.info().decodeBackend != "D3D11VA")
            QSKIP("This adapter did not offer hardware video decoding; software fallback passed");
    }
    void cacheIsBoundedAndProtectsPlayhead() {
        tfv::FrameCache cache(800);
        QImage frame(10, 10, QImage::Format_RGBA8888);
        frame.fill(Qt::red);
        cache.insert(0, frame, 0);
        cache.insert(1, frame, 1);
        cache.insert(2, frame, 2);
        QVERIFY(cache.bytes() <= 800);
        QVERIFY(!cache.contains(0));
        QVERIFY(cache.contains(2));
        cache.setBudget(400, 2);
        QVERIFY(cache.contains(2));
        QCOMPARE(cache.bytes(), 400);
        QVERIFY(!cache.insert(3, QImage(20, 20, QImage::Format_RGBA8888), 3));
    }
    void cacheRangesMergeOnlyAdjacentFrames() {
        tfv::FrameCache cache(10000);
        QImage frame(2, 2, QImage::Format_RGBA8888);
        for (int f : {1, 2, 3, 7, 9, 10})
            cache.insert(f, frame, f);
        const auto ranges = cache.ranges();
        QCOMPARE(ranges.size(), 3);
        QCOMPARE(ranges[0].toMap()["start"].toInt(), 1);
        QCOMPARE(ranges[0].toMap()["end"].toInt(), 3);
    }
    void bookmarksRoundTripAndRejectInvalidBounds() {
        tfv::BookmarkModel original;
        original.add(24, "#a8d5c2");
        original.add(36, "#e6bf79");
        const auto id = original.at(1)->id;
        original.rename(id, "Contact pose");
        QVERIFY(!original.setFrame(id, -1, 100));
        QVERIFY(!original.setFrame(id, 100, 100));
        QVERIFY(original.setFrame(id, 4, 100));
        tfv::BookmarkModel restored;
        QString error;
        QVERIFY(restored.load(original.toJson(), 100, error));
        QCOMPARE(restored.rowCount(), 2);
        QCOMPARE(restored.find(id)->name, QString("Contact pose"));
        QCOMPARE(restored.adjacentFrame(24, true), 4);
        QVERIFY(!restored.load(original.toJson(), 20, error));
        QCOMPARE(restored.rowCount(), 2);
        restored.remove(restored.at(0)->id);
        restored.rename(id, "Renamed after another bookmark was deleted");
        QCOMPARE(restored.at(0)->id, id);
        QCOMPARE(restored.at(0)->name, QString("Renamed after another bookmark was deleted"));
        QSignalSpy edited(&restored, &tfv::BookmarkModel::edited);
        restored.rename("missing-id", "No change");
        restored.rename(id, "   ");
        QVERIFY(!restored.setFrame("missing-id", 1, 100));
        QVERIFY(edited.isEmpty());
    }
    void legacyRangeBookmarksBecomeNamedFrameBookmarks() {
        const QJsonArray legacy{QJsonObject{{"id", "range-id"},
                                            {"name", "Walk cycle"},
                                            {"color", "#a8d5c2"},
                                            {"type", "range"},
                                            {"start", 4},
                                            {"endInclusive", 36}}};
        tfv::BookmarkModel model;
        QString error;
        QVERIFY(model.load(legacy, 100, error));
        QCOMPARE(model.at(0)->frame, 4);
        QCOMPARE(model.at(0)->name, QString("Walk cycle"));
        const auto saved = model.toJson().at(0).toObject();
        QCOMPARE(saved["type"].toString(), QString("frame"));
        QCOMPARE(saved["start"].toInt(), 4);
        QCOMPARE(saved["endInclusive"].toInt(), 4);
        QVERIFY(!model.load(legacy, 30, error));
        QCOMPARE(model.at(0)->frame, 4);
    }
    void projectUsesRelativeMediaAndAtomicRoundTrip() {
        QTemporaryDir directory;
        QFile media(directory.path() + "/footage.mp4");
        QVERIFY(media.open(QIODevice::WriteOnly));
        media.write("test source");
        media.close();
        const auto project = directory.path() + "/review.tfv";
        QString error;
        QVERIFY(tfv::ProjectDocument::save(project, media.fileName(),
                                           {{"frame", 24}, {"bookmarks", QJsonArray{}}}, error));
        QJsonObject state;
        QString source;
        QVERIFY(tfv::ProjectDocument::load(project, state, source, error));
        QCOMPARE(source, media.fileName());
        QCOMPARE(state["frame"].toInt(), 24);
        QCOMPARE(state["media"].toObject()["path"].toString(), QString("footage.mp4"));
        QFile future(directory.path() + "/future.tfv");
        QVERIFY(future.open(QIODevice::WriteOnly));
        future.write("{\"schemaVersion\":99}");
        future.close();
        QVERIFY(!tfv::ProjectDocument::load(future.fileName(), state, source, error));
    }
    void imageSequenceReadsExistingNumberedFrames() {
        QTemporaryDir directory;
        QImage a(16, 16, QImage::Format_RGBA8888);
        a.fill(Qt::red);
        QVERIFY(a.save(directory.path() + "/pose.0001.png"));
        a.fill(Qt::green);
        QVERIFY(a.save(directory.path() + "/pose.0003.png"));
        tfv::Decoder decoder;
        QString error;
        QVERIFY(decoder.open(directory.path() + "/pose.0001.png", error));
        QCOMPARE(decoder.info().frameCount, 2);
        QCOMPARE(decoder.frame(1, error).pixelColor(0, 0), QColor(Qt::green));
        QCOMPARE(decoder.frame(0, error).pixelColor(0, 0), QColor(Qt::red));
    }
    void corruptMediaFailsWithoutCrashing() {
        tfv::Decoder decoder;
        QString error;
        QVERIFY(!decoder.open("missing-footage.mp4", error));
        QVERIFY(!error.isEmpty());
    }
    void compressedVideoRandomSeekMatchesSequentialDecode() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        tfv::Decoder decoder;
        QString error;
        QVERIFY2(decoder.open(path, error), qPrintable(error));
        QCOMPARE(decoder.info().frameCount, 96);
        QCOMPARE(decoder.info().fps, 24.);
        QList<QImage> frames;
        for (int frame = 0; frame < 96; ++frame) {
            const auto image = decoder.frame(frame, error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            frames.append(image);
        }
        for (int frame : {95, 0, 48, 23, 71, 4, 0}) {
            const auto image = decoder.frame(frame, error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QCOMPARE(image, frames[frame]);
        }
    }
    void pcmCacheIsBoundedAndCancellationWorks() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        QString warning;
        const auto pcm = tfv::Decoder::decodeAudio(path, 32000, [] { return false; }, warning);
        QVERIFY(!pcm.isEmpty());
        QVERIFY(pcm.size() <= 32000);
        QVERIFY(!warning.isEmpty());
        const auto cancelled = tfv::Decoder::decodeAudio(path, 32000, [] { return true; }, warning);
        QVERIFY(cancelled.isEmpty());
    }
    void cancelledDecodePreservesTheNextExactFrame() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        tfv::Decoder decoder, expected;
        QString error;
        QVERIFY(decoder.open(path, error));
        QVERIFY(expected.open(path, error));
        QVERIFY(!decoder.frame(0, error).isNull());
        int checks = 0;
        QVERIFY(decoder.frame(95, error, [&] { return ++checks > 20; }).isNull());
        QVERIFY(error.isEmpty());
        QCOMPARE(decoder.frame(1, error), expected.frame(1, error));
        QCOMPARE(decoder.frame(2, error), expected.frame(2, error));
    }
    void fullCacheWarmsTheNewSeekNeighborhood() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        tfv::MediaWorker worker;
        QSignalSpy coverage(&worker, &tfv::MediaWorker::cacheUpdated);
        const qint64 budget = 640 * 360 * 4 * 4;
        worker.open(path, 1, budget, 32000);
        auto contains = [&](int frame) {
            if (coverage.isEmpty())
                return false;
            for (const auto& range : coverage.last().at(2).toList()) {
                const auto bounds = range.toMap();
                if (bounds["start"].toInt() <= frame && bounds["end"].toInt() >= frame)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(contains(3), 3000);
        worker.seek(60);
        QTRY_VERIFY_WITH_TIMEOUT(contains(60) && contains(61) && contains(62), 3000);
        QVERIFY(coverage.last().at(1).toLongLong() <= budget);
    }
    void controllerDiscardsPreviousSourceAndShowsLatestSeek() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        tfv::PlayerController player(preferences);
        QVERIFY(player.suggestedProjectUrl().isEmpty());
        QSignalSpy frames(&player, &tfv::PlayerController::frameReady);
        player.openMedia(QUrl::fromLocalFile("nonexistent-input.mp4"));
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 3000);
        QCOMPARE(player.suggestedProjectUrl(),
                 QUrl::fromLocalFile(QFileInfo(path).absolutePath() + '/' +
                                     QFileInfo(path).completeBaseName() + ".tfv"));
        QVERIFY(player.error().isEmpty());
        frames.clear();
        player.seek(10);
        player.seek(60);
        player.seek(40);
        QTRY_VERIFY_WITH_TIMEOUT(!frames.isEmpty(), 3000);
        QCOMPARE(player.currentFrame(), 40);
        tfv::Decoder expected;
        QString error;
        QVERIFY(expected.open(path, error));
        QCOMPARE(qvariant_cast<tfv::VideoFrame>(frames.last().first()).toImage(), expected.frame(40, error));
    }
    void backwardScrubWarmsEarlierFrames() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        tfv::MediaWorker worker;
        QSignalSpy coverage(&worker, &tfv::MediaWorker::cacheUpdated);
        const qint64 budget = 640 * 360 * 3 / 2 * 4;
        worker.open(path, 1, budget, 32000);
        QTRY_VERIFY_WITH_TIMEOUT(!coverage.isEmpty(), 3000);
        worker.seek(60, 1, -1);
        auto contains = [&](int frame) {
            for (const auto& range : coverage.last().at(2).toList()) {
                const auto bounds = range.toMap();
                if (bounds["start"].toInt() <= frame && bounds["end"].toInt() >= frame)
                    return true;
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(contains(57) && contains(60), 3000);
        QVERIFY(coverage.last().at(1).toLongLong() <= budget);
    }
    void rapidColdSeeksThenPlaybackKeepPresentingCorrectFrames() {
        const QString path = qEnvironmentVariable("TFV_STRESS_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the 1080p integration fixture with scripts/verify.ps1");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        preferences.setCacheMiB(64);
        preferences.setVolume(0);
        tfv::PlayerController player(preferences);
        QList<QPair<int, QImage>> presented;
        connect(&player, &tfv::PlayerController::frameReady, this, [&](tfv::VideoFrame image) {
            if (!image.isNull())
                presented.append({player.currentFrame(), image.toImage()});
        });
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 5000);
        tfv::Decoder expected;
        QString error;
        QVERIFY(expected.open(path, error));
        for (int round = 0; round < 4; ++round) {
            for (int i = 0; i < 30; ++i) {
                player.seek((i * 97 + round * 43) % player.frameCount(), true);
                QTest::qWait(2);
            }
            const int target = 120 + round * 17;
            presented.clear();
            player.seek(target, true);
            QTRY_VERIFY_WITH_TIMEOUT(!presented.isEmpty() && presented.last().first == target, 5000);
            QCOMPARE(presented.last().second, expected.frame(target, error));
            presented.clear();
            player.setSpeed(4);
            player.execute("forward");
            QTRY_VERIFY_WITH_TIMEOUT(presented.size() >= 6, 3000);
            QVERIFY(player.currentFrame() > target);
            player.execute("pause");
            QCOMPARE(presented.last().second, expected.frame(presented.last().first, error));
            QVERIFY(player.cacheUsedMiB() <= 64.);
        }
    }
    void timelineSeekingPreservesTransportAndRebasesTheClock() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        preferences.setVolume(0);
        tfv::PlayerController player(preferences);
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 3000);
        player.setSpeed(1.5);
        player.seek(24);
        player.execute("forward");
        player.seekTimeline(10);
        player.seekTimeline(60);
        QVERIFY(player.playing());
        QCOMPARE(player.currentFrame(), 60);
        QCOMPARE(player.direction(), 1);
        QCOMPARE(player.speed(), 1.5);
        // A held drag's timer flushes without needing a release and starts from
        // the new target rather than allowing the previous playback clock to win.
        QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() > 60, 800);
        player.seek(72);
        player.execute("reverse");
        player.seekTimeline(36);
        player.finishScrub();
        QVERIFY(player.playing());
        QCOMPARE(player.currentFrame(), 36);
        QCOMPARE(player.direction(), -1);
        QCOMPARE(player.speed(), 1.5);
        QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < 36, 800);
        player.execute("pause");
        player.seekTimeline(10);
        player.finishScrub();
        QTest::qWait(100);
        QVERIFY(!player.playing());
        QCOMPARE(player.currentFrame(), 10);
        // Viewer right-drag and frame stepping keep their existing paused-scrub policy.
        player.execute("forward");
        player.seek(31, true);
        player.finishScrub();
        QVERIFY(!player.playing());
        QCOMPARE(player.currentFrame(), 31);
    }
    void singleFrameLoopDoesNotLeaveTheClockPastItsRange() {
        const QString path = qEnvironmentVariable("TFV_TEST_MEDIA");
        if (path.isEmpty())
            QSKIP("Generate the integration fixture with scripts/verify.ps1");
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        tfv::Preferences preferences;
        preferences.setVolume(0);
        tfv::PlayerController player(preferences);
        QSignalSpy frames(&player, &tfv::PlayerController::frameReady);
        player.openMedia(QUrl::fromLocalFile(path));
        QTRY_VERIFY_WITH_TIMEOUT(player.loaded(), 3000);
        player.setRange(24, 24);
        player.seek(24);
        QTRY_VERIFY_WITH_TIMEOUT(
            !frames.isEmpty() && !qvariant_cast<tfv::VideoFrame>(frames.last().first()).isNull(), 3000);
        frames.clear();
        player.execute("forward");
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 4, 1200);
        QCOMPARE(player.currentFrame(), 24);
        player.execute("pause");
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"

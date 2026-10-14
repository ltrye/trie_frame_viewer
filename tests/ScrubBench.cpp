#include "application/PlayerController.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
#include <iostream>
#include <map>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("Trie");
    app.setApplicationName("ScrubBenchmark");
    QCommandLineParser parser;
    parser.addPositionalArgument("media", "Local source");
    parser.addOption({"benchmark", "JSON report", "path"});
    parser.addOption({"cache-mib", "Video cache budget", "MiB", "512"});
    parser.addOption({"decode-mode", "software, auto, or hardware", "mode", "auto"});
    parser.addOption({"decoder-threads", "Software decoder threads", "count", "2"});
    parser.process(app);
    if (parser.positionalArguments().size() != 1 || !parser.isSet("benchmark"))
        return 2;
    const QString path = parser.positionalArguments().first();
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    tfv::Preferences preferences;
    preferences.setCacheMiB(parser.value("cache-mib").toInt());
    preferences.setDecodeMode(parser.value("decode-mode"));
    preferences.setDecoderThreads(parser.value("decoder-threads").toInt());
    preferences.setVolume(0);
    tfv::PlayerController player(preferences);
    QElapsedTimer clock, trace, finalTimer;
    clock.start();
    QTimer input;
    input.setTimerType(Qt::PreciseTimer);
    input.setInterval(8);
    int phase = -1, requests = 0, delivered = 0, target = -1;
    bool active = false, finalPending = false, first = true, valid = true;
    double previousDelivery = 0, maxGap = 0, dragSeconds = 0;
    QList<double> latency;
    QJsonArray traces;
    std::map<int, double> submitted;
    std::function<void()> begin;
    begin = [&] {
        ++phase;
        if (phase == 3) {
            const QJsonObject report{
                {"cacheBudgetMiB", preferences.cacheMiB()}, {"cacheUsedMiB", player.cacheUsedMiB()},
                {"decodeBackend", player.decodeBackend()},  {"includesRendering", false},
                {"allFinalPixelsCorrect", valid},           {"traces", traces}};
            QFile output(parser.value("benchmark"));
            if (!output.open(QIODevice::WriteOnly)) {
                app.exit(3);
                return;
            }
            output.write(QJsonDocument(report).toJson());
            std::cout << QJsonDocument(report).toJson().toStdString();
            app.exit(valid ? 0 : 4);
            return;
        }
        requests = delivered = 0;
        maxGap = 0;
        latency.clear();
        submitted.clear();
        previousDelivery = clock.nsecsElapsed() / 1e6;
        trace.start();
        active = true;
        input.start();
    };
    QObject::connect(&input, &QTimer::timeout, &app, [&] {
        const double progress = std::min(1., trace.elapsed() / 3000.);
        if (phase == 0)
            target = int(progress * (player.frameCount() - 1));
        else if (phase == 1)
            target = int((1 - progress) * (player.frameCount() - 1));
        else
            target = int((quint64(requests) * 104729 + 37) % quint64(player.frameCount()));
        ++requests;
        submitted[target] = clock.nsecsElapsed() / 1e6;
        player.seek(target, true);
        if (trace.elapsed() >= 3000) {
            input.stop();
            dragSeconds = trace.elapsed() / 1000.;
            active = false;
            maxGap = std::max(maxGap, clock.nsecsElapsed() / 1e6 - previousDelivery);
            finalPending = true;
            finalTimer.start();
            player.finishScrub();
        }
    });
    QObject::connect(&player, &tfv::PlayerController::frameReady, &app, [&](tfv::VideoFrame frame) {
        if (frame.isNull())
            return;
        const int shown = player.presentedFrame();
        if (first) {
            first = false;
            QTimer::singleShot(3000, &app, begin);
            return;
        }
        if (active) {
            const double now = clock.nsecsElapsed() / 1e6;
            ++delivered;
            maxGap = std::max(maxGap, now - previousDelivery);
            previousDelivery = now;
            if (const auto it = submitted.find(shown); it != submitted.end())
                latency.append(now - it->second);
        }
        if (finalPending && shown == target) {
            const double finalMs = finalTimer.nsecsElapsed() / 1e6;
            finalPending = false;
            tfv::Decoder reference;
            QString error;
            const bool opened = reference.open(path, error, preferences.decoderThreads(),
                                               player.decodeBackend() == "D3D11VA", 0);
            const auto expected = opened ? reference.videoFrame(target, error) : tfv::VideoFrame{};
            bool exact =
                !expected.isNull() && frame.format() == expected.format() && frame.size() == expected.size();
            if (exact && frame.format() == tfv::VideoFrame::Format::Yuv420) {
                for (int plane = 0; plane < 3; ++plane)
                    exact = exact && frame.plane(plane).bytes == expected.plane(plane).bytes;
            } else if (exact)
                exact = frame.toImage() == expected.toImage();
            valid = valid && exact;
            std::sort(latency.begin(), latency.end());
            const auto percentile = [&](double f) {
                return latency.isEmpty() ? -1. : latency[int((latency.size() - 1) * f)];
            };
            traces.append(QJsonObject{{"name", phase == 0   ? "forward"
                                               : phase == 1 ? "backward"
                                                            : "random"},
                                      {"durationSeconds", dragSeconds},
                                      {"inputRequests", requests},
                                      {"previewsDuringDrag", delivered},
                                      {"maxPreviewGapMs", maxGap},
                                      {"inputToDecodedP50Ms", percentile(.5)},
                                      {"inputToDecodedP95Ms", percentile(.95)},
                                      {"releaseToExactMs", finalMs},
                                      {"finalFrame", target},
                                      {"finalPixelsCorrect", exact}});
            QTimer::singleShot(200, &app, begin);
        }
    });
    QTimer::singleShot(60000, &app, [&] { app.exit(5); });
    player.openMedia(QUrl::fromLocalFile(path));
    return app.exec();
}

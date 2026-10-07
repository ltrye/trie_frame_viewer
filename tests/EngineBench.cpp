#include "application/PlayerController.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>
#include <algorithm>
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on

double processCpu() {
    FILETIME creation, exit, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    const auto ticks = [](FILETIME t) { return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return (ticks(kernel) + ticks(user)) / 1e7;
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("Trie");
    app.setApplicationName("EngineBenchmark");
    QCommandLineParser parser;
    parser.addPositionalArgument("media", "Local source");
    parser.addOption({"benchmark", "JSON report", "path"});
    parser.addOption({"cache-mib", "Video cache budget", "MiB", "512"});
    parser.addOption({"decode-mode", "software or auto", "mode", "software"});
    parser.addOption({"decoder-threads", "Software decoder threads", "count", "2"});
    parser.process(app);
    if (parser.positionalArguments().size() != 1 || !parser.isSet("benchmark"))
        return 2;
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    tfv::Preferences preferences;
    preferences.setCacheMiB(parser.value("cache-mib").toInt());
    preferences.setDecodeMode(parser.value("decode-mode"));
    preferences.setDecoderThreads(parser.value("decoder-threads").toInt());
    preferences.setVolume(0);
    tfv::PlayerController player(preferences);
    QList<double> latency;
    QElapsedTimer total, request, rapidTime;
    total.start();
    const double cpuStart = processCpu();
    int serial = 0, frames = 0, scrubRequests = 0;
    bool ready = false, pending = false, rapidDone = false;
    double firstFrame = -1, finalSeek = -1;
    quint64 working = 0, privateBytes = 0;
    QTimer sampler, scrub;
    std::function<void()> seekNext, finish;
    finish = [&] {
        sampler.stop();
        scrub.stop();
        auto sorted = latency;
        std::sort(sorted.begin(), sorted.end());
        const auto p = [&](double fraction) {
            return sorted.isEmpty() ? -1. : sorted[int((sorted.size() - 1) * fraction)];
        };
        QJsonObject report{{"decodeBackend", player.decodeBackend()},
                           {"frameCount", player.frameCount()},
                           {"width", player.mediaWidth()},
                           {"height", player.mediaHeight()},
                           {"firstFrameMs", firstFrame},
                           {"cpuSeconds", processCpu() - cpuStart},
                           {"wallSeconds", total.elapsed() / 1000.},
                           {"peakWorkingSetMiB", working / 1048576.},
                           {"peakPrivateMiB", privateBytes / 1048576.},
                           {"cacheMiB", player.cacheUsedMiB()},
                           {"seekP50Ms", p(.5)},
                           {"seekP95Ms", p(.95)},
                           {"finalSeekMs", finalSeek},
                           {"framesDelivered", frames},
                           {"rapidScrubRequests", scrubRequests},
                           {"serialSeeksCompleted", latency.size()},
                           {"includesRendering", false}};
        QFile output(parser.value("benchmark"));
        const bool ok = output.open(QIODevice::WriteOnly) && output.write(QJsonDocument(report).toJson()) > 0;
        app.exit(ok && latency.size() == 48 && finalSeek >= 0 ? 0 : 3);
    };
    seekNext = [&] {
        if (serial == 48) {
            pending = false;
            rapidTime.start();
            scrub.start(8);
            return;
        }
        const int ordinal = serial < 24 ? serial : 47 - serial;
        ++serial;
        pending = true;
        request.restart();
        player.seek(int(quint64(ordinal) * 7919 % quint64(player.frameCount())));
    };
    QObject::connect(&player, &tfv::PlayerController::frameReady, &app, [&](const auto& image) {
        if (image.isNull())
            return;
        ++frames;
        if (firstFrame < 0) {
            firstFrame = total.elapsed();
            QTimer::singleShot(3000, &app, [&] {
                ready = true;
                seekNext();
            });
        }
        if (ready && pending) {
            pending = false;
            if (rapidDone) {
                finalSeek = request.nsecsElapsed() / 1e6;
                QTimer::singleShot(300, &app, finish);
            } else {
                latency.append(request.nsecsElapsed() / 1e6);
                QTimer::singleShot(0, &app, seekNext);
            }
        }
    });
    scrub.setTimerType(Qt::PreciseTimer);
    QObject::connect(&scrub, &QTimer::timeout, &app, [&] {
        player.seek(int((quint64(scrubRequests++) * 104729 + 37) % quint64(player.frameCount())), true);
        if (rapidTime.elapsed() >= 3000) {
            scrub.stop();
            rapidDone = pending = true;
            request.restart();
            player.seek(player.frameCount() / 2);
        }
    });
    QObject::connect(&sampler, &QTimer::timeout, &app, [&] {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters))) {
            working = std::max(working, quint64(counters.WorkingSetSize));
            privateBytes = std::max(privateBytes, quint64(counters.PrivateUsage));
        }
    });
    sampler.start(25);
    QTimer::singleShot(90000, &app, finish);
    player.openMedia(QUrl::fromLocalFile(parser.positionalArguments().first()));
    return app.exec();
}

#include "application/PlayerController.h"
#include "presentation/VideoItem.h"
#include "presentation/WindowChrome.h"
#ifdef TFV_DEVELOPMENT_TESTS
#include "../../tests/AppSmoke.h"
#include "../../tests/PerformanceBench.h"
#endif
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Trie");
    app.setApplicationName("FrameViewer");
    app.setApplicationVersion(QStringLiteral(TFV_APP_VERSION));
    app.setWindowIcon(QIcon(":/tfv/branding/fv-icon.svg"));
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<tfv::VideoItem>("Trie.Media", 1, 0, "VideoSurface");
    qmlRegisterType<tfv::WindowChrome>("Trie.Window", 1, 0, "WindowChrome");
    QCommandLineParser parser;
    parser.setApplicationDescription("Trie FrameViewer — animation reference and playblast review");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("media", "Optional video, sequence image, or .tfv project");
    parser.addOption({"screenshot", "Capture the app to a PNG and exit", "path"});
    parser.addOption({"benchmark", "Run repeatable native seek/memory benchmark and exit", "report"});
    parser.addOption({"cache-mib", "Video cache budget for development benchmark", "MiB", "512"});
    parser.addOption(
        {"decode-mode", "Decoder preference for benchmark: software, auto, hardware", "mode", "software"});
    parser.addOption({"decoder-threads", "Software decoder threads for benchmark", "threads", "2"});
    parser.addOption({"smoke-test",
                      "Exercise playback and project persistence, capture screenshots, and exit",
                      "directory"});
    parser.process(app);
    if (parser.isSet("smoke-test") || parser.isSet("screenshot") || parser.isSet("benchmark")) {
        const QString output = parser.isSet("benchmark") ? QFileInfo(parser.value("benchmark")).absolutePath()
                               : parser.isSet("smoke-test")
                                   ? QFileInfo(parser.value("smoke-test")).absoluteFilePath()
                                   : QFileInfo(parser.value("screenshot")).absolutePath();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, output + "/settings");
    }
    tfv::Preferences preferences;
    if (parser.isSet("benchmark")) {
        preferences.setCacheMiB(parser.value("cache-mib").toInt());
        preferences.setDecodeMode(parser.value("decode-mode"));
        preferences.setDecoderThreads(parser.value("decoder-threads").toInt());
    }
    tfv::PlayerController player(preferences);
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{"player", QVariant::fromValue(&player)}, {"preferences", QVariant::fromValue(&preferences)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Trie.FrameViewer", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    for (auto* surface : window->findChildren<tfv::VideoItem*>())
        QObject::connect(&player, &tfv::PlayerController::frameReady, surface, &tfv::VideoItem::setFrame);
    const auto arguments = parser.positionalArguments();
    if (!arguments.isEmpty()) {
        const QUrl source = QUrl::fromLocalFile(QFileInfo(arguments.first()).absoluteFilePath());
        if (arguments.first().endsWith(".tfv", Qt::CaseInsensitive) ||
            arguments.first().endsWith(".tfv.autosave", Qt::CaseInsensitive))
            player.openProject(source);
        else
            player.openMedia(source);
    }
    if (parser.isSet("screenshot")) {
        const QString path = parser.value("screenshot");
        QTimer::singleShot(
            2200, &app, [window, path] { QCoreApplication::exit(window->grabWindow().save(path) ? 0 : 2); });
    }
#ifdef TFV_DEVELOPMENT_TESTS
    if (parser.isSet("benchmark"))
        startPerformanceBench(app, *window, player, preferences,
                              QFileInfo(parser.value("benchmark")).absoluteFilePath());
    if (parser.isSet("smoke-test"))
        startAppSmoke(app, *window, player, preferences,
                      QFileInfo(parser.value("smoke-test")).absoluteFilePath());
#endif
    return app.exec();
}

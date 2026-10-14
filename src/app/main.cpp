#include "application/PlayerController.h"
#include "platform/ResourceMonitor.h"
#include "platform/WindowsIntegration.h"
#include "presentation/VideoItem.h"
#include "presentation/WindowChrome.h"
#include "presentation/WindowDiagnostics.h"
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
#include <QTextStream>
#include <QTimer>

static void setApplicationIdentity(QCoreApplication& app) {
    app.setOrganizationName("Trie");
    app.setApplicationName("FrameViewer");
    app.setApplicationVersion(QStringLiteral(TFV_APP_VERSION));
}

static int shellIntegrationCommand(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    setApplicationIdentity(app);
    QCommandLineParser parser;
    parser.addOption({"register-context-menu", "Register the video context menu for this user"});
    parser.addOption({"unregister-context-menu", "Remove this copy's video context menu registration"});
    parser.process(app);
    const bool install = parser.isSet("register-context-menu");
    if (install == parser.isSet("unregister-context-menu") || !parser.positionalArguments().isEmpty())
        return 2;
    tfv::WindowsIntegration integration;
    const bool success = install ? integration.registerContextMenu() : integration.unregisterContextMenu();
    QTextStream(success ? stdout : stderr) << integration.message() << Qt::endl;
    return success ? 0 : 1;
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--register-context-menu" ||
            QByteArray(argv[i]) == "--unregister-context-menu")
            return shellIntegrationCommand(argc, argv);
    QGuiApplication app(argc, argv);
    setApplicationIdentity(app);
    app.setWindowIcon(QIcon(":/tfv/branding/fv-icon.svg"));
    QQuickStyle::setStyle("Basic");
    qmlRegisterType<tfv::VideoItem>("Trie.Media", 1, 0, "VideoSurface");
    qmlRegisterType<tfv::WindowChrome>("Trie.Window", 1, 0, "WindowChrome");
    QCommandLineParser parser;
    parser.setApplicationDescription("Trie FrameViewer — animation reference and playblast review");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"register-context-menu", "Register the video context menu for this user and exit"});
    parser.addOption(
        {"unregister-context-menu", "Remove this copy's video context menu registration and exit"});
    parser.addPositionalArgument("media", "Optional video, sequence image, or .tfv project");
    parser.addOption({"screenshot", "Capture the app to a PNG and exit", "path"});
#ifdef TFV_DEVELOPMENT_TESTS
    parser.addOption({"benchmark", "Run repeatable native seek/memory benchmark and exit", "report"});
    parser.addOption({"cache-mib", "Video cache budget for development benchmark", "MiB", "512"});
    parser.addOption(
        {"decode-mode", "Decoder preference for benchmark: software, auto, hardware", "mode", "software"});
    parser.addOption({"decoder-threads", "Software decoder threads for benchmark", "threads", "2"});
    parser.addOption({"smoke-test",
                      "Exercise playback and project persistence, capture screenshots, and exit",
                      "directory"});
#endif
    parser.process(app);
    QString diagnosticDirectory;
    if (parser.isSet("screenshot"))
        diagnosticDirectory = QFileInfo(parser.value("screenshot")).absolutePath();
#ifdef TFV_DEVELOPMENT_TESTS
    if (parser.isSet("benchmark"))
        diagnosticDirectory = QFileInfo(parser.value("benchmark")).absolutePath();
    else if (parser.isSet("smoke-test"))
        diagnosticDirectory = QFileInfo(parser.value("smoke-test")).absoluteFilePath();
#endif
    if (!diagnosticDirectory.isEmpty()) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, diagnosticDirectory + "/settings");
    }
    tfv::Preferences preferences;
#ifdef TFV_DEVELOPMENT_TESTS
    if (parser.isSet("benchmark")) {
        preferences.setCacheMiB(parser.value("cache-mib").toInt());
        preferences.setDecodeMode(parser.value("decode-mode"));
        preferences.setDecoderThreads(parser.value("decoder-threads").toInt());
    }
#endif
    tfv::PlayerController player(preferences);
    tfv::ResourceMonitor resourceMonitor(preferences.integer("debugRefreshIntervalMs"));
    resourceMonitor.setEnabled(preferences.debugMode());
    QObject::connect(&preferences, &tfv::Preferences::debugModeChanged, &resourceMonitor,
                     [&] { resourceMonitor.setEnabled(preferences.debugMode()); });
    tfv::WindowsIntegration windowsIntegration;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"player", QVariant::fromValue(&player)},
                                 {"preferences", QVariant::fromValue(&preferences)},
                                 {"resourceMonitor", QVariant::fromValue(&resourceMonitor)},
                                 {"windowsIntegration", QVariant::fromValue(&windowsIntegration)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    engine.loadFromModule("Trie.FrameViewer", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    tfv::connectWindowDiagnostics(*window, resourceMonitor);
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

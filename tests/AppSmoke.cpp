#include "AppSmoke.h"
#include "application/PlayerController.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtTest/QTest>

static void clickItem(QQuickWindow& window, QQuickItem* item) {
    if (!item)
        return;
    const QPointF point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
}

static bool editFrameFields(QQuickWindow& window, tfv::PlayerController& player) {
    const int originalStart = player.rangeStart(), originalEnd = player.rangeEnd();
    window.grabWindow(); // Complete hidden-window layout before clicking fields.
    const auto edit = [&](const char* name, const QString& text, Qt::Key key = Qt::Key_Return) {
        auto* field = window.findChild<QQuickItem*>(name);
        if (!field)
            return false;
        clickItem(window, field);
        field->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
        for (auto character : text)
            QTest::keyClick(&window, character.toLatin1());
        QTest::keyClick(&window, key);
        QCoreApplication::processEvents();
        return true;
    };
    bool valid = edit("playbackStartField", "9") && player.rangeStart() == 8;
    valid = edit("playbackEndField", "32") && player.rangeEnd() == 31 && valid;
    valid = edit("currentFrameField", "17") && player.currentFrame() == 16 && valid;

    // Below-range values cannot commit. Moving focus restores the model value.
    valid = edit("playbackStartField", "0") && valid;
    auto* current = window.findChild<QQuickItem*>("currentFrameField");
    auto* start = window.findChild<QQuickItem*>("playbackStartField");
    if (!current || !start)
        return false;
    current->forceActiveFocus();
    QCoreApplication::processEvents();
    valid = player.rangeStart() == 8 && start->property("text").toString() == "9" && valid;
    valid = edit("playbackStartField", "12", Qt::Key_Escape) && player.rangeStart() == 8 && valid;

    // A valid edit commits on focus loss too, and controller seeks keep the display in sync.
    valid = edit("playbackStartField", "10", Qt::Key_Tab) && player.rangeStart() == 9 && valid;
    current->setFocus(false);
    start->setFocus(false);
    auto* end = window.findChild<QQuickItem*>("playbackEndField");
    if (!end)
        return false;
    end->setFocus(false);
    player.setRange(originalStart, originalEnd);
    player.seek(24);
    QCoreApplication::processEvents();
    valid = current->property("text").toString() == "25" && valid;
    // Focusing a field without editing must not undo subsequent playback updates.
    current->forceActiveFocus();
    player.seek(25);
    end->forceActiveFocus();
    QCoreApplication::processEvents();
    valid = player.currentFrame() == 25 && valid;
    player.seek(24);
    // Leave a range field focused to exercise source reload with active input.
    return valid;
}

static bool centeredButtons(QQuickWindow& window) {
    int count = 0;
    for (auto* contents : window.findChildren<QQuickItem*>("buttonContents")) {
        auto* parent = contents->parentItem();
        if (!contents->isVisible() || !parent || parent->width() <= 0 || parent->height() <= 0)
            continue;
        ++count;
        if (qAbs(contents->x() + contents->width() / 2 - parent->width() / 2) > 0.5 ||
            qAbs(contents->y() + contents->height() / 2 - parent->height() / 2) > 0.5)
            return false;
    }
    return count > 0;
}

static bool nativeCaptionMatches(QQuickWindow& window, bool dark) {
    auto* chrome = window.findChild<QObject*>("windowChrome");
    if (!chrome || chrome->property("dark").toBool() != dark)
        return false;
    // Caption/text colors are write-only DWM attributes. The adapter reports
    // success only after both Windows calls accept the current theme colors.
    return QGuiApplication::platformName() != "windows" || chrome->property("applied").toBool();
}

static void finishUiSmoke(QGuiApplication& app, QQuickWindow& window, tfv::Preferences& preferences,
                          const QString& directory, std::shared_ptr<QJsonObject> report,
                          std::shared_ptr<bool> valid) {
    window.resize(1160, 800);
    preferences.setTheme("dark");
    // A resize must finish its layout pass before pointer coordinates are sampled.
    QTimer::singleShot(120, &app,
                       [&] { clickItem(window, window.findChild<QQuickItem*>("bookmarkToggle")); });
    QTimer::singleShot(300, &app, [&, directory, report, valid] {
        // Hidden diagnostic windows may defer layout until a frame is requested.
        const QImage expandedImage = window.grabWindow();
        auto* panel = window.findChild<QQuickItem*>("bookmarkPanel");
        const bool expanded =
            panel && !panel->property("collapsed").toBool() && qAbs(panel->width() - 220) < 0.5;
        (*report)["bookmarkButtonExpands"] = expanded;
        *valid = expanded && centeredButtons(window) && *valid;
        *valid = expandedImage.save(directory + "/review-expanded.png") && *valid;
        clickItem(window, window.findChild<QQuickItem*>("menuTitle_Bookmark"));
        QTimer::singleShot(120, &app, [&, directory, report, valid] {
            clickItem(window, window.findChild<QQuickItem*>("menu_toggleBookmarks"));
            QTimer::singleShot(120, &app, [&, directory, report, valid] {
                *valid = window.grabWindow().save(directory + "/review-collapsed.png") && *valid;
                auto* panel = window.findChild<QQuickItem*>("bookmarkPanel");
                const bool collapsed =
                    panel && panel->property("collapsed").toBool() && qAbs(panel->width() - 72) < 0.5;
                (*report)["bookmarkMenuCollapses"] = collapsed;
                *valid = collapsed && *valid;
                clickItem(window, window.findChild<QQuickItem*>("menuTitle_File"));
                QTimer::singleShot(120, &app, [&, directory, report, valid] {
                    QQmlListReference menus(window.findChild<QObject*>("appMenuBar"), "menus");
                    auto* fileMenu = menus.isValid() && menus.count() ? menus.at(0) : nullptr;
                    const bool menuOpened = fileMenu && fileMenu->property("opened").toBool();
                    (*report)["fileMenuOpens"] = menuOpened;
                    *valid = menuOpened && *valid;
                    *valid = window.grabWindow().save(directory + "/review-menu.png") && *valid;
                    if (fileMenu)
                        QMetaObject::invokeMethod(fileMenu, "close");
                    QMetaObject::invokeMethod(&window, "dispatch", Q_ARG(QVariant, QVariant("settings")));
                    QTimer::singleShot(200, &app, [&, directory, report, valid] {
                        *valid = window.grabWindow().save(directory + "/preferences.png") && *valid;
                        QFile output(directory + "/smoke-report.json");
                        output.open(QIODevice::WriteOnly);
                        output.write(QJsonDocument(*report).toJson());
                        QCoreApplication::exit(*valid ? 0 : 4);
                    });
                });
            });
        });
    });
}

static void stage(const QString& directory, const char* message) {
    QFile file(directory + "/smoke-stages.log");
    file.open(QIODevice::Append);
    file.write(message);
    file.write("\n");
}

static void replayAndLoopSmoke(QGuiApplication& app, QQuickWindow& window, tfv::PlayerController& player,
                               tfv::Preferences& preferences, const QString& directory,
                               std::shared_ptr<QJsonObject> report, std::shared_ptr<bool> valid) {
    auto wraps = std::make_shared<int>(0);
    auto previous = std::make_shared<int>(player.currentFrame());
    const auto connection =
        QObject::connect(&player, &tfv::PlayerController::positionChanged, &app, [&, wraps, previous] {
            if (player.playing() && player.direction() > 0 && player.currentFrame() < *previous)
                ++*wraps;
            *previous = player.currentFrame();
        });
    player.setRange(24, 28);
    player.execute("forward");
    QTimer::singleShot(900, &app, [&, directory, report, valid, wraps, connection] {
        QObject::disconnect(connection);
        auto* audio = player.findChild<tfv::AudioEngine*>();
        const bool audioExpected = player.audioAvailable() && preferences.volume() > 0;
        const bool audible = audio && audio->clockAvailable() && audio->seconds() > 1.;
        (*report)["replayedLoopCount"] = *wraps;
        (*report)["replayedAudioActive"] = audioExpected ? QJsonValue(audible) : QJsonValue();
        *valid = *wraps >= 2 && (!audioExpected || audible) && *valid;
        player.execute("pause");
        player.setRange(4, 36);
        player.seek(24);
        preferences.setTheme("light");
        QTimer::singleShot(300, &app, [&, directory, report, valid] {
            (*report)["lightCaptionMatches"] = nativeCaptionMatches(window, false);
            *valid = nativeCaptionMatches(window, false) && *valid;
            *valid = window.grabWindow().save(directory + "/review-light.png") && *valid;
            window.resize(860, 540);
            QTimer::singleShot(200, &app, [&, directory, report, valid] {
                *valid = window.grabWindow().save(directory + "/review-compact.png") && *valid;
                *valid = centeredButtons(window) && *valid;
                finishUiSmoke(app, window, preferences, directory, report, valid);
            });
        });
    });
}

void startAppSmoke(QGuiApplication& app, QQuickWindow& window, tfv::PlayerController& player,
                   tfv::Preferences& preferences, const QString& directory) {
    QDir().mkpath(directory);
    // Exercise the output device quietly, even after a previous muted diagnostic run.
    preferences.setVolume(0.02);
    auto done = std::make_shared<bool>(false);
    QObject::connect(&player, &tfv::PlayerController::cacheChanged, &app, [&, directory, done] {
        if (*done || !player.loaded() || player.cacheUsedMiB() < 1)
            return;
        *done = true;
        const bool frameFieldsValid = editFrameFields(window, player);
        player.seek(24);
        player.addBookmark(false, "#e6bf79");
        player.setRange(4, 36);
        player.addBookmark(true, "#a8d5c2");
        player.rotate(90);
        player.flip(false);
        const QString path = directory + "/review.tfv";
        if (!player.saveProject(QUrl::fromLocalFile(path))) {
            QCoreApplication::exit(3);
            return;
        }
        player.openProject(QUrl::fromLocalFile(path));
        QTimer::singleShot(1500, &app, [&, directory, frameFieldsValid] {
            auto* panel = window.findChild<QQuickItem*>("bookmarkPanel");
            QQmlListReference menus(window.findChild<QObject*>("appMenuBar"), "menus");
            const bool railDefault = panel && panel->property("collapsed").toBool() && panel->width() == 72;
            const auto fieldValue = [&](const char* name) {
                auto* field = window.findChild<QQuickItem*>(name);
                return field ? field->property("text").toString() : QString{};
            };
            const bool fieldsRestored = fieldValue("playbackStartField") == "5" &&
                                        fieldValue("playbackEndField") == "37" &&
                                        fieldValue("currentFrameField") == "25";
            auto report = std::make_shared<QJsonObject>(QJsonObject{
                {"loaded", player.loaded()},
                {"frame", player.currentFrame()},
                {"bookmarks", player.bookmarks()->property("count").toInt()},
                {"rotation", player.rotation()},
                {"flipH", player.flipH()},
                {"dirty", player.dirty()},
                {"error", player.error()},
                {"cacheMiB", player.cacheUsedMiB()},
                {"audioAvailable", player.audioAvailable()},
                {"bookmarkRailDefault", railDefault},
                {"buttonsCentered", centeredButtons(window)},
                {"frameFieldsEditAndSync", frameFieldsValid},
                {"frameFieldsRestoreWithProject", fieldsRestored},
                {"menuCount", menus.count()},
                {"darkCaptionMatches", nativeCaptionMatches(window, true)},
                {"graphicsBackend",
                 window.rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11 ? "Direct3D11"
                                                                                               : "Other"}});
            auto valid = std::make_shared<bool>(
                player.loaded() && player.currentFrame() == 24 &&
                player.bookmarks()->property("count").toInt() == 2 && player.rotation() == 90 &&
                player.flipH() && !player.dirty() && player.error().isEmpty() && railDefault &&
                centeredButtons(window) && frameFieldsValid && fieldsRestored && menus.count() == 8 &&
                nativeCaptionMatches(window, true));
            *valid = window.grabWindow().save(directory + "/review-dark.png") && *valid;
            stage(directory, "Starting forward playback");
            if (qEnvironmentVariableIsSet("TFV_SMOKE_SILENT"))
                preferences.setVolume(0);
            player.execute("forward");
            stage(directory, "Forward playback started");
            QTimer::singleShot(240, &app, [&, directory, report, valid] {
                (*report)["forwardPlayback"] = player.currentFrame() > 24;
                *valid = player.currentFrame() > 24 && *valid;
                stage(directory, "Checking forward playback");
                player.execute("pause");
                player.seek(24);
                player.execute("reverse");
                stage(directory, "Reverse playback started");
                QTimer::singleShot(180, &app, [&, directory, report, valid] {
                    (*report)["reversePlayback"] = player.currentFrame() < 24;
                    *valid = player.currentFrame() < 24 && *valid;
                    player.execute("pause");
                    player.seek(24);
                    replayAndLoopSmoke(app, window, player, preferences, directory, report, valid);
                });
            });
        });
    });
    QTimer::singleShot(20000, &app, [] { QCoreApplication::exit(5); });
}

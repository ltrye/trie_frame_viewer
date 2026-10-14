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

static QQuickItem* visualItem(QQuickItem* parent, const QString& name) {
    if (parent->objectName() == name)
        return parent;
    for (auto* child : parent->childItems())
        if (auto* found = visualItem(child, name))
            return found;
    return nullptr;
}

static bool visibleText(QQuickItem* parent, const QString& text) {
    if (parent->isVisible() && parent->property("text").toString() == text)
        return true;
    for (auto* child : parent->childItems())
        if (visibleText(child, text))
            return true;
    return false;
}

static void clickItem(QQuickWindow& window, QQuickItem* item, Qt::MouseButton button = Qt::LeftButton) {
    if (!item)
        return;
    const QPointF point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(&window, button, Qt::NoModifier, point.toPoint());
}

static void typeText(QQuickWindow& window, QQuickItem* field, const QString& text) {
    field->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
    for (auto character : text)
        QTest::keyClick(&window, character.toLatin1());
}

static bool bookmarkUiSmoke(QQuickWindow& window, tfv::PlayerController& player, const QString& directory) {
    const auto log = [&](const QString& message) {
        QFile file(directory + "/bookmark-ui.log");
        file.open(QIODevice::WriteOnly | QIODevice::Append);
        file.write(message.toUtf8() + '\n');
    };
    auto* model = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
    log(QString("Bookmark UI: model=%1 count=%2").arg(model != nullptr).arg(model ? model->rowCount() : -1));
    if (!model || model->rowCount() != 2)
        return false;
    const QString id = model->at(0)->id;
    const auto markerFor = [&](const QString& bookmarkId) {
        // Repeater delegates belong to the visual tree, independently of QObject ownership.
        return visualItem(window.contentItem(), "bookmarkMarker_" + bookmarkId);
    };
    const auto menuAction = [&](const QString& bookmarkId, const char* action) {
        window.grabWindow();
        auto* marker = markerFor(bookmarkId);
        if (!marker) {
            log("Marker missing: " + bookmarkId);
            qWarning() << "Bookmark UI: marker missing" << bookmarkId;
            return false;
        }
        const int before = player.currentFrame();
        clickItem(window, marker, Qt::RightButton);
        QTest::qWait(150);
        auto* menu = window.findChild<QObject*>("bookmarkContextMenu");
        if (!menu || !menu->property("visible").toBool() || player.currentFrame() != before) {
            log(QString("Context menu failed: visible=%1 before=%2 after=%3")
                    .arg(menu && menu->property("visible").toBool())
                    .arg(before)
                    .arg(player.currentFrame()));
            qWarning() << "Bookmark UI: context menu did not open" << marker->mapToScene({0, 0})
                       << (menu ? menu->property("visible") : QVariant{});
            window.grabWindow().save(directory + "/bookmark-failure.png");
            return false;
        }
        window.grabWindow();
        auto* item = window.findChild<QQuickItem*>(action);
        if (!item) {
            log(QString("Action missing: %1").arg(action));
            qWarning() << "Bookmark UI: menu action missing" << action;
            return false;
        }
        clickItem(window, item);
        QTest::qWait(180);
        log(QString("Clicked action: %1").arg(action));
        return true;
    };
    if (!menuAction(id, "bookmarkRenameAction"))
        return false;
    auto* editor = window.findChild<QObject*>("bookmarkEditor");
    auto* input = window.findChild<QQuickItem*>("bookmarkEditorInput");
    if (!editor || !input || !editor->property("visible").toBool()) {
        log("Editor did not open");
        qWarning() << "Bookmark UI: editor did not open";
        window.grabWindow().save(directory + "/bookmark-failure.png");
        return false;
    }
    typeText(window, input, "contact pose");
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::qWait(150);
    if (model->find(id)->name != "contact pose" || !player.dirty()) {
        log("Rename did not commit: " + model->find(id)->name);
        qWarning() << "Bookmark UI: rename did not commit" << input->property("text")
                   << model->find(id)->name;
        window.grabWindow().save(directory + "/bookmark-failure.png");
        return false;
    }
    // Hover uses the actual marker hit area, and the name must follow a rename.
    auto* marker = markerFor(id);
    if (!marker)
        return false;
    QTest::mouseMove(&window, marker->mapToScene({marker->width() / 2, marker->height() / 2}).toPoint());
    QTest::qWait(750);
    const bool tooltip = visibleText(window.contentItem(), "contact pose");
    if (!tooltip || !window.grabWindow().save(directory + "/bookmark-tooltip.png")) {
        qWarning() << "Bookmark UI: tooltip not found";
        window.grabWindow().save(directory + "/bookmark-failure.png");
        return false;
    }
    const qreal oldX = marker->x();
    if (!menuAction(id, "bookmarkFrameAction"))
        return false;
    typeText(window, input, "0");
    QTest::keyClick(&window, Qt::Key_Return);
    if (!editor->property("visible").toBool() || model->find(id)->frame != 24)
        return false;
    typeText(window, input, "41");
    if (!window.grabWindow().save(directory + "/bookmark-edit.png"))
        return false;
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::qWait(150);
    marker = markerFor(id);
    if (!marker || model->find(id)->frame != 40 || marker->x() <= oldX)
        return false;
    clickItem(window, marker);
    if (player.currentFrame() != 40)
        return false;
    // Delete a temporary bookmark through the context menu, preserving the two saved markers.
    player.seek(60);
    player.addBookmark("#aebbe8");
    const QString temporaryId = model->at(2)->id;
    QCoreApplication::processEvents();
    if (!menuAction(temporaryId, "bookmarkDeleteAction"))
        return false;
    return model->rowCount() == 2 && !model->find(temporaryId) && !markerFor(temporaryId);
}

static bool frameFieldsOnOneRow(QQuickWindow& window) {
    const auto* current = window.findChild<QQuickItem*>("currentFrameField");
    const auto* start = window.findChild<QQuickItem*>("playbackStartField");
    const auto* end = window.findChild<QQuickItem*>("playbackEndField");
    return current && start && end && current->x() < start->x() && start->x() < end->x() &&
           qAbs(current->y() - start->y()) < 0.5 && qAbs(start->y() - end->y()) < 0.5;
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
    QTimer::singleShot(300, &app, [&, directory, report, valid] {
        *valid = window.grabWindow().save(directory + "/review-final.png") && *valid;
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
        auto* saveDialog = window.findChild<QObject*>("saveProjectDialog");
        const bool savePrepared = saveDialog && QMetaObject::invokeMethod(saveDialog, "prepareForMedia");
        const bool saveNameValid =
            savePrepared && saveDialog->property("selectedFile").toUrl() == player.suggestedProjectUrl() &&
            saveDialog->property("currentFolder").toUrl().isParentOf(player.suggestedProjectUrl());
        const bool frameFieldsValid = editFrameFields(window, player);
        player.seek(24);
        player.addBookmark("#e6bf79");
        player.seek(4);
        player.addBookmark("#a8d5c2");
        const bool bookmarksValid = bookmarkUiSmoke(window, player, directory);
        player.setRange(4, 36);
        player.seek(24);
        if (auto* field = window.findChild<QQuickItem*>("playbackEndField"))
            field->forceActiveFocus();
        player.rotate(90);
        player.flip(false);
        const QString path = directory + "/review.tfv";
        if (!player.saveProject(QUrl::fromLocalFile(path))) {
            QCoreApplication::exit(3);
            return;
        }
        player.openProject(QUrl::fromLocalFile(path));
        QTimer::singleShot(1500, &app, [&, directory, frameFieldsValid, bookmarksValid, saveNameValid] {
            QQmlListReference menus(window.findChild<QObject*>("appMenuBar"), "menus");
            const bool noSidebar = !window.findChild<QQuickItem*>("bookmarkPanel");
            auto* model = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
            const bool bookmarksRestored = model && model->rowCount() == 2 &&
                                           model->at(0)->name == "contact pose" && model->at(0)->frame == 40;
            const auto fieldValue = [&](const char* name) {
                auto* field = window.findChild<QQuickItem*>(name);
                return field ? field->property("text").toString() : QString{};
            };
            const bool fieldsRestored = fieldValue("playbackStartField") == "5" &&
                                        fieldValue("playbackEndField") == "37" &&
                                        fieldValue("currentFrameField") == "25";
            auto report = std::make_shared<QJsonObject>(QJsonObject{
                {"loaded", player.loaded()},
                {"projectSaveDefaultsToMediaName", saveNameValid},
                {"contextMenuSettingsAvailable",
                 window.findChild<QQuickItem*>("registerContextMenuButton") != nullptr},
                {"frame", player.currentFrame()},
                {"bookmarks", player.bookmarks()->property("count").toInt()},
                {"rotation", player.rotation()},
                {"flipH", player.flipH()},
                {"dirty", player.dirty()},
                {"error", player.error()},
                {"cacheMiB", player.cacheUsedMiB()},
                {"audioAvailable", player.audioAvailable()},
                {"bookmarkSidebarRemoved", noSidebar},
                {"bookmarkTooltipAndContextEditing", bookmarksValid},
                {"bookmarkEditsRestoreWithProject", bookmarksRestored},
                {"frameFieldsOnOneRow", frameFieldsOnOneRow(window)},
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
                player.flipH() && !player.dirty() && player.error().isEmpty() && noSidebar &&
                bookmarksValid && bookmarksRestored && saveNameValid && frameFieldsOnOneRow(window) &&
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

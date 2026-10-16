#include "AppSmoke.h"
#include "application/PlayerController.h"
#include "platform/ResourceMonitor.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QPointer>
#include <QQmlListReference>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

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

static bool timelineTransportSmoke(QQuickWindow& window, tfv::PlayerController& player,
                                   tfv::Preferences& preferences) {
    auto* timeline = window.findChild<QQuickItem*>("timeline");
    if (!timeline)
        return false;
    window.grabWindow();
    const auto pointFor = [&](int frame) {
        const double x =
            timeline->property("trackLeft").toDouble() +
            double(frame) / (player.frameCount() - 1) * timeline->property("trackWidth").toDouble();
        return timeline->mapToScene({x, timeline->height() / 2}).toPoint();
    };
    player.execute("pause");
    player.seek(24);
    player.execute("forward");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, pointFor(60));
    bool valid = player.playing() && player.currentFrame() == 60;
    QTest::qWait(200);
    valid = player.playing() && player.currentFrame() > 60 && valid;
    if (player.audioAvailable() && preferences.volume() > 0) {
        auto* audio = player.findChild<tfv::AudioEngine*>();
        valid = audio && audio->clockAvailable() && valid;
    }
    player.execute("reverse");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, pointFor(36));
    valid = player.playing() && player.direction() == -1 && player.currentFrame() == 36 && valid;
    QTest::qWait(150);
    valid = player.currentFrame() < 36 && valid;
    player.execute("forward");
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, pointFor(20));
    QTest::qWait(40);
    QTest::mouseMove(&window, pointFor(50));
    QTest::qWait(40);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, pointFor(50));
    const int releasedFrame = player.currentFrame();
    valid = player.playing() && releasedFrame >= 50 && valid;
    QTest::qWait(150);
    valid = player.playing() && player.currentFrame() > releasedFrame && valid;
    player.execute("pause");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, pointFor(15));
    QTest::qWait(100);
    valid = !player.playing() && player.currentFrame() == 15 && valid;
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, pointFor(20));
    QTest::mouseMove(&window, pointFor(30));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, pointFor(30));
    QTest::qWait(100);
    valid = !player.playing() && player.currentFrame() == 30 && valid;
    // Bookmark markers share the timeline's transport policy.
    auto* model = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
    if (model && model->rowCount()) {
        const auto bookmark = *model->at(0);
        auto* marker = visualItem(window.contentItem(), "bookmarkMarker_" + bookmark.id);
        player.execute("forward");
        clickItem(window, marker);
        valid = marker && player.playing() && player.currentFrame() == bookmark.frame && valid;
    } else
        valid = false;
    player.execute("pause");
    player.seek(24);
    return valid;
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

static bool alwaysOnTopSmoke(QQuickWindow& window, QJsonObject& report) {
    const auto geometry = window.geometry();
    const auto visibility = window.visibility();
    auto* command = window.findChild<QQuickItem*>("menu_alwaysOnTop");
    const auto cycle = [&] {
        const auto flags = window.flags();
        const auto state = window.visibility();
        const auto bounds = window.geometry();
        const WId id = window.winId();
#ifdef Q_OS_WIN
        const auto handle = reinterpret_cast<HWND>(id);
        const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
        if (state != QWindow::FullScreen && (style & WS_CAPTION) != WS_CAPTION)
            return false;
#endif
        bool valid = command != nullptr;
        for (const bool topmost : {true, false, true, false}) {
            QMetaObject::invokeMethod(&window, "dispatch", Q_ARG(QVariant, QVariant("alwaysOnTop")));
            QTest::qWait(70);
            valid = window.property("alwaysOnTop").toBool() == topmost && window.flags() == flags &&
                    window.visibility() == state && window.geometry() == bounds && window.winId() == id &&
                    command && command->property("checked").toBool() == topmost && valid;
#ifdef Q_OS_WIN
            valid = GetWindowLongPtrW(handle, GWL_STYLE) == style &&
                    bool(GetWindowLongPtrW(handle, GWL_EXSTYLE) & WS_EX_TOPMOST) == topmost && valid;
#endif
        }
        return valid;
    };
    const bool windowed = cycle();
    bool maximized = true;
    const bool exposed = window.isExposed();
    if (exposed) {
        window.showMaximized();
        QTest::qWait(200);
        maximized = window.visibility() == QWindow::Maximized && cycle();
        window.setVisibility(visibility);
        window.setGeometry(geometry);
        QTest::qWait(150);
    }
    QMetaObject::invokeMethod(&window, "setCinemaMode", Q_ARG(QVariant, QVariant(true)));
    QTest::qWait(150);
    const bool cinema = window.property("cinemaMode").toBool() && cycle();
    QMetaObject::invokeMethod(&window, "setCinemaMode", Q_ARG(QVariant, QVariant(false)));
    QTest::qWait(200);
    const bool restored = window.visibility() == visibility && window.geometry() == geometry;
#ifdef Q_OS_WIN
    const bool caption =
        (GetWindowLongPtrW(reinterpret_cast<HWND>(window.winId()), GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
#else
    const bool caption = true;
#endif
    report["topmostPreservesCaptionAndWindow"] = windowed && restored && caption;
    report["topmostMaximizedChecked"] = exposed;
    report["topmostPreservesMaximizedWindow"] = maximized;
    report["topmostPreservesCinemaMode"] = cinema;
    return windowed && maximized && cinema && restored && caption;
}

static bool exportUiSmoke(QQuickWindow& window, tfv::PlayerController& player, const QString& directory,
                          QJsonObject& report) {
    auto* save = window.findChild<QObject*>("exportClipDialog");
    auto* progress = window.findChild<QObject*>("exportProgressDialog");
    auto* command = window.findChild<QQuickItem*>("menu_exportRange");
    auto* exporter = qobject_cast<tfv::ClipExporter*>(player.clipExporter());
    if (!save || !progress || !command || !exporter)
        return false;
    clickItem(window, window.findChild<QQuickItem*>("menuTitle_Tools"));
    QTest::qWait(100);
    const bool commandAvailable = command->isVisible() && command->isEnabled();
    window.grabWindow().save(directory + "/tools-export-menu.png");
    QTest::keyClick(&window, Qt::Key_Escape);
    const bool prepared = QMetaObject::invokeMethod(save, "prepareForRange") &&
                          save->property("selectedFile").toUrl() == player.suggestedClipUrl();
    const int frame = player.currentFrame(), start = player.rangeStart(), end = player.rangeEnd();
    const bool dirty = player.dirty(), playing = player.playing();
    const double speed = player.speed();
    const auto target = QUrl::fromLocalFile(directory + "/ui-range-export.mp4");
    save->setProperty("selectedFile", target);
    QSignalSpy finished(exporter, &tfv::ClipExporter::finished);
    const bool accepted = QMetaObject::invokeMethod(save, "accepted");
    const bool progressShown = progress->property("visible").toBool() && exporter->busy();
    const bool completed = QTest::qWaitFor([&] { return !finished.isEmpty(); }, 15000) &&
                           finished.first()[0].toBool() && QFileInfo::exists(target.toLocalFile());
    QTest::qWait(80);
    window.grabWindow().save(directory + "/export-completed.png");
    auto* buttons = window.findChild<QQuickItem*>("exportDialogButtons");
    const auto popupLeft = progress->property("x").toDouble();
    const auto popupRight = popupLeft + progress->property("width").toDouble();
    bool buttonsContained = buttons != nullptr;
    if (buttons)
        for (auto* button : buttons->findChildren<QQuickItem*>("exportDialogAction")) {
            const auto left = button->mapToScene({0, 0}).x();
            buttonsContained =
                (!button->isVisible() || (left >= popupLeft && left + button->width() <= popupRight)) &&
                buttonsContained;
        }
    const bool reviewUnchanged = player.currentFrame() == frame && player.rangeStart() == start &&
                                 player.rangeEnd() == end && player.dirty() == dirty &&
                                 player.playing() == playing && player.speed() == speed;
    QMetaObject::invokeMethod(progress, "close");
    report["exportCommandInTools"] = commandAvailable;
    report["exportFilenameDefaultsToRange"] = prepared;
    report["exportProgressDialogShown"] = progressShown;
    report["exportClipSaved"] = completed;
    report["exportPreservesReviewState"] = reviewUnchanged;
    report["exportDialogButtonsContained"] = buttonsContained;
    return commandAvailable && prepared && accepted && progressShown && completed && reviewUnchanged &&
           buttonsContained;
}

static bool fullVideoExportUiSmoke(QQuickWindow& window, tfv::PlayerController& player,
                                   const QString& directory, QJsonObject& report) {
    auto* options = window.findChild<QObject*>("exportVideoOptionsDialog");
    auto* resolution = window.findChild<QObject*>("videoExportResolution");
    auto* save = window.findChild<QObject*>("exportClipDialog");
    auto* progress = window.findChild<QObject*>("exportProgressDialog");
    auto* command = window.findChild<QQuickItem*>("menu_exportVideo");
    auto* exporter = qobject_cast<tfv::ClipExporter*>(player.clipExporter());
    if (!options || !resolution || !save || !progress || !command || !exporter)
        return false;
    clickItem(window, window.findChild<QQuickItem*>("menuTitle_Tools"));
    QTest::qWait(100);
    const bool commandAvailable = command->isVisible() && command->isEnabled();
    clickItem(window, command);
    QTest::qWait(100);
    const bool defaultFullHd =
        options->property("visible").toBool() && options->property("resolutionId").toString() == "fullHd";
    window.grabWindow().save(directory + "/export-video-options.png");
    resolution->setProperty("currentIndex", 1);
    const bool selectable = options->property("resolutionId").toString() == "hd";
    QMetaObject::invokeMethod(options, "close");
    QMetaObject::invokeMethod(options, "openForVideo");
    const bool resetsToFullHd = options->property("resolutionId").toString() == "fullHd";
    QMetaObject::invokeMethod(options, "close");
    const bool prepared =
        QMetaObject::invokeMethod(save, "prepareForVideo", Q_ARG(QVariant, QVariant("fullHd"))) &&
        save->property("selectedFile").toUrl() == player.suggestedVideoExportUrl("fullHd") &&
        save->property("wholeVideo").toBool();
    const int frame = player.currentFrame(), start = player.rangeStart(), end = player.rangeEnd();
    const bool dirty = player.dirty();
    const auto target = QUrl::fromLocalFile(directory + "/ui-full-video-export.mp4");
    save->setProperty("selectedFile", target);
    QSignalSpy finished(exporter, &tfv::ClipExporter::finished);
    const bool accepted = QMetaObject::invokeMethod(save, "accepted");
    const bool completed =
        QTest::qWaitFor([&] { return !finished.isEmpty(); }, 30000) && finished.first()[0].toBool();
    tfv::Decoder output;
    QString error;
    const bool validOutput = completed && output.open(target.toLocalFile(), error) &&
                             output.info().width == 1920 && output.info().height == 1080 &&
                             output.info().frameCount == player.frameCount();
    const bool reviewUnchanged = player.currentFrame() == frame && player.rangeStart() == start &&
                                 player.rangeEnd() == end && player.dirty() == dirty;
    QMetaObject::invokeMethod(progress, "close");
    report["fullVideoExportInTools"] = commandAvailable;
    report["fullVideoDefaultFullHd"] = defaultFullHd && resetsToFullHd;
    report["fullVideoResolutionSelectable"] = selectable;
    report["fullVideoFilenamePrepared"] = prepared;
    report["fullVideoExportsAllFramesAtFullHd"] = validOutput;
    report["fullVideoPreservesReviewRange"] = reviewUnchanged;
    return commandAvailable && defaultFullHd && resetsToFullHd && selectable && prepared && accepted &&
           validOutput && reviewUnchanged;
}

static bool savePromptSmoke(QQuickWindow& window, tfv::PlayerController& player, const QString& directory) {
    auto* dialog = window.findChild<QObject*>("unsavedProjectDialog");
    auto* model = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
    if (!dialog || !model || model->rowCount())
        return false;
    const auto promptVisible = [&] {
        QMetaObject::invokeMethod(&window, "guarded", Q_ARG(QVariant, QVariant("testNoAction")));
        QCoreApplication::processEvents();
        const bool visible = dialog->property("visible").toBool();
        if (visible) {
            QMetaObject::invokeMethod(dialog, "reject");
            QTest::qWait(150);
        }
        return visible;
    };
    player.setSpeed(2);
    player.setZoom(1.1);
    bool valid = !player.dirty() && !promptVisible();
    player.addBookmark("#e6bf79");
    valid = player.dirty() && promptVisible() && valid;
    const QString id = model->at(0)->id;
    valid = player.saveProject(QUrl::fromLocalFile(directory + "/save-prompt.tfv")) && valid;
    player.setSpeed(0.5);
    valid = !player.dirty() && !promptVisible() && valid;
    model->rename(id, "Unsaved pose");
    valid = player.dirty() && promptVisible() && valid;
    model->remove(id);
    valid = !player.dirty() && !promptVisible() && valid;
    player.setSpeed(1);
    player.resetView();
    return valid;
}

static bool debugUiSmoke(QQuickWindow& window, tfv::Preferences& preferences, const QString& directory,
                         QJsonObject& report) {
    auto* monitor = qobject_cast<tfv::ResourceMonitor*>(window.property("resourceMonitor").value<QObject*>());
    auto* checkbox = window.findChild<QQuickItem*>("debugModeCheckBox");
    auto* overlay = visualItem(window.contentItem(), "debugMemoryOverlay");
    auto* dialog = window.findChild<QObject*>("preferencesDialog");
    if (!monitor || !checkbox || !overlay || !dialog)
        return false;
    preferences.setDebugMode(false);
    QCoreApplication::processEvents();
    bool valid = !monitor->enabled() && !overlay->isVisible();
    QSignalSpy samples(monitor, &tfv::ResourceMonitor::metricsChanged);
    clickItem(window, checkbox);
    // Hidden diagnostic windows need an explicit render to service the native
    // adapter fallback; ordinary exposed windows render on update requests.
    window.grabWindow();
    QTest::qWait(650);
    report["debugPreferenceEnabled"] = preferences.debugMode() && monitor->enabled();
    report["debugOverlayVisible"] = overlay->isVisible();
    report["debugSampleCount"] = samples.count();
    valid = preferences.debugMode() && monitor->enabled() && overlay->isVisible() && samples.count() >= 2 &&
            monitor->ramAvailable() && monitor->ramMiB() > 0 && valid;
    const bool direct3D = window.rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11;
    valid =
        (!direct3D || (monitor->gpuAvailable() && monitor->vramMiB() + monitor->sharedGpuMiB() > 0)) && valid;
    report["debugRamMiB"] = monitor->ramMiB();
    report["debugVramMiB"] = monitor->vramMiB();
    report["debugSharedGpuMiB"] = monitor->sharedGpuMiB();
    report["debugGpuAvailable"] = monitor->gpuAvailable();
    report["debugGpuAllAdapters"] = monitor->gpuAllAdapters();
    valid = window.grabWindow().save(directory + "/preferences-debug.png") && valid;
    QMetaObject::invokeMethod(dialog, "close");
    QTest::qWait(150);
    valid = window.grabWindow().save(directory + "/debug-memory.png") && valid;
    preferences.setTheme("light");
    QTest::qWait(150);
    window.resize(860, 540);
    QTest::qWait(100);
    valid = overlay->isVisible() && overlay->mapToScene({0, 0}).x() >= 0 &&
            window.grabWindow().save(directory + "/debug-memory-light.png") && valid;
    preferences.setDebugMode(false);
    QCoreApplication::processEvents();
    report["debugDisabledStopsAndHides"] = !monitor->enabled() && !overlay->isVisible();
    valid = !monitor->enabled() && !overlay->isVisible() && valid;
    preferences.setTheme("dark");
    window.resize(1160, 800);
    return valid;
}

static bool volumeUiSmoke(QQuickWindow& window, tfv::PlayerController& player, tfv::Preferences& preferences,
                          QJsonObject& report) {
    auto* slider = window.findChild<QQuickItem*>("volumeSlider");
    QPointer<QQuickItem> command = window.findChild<QQuickItem*>("menu_cinemaMode");
    auto* audio = player.findChild<tfv::AudioEngine*>();
    if (!slider || !slider->isEnabled() || !command || !audio)
        return false;
    const double originalVolume = preferences.volume();
    const double originalSpeed = player.speed();
    slider->setProperty("to", 0.04); // Keep the native playback test quiet.
    preferences.setVolume(0.02);
    player.setSpeed(1);
    player.seek(4); // Leave enough of the short fixture range to avoid a loop during this check.
    player.execute("forward");
    QTest::qWait(150);
    QSignalSpy positions(&player, &tfv::PlayerController::positionChanged);
    const auto point = [&](double fraction) {
        return slider->mapToScene({8 + fraction * (slider->width() - 16), slider->height() / 2}).toPoint();
    };
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, point(0.5));
    bool retained = true;
    qint64 maxHandlerUs = 0;
    for (int step = 0; step < 40; ++step) {
        QElapsedTimer handler;
        handler.start();
        QTest::mouseMove(&window, point((step % 11) / 10.), 0);
        maxHandlerUs = std::max(maxHandlerUs, handler.nsecsElapsed() / 1000);
        QTest::qWait(8);
        retained = retained && command && window.findChild<QQuickItem*>("menu_cinemaMode") == command;
    }
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, point(0.7));
    const bool changed = preferences.volume() > 0.025 && preferences.volume() < 0.035;
    const double beforeMute = audio->seconds();
    preferences.setVolume(0);
    QTest::qWait(40);
    preferences.setVolume(0.02);
    QTest::qWait(40);
    const bool continuous = audio->clockAvailable() && audio->seconds() >= beforeMute;
    report["volumeAudioSecondsBeforeMute"] = beforeMute;
    report["volumeAudioSecondsAfterUnmute"] = audio->seconds();
    report["volumeAudioActiveAfterUnmute"] = audio->clockAvailable();
    const bool advanced = player.playing() && positions.count() > 2;
    report["volumeDragMaxHandlerMs"] = maxHandlerUs / 1000.;
    report["volumeDragRetainsMenuObjects"] = retained;
    report["volumeDragUpdatesGain"] = changed;
    report["volumeDragPlaybackAdvances"] = advanced;
    report["muteKeepsAudioClock"] = continuous;
    player.execute("pause");
    player.setSpeed(originalSpeed);
    player.seek(24);
    slider->setProperty("to", 1);
    preferences.setVolume(originalVolume);
    return retained && changed && continuous && advanced;
}

static bool cinemaUiSmoke(QQuickWindow& window, tfv::PlayerController& player, const QString& directory,
                          QJsonObject& report) {
    auto* stage = visualItem(window.contentItem(), "reviewStage");
    auto* viewer = visualItem(window.contentItem(), "mediaViewer");
    auto* panel = visualItem(window.contentItem(), "playbackPanel");
    auto* timeline = visualItem(window.contentItem(), "timeline");
    auto* transport = visualItem(window.contentItem(), "playbackBar");
    auto* surface = visualItem(window.contentItem(), "videoSurface");
    auto* menuBar = window.findChild<QQuickItem*>("appMenuBar");
    auto* command = window.findChild<QQuickItem*>("menu_cinemaMode");
    auto* loopMenu = window.findChild<QObject*>("playbackLoopMenu");
    auto* play = window.findChild<QQuickItem*>("togglePlaybackButton");
    if (!stage || !viewer || !panel || !timeline || !transport || !surface || !menuBar || !command ||
        !loopMenu || !play)
        return false;
    const QRect originalGeometry = window.geometry();
    const auto originalVisibility = window.visibility();
    const double originalSpeed = player.speed();
    QStringList visibilityTrace;
    const auto visibilityConnection =
        QObject::connect(&window, &QWindow::visibilityChanged, &window, [&](QWindow::Visibility visibility) {
            visibilityTrace.append(QString("%1 cinema=%2 restore=%3 changing=%4")
                                       .arg(int(visibility))
                                       .arg(window.property("cinemaMode").toBool())
                                       .arg(window.property("cinemaRestoreVisibility").toInt())
                                       .arg(window.property("changingCinemaVisibility").toBool()));
        });
    const auto cinema = [&] { return window.property("cinemaMode").toBool(); };
    const auto controls = [&] {
        return panel->isVisible() && panel->isEnabled() && timeline->isVisible() && transport->isVisible();
    };
    const auto controlsBelowVideo = [&] {
        return panel->height() > 0 && viewer->height() > 0 && viewer->y() + viewer->height() == panel->y() &&
               panel->y() + panel->height() == stage->height();
    };
    const auto videoBounds = [&] { return QRectF(surface->mapToScene({0, 0}), surface->size()); };
    const auto describeBounds = [&](const QRectF& bounds) {
        return QString("%1,%2 %3x%4")
            .arg(bounds.x())
            .arg(bounds.y())
            .arg(bounds.width())
            .arg(bounds.height());
    };
    const auto hiddenItem = [&](const char* name) {
        auto* item = visualItem(window.contentItem(), name);
        return item && !item->isVisible();
    };
    const auto moveOverVideo = [&](int offset) {
        QTest::mouseMove(&window,
                         viewer->mapToScene({viewer->width() / 2 + offset, viewer->height() / 2}).toPoint());
    };
    player.setSpeed(1.5);
    player.execute("forward");
    QSignalSpy positions(&player, &tfv::PlayerController::positionChanged);
    clickItem(window, window.findChild<QQuickItem*>("menuTitle_Playback"));
    QTest::qWait(120);
    bool valid = command->isVisible() && command->property("checkable").toBool() &&
                 !command->property("checked").toBool();
    valid = window.grabWindow().save(directory + "/cinema-menu.png") && valid;
    clickItem(window, command);
    window.requestActivate();
    QTest::qWait(300);
    window.grabWindow();
    const QRectF fullscreenBounds = videoBounds();
    report["cinemaEntered"] = cinema();
    report["cinemaVideoBounds"] = describeBounds(fullscreenBounds);
    valid = cinema() && window.visibility() == QWindow::FullScreen && !menuBar->isVisible() && controls() &&
            controlsBelowVideo() && stage->mapToScene({0, 0}).y() == 0 && hiddenItem("viewerTools") &&
            hiddenItem("viewerZoom") && hiddenItem("debugMemoryOverlay") && valid;
    // Wait beyond the former auto-hide delay with the pointer outside the controls.
    moveOverVideo(0);
    QTest::qWait(2300);
    const bool controlsStayVisible = cinema() && controls() && controlsBelowVideo() && player.playing() &&
                                     player.speed() == 1.5 && positions.count() > 1;
    valid =
        controlsStayVisible && window.grabWindow().save(directory + "/cinema-fixed-controls.png") && valid;
    moveOverVideo(17);
    QTest::qWait(100);
    window.grabWindow();
    const bool stableLayout = controls() && controlsBelowVideo() && videoBounds() == fullscreenBounds;
    valid = stableLayout && valid;
    QTest::mouseMove(&window, play->mapToScene({play->width() / 2, play->height() / 2}).toPoint());
    clickItem(window, play);
    QTest::qWait(2300);
    const bool pausedControlsStayVisible = !player.playing() && controls() && controlsBelowVideo();
    valid = pausedControlsStayVisible && valid;
    QTest::keyClick(&window, Qt::Key_Space);
    valid = player.playing() && valid;
    clickItem(window, window.findChild<QQuickItem*>("playbackLoopButton"));
    moveOverVideo(31);
    QTest::qWait(120);
    const bool popupKeepsControls = loopMenu->property("visible").toBool() && controls();
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::qWait(120);
    valid = popupKeepsControls && cinema() && !loopMenu->property("visible").toBool() && valid;
    moveOverVideo(43);
    valid = controls() && controlsBelowVideo() && videoBounds() == fullscreenBounds && valid;
    QTest::keyClick(&window, Qt::Key_F11);
    QTest::qWait(200);
    const bool restored = !cinema() && window.visibility() == originalVisibility &&
                          window.size() == originalGeometry.size() && menuBar->isVisible() && controls();
    valid = restored && valid;
    player.execute("pause");
    if (auto* field = window.findChild<QQuickItem*>("currentFrameField"))
        field->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_F11);
    QTest::qWait(150);
    report["cinemaEnteredWhileFrameFieldFocused"] = cinema();
    valid = cinema() && !player.playing() && valid;
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::qWait(150);
    report["cinemaEscapeRestoredWindow"] = !cinema();
    valid = !cinema() && !player.playing() && valid;
    const bool checkMaximized = window.isExposed();
    bool restoredMaximized = false;
    if (checkMaximized) {
        window.setVisibility(QWindow::Maximized);
        QTest::qWait(150);
        report["cinemaVisibilityBeforeMaximizedEntry"] = int(window.visibility());
        QTest::keyClick(&window, Qt::Key_F11);
        QTest::qWait(150);
        report["cinemaEnteredFromMaximized"] = cinema();
        report["cinemaCapturedRestoreVisibility"] = window.property("cinemaRestoreVisibility").toInt();
        valid = cinema() && valid;
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::qWait(150);
        report["cinemaVisibilityAfterMaximizedExit"] = int(window.visibility());
        visibilityTrace.append("MAX_CHECK " + QString::number(window.visibility()));
        report["cinemaActiveAfterMaximizedExit"] = cinema();
        restoredMaximized = !cinema() && window.visibility() == QWindow::Maximized;
        valid = restoredMaximized && valid;
    }
    window.setVisibility(originalVisibility);
    window.setGeometry(originalGeometry);
    player.setSpeed(originalSpeed);
    player.seek(24);
    QObject::disconnect(visibilityConnection);
    report["cinemaVisibilityTrace"] = visibilityTrace.join("; ");
    report["cinemaControlsAlwaysVisibleDuringPlayback"] = controlsStayVisible;
    report["cinemaControlsAlwaysVisibleWhilePaused"] = pausedControlsStayVisible;
    report["cinemaControlsBelowVideoAndStableOnMouseMove"] = stableLayout;
    report["cinemaControlsStayForPopups"] = popupKeepsControls;
    report["cinemaRestoresWindow"] = restored;
    report["cinemaMaximizedRestorationChecked"] = checkMaximized;
    report["cinemaRestoresMaximizedWindow"] = checkMaximized ? QJsonValue(restoredMaximized) : QJsonValue();
    return valid;
}

static void finishUiSmoke(QGuiApplication& app, QQuickWindow& window, tfv::Preferences& preferences,
                          const QString& directory, std::shared_ptr<QJsonObject> report,
                          std::shared_ptr<bool> valid) {
    window.resize(1160, 800);
    preferences.setTheme("dark");
    QTimer::singleShot(300, &app, [&, directory, report, valid] {
        *valid = window.grabWindow().save(directory + "/review-final.png") && *valid;
        auto* player = qobject_cast<tfv::PlayerController*>(window.property("player").value<QObject*>());
        const bool topmostValid = alwaysOnTopSmoke(window, *report);
        const bool exportValid = player && exportUiSmoke(window, *player, directory, *report);
        const bool fullVideoValid = player && fullVideoExportUiSmoke(window, *player, directory, *report);
        *valid = topmostValid && exportValid && fullVideoValid && *valid;
        const bool volumeValid = player && volumeUiSmoke(window, *player, preferences, *report);
        (*report)["volumeDragResponsive"] = volumeValid;
        *valid = volumeValid && *valid;
        const bool cinemaValid = player && cinemaUiSmoke(window, *player, directory, *report);
        (*report)["cinemaMode"] = cinemaValid;
        *valid = cinemaValid && *valid;
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
                const bool debugValid = debugUiSmoke(window, preferences, directory, *report);
                (*report)["debugModeShowsLiveMemory"] = debugValid;
                *valid = debugValid && *valid;
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
        if (qEnvironmentVariable("TFV_SMOKE_FOCUS") == "always-on-top") {
            window.requestActivate();
            const bool exposed = QTest::qWaitForWindowExposed(&window, 3000);
            QJsonObject report;
            const bool valid = alwaysOnTopSmoke(window, report);
            report["exposedWindow"] = exposed;
            window.grabWindow().save(directory + "/always-on-top-restored.png");
            QFile output(directory + "/always-on-top-report.json");
            output.open(QIODevice::WriteOnly);
            output.write(QJsonDocument(report).toJson());
            QCoreApplication::exit(valid && exposed ? 0 : 4);
            return;
        }
        if (qEnvironmentVariable("TFV_SMOKE_FOCUS") == "cinema-window-state") {
            window.requestActivate();
            window.setVisibility(QWindow::Maximized);
            const bool exposed = QTest::qWaitForWindowExposed(&window, 3000);
            QTest::qWait(150);
            QMetaObject::invokeMethod(&window, "setCinemaMode", Q_ARG(QVariant, QVariant(true)));
            QTest::qWait(200);
            const bool entered =
                window.property("cinemaMode").toBool() && window.visibility() == QWindow::FullScreen;
            QMetaObject::invokeMethod(&window, "setCinemaMode", Q_ARG(QVariant, QVariant(false)));
            QTest::qWait(400);
            const bool restored =
                !window.property("cinemaMode").toBool() && window.visibility() == QWindow::Maximized;
            const QJsonObject report{
                {"exposedWindow", exposed}, {"enteredFullscreen", entered}, {"restoredMaximized", restored}};
            window.grabWindow().save(directory + "/cinema-restored-maximized.png");
            QFile output(directory + "/cinema-window-state-report.json");
            output.open(QIODevice::WriteOnly);
            output.write(QJsonDocument(report).toJson());
            QCoreApplication::exit(exposed && entered && restored ? 0 : 4);
            return;
        }
        auto* saveDialog = window.findChild<QObject*>("saveProjectDialog");
        const bool savePrepared = saveDialog && QMetaObject::invokeMethod(saveDialog, "prepareForMedia");
        const bool saveNameValid =
            savePrepared && saveDialog->property("selectedFile").toUrl() == player.suggestedProjectUrl() &&
            saveDialog->property("currentFolder").toUrl().isParentOf(player.suggestedProjectUrl());
        const bool savePromptValid = savePromptSmoke(window, player, directory);
        const bool frameFieldsValid = editFrameFields(window, player);
        player.seek(24);
        player.addBookmark("#e6bf79");
        player.seek(4);
        player.addBookmark("#a8d5c2");
        const bool bookmarksValid = bookmarkUiSmoke(window, player, directory);
        const bool timelineTransportValid = timelineTransportSmoke(window, player, preferences);
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
        QTimer::singleShot(
            1500, &app,
            [&, directory, frameFieldsValid, bookmarksValid, saveNameValid, savePromptValid,
             timelineTransportValid] {
                QQmlListReference menus(window.findChild<QObject*>("appMenuBar"), "menus");
                const bool noSidebar = !window.findChild<QQuickItem*>("bookmarkPanel");
                auto* model = qobject_cast<tfv::BookmarkModel*>(player.bookmarks());
                const bool bookmarksRestored = model && model->rowCount() == 2 &&
                                               model->at(0)->name == "contact pose" &&
                                               model->at(0)->frame == 40;
                const auto fieldValue = [&](const char* name) {
                    auto* field = window.findChild<QQuickItem*>(name);
                    return field ? field->property("text").toString() : QString{};
                };
                const bool fieldsRestored = fieldValue("playbackStartField") == "5" &&
                                            fieldValue("playbackEndField") == "37" &&
                                            fieldValue("currentFrameField") == "25";
                auto report = std::make_shared<QJsonObject>(
                    QJsonObject{{"loaded", player.loaded()},
                                {"timelinePreservesPlaybackState", timelineTransportValid},
                                {"projectSaveDefaultsToMediaName", saveNameValid},
                                {"savePromptRequiresBookmarksAndIgnoresSpeed", savePromptValid},
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
                                 window.rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11
                                     ? "Direct3D11"
                                     : "Other"}});
                auto valid = std::make_shared<bool>(
                    player.loaded() && player.currentFrame() == 24 &&
                    player.bookmarks()->property("count").toInt() == 2 && player.rotation() == 90 &&
                    player.flipH() && !player.dirty() && player.error().isEmpty() && noSidebar &&
                    bookmarksValid && bookmarksRestored && saveNameValid && savePromptValid &&
                    timelineTransportValid && frameFieldsOnOneRow(window) && centeredButtons(window) &&
                    frameFieldsValid && fieldsRestored && menus.count() == 8 &&
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
    QTimer::singleShot(45000, &app, [] { QCoreApplication::exit(5); });
}

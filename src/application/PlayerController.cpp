#include "PlayerController.h"
#include "domain/ProjectDocument.h"
#include <QFileInfo>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace tfv {
PlayerController::PlayerController(Preferences& preferences, QObject* parent)
    : QObject(parent), m_preferences(preferences), m_bookmarks(this), m_worker(this), m_audio(this) {
    qRegisterMetaType<MediaInfo>();
    qRegisterMetaType<VideoFrame>();
    m_lastVolume = m_preferences.volume();
    applyDecodePreferences();
    m_scrubRequests.setSingleShot(true);
    m_scrubRequests.setTimerType(Qt::PreciseTimer);
    m_scrubRequests.setInterval(m_preferences.integer("scrubIntervalMs"));
    connect(&m_scrubRequests, &QTimer::timeout, this, &PlayerController::finishScrub);
    m_timer.setInterval(4);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &PlayerController::tick);
    m_autosave.setInterval(30000);
    connect(&m_autosave, &QTimer::timeout, this, [this] {
        if (m_dirty && !m_projectPath.isEmpty()) {
            QString error;
            ProjectDocument::save(m_projectPath + ".autosave", m_mediaPath, state(), error);
        }
    });
    m_autosave.start();
    connect(&m_bookmarks, &BookmarkModel::edited, this, &PlayerController::markDirty);
    connect(&m_preferences, &Preferences::changed, this, [this] {
        m_worker.setBudget(qint64(m_preferences.cacheMiB()) * 1048576);
        applyDecodePreferences();
        m_audio.setVolume(m_preferences.volume());
        if (m_playing && m_lastVolume <= 0 && m_preferences.volume() > 0)
            resetClock();
        m_lastVolume = m_preferences.volume();
    });
    connect(&m_worker, &MediaWorker::opened, this, [this](quint64 gen, MediaInfo info) {
        if (gen != m_generation)
            return;
        m_info = info;
        m_loaded = true;
        m_loading = false;
        m_in = 0;
        m_out = info.frameCount - 1;
        m_status = "Footage loaded";
        if (!m_pendingState.isEmpty()) {
            const auto saved = m_pendingState;
            m_pendingState = {};
            if (restore(saved)) {
                m_projectPath = m_pendingProjectPath;
                m_dirty = false;
                m_status = "Project restored";
            }
        }
        emit mediaChanged();
        emit playbackChanged();
        emit statusChanged();
        emit projectChanged();
    });
    connect(&m_worker, &MediaWorker::decoded, this,
            [this](quint64 gen, quint64 epoch, int index, VideoFrame image) {
                if (gen != m_generation || epoch != m_transportEpoch)
                    return;
                if (m_playing) {
                    const bool advances = m_direction > 0 ? index >= m_frame && index <= m_requestedFrame
                                                          : index <= m_frame && index >= m_requestedFrame;
                    if (!advances)
                        return;
                    if (m_frame != index) {
                        m_frame = index;
                        emit positionChanged();
                    }
                } else if (index != m_requestedFrame)
                    return;
                m_presentedFrame = index;
                emit frameReady(image);
            });
    connect(&m_worker, &MediaWorker::cacheUpdated, this,
            [this](quint64 gen, qint64 bytes, QVariantList ranges) {
                if (gen != m_generation)
                    return;
                m_cacheBytes = bytes;
                m_ranges = std::move(ranges);
                emit cacheChanged();
            });
    connect(&m_worker, &MediaWorker::audioReady, this, [this](quint64 gen, QByteArray pcm) {
        if (gen != m_generation)
            return;
        m_audio.load(std::move(pcm));
        emit mediaChanged();
        if (m_playing)
            restartAudio();
    });
    connect(&m_worker, &MediaWorker::failed, this, [this](quint64 gen, const QString& text) {
        if (gen != m_generation)
            return;
        m_loading = false;
        m_loaded = false;
        fail(text);
        emit mediaChanged();
    });
    connect(&m_worker, &MediaWorker::warning, this, [this](quint64 gen, const QString& text) {
        if (gen == m_generation)
            setStatus(text);
    });
}
QString PlayerController::fileName() const {
    return QFileInfo(m_mediaPath).fileName();
}
void PlayerController::applyDecodePreferences() {
    const QString mode = m_preferences.decodeMode();
    m_worker.setDecodeOptions(m_preferences.decoderThreads(), mode != "software",
                              mode == "auto" ? m_preferences.integer("hardwareMinimumPixels") : 0,
                              m_preferences.integer("prefetchDelayMs"));
}
QString PlayerController::mediaDescription() const {
    return QString("%1 × %2  ·  %3 fps  ·  %4")
        .arg(m_info.width)
        .arg(m_info.height)
        .arg(m_info.fps, 0, 'f', m_info.fps == int(m_info.fps) ? 0 : 2)
        .arg(m_info.codec);
}
void PlayerController::setStatus(const QString& text) {
    m_status = text;
    emit statusChanged();
}
void PlayerController::fail(const QString& text) {
    m_error = text;
    m_status = text;
    emit statusChanged();
}
void PlayerController::clearError() {
    m_error.clear();
    emit statusChanged();
}
void PlayerController::markDirty() {
    if (m_dirty || !m_loaded)
        return;
    m_dirty = true;
    emit projectChanged();
}
void PlayerController::openMedia(const QUrl& url) {
    m_pendingState = {};
    m_pendingProjectPath.clear();
    open(url.toLocalFile(), false);
}
void PlayerController::open(const QString& path, bool project) {
    if (path.isEmpty())
        return;
    pause();
    m_audio.load({});
    ++m_generation;
    m_transportEpoch = 0;
    m_requestedFrame = 0;
    m_presentedFrame = -1;
    m_info = {};
    m_loaded = false;
    m_loading = true;
    m_frame = 0;
    m_mediaPath = path;
    m_projectPath.clear();
    m_bookmarks.clear();
    m_dirty = false;
    m_cacheBytes = 0;
    m_ranges.clear();
    m_zoom = 1;
    m_panX = m_panY = 0;
    m_rotation = 0;
    m_flipH = m_flipV = false;
    m_in = m_out = 0;
    m_audioOffset = 0;
    m_speed = 1;
    m_error.clear();
    m_status = project ? "Restoring project…" : "Opening footage…";
    emit frameReady({});
    emit mediaChanged();
    emit positionChanged();
    emit viewChanged();
    emit playbackChanged();
    emit projectChanged();
    emit cacheChanged();
    emit statusChanged();
    m_worker.open(path, m_generation, qint64(m_preferences.cacheMiB()) * 1048576,
                  qint64(m_preferences.integer("audioCacheMiB")) * 1048576);
}
void PlayerController::openProject(const QUrl& url) {
    QString path, error;
    QJsonObject saved;
    if (!ProjectDocument::load(url.toLocalFile(), saved, path, error)) {
        fail(error);
        return;
    }
    // Validate bookmarks before replacing a currently open document.
    BookmarkModel validator;
    QString validation;
    if (!saved.value("bookmarks").isArray() ||
        !validator.load(saved.value("bookmarks").toArray(), std::numeric_limits<int>::max(), validation)) {
        fail(validation.isEmpty() ? "Project has no bookmark array." : validation);
        return;
    }
    m_pendingState = saved;
    m_pendingProjectPath = url.toLocalFile();
    if (!QFileInfo::exists(path)) {
        emit relinkRequired(path);
        return;
    }
    const auto identity = saved.value("media").toObject().value("identity").toObject();
    if (!identity.isEmpty() && identity != ProjectDocument::identity(path)) {
        fail("The referenced footage has changed. Relink the original footage to preserve bookmark "
             "positions.");
        emit relinkRequired(path);
        return;
    }
    open(path, true);
}
void PlayerController::relinkMedia(const QUrl& url) {
    if (m_pendingState.isEmpty())
        return;
    const auto expected = m_pendingState.value("media").toObject().value("identity").toObject();
    if (!expected.isEmpty() && expected != ProjectDocument::identity(url.toLocalFile())) {
        fail("This file differs from the project's original footage. Select the matching source.");
        return;
    }
    open(url.toLocalFile(), true);
}
bool PlayerController::saveProject(const QUrl& url) {
    if (!m_loaded) {
        fail("Open footage before saving a project.");
        return false;
    }
    QString path = url.isEmpty() ? m_projectPath : url.toLocalFile();
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".tfv", Qt::CaseInsensitive))
        path += ".tfv";
    QString error;
    if (!ProjectDocument::save(path, m_mediaPath, state(), error)) {
        fail(error);
        return false;
    }
    m_projectPath = path;
    m_dirty = false;
    m_status = "Project saved";
    emit projectChanged();
    emit statusChanged();
    return true;
}
QJsonObject PlayerController::state() const {
    return {{"bookmarks", m_bookmarks.toJson()},
            {"frame", m_frame},
            {"range", QJsonObject{{"start", m_in}, {"endInclusive", m_out}}},
            {"playback", QJsonObject{{"speed", m_speed}, {"loop", m_loop}, {"audioOffsetMs", m_audioOffset}}},
            {"view", QJsonObject{{"zoom", m_zoom},
                                 {"panX", m_panX},
                                 {"panY", m_panY},
                                 {"rotation", m_rotation},
                                 {"flipHorizontal", m_flipH},
                                 {"flipVertical", m_flipV}}}};
}
bool PlayerController::restore(const QJsonObject& obj) {
    QString error;
    if (!m_bookmarks.load(obj.value("bookmarks").toArray(), m_info.frameCount, error)) {
        fail(error);
        return false;
    }
    const auto range = obj.value("range").toObject(), playback = obj.value("playback").toObject(),
               view = obj.value("view").toObject();
    m_in = std::clamp(range.value("start").toInt(), 0, m_info.frameCount - 1);
    m_out = std::clamp(range.value("endInclusive").toInt(m_info.frameCount - 1), m_in, m_info.frameCount - 1);
    m_speed = std::clamp(playback.value("speed").toDouble(1), 0.25, 4.);
    const auto loop = playback.value("loop").toString("loop");
    m_loop = QStringList{"loop", "once", "pingpong"}.contains(loop) ? loop : "loop";
    m_audioOffset = std::clamp(playback.value("audioOffsetMs").toInt(), -10000, 10000);
    m_zoom = std::clamp(view.value("zoom").toDouble(1), 0.1, 16.);
    m_panX = std::clamp(view.value("panX").toDouble(), -100000., 100000.);
    m_panY = std::clamp(view.value("panY").toDouble(), -100000., 100000.);
    m_rotation = (view.value("rotation").toInt() % 360 + 360) % 360;
    m_flipH = view.value("flipHorizontal").toBool();
    m_flipV = view.value("flipVertical").toBool();
    seek(obj.value("frame").toInt());
    emit viewChanged();
    return true;
}
void PlayerController::pause() {
    const bool wasPlaying = m_playing;
    m_playing = false;
    m_timer.stop();
    m_audio.stop();
    if (wasPlaying) {
        ++m_transportEpoch;
        requestFrame(m_frame);
    }
    emit playbackChanged();
}
void PlayerController::restartAudio() {
    m_audio.stop();
    if (m_playing && m_direction > 0)
        m_audio.play(double(m_frame) / m_info.fps - m_audioOffset / 1000., m_speed, m_preferences.volume());
}
void PlayerController::resetClock() {
    m_anchor = m_frame;
    m_requestedFrame = m_frame;
    m_clock.restart();
    restartAudio();
}
void PlayerController::play(int direction) {
    if (!m_loaded)
        return;
    if (m_playing)
        pause();
    m_scrubRequests.stop();
    m_direction = direction;
    if (m_frame < m_in || m_frame > m_out || (direction > 0 && m_frame == m_out) ||
        (direction < 0 && m_frame == m_in))
        seek(direction > 0 ? m_in : m_out);
    m_playing = true;
    ++m_transportEpoch;
    requestFrame(m_frame);
    resetClock();
    m_timer.start();
    emit playbackChanged();
}
void PlayerController::tick() {
    if (!m_loaded || !m_playing)
        return;
    const double elapsed = m_clock.elapsed() / 1000.;
    int next = m_anchor + int(std::floor(elapsed * m_info.fps * m_speed)) * m_direction;
    if (m_direction > 0 && m_audio.clockAvailable())
        next = int(std::floor((m_audio.seconds() + m_audioOffset / 1000.) * m_info.fps));
    // An output underrun or a change of clock must not send playback backwards.
    next = m_direction > 0 ? std::max(next, m_requestedFrame) : std::min(next, m_requestedFrame);
    if (next > m_out || next < m_in) {
        if (m_loop == "once") {
            pause();
            seek(m_direction > 0 ? m_out : m_in);
            return;
        }
        if (m_loop == "pingpong")
            m_direction = -m_direction;
        next = m_direction > 0 ? m_in : m_out;
        seek(next);
        emit playbackChanged();
        return;
    }
    if (next != m_requestedFrame)
        requestFrame(next);
}
void PlayerController::requestFrame(int frame) {
    m_requestedFrame = frame;
    m_worker.seek(frame, m_transportEpoch, m_playing ? m_direction : m_scrubDirection);
}
void PlayerController::seek(int frame, bool withAudio) {
    if (!m_loaded)
        return;
    if (withAudio && m_playing)
        pause();
    const int value = std::clamp(frame, 0, m_info.frameCount - 1);
    if (withAudio && !m_playing && value == m_requestedFrame && value == m_presentedFrame)
        return;
    if (value != m_requestedFrame)
        m_scrubDirection = value > m_requestedFrame ? 1 : -1;
    if (value != m_frame) {
        m_frame = value;
        emit positionChanged();
    }
    if (withAudio) {
        m_requestedFrame = value;
        if (!m_scrubRequests.isActive())
            m_scrubRequests.start();
        return;
    }
    m_scrubRequests.stop();
    ++m_transportEpoch;
    requestFrame(value);
    if (m_playing)
        resetClock();
}
void PlayerController::finishScrub() {
    if (!m_loaded || m_playing)
        return;
    m_scrubRequests.stop();
    ++m_transportEpoch;
    requestFrame(m_frame);
    m_audio.scrub(double(m_frame) / m_info.fps - m_audioOffset / 1000.,
                  m_preferences.integer("scrubMilliseconds"), m_preferences.volume());
}
void PlayerController::setSpeed(double value) {
    if (!std::isfinite(value))
        return;
    m_speed = std::clamp(value, 0.25, 4.);
    if (m_playing)
        resetClock();
    markDirty();
    emit playbackChanged();
}
void PlayerController::setLoopMode(const QString& mode) {
    if (!QStringList{"once", "loop", "pingpong"}.contains(mode))
        return;
    m_loop = mode;
    markDirty();
    emit playbackChanged();
}
void PlayerController::setAudioOffsetMs(int value) {
    m_audioOffset = std::clamp(value, -10000, 10000);
    if (m_playing)
        resetClock();
    markDirty();
    emit playbackChanged();
}
void PlayerController::setRange(int start, int end) {
    if (!m_loaded)
        return;
    m_in = std::clamp(start, 0, m_info.frameCount - 1);
    m_out = std::clamp(end, m_in, m_info.frameCount - 1);
    if (m_playing) {
        seek(std::clamp(m_frame, m_in, m_out));
    }
    markDirty();
    emit playbackChanged();
}
void PlayerController::resetRange() {
    setRange(0, m_info.frameCount - 1);
}
void PlayerController::activateBookmark(const QString& id) {
    const auto* b = m_bookmarks.find(id);
    if (!b)
        return;
    pause();
    seek(b->frame, true);
}
void PlayerController::addBookmark(const QString& color) {
    if (m_loaded)
        m_bookmarks.add(m_frame, color);
}
bool PlayerController::setBookmarkFrame(const QString& id, int frame) {
    return m_loaded && m_bookmarks.setFrame(id, frame, m_info.frameCount);
}
void PlayerController::setZoom(double value) {
    if (!std::isfinite(value))
        return;
    m_zoom = std::clamp(value, 0.1, 16.);
    markDirty();
    emit viewChanged();
}
void PlayerController::pan(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return;
    m_panX += x;
    m_panY += y;
    markDirty();
    emit viewChanged();
}
void PlayerController::rotate(int delta) {
    m_rotation = (m_rotation + delta + 360) % 360;
    markDirty();
    emit viewChanged();
}
void PlayerController::flip(bool vertical) {
    if (vertical)
        m_flipV = !m_flipV;
    else
        m_flipH = !m_flipH;
    markDirty();
    emit viewChanged();
}
void PlayerController::resetView() {
    m_zoom = 1;
    m_panX = m_panY = 0;
    m_rotation = 0;
    m_flipH = m_flipV = false;
    markDirty();
    emit viewChanged();
}
double PlayerController::scrubSensitivity(bool fine, double distance) const {
    return m_preferences.integer(fine ? "fineScrubPixelsPerFrame" : "scrubPixelsPerFrame") *
           (1 + std::abs(distance) / 80.);
}
void PlayerController::execute(const QString& action) {
    if (action == "playPause") {
        if (m_playing)
            pause();
        else
            play(m_direction);
    } else if (action == "pause")
        pause();
    else if (action == "reverse")
        play(-1);
    else if (action == "forward")
        play(1);
    else if (action == "previousFrame")
        seek(m_frame - 1, true);
    else if (action == "nextFrame")
        seek(m_frame + 1, true);
    else if (action == "rangeStart")
        setRange(m_frame, std::max(m_frame, m_out));
    else if (action == "rangeEnd")
        setRange(std::min(m_in, m_frame), m_frame);
    else if (action == "previousBookmark")
        seek(m_bookmarks.adjacentFrame(m_frame, false), true);
    else if (action == "nextBookmark")
        seek(m_bookmarks.adjacentFrame(m_frame, true), true);
    else if (action == "resetView")
        resetView();
    else if (action == "flipHorizontal")
        flip(false);
    else if (action == "rotate")
        rotate(90);
}
} // namespace tfv

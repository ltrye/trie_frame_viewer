#pragma once
#include "Preferences.h"
#include "domain/BookmarkModel.h"
#include "media/AudioEngine.h"
#include "media/MediaWorker.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>

namespace tfv {
class PlayerController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY mediaChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY mediaChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY mediaChanged)
    Q_PROPERTY(QUrl suggestedProjectUrl READ suggestedProjectUrl NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaDescription READ mediaDescription NOTIFY mediaChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY mediaChanged)
    Q_PROPERTY(double fps READ fps NOTIFY mediaChanged)
    Q_PROPERTY(int mediaWidth READ mediaWidth NOTIFY mediaChanged)
    Q_PROPERTY(int mediaHeight READ mediaHeight NOTIFY mediaChanged)
    Q_PROPERTY(int currentFrame READ currentFrame NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(int direction READ direction NOTIFY playbackChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY playbackChanged)
    Q_PROPERTY(int rangeStart READ rangeStart NOTIFY playbackChanged)
    Q_PROPERTY(int rangeEnd READ rangeEnd NOTIFY playbackChanged)
    Q_PROPERTY(QString loopMode READ loopMode WRITE setLoopMode NOTIFY playbackChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)
    Q_PROPERTY(double cacheUsedMiB READ cacheUsedMiB NOTIFY cacheChanged)
    Q_PROPERTY(QVariantList cachedRanges READ cachedRanges NOTIFY cacheChanged)
    Q_PROPERTY(QObject* bookmarks READ bookmarks CONSTANT)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(double panX READ panX NOTIFY viewChanged)
    Q_PROPERTY(double panY READ panY NOTIFY viewChanged)
    Q_PROPERTY(int rotation READ rotation NOTIFY viewChanged)
    Q_PROPERTY(bool flipH READ flipH NOTIFY viewChanged)
    Q_PROPERTY(bool flipV READ flipV NOTIFY viewChanged)
    Q_PROPERTY(int audioOffsetMs READ audioOffsetMs WRITE setAudioOffsetMs NOTIFY playbackChanged)
    Q_PROPERTY(bool audioAvailable READ audioAvailable NOTIFY mediaChanged)
    Q_PROPERTY(QString decodeBackend READ decodeBackend NOTIFY mediaChanged)
  public:
    explicit PlayerController(Preferences& preferences, QObject* parent = nullptr);
    bool loaded() const {
        return m_loaded;
    }
    bool loading() const {
        return m_loading;
    }
    QString fileName() const;
    QUrl suggestedProjectUrl() const;
    QString mediaDescription() const;
    QString decodeBackend() const {
        return m_info.decodeBackend;
    }
    int frameCount() const {
        return m_info.frameCount;
    }
    double fps() const {
        return m_info.fps;
    }
    int mediaWidth() const {
        return m_info.width;
    }
    int mediaHeight() const {
        return m_info.height;
    }
    int currentFrame() const {
        return m_frame;
    }
    bool playing() const {
        return m_playing;
    }
    int direction() const {
        return m_direction;
    }
    double speed() const {
        return m_speed;
    }
    int rangeStart() const {
        return m_in;
    }
    int rangeEnd() const {
        return m_out;
    }
    QString loopMode() const {
        return m_loop;
    }
    QString status() const {
        return m_status;
    }
    QString error() const {
        return m_error;
    }
    double cacheUsedMiB() const {
        return m_cacheBytes / 1048576.;
    }
    QVariantList cachedRanges() const {
        return m_ranges;
    }
    QObject* bookmarks() {
        return &m_bookmarks;
    }
    QString projectPath() const {
        return m_projectPath;
    }
    bool dirty() const {
        return m_dirty;
    }
    double zoom() const {
        return m_zoom;
    }
    double panX() const {
        return m_panX;
    }
    double panY() const {
        return m_panY;
    }
    int rotation() const {
        return m_rotation;
    }
    bool flipH() const {
        return m_flipH;
    }
    bool flipV() const {
        return m_flipV;
    }
    int audioOffsetMs() const {
        return m_audioOffset;
    }
    bool audioAvailable() const {
        return m_audio.available();
    }
    void setSpeed(double value);
    void setLoopMode(const QString& mode);
    void setZoom(double value);
    void setAudioOffsetMs(int value);
    Q_INVOKABLE void openMedia(const QUrl& url);
    Q_INVOKABLE void openProject(const QUrl& url);
    Q_INVOKABLE void relinkMedia(const QUrl& url);
    Q_INVOKABLE bool saveProject(const QUrl& url);
    Q_INVOKABLE void execute(const QString& action);
    Q_INVOKABLE void seek(int frame, bool withAudio = false);
    Q_INVOKABLE void seekTimeline(int frame);
    Q_INVOKABLE void finishScrub();
    Q_INVOKABLE void setRange(int start, int end);
    Q_INVOKABLE void resetRange();
    Q_INVOKABLE void activateBookmark(const QString& id);
    Q_INVOKABLE void addBookmark(const QString& color);
    Q_INVOKABLE bool setBookmarkFrame(const QString& id, int frame);
    Q_INVOKABLE void pan(double x, double y);
    Q_INVOKABLE void rotate(int delta);
    Q_INVOKABLE void flip(bool vertical);
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void setStatus(const QString& text);
    Q_INVOKABLE double scrubSensitivity(bool fine, double verticalDistance) const;
  signals:
    void mediaChanged();
    void positionChanged();
    void playbackChanged();
    void statusChanged();
    void cacheChanged();
    void projectChanged();
    void viewChanged();
    void frameReady(tfv::VideoFrame image);
    void relinkRequired(QString missingPath);

  private:
    void seek(int frame, bool withAudio, bool preservePlayback);
    void open(const QString& path, bool project);
    void pause();
    void play(int direction);
    void tick();
    void resetClock();
    void restartAudio();
    void requestFrame(int frame);
    void applyDecodePreferences();
    void markDirty();
    void fail(const QString& text);
    QJsonObject state() const;
    bool restore(const QJsonObject& object);
    Preferences& m_preferences;
    BookmarkModel m_bookmarks;
    MediaWorker m_worker;
    AudioEngine m_audio;
    MediaInfo m_info;
    QTimer m_timer, m_autosave;
    QTimer m_scrubRequests;
    QElapsedTimer m_clock;
    quint64 m_generation = 0, m_transportEpoch = 0;
    int m_requestedFrame = 0;
    int m_scrubDirection = 1;
    int m_presentedFrame = -1;
    double m_lastVolume = 0;
    int m_frame = 0, m_anchor = 0, m_in = 0, m_out = 0, m_direction = 1, m_rotation = 0, m_audioOffset = 0;
    double m_speed = 1, m_zoom = 1, m_panX = 0, m_panY = 0;
    bool m_loaded = false, m_loading = false, m_playing = false, m_dirty = false, m_flipH = false,
         m_flipV = false;
    QString m_mediaPath, m_projectPath, m_status = "Ready when you are", m_error, m_loop = "loop",
                                        m_pendingProjectPath;
    qint64 m_cacheBytes = 0;
    QVariantList m_ranges;
    QJsonObject m_pendingState;
};
} // namespace tfv

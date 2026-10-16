#pragma once
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>

namespace tfv {
class Preferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cacheMiB READ cacheMiB WRITE setCacheMiB NOTIFY cacheChanged)
    Q_PROPERTY(int gpuCacheMiB READ gpuCacheMiB WRITE setGpuCacheMiB NOTIFY gpuCacheChanged)
    Q_PROPERTY(int decoderThreads READ decoderThreads WRITE setDecoderThreads NOTIFY decoderChanged)
    Q_PROPERTY(QString decodeMode READ decodeMode WRITE setDecodeMode NOTIFY decoderChanged)
    Q_PROPERTY(QVariantMap cacheLimits READ cacheLimits CONSTANT)
    Q_PROPERTY(QVariantList decodeOptions READ decodeOptions CONSTANT)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(bool debugMode READ debugMode WRITE setDebugMode NOTIFY debugModeChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList playbackSpeeds READ playbackSpeeds CONSTANT)
    Q_PROPERTY(double zoomWheelFactor READ zoomWheelFactor CONSTANT)
  public:
    explicit Preferences(QObject* parent = nullptr);
    ~Preferences() override;
    int cacheMiB() const;
    int gpuCacheMiB() const;
    int decoderThreads() const;
    QString decodeMode() const;
    QVariantMap cacheLimits() const;
    QVariantList decodeOptions() const;
    void setGpuCacheMiB(int value);
    void setDecoderThreads(int value);
    void setDecodeMode(const QString& value);
    double volume() const;
    QString theme() const;
    bool debugMode() const;
    QVariantList actions() const;
    QVariantMap shortcuts() const;
    QVariantList playbackSpeeds() const;
    double zoomWheelFactor() const;
    int integer(const QString& key) const;
    QJsonObject exportOptions() const {
        return m_defaults.value("export").toObject();
    }
    void setCacheMiB(int value);
    void setVolume(double value);
    void setTheme(const QString& value);
    void setDebugMode(bool enabled);
    Q_INVOKABLE void setShortcut(const QString& id, const QString& shortcut);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE QString shortcut(const QString& id) const;
  signals:
    void cacheChanged();
    void gpuCacheChanged();
    void decoderChanged();
    void volumeChanged();
    void themeChanged();
    void debugModeChanged();
    void shortcutsChanged();

  private:
    QJsonObject m_defaults;
    QSettings m_settings;
    QTimer m_volumeSave;
    double m_volume = 0;
};
} // namespace tfv

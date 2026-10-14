#pragma once
#include <QJsonObject>
#include <QObject>
#include <QSettings>
#include <QVariantList>

namespace tfv {
class Preferences final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int cacheMiB READ cacheMiB WRITE setCacheMiB NOTIFY changed)
    Q_PROPERTY(int gpuCacheMiB READ gpuCacheMiB WRITE setGpuCacheMiB NOTIFY changed)
    Q_PROPERTY(int decoderThreads READ decoderThreads WRITE setDecoderThreads NOTIFY changed)
    Q_PROPERTY(QString decodeMode READ decodeMode WRITE setDecodeMode NOTIFY changed)
    Q_PROPERTY(QVariantMap cacheLimits READ cacheLimits CONSTANT)
    Q_PROPERTY(QVariantList decodeOptions READ decodeOptions CONSTANT)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(QString theme READ theme WRITE setTheme NOTIFY changed)
    Q_PROPERTY(bool debugMode READ debugMode WRITE setDebugMode NOTIFY changed)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY changed)
    Q_PROPERTY(QVariantList playbackSpeeds READ playbackSpeeds CONSTANT)
    Q_PROPERTY(double zoomWheelFactor READ zoomWheelFactor CONSTANT)
  public:
    explicit Preferences(QObject* parent = nullptr);
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
    void setCacheMiB(int value);
    void setVolume(double value);
    void setTheme(const QString& value);
    void setDebugMode(bool enabled);
    Q_INVOKABLE void setShortcut(const QString& id, const QString& shortcut);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void toggleMute();
    Q_INVOKABLE QString shortcut(const QString& id) const;
  signals:
    void changed();

  private:
    QJsonObject m_defaults;
    QSettings m_settings;
};
} // namespace tfv

#pragma once
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QTemporaryFile>
#include <QUrl>
#include <memory>

namespace tfv {
struct ClipExportRequest {
    QString source;
    QStringList images;
    int start = 0, end = 0, frameCount = 0, width = 0, height = 0;
    double fps = 24;
    int outputWidth = 0, outputHeight = 0; // Zero keeps source resolution.
};

// Owns one asynchronous export, independent of the review decoder and its caches.
class ClipExporter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QUrl outputUrl READ outputUrl NOTIFY stateChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged)
  public:
    explicit ClipExporter(const QJsonObject& options, QObject* parent = nullptr);
    ~ClipExporter() override;
    bool busy() const {
        return m_busy;
    }
    double progress() const {
        return m_progress;
    }
    QString error() const {
        return m_error;
    }
    QUrl outputUrl() const {
        return m_output;
    }
    QString outputPath() const {
        return m_output.toLocalFile();
    }
    bool start(const ClipExportRequest& request, const QUrl& destination);
    Q_INVOKABLE void cancel();
  signals:
    void stateChanged();
    void finished(bool success, bool cancelled, QUrl output);

  private:
    void complete(bool success, const QString& error = {});
    bool commit(QString& error);
    QJsonObject m_options;
    QProcess m_process;
    std::unique_ptr<QTemporaryFile> m_temporary, m_sequence;
    QUrl m_output;
    QString m_error;
    QByteArray m_stderr;
    int m_frames = 0;
    bool m_busy = false, m_cancelled = false;
    double m_progress = 0;
};
} // namespace tfv

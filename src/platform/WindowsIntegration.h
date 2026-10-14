#pragma once
#include <QJsonObject>
#include <QObject>
#include <QStringList>

namespace tfv {
// Per-user shell integration. Registry ownership is checked before removing entries.
class WindowsIntegration final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool registered READ registered NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool hasError READ hasError NOTIFY changed)
  public:
    explicit WindowsIntegration(QObject* parent = nullptr);
    WindowsIntegration(const QString& executable, const QString& classesRoot, QObject* parent = nullptr);
    bool supported() const;
    bool registered() const {
        return m_registered;
    }
    QString message() const {
        return m_message;
    }
    bool hasError() const {
        return m_hasError;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool registerContextMenu();
    Q_INVOKABLE bool unregisterContextMenu();
  signals:
    void changed();

  private:
    QString command() const;
    QString applicationKey() const;
    QString verbKey(const QString& extension) const;
    QStringList extensions() const;
    void notifyShell() const;
    bool finish(bool success, const QString& successMessage);
    const QString m_executable, m_classesRoot;
    QJsonObject m_config;
    bool m_registered = false, m_hasError = false;
    QString m_message;
};
} // namespace tfv

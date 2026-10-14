#include "WindowsIntegration.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#ifdef Q_OS_WIN
#include <shlobj.h>
#include <windows.h>
#endif

static void initializeIntegrationResources() {
    Q_INIT_RESOURCE(tfv_windows_integration);
}

namespace tfv {
static constexpr auto ClassesRoot = "HKEY_CURRENT_USER\\Software\\Classes";

WindowsIntegration::WindowsIntegration(QObject* parent)
    : WindowsIntegration(QCoreApplication::applicationFilePath(), QString::fromLatin1(ClassesRoot), parent) {}

WindowsIntegration::WindowsIntegration(const QString& executable, const QString& classesRoot, QObject* parent)
    : QObject(parent), m_executable(QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath())),
      m_classesRoot(classesRoot) {
    initializeIntegrationResources();
    QFile config(":/tfv/config/windows-integration.json");
    if (!config.open(QIODevice::ReadOnly))
        qFatal("Missing Windows integration configuration");
    m_config = QJsonDocument::fromJson(config.readAll()).object();
    refresh();
}

bool WindowsIntegration::supported() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
QString WindowsIntegration::command() const {
    return '"' + m_executable + "\" \"%1\"";
}
QString WindowsIntegration::applicationKey() const {
    return "Applications/" + m_config["executableName"].toString();
}
QString WindowsIntegration::verbKey(const QString& extension) const {
    return "SystemFileAssociations/" + extension + "/shell/" + m_config["verbId"].toString();
}
QStringList WindowsIntegration::extensions() const {
    QStringList result;
    for (const auto& extension : m_config["videoExtensions"].toArray())
        result.append(extension.toString());
    return result;
}
void WindowsIntegration::refresh() {
    m_registered = supported();
    if (supported()) {
        QSettings registry(m_classesRoot, QSettings::NativeFormat);
        m_registered = registry.value(applicationKey() + "/shell/open/command/.")
                           .toString()
                           .compare(command(), Qt::CaseInsensitive) == 0;
        for (const auto& extension : extensions())
            m_registered = m_registered && registry.value(verbKey(extension) + "/command/.")
                                                   .toString()
                                                   .compare(command(), Qt::CaseInsensitive) == 0;
    }
    emit changed();
}
void WindowsIntegration::notifyShell() const {
#ifdef Q_OS_WIN
    if (m_classesRoot == QLatin1String(ClassesRoot))
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}
bool WindowsIntegration::finish(bool success, const QString& successMessage) {
    m_hasError = !success;
    m_message = success ? successMessage : tr("Windows could not update the context menu for this user.");
    refresh();
    if (success)
        notifyShell();
    return success;
}
bool WindowsIntegration::registerContextMenu() {
    if (!supported())
        return finish(false, {});
    QSettings registry(m_classesRoot, QSettings::NativeFormat);
    const QString name = m_config["displayName"].toString();
    const QString icon = '"' + m_executable + "\",0";
    registry.setValue(applicationKey() + "/FriendlyAppName", name);
    registry.setValue(applicationKey() + "/DefaultIcon/.", icon);
    registry.setValue(applicationKey() + "/shell/open/command/.", command());
    const auto registerType = [&](const QString& progId, const QString& description) {
        registry.setValue(progId + "/.", description);
        registry.setValue(progId + "/DefaultIcon/.", icon);
        registry.setValue(progId + "/shell/open/command/.", command());
    };
    const QString mediaType = m_config["mediaProgId"].toString();
    const QString projectType = m_config["projectProgId"].toString();
    registerType(mediaType, tr("Video for %1").arg(name));
    registerType(projectType, tr("%1 project").arg(name));
    for (const auto& extension : extensions()) {
        const QString key = verbKey(extension);
        registry.setValue(key + "/.", tr("Open with %1").arg(name));
        registry.setValue(key + "/Icon", icon);
        registry.setValue(key + "/MultiSelectModel", "Single");
        registry.setValue(key + "/command/.", command());
        registry.setValue(applicationKey() + "/SupportedTypes/" + extension, "");
        registry.setValue(extension + "/OpenWithProgids/" + mediaType, "");
    }
    const QString projectExtension = m_config["projectExtension"].toString();
    registry.setValue(applicationKey() + "/SupportedTypes/" + projectExtension, "");
    registry.setValue(projectExtension + "/OpenWithProgids/" + projectType, "");
    registry.sync();
    return finish(registry.status() == QSettings::NoError,
                  tr("Video context menu registered for this user."));
}
bool WindowsIntegration::unregisterContextMenu() {
    if (!supported())
        return finish(false, {});
    QSettings registry(m_classesRoot, QSettings::NativeFormat);
    const auto owns = [&](const QString& key) {
        return registry.value(key + "/shell/open/command/.")
                   .toString()
                   .compare(command(), Qt::CaseInsensitive) == 0;
    };
    for (const auto& extension : extensions()) {
        const QString key = verbKey(extension);
        if (registry.value(key + "/command/.").toString().compare(command(), Qt::CaseInsensitive) == 0)
            registry.remove(key);
    }
    const QString mediaType = m_config["mediaProgId"].toString();
    if (owns(mediaType)) {
        for (const auto& extension : extensions())
            registry.remove(extension + "/OpenWithProgids/" + mediaType);
        registry.remove(mediaType);
    }
    const QString projectType = m_config["projectProgId"].toString();
    if (owns(projectType)) {
        registry.remove(m_config["projectExtension"].toString() + "/OpenWithProgids/" + projectType);
        registry.remove(projectType);
    }
    if (owns(applicationKey()))
        registry.remove(applicationKey());
    registry.sync();
    return finish(registry.status() == QSettings::NoError, tr("Video context menu removed for this user."));
}
} // namespace tfv

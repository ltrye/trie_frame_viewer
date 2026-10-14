#include "Preferences.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

static void initializeDefaultResources() {
    Q_INIT_RESOURCE(tfv_defaults);
}

namespace tfv {
Preferences::Preferences(QObject* parent) : QObject(parent) {
    initializeDefaultResources();
    QFile file(":/tfv/config/defaults.json");
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Missing defaults.json resource");
    m_defaults = QJsonDocument::fromJson(file.readAll()).object();
    if (!m_settings.value("migrations/cinemaModeShortcut", false).toBool()) {
        QString cinemaDefault;
        for (const auto& entry : m_defaults.value("actions").toArray()) {
            const auto action = entry.toObject();
            if (action.value("id").toString() == "cinemaMode")
                cinemaDefault = action.value("shortcut").toString();
        }
        if (!cinemaDefault.isEmpty() && !m_settings.contains("shortcuts/cinemaMode") &&
            m_settings.value("shortcuts/fullscreen")
                    .toString()
                    .trimmed()
                    .compare(cinemaDefault, Qt::CaseInsensitive) == 0) {
            m_settings.remove("shortcuts/fullscreen");
            m_settings.setValue("shortcuts/cinemaMode", cinemaDefault);
        }
        m_settings.setValue("migrations/cinemaModeShortcut", true);
    }
}
int Preferences::integer(const QString& key) const {
    return m_defaults.value(key).toInt();
}
int Preferences::cacheMiB() const {
    return m_settings.value("cacheMiB", integer("cacheMiB")).toInt();
}
int Preferences::gpuCacheMiB() const {
    return m_settings.value("gpuCacheMiB", integer("gpuCacheMiB")).toInt();
}
int Preferences::decoderThreads() const {
    return m_settings.value("decoderThreads", integer("decoderThreads")).toInt();
}
QString Preferences::decodeMode() const {
    return m_settings.value("decodeMode", m_defaults.value("decodeMode").toString()).toString();
}
QVariantMap Preferences::cacheLimits() const {
    return m_defaults.value("cacheLimits").toObject().toVariantMap();
}
QVariantList Preferences::decodeOptions() const {
    return m_defaults.value("decodeOptions").toArray().toVariantList();
}
void Preferences::setGpuCacheMiB(int value) {
    const auto limits = cacheLimits();
    m_settings.setValue("gpuCacheMiB", std::clamp(value, limits["gpuMin"].toInt(), limits["gpuMax"].toInt()));
    emit changed();
}
void Preferences::setDecoderThreads(int value) {
    m_settings.setValue("decoderThreads", std::clamp(value, 1, 16));
    emit changed();
}
void Preferences::setDecodeMode(const QString& value) {
    bool valid = false;
    for (const auto& option : decodeOptions())
        valid = valid || option.toMap()["id"].toString() == value;
    if (!valid)
        return;
    m_settings.setValue("decodeMode", value);
    emit changed();
}
double Preferences::volume() const {
    return m_settings.value("volume", m_defaults.value("volume").toDouble()).toDouble();
}
QString Preferences::theme() const {
    return m_settings.value("theme", m_defaults.value("theme").toString()).toString();
}
bool Preferences::debugMode() const {
    return m_settings.value("debugMode", m_defaults.value("debugMode").toBool()).toBool();
}
void Preferences::setDebugMode(bool enabled) {
    if (debugMode() == enabled)
        return;
    m_settings.setValue("debugMode", enabled);
    emit changed();
}
void Preferences::setCacheMiB(int v) {
    const auto limits = cacheLimits();
    m_settings.setValue("cacheMiB", std::clamp(v, limits["videoMin"].toInt(), limits["videoMax"].toInt()));
    emit changed();
}
void Preferences::setVolume(double v) {
    m_settings.setValue("volume", std::clamp(v, 0., 1.));
    emit changed();
}
void Preferences::setTheme(const QString& v) {
    if (v != "dark" && v != "light")
        return;
    m_settings.setValue("theme", v);
    emit changed();
}
QVariantList Preferences::actions() const {
    QVariantList result;
    for (const auto& v : m_defaults.value("actions").toArray()) {
        auto action = v.toObject().toVariantMap();
        action["shortcut"] = m_settings.value("shortcuts/" + action["id"].toString(), action["shortcut"]);
        result.append(action);
    }
    return result;
}
QString Preferences::shortcut(const QString& id) const {
    for (const auto& a : actions())
        if (a.toMap()["id"] == id)
            return a.toMap()["shortcut"].toString();
    return {};
}
void Preferences::setShortcut(const QString& id, const QString& value) {
    for (const auto& a : actions())
        if (a.toMap()["id"] == id) {
            m_settings.setValue("shortcuts/" + id, value);
            emit changed();
            return;
        }
}
void Preferences::resetShortcuts() {
    m_settings.remove("shortcuts");
    emit changed();
}
QVariantList Preferences::playbackSpeeds() const {
    return m_defaults.value("playbackSpeeds").toArray().toVariantList();
}
QVariantMap Preferences::shortcuts() const {
    QVariantMap result;
    for (const auto& entry : actions()) {
        const auto action = entry.toMap();
        result[action.value("id").toString()] = action.value("shortcut");
    }
    return result;
}
double Preferences::zoomWheelFactor() const {
    return m_defaults.value("zoomWheelFactor").toDouble();
}
void Preferences::toggleMute() {
    if (volume() > 0) {
        m_settings.setValue("lastVolume", volume());
        setVolume(0);
    } else {
        setVolume(m_settings.value("lastVolume", m_defaults.value("volume").toDouble()).toDouble());
    }
}
} // namespace tfv

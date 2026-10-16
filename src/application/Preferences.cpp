#include "Preferences.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

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
    const double savedVolume = m_settings.value("volume", m_defaults.value("volume").toDouble()).toDouble();
    m_volume =
        std::isfinite(savedVolume) ? std::clamp(savedVolume, 0., 1.) : m_defaults.value("volume").toDouble();
    m_volumeSave.setSingleShot(true);
    m_volumeSave.setInterval(integer("volumeSaveDelayMs"));
    connect(&m_volumeSave, &QTimer::timeout, this, [this] { m_settings.setValue("volume", m_volume); });
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
Preferences::~Preferences() {
    if (m_volumeSave.isActive())
        m_settings.setValue("volume", m_volume);
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
    value = std::clamp(value, limits["gpuMin"].toInt(), limits["gpuMax"].toInt());
    if (value == gpuCacheMiB())
        return;
    m_settings.setValue("gpuCacheMiB", value);
    emit gpuCacheChanged();
}
void Preferences::setDecoderThreads(int value) {
    value = std::clamp(value, 1, 16);
    if (value == decoderThreads())
        return;
    m_settings.setValue("decoderThreads", value);
    emit decoderChanged();
}
void Preferences::setDecodeMode(const QString& value) {
    bool valid = false;
    for (const auto& option : decodeOptions())
        valid = valid || option.toMap()["id"].toString() == value;
    if (!valid || value == decodeMode())
        return;
    m_settings.setValue("decodeMode", value);
    emit decoderChanged();
}
double Preferences::volume() const {
    return m_volume;
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
    emit debugModeChanged();
}
void Preferences::setCacheMiB(int v) {
    const auto limits = cacheLimits();
    v = std::clamp(v, limits["videoMin"].toInt(), limits["videoMax"].toInt());
    if (v == cacheMiB())
        return;
    m_settings.setValue("cacheMiB", v);
    emit cacheChanged();
}
void Preferences::setVolume(double v) {
    if (!std::isfinite(v))
        return;
    v = std::clamp(v, 0., 1.);
    if (v == m_volume)
        return;
    m_volume = v;
    m_volumeSave.start();
    emit volumeChanged();
}
void Preferences::setTheme(const QString& v) {
    if ((v != "dark" && v != "light") || v == theme())
        return;
    m_settings.setValue("theme", v);
    emit themeChanged();
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
            emit shortcutsChanged();
            return;
        }
}
void Preferences::resetShortcuts() {
    m_settings.remove("shortcuts");
    emit shortcutsChanged();
}
QVariantList Preferences::playbackSpeeds() const {
    return m_defaults.value("playbackSpeeds").toArray().toVariantList();
}
QVariantList Preferences::videoExportPresets() const {
    return exportOptions().value("resolutions").toArray().toVariantList();
}
QString Preferences::defaultVideoExportPreset() const {
    return exportOptions().value("defaultResolution").toString();
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

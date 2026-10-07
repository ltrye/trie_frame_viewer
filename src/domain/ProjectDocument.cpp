#include "ProjectDocument.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace tfv {
QJsonObject ProjectDocument::identity(const QString& path) {
    QFile f(path);
    const QFileInfo info(path);
    QByteArray sample;
    if (f.open(QIODevice::ReadOnly)) {
        sample = f.read(65536);
        if (f.size() > 65536) {
            f.seek(std::max<qint64>(0, f.size() - 65536));
            sample += f.read(65536);
        }
    }
    return {{"size", QString::number(info.size())},
            {"sampleSha256",
             QString::fromLatin1(QCryptographicHash::hash(sample, QCryptographicHash::Sha256).toHex())}};
}
bool ProjectDocument::save(const QString& path, const QString& media, QJsonObject state, QString& error) {
    state["schemaVersion"] = SchemaVersion;
    state["application"] = "Trie FrameViewer";
    state["media"] = QJsonObject{{"path", QDir(QFileInfo(path).absolutePath()).relativeFilePath(media)},
                                 {"identity", identity(media)}};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(state).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}
bool ProjectDocument::load(const QString& path, QJsonObject& state, QString& media, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    if (file.size() > 4 * 1024 * 1024) {
        error = "Project file exceeds the 4 MiB safety limit.";
        return false;
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        error = "Invalid project JSON: " + parse.errorString();
        return false;
    }
    const auto obj = doc.object();
    if (obj.value("schemaVersion").toInt() != SchemaVersion) {
        error = "Unsupported project version.";
        return false;
    }
    const QString source = obj.value("media").toObject().value("path").toString();
    if (source.isEmpty()) {
        error = "Project has no media reference.";
        return false;
    }
    media = QDir::cleanPath(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(source));
    state = obj;
    return true;
}
} // namespace tfv

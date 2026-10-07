#pragma once
#include <QJsonObject>
#include <QString>

namespace tfv {
class ProjectDocument {
  public:
    static constexpr int SchemaVersion = 1;
    static bool save(const QString& projectPath, const QString& mediaPath, QJsonObject state, QString& error);
    static bool load(const QString& projectPath, QJsonObject& state, QString& mediaPath, QString& error);
    static QJsonObject identity(const QString& mediaPath);
};
} // namespace tfv

#pragma once
#include <QString>
class QGuiApplication;
class QQuickWindow;
namespace tfv {
class PlayerController;
class Preferences;
} // namespace tfv
void startAppSmoke(QGuiApplication& app, QQuickWindow& window, tfv::PlayerController& player,
                   tfv::Preferences& preferences, const QString& directory);

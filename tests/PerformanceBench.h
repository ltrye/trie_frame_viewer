#pragma once
#include <QString>
class QGuiApplication;
class QQuickWindow;
namespace tfv {
class PlayerController;
class Preferences;
} // namespace tfv
void startPerformanceBench(QGuiApplication&, QQuickWindow&, tfv::PlayerController&, tfv::Preferences&,
                           const QString& reportPath);

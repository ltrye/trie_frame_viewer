#pragma once
#include "VideoFrame.h"
#include <QByteArray>
#include <QImage>
#include <QMetaType>
#include <QString>
#include <functional>
#include <memory>

namespace tfv {
struct MediaInfo {
    int width = 0, height = 0, frameCount = 0;
    double fps = 24;
    QString codec;
    bool sequence = false;
    bool variableRate = false;
    QString decodeBackend = "Software";
};
// All calls must occur on one worker thread. No UI or project dependencies.
class Decoder {
  public:
    Decoder();
    ~Decoder();
    bool open(const QString& path, QString& error, int threads = 2, bool hardware = false,
              int hardwareMinimumPixels = 0);
    const MediaInfo& info() const;
    QImage frame(int index, QString& error, const std::function<bool()>& cancelled = {});
    VideoFrame videoFrame(int index, QString& error, const std::function<bool()>& cancelled = {},
                          const std::function<void(int, const VideoFrame&)>& intermediate = {},
                          const std::function<bool(int)>& wanted = {});
    static QByteArray decodeAudio(const QString& path, qint64 budget, const std::function<bool()>& cancelled,
                                  QString& warning);

  private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
} // namespace tfv
Q_DECLARE_METATYPE(tfv::MediaInfo)

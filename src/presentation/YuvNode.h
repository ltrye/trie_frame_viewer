#pragma once
#include "media/VideoFrame.h"
#include <QSGGeometryNode>
#include <memory>
namespace tfv {
// QRhi integration is isolated here; no graphics APIs are required by media/UI.
class YuvNode final : public QSGGeometryNode {
  public:
    YuvNode();
    ~YuvNode() override;
    void setFrame(const VideoFrame& frame, qint64 gpuBudget);
    void setRect(const QRectF& rect);
    qint64 cacheBytes() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> m_data;
};
} // namespace tfv

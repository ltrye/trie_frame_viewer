#pragma once
#include "media/VideoFrame.h"
#include <QQuickItem>
#include <atomic>
namespace tfv {
class VideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(int gpuCacheMiB READ gpuCacheMiB WRITE setGpuCacheMiB NOTIFY gpuCacheMiBChanged)
  public:
    explicit VideoItem(QQuickItem* parent = nullptr);
    void setFrame(const VideoFrame& image);
    int gpuCacheMiB() const {
        return m_gpuCacheMiB;
    }
    void setGpuCacheMiB(int value);
    double gpuCacheUsedMiB() const {
        return m_gpuBytes.load() / 1048576.;
    }
  signals:
    void gpuCacheMiBChanged();

  protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;

  private:
    VideoFrame m_image;
    int m_gpuCacheMiB = 0;
    bool m_usingYuv = false;
    std::atomic<qint64> m_gpuBytes{0};
    bool m_changed = true;
};
} // namespace tfv

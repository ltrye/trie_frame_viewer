#pragma once
#include "media/VideoFrame.h"
#include <QQuickItem>
#include <atomic>
#include <limits>
#include <memory>
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
    quint64 textureSetsCreated() const {
        return m_textureCreations.load();
    }
    quint64 textureSetsReused() const {
        return m_textureReuses.load();
    }
  signals:
    void gpuCacheMiBChanged();

  protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

  private:
    void watchWindow(QQuickWindow* window);
    VideoFrame m_image;
    int m_gpuCacheMiB = 0;
    bool m_usingYuv = false;
    std::atomic<qint64> m_gpuBytes{0};
    bool m_changed = true;
    std::atomic<quint64> m_textureCreations{0}, m_textureReuses{0};
    struct PresentationStamp {
        std::atomic<quint64> serial{0}, identity{0}, reportedSerial{std::numeric_limits<quint64>::max()},
            reportedIdentity{0};
        std::atomic<int> frame{-1};
        std::atomic<bool> valid{false};
    };
    std::shared_ptr<PresentationStamp> m_stamp = std::make_shared<PresentationStamp>();
    QMetaObject::Connection m_swapConnection;
};
} // namespace tfv

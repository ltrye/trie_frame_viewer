#include "VideoItem.h"
#include "YuvNode.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
namespace tfv {
VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
}
void VideoItem::setFrame(const VideoFrame& image) {
    m_image = image;
    m_changed = true;
    update();
}
void VideoItem::setGpuCacheMiB(int value) {
    value = std::max(0, value);
    if (value == m_gpuCacheMiB)
        return;
    m_gpuCacheMiB = value;
    m_changed = true;
    update();
    emit gpuCacheMiBChanged();
}
QSGNode* VideoItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) {
    if (m_image.isNull()) {
        delete previous;
        m_gpuBytes = 0;
        return nullptr;
    }
    const bool yuv = m_image.format() == VideoFrame::Format::Yuv420 &&
                     window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
    if (previous && yuv != m_usingYuv) {
        delete previous;
        previous = nullptr;
    }
    m_usingYuv = yuv;
    if (yuv) {
        auto* node = static_cast<YuvNode*>(previous);
        if (!node) {
            node = new YuvNode;
            m_changed = true;
        }
        if (m_changed) {
            node->setFrame(m_image, qint64(m_gpuCacheMiB) * 1048576);
            m_gpuBytes = node->cacheBytes();
            m_changed = false;
        }
        node->setRect(boundingRect());
        return node;
    }
    auto* node = static_cast<QSGSimpleTextureNode*>(previous);
    m_gpuBytes = 0;
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        m_changed = true;
    }
    if (m_changed) {
        // setTexture releases the previous texture when the node owns it.
        node->setTexture(window()->createTextureFromImage(m_image.toImage()));
        node->setFiltering(QSGTexture::Linear);
        m_changed = false;
    }
    node->setRect(boundingRect());
    return node;
}
} // namespace tfv

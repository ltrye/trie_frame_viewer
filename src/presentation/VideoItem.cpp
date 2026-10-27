#include "VideoItem.h"
#include "YuvNode.h"
#include "media/PlaybackTrace.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
namespace tfv {
VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    watchWindow(window());
}
void VideoItem::setFrame(const VideoFrame& image) {
    if (image.identity() == m_image.identity()) {
        m_image = image;
        tracePlayback("display.same", image.requestSerial(), image.sourceIndex(), qint64(image.identity()));
        return;
    }
    m_image = image;
    m_changed = true;
    update();
}
void VideoItem::itemChange(ItemChange change, const ItemChangeData& data) {
    QQuickItem::itemChange(change, data);
    if (change != ItemSceneChange)
        return;
    watchWindow(data.window);
}
void VideoItem::watchWindow(QQuickWindow* window) {
    QObject::disconnect(m_swapConnection);
    m_stamp->valid = false;
    if (window) {
        const auto stamp = m_stamp;
        m_swapConnection = connect(
            window, &QQuickWindow::frameSwapped, this,
            [stamp] {
                if (!stamp->valid.load())
                    return;
                const auto serial = stamp->serial.load(), identity = stamp->identity.load();
                if (stamp->reportedSerial.exchange(serial) != serial ||
                    stamp->reportedIdentity.load() != identity)
                    tracePlayback("present.queued", serial, stamp->frame.load(), qint64(identity));
                stamp->reportedIdentity = identity;
            },
            Qt::DirectConnection);
    }
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
        m_stamp->valid = false;
        delete previous;
        m_gpuBytes = 0;
        return nullptr;
    }
    m_stamp->serial = m_image.requestSerial();
    m_stamp->identity = m_image.identity();
    m_stamp->frame = m_image.sourceIndex();
    m_stamp->valid = true;
    tracePlayback("render.frame", m_image.requestSerial(), m_image.sourceIndex(), qint64(m_image.identity()));
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
            m_textureCreations = node->textureSetsCreated();
            m_textureReuses = node->textureSetsReused();
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

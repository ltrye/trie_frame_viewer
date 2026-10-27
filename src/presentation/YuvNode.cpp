#include "YuvNode.h"
#include "media/PlaybackTrace.h"
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <cstring>
#include <list>
#include <map>
#include <rhi/qrhi.h>

namespace tfv {
namespace {
class PlaneTexture final : public QSGTexture {
  public:
    explicit PlaneTexture(const VideoFrame::Plane& plane)
        : m_size(plane.size), m_bytes(plane.bytes), m_stride(plane.stride) {
        setFiltering(Linear);
    }
    void setPixels(const VideoFrame::Plane& plane, quint64 serial, int index) {
        m_bytes = plane.bytes;
        m_stride = plane.stride;
        m_serial = serial;
        m_index = index;
    }
    void setTraceContext(quint64 serial, int index) {
        m_serial = serial;
        m_index = index;
    }
    qint64 comparisonKey() const override {
        return qint64(this);
    }
    QRhiTexture* rhiTexture() const override {
        return m_texture.get();
    }
    QSize textureSize() const override {
        return m_size;
    }
    bool hasAlphaChannel() const override {
        return false;
    }
    bool hasMipmaps() const override {
        return false;
    }
    void commitTextureOperations(QRhi* rhi, QRhiResourceUpdateBatch* batch) override {
        if (!m_texture) {
            m_texture.reset(rhi->newTexture(QRhiTexture::R8, m_size));
            if (!m_texture->create()) {
                m_texture.reset();
                return;
            }
            tracePlayback("texture.created", m_serial, m_index, qint64(m_size.width()) * m_size.height());
        }
        if (!m_bytes.isEmpty()) {
            QRhiTextureSubresourceUploadDescription upload(m_bytes);
            upload.setDataStride(m_stride);
            batch->uploadTexture(m_texture.get(),
                                 QRhiTextureUploadDescription({QRhiTextureUploadEntry(0, 0, upload)}));
            tracePlayback("upload.enqueued", m_serial, m_index, m_bytes.size());
            m_bytes.clear();
        }
    }

  private:
    QSize m_size;
    QByteArray m_bytes;
    int m_stride;
    std::unique_ptr<QRhiTexture> m_texture;
    quint64 m_serial = 0;
    int m_index = -1;
};
struct Textures {
    explicit Textures(const VideoFrame& frame) : bytes(frame.sizeInBytes()), size(frame.size()) {
        for (int p = 0; p < 3; ++p)
            planes[p] = std::make_unique<PlaneTexture>(frame.plane(p));
    }
    void setPixels(const VideoFrame& frame) {
        for (int p = 0; p < 3; ++p)
            planes[p]->setPixels(frame.plane(p), frame.requestSerial(), frame.sourceIndex());
    }
    void setTraceContext(const VideoFrame& frame) {
        for (auto& plane : planes)
            plane->setTraceContext(frame.requestSerial(), frame.sourceIndex());
    }
    std::array<std::unique_ptr<PlaneTexture>, 3> planes;
    qint64 bytes;
    QSize size;
};
class YuvMaterial;
class YuvShader final : public QSGMaterialShader {
  public:
    YuvShader() {
        setShaderFileName(VertexStage, ":/tfv/shaders/yuv.vert.qsb");
        setShaderFileName(FragmentStage, ":/tfv/shaders/yuv.frag.qsb");
    }
    bool updateUniformData(RenderState&, QSGMaterial*, QSGMaterial*) override;
    void updateSampledImage(RenderState&, int, QSGTexture**, QSGMaterial*, QSGMaterial*) override;
};
class YuvMaterial final : public QSGMaterial {
  public:
    YuvMaterial() {
        setFlag(Blending);
    }
    QSGMaterialType* type() const override {
        static QSGMaterialType type;
        return &type;
    }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override {
        return new YuvShader;
    }
    int compare(const QSGMaterial* other) const override {
        return this == other ? 0 : std::less<const QSGMaterial*>{}(this, other) ? -1 : 1;
    }
    std::shared_ptr<Textures> textures;
    QMatrix4x4 transform;
};
bool YuvShader::updateUniformData(RenderState& state, QSGMaterial* material, QSGMaterial*) {
    auto* data = state.uniformData();
    const auto* yuv = static_cast<YuvMaterial*>(material);
    const auto matrix = state.combinedMatrix();
    const float opacity = state.opacity();
    std::memcpy(data->data(), matrix.constData(), 64);
    std::memcpy(data->data() + 64, yuv->transform.constData(), 64);
    std::memcpy(data->data() + 128, &opacity, 4);
    return true;
}
void YuvShader::updateSampledImage(RenderState& state, int binding, QSGTexture** texture,
                                   QSGMaterial* material, QSGMaterial*) {
    const auto* yuv = static_cast<YuvMaterial*>(material);
    if (binding >= 1 && binding <= 3 && yuv->textures) {
        *texture = yuv->textures->planes[binding - 1].get();
        (*texture)->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
    }
}
} // namespace
struct YuvNode::Impl {
    YuvMaterial* material = nullptr;
    struct Entry {
        std::shared_ptr<Textures> textures;
        std::list<quint64>::iterator age;
    };
    std::map<quint64, Entry> cache;
    std::list<quint64> lru;
    qint64 bytes = 0;
    quint64 created = 0, reused = 0;
};
YuvNode::YuvNode() : m_data(std::make_unique<Impl>()) {
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4);
    geometry->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    setGeometry(geometry);
    setFlag(OwnsGeometry);
    m_data->material = new YuvMaterial;
    setMaterial(m_data->material);
    setFlag(OwnsMaterial);
}
YuvNode::~YuvNode() = default;
qint64 YuvNode::cacheBytes() const {
    return m_data->bytes;
}
quint64 YuvNode::textureSetsCreated() const {
    return m_data->created;
}
quint64 YuvNode::textureSetsReused() const {
    return m_data->reused;
}
void YuvNode::setFrame(const VideoFrame& frame, qint64 gpuBudget) {
    const auto id = frame.identity();
    auto it = m_data->cache.find(id);
    if (it == m_data->cache.end()) {
        std::shared_ptr<Textures> textures;
        // Recycle an evicted set in-place. QRhi orders its queued upload after
        // earlier draws; the set is removed from every retained frame key first.
        const qint64 limit = std::max(gpuBudget, frame.sizeInBytes());
        while (m_data->bytes + frame.sizeInBytes() > limit && !m_data->lru.empty()) {
            const auto oldest = m_data->cache.find(m_data->lru.front());
            if (!textures && oldest->second.textures->size == frame.size())
                textures = oldest->second.textures;
            m_data->bytes -= oldest->second.textures->bytes;
            m_data->cache.erase(oldest);
            m_data->lru.pop_front();
        }
        if (textures) {
            textures->setPixels(frame);
            ++m_data->reused;
            tracePlayback("textures.reused", frame.requestSerial(), frame.sourceIndex(), frame.sizeInBytes());
        } else {
            textures = std::make_shared<Textures>(frame);
            ++m_data->created;
        }
        m_data->bytes += textures->bytes;
        m_data->lru.push_back(id);
        it = m_data->cache.emplace(id, Impl::Entry{textures, std::prev(m_data->lru.end())}).first;
    } else {
        m_data->lru.splice(m_data->lru.end(), m_data->lru, it->second.age);
    }
    m_data->material->textures = it->second.textures;
    it->second.textures->setTraceContext(frame);
    m_data->material->transform = frame.colorTransform();
    // Always retain the currently displayed frame, even with caching disabled.
    while (m_data->bytes > gpuBudget && m_data->lru.size() > 1) {
        const auto oldest = m_data->cache.find(m_data->lru.front());
        m_data->bytes -= oldest->second.textures->bytes;
        m_data->cache.erase(oldest);
        m_data->lru.pop_front();
    }
    markDirty(DirtyMaterial);
}
void YuvNode::setRect(const QRectF& rect) {
    if (m_rectSet && rect == m_rect)
        return;
    m_rect = rect;
    m_rectSet = true;
    QSGGeometry::updateTexturedRectGeometry(geometry(), rect, QRectF(0, 0, 1, 1));
    markDirty(DirtyGeometry);
}
} // namespace tfv

#include "VideoFrame.h"
#include <atomic>
#include <cstring>
extern "C" {
#include <libswscale/swscale.h>
}
namespace tfv {
static std::atomic<quint64> nextFrameId{1};
VideoFrame::VideoFrame(const QImage& image) {
    if (image.isNull())
        return;
    auto data = std::make_shared<Data>();
    data->id = nextFrameId++;
    data->size = image.size();
    data->rgba = image.convertToFormat(QImage::Format_RGBA8888);
    m_data = std::move(data);
}
VideoFrame VideoFrame::copyYuv420(int width, int height, const uint8_t* const pixels[], const int strides[],
                                  Matrix matrix, bool fullRange, bool interleaved) {
    VideoFrame frame;
    if (width <= 0 || height <= 0 || !pixels[0] || !pixels[1] || (!interleaved && !pixels[2]))
        return frame;
    auto data = std::make_shared<Data>();
    data->id = nextFrameId++;
    data->size = QSize(width, height);
    data->format = Format::Yuv420;
    data->matrix = matrix;
    data->fullRange = fullRange;
    for (int p = 0; p < 3; ++p) {
        auto& plane = data->planes[p];
        plane.size = p == 0 ? data->size : QSize((width + 1) / 2, (height + 1) / 2);
        plane.stride = plane.size.width();
        plane.bytes.resize(qsizetype(plane.stride) * plane.size.height());
        for (int y = 0; y < plane.size.height(); ++y) {
            auto* destination = reinterpret_cast<uint8_t*>(plane.bytes.data()) + y * plane.stride;
            if (interleaved && p > 0) {
                const auto* row = pixels[1] + y * strides[1];
                for (int x = 0; x < plane.stride; ++x)
                    destination[x] = row[x * 2 + p - 1];
            } else {
                std::memcpy(destination, pixels[p] + y * strides[p], plane.stride);
            }
        }
    }
    frame.m_data = std::move(data);
    return frame;
}
qint64 VideoFrame::sizeInBytes() const {
    if (!m_data)
        return 0;
    if (format() == Format::Rgba)
        return m_data->rgba.sizeInBytes();
    qint64 bytes = 0;
    for (const auto& plane : m_data->planes)
        bytes += plane.bytes.size();
    return bytes;
}
QImage VideoFrame::toImage() const {
    if (!m_data)
        return {};
    if (format() == Format::Rgba)
        return m_data->rgba;
    QImage image(size(), QImage::Format_RGBA8888);
    auto* scaler = sws_getContext(size().width(), size().height(), AV_PIX_FMT_YUV420P, size().width(),
                                  size().height(), AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scaler || image.isNull()) {
        sws_freeContext(scaler);
        return {};
    }
    const int cs = m_data->matrix == Matrix::Bt709    ? SWS_CS_ITU709
                   : m_data->matrix == Matrix::Bt2020 ? SWS_CS_BT2020
                                                      : SWS_CS_ITU601;
    const int* coefficients = sws_getCoefficients(cs);
    sws_setColorspaceDetails(scaler, coefficients, m_data->fullRange, coefficients, 1, 0, 1 << 16, 1 << 16);
    const uint8_t* input[4] = {};
    int strides[4] = {};
    for (int p = 0; p < 3; ++p) {
        input[p] = reinterpret_cast<const uint8_t*>(plane(p).bytes.constData());
        strides[p] = plane(p).stride;
    }
    uint8_t* output[] = {image.bits(), nullptr, nullptr, nullptr};
    int outputStride[] = {int(image.bytesPerLine()), 0, 0, 0};
    sws_scale(scaler, input, strides, 0, size().height(), output, outputStride);
    sws_freeContext(scaler);
    return image;
}
QMatrix4x4 VideoFrame::colorTransform() const {
    const float kr = m_data->matrix == Matrix::Bt709    ? .2126f
                     : m_data->matrix == Matrix::Bt2020 ? .2627f
                                                        : .299f;
    const float kb = m_data->matrix == Matrix::Bt709    ? .0722f
                     : m_data->matrix == Matrix::Bt2020 ? .0593f
                                                        : .114f;
    const float kg = 1 - kr - kb;
    const float ys = m_data->fullRange ? 1.f : 255.f / 219;
    const float cs = m_data->fullRange ? 1.f : 255.f / 224;
    const float yo = m_data->fullRange ? 0.f : 16.f / 255;
    const float co = 128.f / 255;
    const float rv = (2 - 2 * kr) * cs, bu = (2 - 2 * kb) * cs;
    const float gu = -kb * (2 - 2 * kb) / kg * cs, gv = -kr * (2 - 2 * kr) / kg * cs;
    return QMatrix4x4(ys, 0, rv, -ys * yo - rv * co, ys, gu, gv, -ys * yo - (gu + gv) * co, ys, bu, 0,
                      -ys * yo - bu * co, 0, 0, 0, 1);
}
} // namespace tfv

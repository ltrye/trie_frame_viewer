#pragma once
#include <QByteArray>
#include <QImage>
#include <QMatrix4x4>
#include <QMetaType>
#include <array>
#include <memory>

namespace tfv {
// Immutable, implicitly shared pixels. FFmpeg and GPU resources never escape
// into the controller or QML; renderer-specific storage stays in presentation.
class VideoFrame {
  public:
    enum class Format { Rgba, Yuv420 };
    enum class Matrix { Bt601, Bt709, Bt2020 };
    struct Plane {
        QByteArray bytes;
        QSize size;
        int stride = 0;
    };
    VideoFrame() = default;
    VideoFrame(const QImage& image);
    static VideoFrame copyYuv420(int width, int height, const uint8_t* const data[], const int strides[],
                                 Matrix matrix, bool fullRange, bool interleaved = false);
    bool isNull() const {
        return !m_data;
    }
    QSize size() const {
        return m_data ? m_data->size : QSize{};
    }
    Format format() const {
        return m_data ? m_data->format : Format::Rgba;
    }
    qint64 sizeInBytes() const;
    const Plane& plane(int index) const {
        return m_data->planes.at(index);
    }
    QImage toImage() const;
    QMatrix4x4 colorTransform() const;
    quint64 identity() const {
        return m_data ? m_data->id : 0;
    }
    quint64 requestSerial() const {
        return m_requestSerial;
    }
    int sourceIndex() const {
        return m_sourceIndex;
    }
    VideoFrame forRequest(quint64 serial, int index) const {
        auto frame = *this;
        frame.m_requestSerial = serial;
        frame.m_sourceIndex = index;
        return frame;
    }

  private:
    struct Data {
        quint64 id = 0;
        QSize size;
        Format format = Format::Rgba;
        Matrix matrix = Matrix::Bt601;
        bool fullRange = false;
        std::array<Plane, 3> planes;
        QImage rgba;
    };
    std::shared_ptr<const Data> m_data;
    quint64 m_requestSerial = 0;
    int m_sourceIndex = -1;
};
} // namespace tfv
Q_DECLARE_METATYPE(tfv::VideoFrame)

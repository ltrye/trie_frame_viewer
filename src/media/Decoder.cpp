#include "Decoder.h"
#include "PlaybackTrace.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include <limits>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace tfv {
namespace {
QString avError(int result) {
    char text[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(result, text, sizeof(text));
    return QString::fromUtf8(text);
}
bool openContext(const QString& path, AVMediaType type, AVFormatContext*& format, AVCodecContext*& codec,
                 int& stream, QString& error, int threads = 2, bool hardware = false,
                 int hardwareMinimumPixels = 0) {
    const auto utf8 = QDir::fromNativeSeparators(path).toUtf8();
    int result = avformat_open_input(&format, utf8.constData(), nullptr, nullptr);
    if (result < 0) {
        error = avError(result);
        return false;
    }
    if ((result = avformat_find_stream_info(format, nullptr)) < 0) {
        error = avError(result);
        return false;
    }
    stream = av_find_best_stream(format, type, -1, -1, nullptr, 0);
    if (stream < 0) {
        error = "Media does not contain the requested stream.";
        return false;
    }
    const AVCodec* decoder = avcodec_find_decoder(format->streams[stream]->codecpar->codec_id);
    if (!decoder) {
        error = "This codec has no available decoder.";
        return false;
    }
    codec = avcodec_alloc_context3(decoder);
    if (!codec) {
        error = "Could not allocate decoder.";
        return false;
    }
    avcodec_parameters_to_context(codec, format->streams[stream]->codecpar);
    codec->thread_count = std::clamp(threads, 1, 16);
    if (hardware && type == AVMEDIA_TYPE_VIDEO &&
        qint64(codec->width) * codec->height >= hardwareMinimumPixels) {
        bool supported = false;
        for (int n = 0; const auto* config = avcodec_get_hw_config(decoder, n); ++n)
            if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
                (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                supported = true;
        if (supported && av_hwdevice_ctx_create(&codec->hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA, nullptr,
                                                nullptr, 0) >= 0) {
            codec->get_format = [](AVCodecContext*, const AVPixelFormat* formats) {
                for (const auto* format = formats; *format != AV_PIX_FMT_NONE; ++format)
                    if (*format == AV_PIX_FMT_D3D11)
                        return *format;
                return formats[0];
            };
        }
    }
    if ((result = avcodec_open2(codec, decoder, nullptr)) < 0) {
        error = avError(result);
        return false;
    }
    return true;
}
} // namespace
struct Decoder::Impl {
    MediaInfo meta;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    AVFrame* downloaded = av_frame_alloc();
    SwsContext* scaler = nullptr;
    int stream = -1, lastIndex = -1;
    int64_t startPts = 0;
    bool draining = false;
    bool requireSeek = false;
    QStringList images;
    VideoFrame lastImage;
    ~Impl() {
        sws_freeContext(scaler);
        av_frame_free(&decoded);
        av_frame_free(&downloaded);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    bool receive(QString& error, const std::function<bool()>& cancelled) {
        while (true) {
            if (cancelled && cancelled())
                return false;
            int r = avcodec_receive_frame(codec, decoded);
            if (r == 0)
                return true;
            if (r == AVERROR_EOF)
                return false;
            if (r != AVERROR(EAGAIN)) {
                error = avError(r);
                return false;
            }
            if (draining)
                return false;
            while ((r = av_read_frame(format, packet)) >= 0) {
                if (cancelled && cancelled()) {
                    av_packet_unref(packet);
                    return false;
                }
                if (packet->stream_index == stream) {
                    r = avcodec_send_packet(codec, packet);
                    av_packet_unref(packet);
                    if (r < 0 && r != AVERROR(EAGAIN)) {
                        error = avError(r);
                        return false;
                    }
                    break;
                }
                av_packet_unref(packet);
            }
            if (r < 0) {
                avcodec_send_packet(codec, nullptr);
                draining = true;
            }
        }
    }
};
Decoder::Decoder() : d(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;
const MediaInfo& Decoder::info() const {
    return d->meta;
}
QStringList Decoder::imageSequenceFiles(const QString& path) {
    QStringList images;
    const QFileInfo file(path);
    const auto suffix = file.suffix().toLower();
    if (QStringList{"png", "jpg", "jpeg", "bmp", "tif", "tiff"}.contains(suffix)) {
        const QRegularExpression expression("^(.*?)(\\d+)(\\.[^.]+)$");
        const auto match = expression.match(file.fileName());
        if (match.hasMatch()) {
            const QRegularExpression member("^" + QRegularExpression::escape(match.captured(1)) + "(\\d{" +
                                            QString::number(match.captured(2).size()) + "})" +
                                            QRegularExpression::escape(match.captured(3)) + "$");
            for (const auto& name : file.dir().entryList(QDir::Files, QDir::Name))
                if (member.match(name).hasMatch())
                    images.append(file.dir().absoluteFilePath(name));
        }
        if (images.isEmpty())
            images.append(file.absoluteFilePath());
    }
    return images;
}
bool Decoder::open(const QString& path, QString& error, int threads, bool hardware,
                   int hardwareMinimumPixels) {
    d = std::make_unique<Impl>();
    d->images = imageSequenceFiles(path);
    if (!d->images.isEmpty()) {
        const QImage first(d->images.first());
        if (first.isNull()) {
            error = "Image could not be decoded.";
            return false;
        }
        d->meta = {first.width(), first.height(), int(d->images.size()), 24, "Image sequence", true, false};
        return true;
    }
    if (!openContext(path, AVMEDIA_TYPE_VIDEO, d->format, d->codec, d->stream, error, threads, hardware,
                     hardwareMinimumPixels)) {
        if (!hardware)
            return false;
        d = std::make_unique<Impl>();
        error.clear();
        if (!openContext(path, AVMEDIA_TYPE_VIDEO, d->format, d->codec, d->stream, error, threads, false))
            return false;
    }
    const auto* s = d->format->streams[d->stream];
    AVRational fps = av_guess_frame_rate(d->format, const_cast<AVStream*>(s), nullptr);
    double rate = av_q2d(fps);
    if (!std::isfinite(rate) || rate < 1 || rate > 1000)
        rate = 24;
    const double seconds = s->duration != AV_NOPTS_VALUE ? s->duration * av_q2d(s->time_base)
                                                         : double(d->format->duration) / AV_TIME_BASE;
    const qint64 count =
        s->nb_frames > 0 ? s->nb_frames : std::max<qint64>(1, llround(std::max(0., seconds) * rate));
    if (count > std::numeric_limits<int>::max()) {
        error = "Media exceeds the supported frame count.";
        return false;
    }
    const double avg = av_q2d(s->avg_frame_rate);
    const bool vfr = avg > 0 && std::abs(avg - rate) / rate > 0.01;
    d->startPts = s->start_time == AV_NOPTS_VALUE ? 0 : s->start_time;
    d->meta = {d->codec->width,
               d->codec->height,
               int(count),
               rate,
               QString::fromUtf8(avcodec_get_name(d->codec->codec_id)),
               false,
               vfr};
    if (d->meta.width <= 0 || d->meta.height <= 0 || qint64(d->meta.width) * d->meta.height > 100'000'000) {
        error = "Unsupported frame dimensions.";
        return false;
    }
    return true;
}
QImage Decoder::frame(int index, QString& error, const std::function<bool()>& cancelled) {
    return videoFrame(index, error, cancelled).toImage();
}
VideoFrame Decoder::videoFrame(int index, QString& error, const std::function<bool()>& cancelled,
                               const std::function<void(int, const VideoFrame&)>& intermediate,
                               const std::function<bool(int)>& wanted, quint64 traceRequest) {
    tracePlayback("decode.begin", traceRequest, index);
    const auto traceEnd = qScopeGuard([&] { tracePlayback("decode.end", traceRequest, index); });
    error.clear();
    if (cancelled && cancelled())
        return {};
    if (index < 0 || index >= d->meta.frameCount)
        return {};
    if (d->meta.sequence) {
        QImage result(d->images[index]);
        if (result.isNull())
            error = "Could not decode sequence frame " + d->images[index];
        else if (result.size() != QSize(d->meta.width, d->meta.height)) {
            error = "Image sequence frames must have matching dimensions.";
            return {};
        }
        return VideoFrame(result);
    }
    if (d->lastIndex == index)
        return d->lastImage;
    const auto* s = d->format->streams[d->stream];
    // Small forward jumps can continue decoding instead of flushing the GOP.
    if (index <= d->lastIndex || index - d->lastIndex > 256 || d->draining || d->requireSeek) {
        const auto target = d->startPts + llround((double(index) / d->meta.fps) / av_q2d(s->time_base));
        tracePlayback("seek.begin", traceRequest, index);
        int r = av_seek_frame(d->format, d->stream, target, AVSEEK_FLAG_BACKWARD);
        tracePlayback("seek.end", traceRequest, index);
        if (r < 0) {
            error = "Seek failed: " + avError(r);
            return {};
        }
        avcodec_flush_buffers(d->codec);
        d->draining = false;
        d->requireSeek = false;
    }
    while (d->receive(error, cancelled)) {
        if (cancelled && cancelled()) {
            d->requireSeek = true;
            return {};
        }
        const auto pts = d->decoded->best_effort_timestamp;
        const int ordinal = pts == AV_NOPTS_VALUE
                                ? d->lastIndex + 1
                                : int(llround((pts - d->startPts) * av_q2d(s->time_base) * d->meta.fps));
        tracePlayback("codec.frame", traceRequest, ordinal);
        if (ordinal < 0 || (ordinal < index && (!intermediate || (wanted && !wanted(ordinal))))) {
            tracePlayback("frame.skipped", traceRequest, ordinal);
            continue;
        }
        VideoFrame pixels;
        const AVFrame* source = d->decoded;
        if (source->format == AV_PIX_FMT_D3D11) {
            tracePlayback("download.begin", traceRequest, ordinal);
            av_frame_unref(d->downloaded);
            const int result = av_hwframe_transfer_data(d->downloaded, source, 0);
            tracePlayback("download.end", traceRequest, ordinal);
            if (result < 0) {
                error = "GPU frame transfer failed: " + avError(result);
                return {};
            }
            av_frame_copy_props(d->downloaded, source);
            source = d->downloaded;
            d->meta.decodeBackend = "D3D11VA";
        }
        const auto format = AVPixelFormat(source->format);
        tracePlayback("pixels.begin", traceRequest, ordinal);
        if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P || format == AV_PIX_FMT_NV12) {
            const auto matrix = source->colorspace == AVCOL_SPC_BT709        ? VideoFrame::Matrix::Bt709
                                : source->colorspace == AVCOL_SPC_BT2020_NCL ? VideoFrame::Matrix::Bt2020
                                                                             : VideoFrame::Matrix::Bt601;
            pixels = VideoFrame::copyYuv420(
                source->width, source->height, source->data, source->linesize, matrix,
                source->color_range == AVCOL_RANGE_JPEG || format == AV_PIX_FMT_YUVJ420P,
                format == AV_PIX_FMT_NV12);
        } else {
            QImage image(d->meta.width, d->meta.height, QImage::Format_RGBA8888);
            if (image.isNull()) {
                error = "Not enough memory for the decoded frame.";
                return {};
            }
            d->scaler = sws_getCachedContext(d->scaler, source->width, source->height,
                                             AVPixelFormat(source->format), image.width(), image.height(),
                                             AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!d->scaler) {
                error = "Unsupported pixel conversion.";
                return {};
            }
            const auto cs = source->colorspace == AVCOL_SPC_BT709 ? SWS_CS_ITU709 : SWS_CS_ITU601;
            const int* coefficients = sws_getCoefficients(cs);
            sws_setColorspaceDetails(d->scaler, coefficients, source->color_range == AVCOL_RANGE_JPEG,
                                     coefficients, 1, 0, 1 << 16, 1 << 16);
            uint8_t* output[] = {image.bits(), nullptr, nullptr, nullptr};
            int strides[] = {int(image.bytesPerLine()), 0, 0, 0};
            sws_scale(d->scaler, source->data, source->linesize, 0, source->height, output, strides);
            pixels = VideoFrame(image);
        }
        d->lastIndex = ordinal;
        tracePlayback("pixels.end", traceRequest, ordinal, pixels.sizeInBytes());
        d->lastImage = pixels;
        if (intermediate && ordinal < index)
            intermediate(ordinal, pixels);
        if (ordinal >= index)
            return pixels;
    }
    if (cancelled && cancelled()) {
        d->requireSeek = true;
        return {};
    }
    if (error.isEmpty())
        error = "Requested frame is beyond the decoded end of this clip.";
    return {};
}
QByteArray Decoder::decodeAudio(const QString& path, qint64 budget, const std::function<bool()>& cancelled,
                                QString& warning) {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    int stream = -1;
    QString error;
    QByteArray pcm;
    if (!openContext(path, AVMEDIA_TYPE_AUDIO, format, codec, stream, error)) {
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        return {};
    }
    SwrContext* resampler = nullptr;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    int result = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, 48000, &codec->ch_layout,
                                     codec->sample_fmt, codec->sample_rate, 0, nullptr);
    if (result < 0 || swr_init(resampler) < 0) {
        warning = "Audio conversion unavailable.";
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
        return {};
    }
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool limit = false;
    auto receive = [&]() {
        while (!cancelled() && avcodec_receive_frame(codec, frame) == 0) {
            int count = int(av_rescale_rnd(swr_get_delay(resampler, codec->sample_rate) + frame->nb_samples,
                                           48000, codec->sample_rate, AV_ROUND_UP));
            QByteArray output(count * 4, Qt::Uninitialized);
            uint8_t* destination = reinterpret_cast<uint8_t*>(output.data());
            const int converted =
                swr_convert(resampler, &destination, count, const_cast<const uint8_t**>(frame->extended_data),
                            frame->nb_samples);
            if (converted > 0) {
                output.resize(converted * 4);
                if (pcm.size() + output.size() > budget) {
                    limit = true;
                    break;
                }
                pcm.append(output);
            }
        }
    };
    while (!cancelled() && !limit && av_read_frame(format, packet) >= 0) {
        if (packet->stream_index == stream) {
            avcodec_send_packet(codec, packet);
            receive();
        }
        av_packet_unref(packet);
    }
    if (!limit && !cancelled()) {
        avcodec_send_packet(codec, nullptr);
        receive();
    }
    if (limit)
        warning = "Audio RAM limit reached; audio is available for the cached beginning of this clip.";
    av_frame_free(&frame);
    av_packet_free(&packet);
    swr_free(&resampler);
    avcodec_free_context(&codec);
    avformat_close_input(&format);
    return cancelled() ? QByteArray{} : pcm;
}
} // namespace tfv

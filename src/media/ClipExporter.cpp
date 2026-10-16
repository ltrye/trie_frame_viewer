#include "ClipExporter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace tfv {
ClipExporter::ClipExporter(const QJsonObject& options, QObject* parent)
    : QObject(parent), m_options(options) {
    m_process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* arguments) { arguments->flags |= CREATE_NO_WINDOW; });
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_cancelled)
            m_process.kill();
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process.readAllStandardError();
        m_stderr = m_stderr.right(16384);
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_process.canReadLine()) {
            const auto line = m_process.readLine().trimmed();
            if (!line.startsWith("frame="))
                continue;
            const double progress = std::clamp(line.mid(6).toDouble() / m_frames, 0., 0.99);
            if (progress != m_progress) {
                m_progress = progress;
                emit stateChanged();
            }
        }
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            complete(false,
                     tr("Could not start the bundled video exporter: %1").arg(m_process.errorString()));
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_stderr += m_process.readAllStandardError();
        if (m_cancelled) {
            complete(false);
            return;
        }
        if (code != 0 || status != QProcess::NormalExit) {
            complete(false, tr("Export failed. %1").arg(QString::fromUtf8(m_stderr.right(4000)).trimmed()));
            return;
        }
        QString error;
        const bool success = commit(error);
        complete(success, error);
    });
}
ClipExporter::~ClipExporter() {
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
}
bool ClipExporter::start(const ClipExportRequest& request, const QUrl& destination) {
    if (m_busy)
        return false;
    m_error.clear();
    m_progress = 0;
    m_cancelled = false;
    m_output = destination;
    m_stderr.clear();
    const auto reject = [this](const QString& error) {
        m_error = error;
        m_temporary.reset();
        m_sequence.reset();
        emit stateChanged();
        return false;
    };
    if (!destination.isLocalFile() ||
        QFileInfo(destination.toLocalFile()).suffix().compare("mp4", Qt::CaseInsensitive))
        return reject(tr("Choose a local .mp4 output file."));
    const QFileInfo source(request.source), output(destination.toLocalFile());
    if (!source.isFile() || request.start < 0 || request.end < request.start ||
        request.end >= request.frameCount || !std::isfinite(request.fps) || request.fps < 1 ||
        request.fps > 1000 || request.width <= 0 || request.height <= 0)
        return reject(tr("The media or selected frame range is invalid."));
    const bool resize = request.outputWidth != 0 || request.outputHeight != 0;
    if (resize && (request.outputWidth < 2 || request.outputHeight < 2 || request.outputWidth > 8192 ||
                   request.outputHeight > 8192 || request.outputWidth % 2 || request.outputHeight % 2))
        return reject(tr("Choose a valid export resolution."));
    const auto sameFile = [](const QFileInfo& a, const QFileInfo& b) {
        return a.exists() && b.exists() &&
               a.canonicalFilePath().compare(b.canonicalFilePath(), Qt::CaseInsensitive) == 0;
    };
    if (sameFile(source, output))
        return reject(tr("Choose a new filename. The original media cannot be replaced."));
    for (const auto& image : request.images)
        if (sameFile(QFileInfo(image), output))
            return reject(tr("The original media cannot be replaced."));
    const QString executable = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (!QFileInfo::exists(executable))
        return reject(tr("The bundled video exporter is missing. Reinstall FrameViewer."));
    m_temporary = std::make_unique<QTemporaryFile>(output.absolutePath() + "/.tfv-export-XXXXXX.mp4");
    if (!m_temporary->open())
        return reject(tr("Cannot write to this folder: %1").arg(m_temporary->errorString()));
    const QString staging = m_temporary->fileName();
    m_temporary->close();
    const auto seconds = [](double value) { return QString::number(value, 'f', 9); };
    const double duration = double(request.end - request.start + 1) / request.fps;
    const int threads = std::clamp(m_options.value("threads").toInt(2), 1, 16);
    QStringList arguments{"-hide_banner",
                          "-nostdin",
                          "-loglevel",
                          "error",
                          "-y",
                          "-progress",
                          "pipe:1",
                          "-stats_period",
                          "0.1",
                          "-filter_threads",
                          "1",
                          "-threads",
                          QString::number(threads)};
    if (request.images.isEmpty()) {
        arguments << "-ss" << seconds(request.start / request.fps) << "-i" << source.absoluteFilePath();
    } else {
        if (request.images.size() != request.frameCount)
            return reject(tr("The image sequence has changed. Reopen it before exporting."));
        m_sequence = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/tfv-sequence-XXXXXX.ffconcat");
        if (!m_sequence->open())
            return reject(tr("Cannot prepare the image sequence for export."));
        QByteArray manifest("ffconcat version 1.0\n");
        const auto entry = [](QString file) {
            file = QDir::fromNativeSeparators(file);
            file.replace("'", "'\\''");
            return "file '" + file.toUtf8() + "'\n";
        };
        for (int frame = request.start; frame <= request.end; ++frame) {
            manifest += entry(request.images[frame]);
            manifest += "option framerate " + QString::number(request.fps, 'g', 15).toUtf8() + '\n';
            manifest += "duration " + seconds(1. / request.fps).toUtf8() + '\n';
        }
        manifest += entry(request.images[request.end]);
        manifest += "option framerate " + QString::number(request.fps, 'g', 15).toUtf8() + '\n';
        if (m_sequence->write(manifest) != manifest.size() || !m_sequence->flush())
            return reject(tr("Cannot prepare the image sequence for export."));
        const QString sequence = m_sequence->fileName();
        m_sequence->close();
        arguments << "-f" << "concat" << "-safe" << "0" << "-i" << sequence;
    }
    const int outputWidth = resize ? request.outputWidth : request.width;
    const int outputHeight = resize ? request.outputHeight : request.height;
    const QString filter =
        resize ? QString("scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2:reset_sar=1,"
                         "pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1,format=yuv420p")
                     .arg(outputWidth)
                     .arg(outputHeight)
               : QString("pad=ceil(iw/2)*2:ceil(ih/2)*2,format=yuv420p");
    const qint64 bitrate = std::clamp<qint64>(qint64(outputWidth * double(outputHeight) * request.fps *
                                                     m_options.value("bitsPerPixel").toDouble(0.2)),
                                              m_options.value("minimumBitrate").toInt(2000000),
                                              m_options.value("maximumBitrate").toInt(80000000));
    arguments << "-map" << "0:v:0" << "-map" << "0:a:0?" << "-t" << seconds(duration) << "-frames:v"
              << QString::number(request.end - request.start + 1) << "-r"
              << QString::number(request.fps, 'g', 15) << "-vf" << filter << "-c:v"
              << m_options.value("videoEncoder").toString("libopenh264") << "-b:v" << QString::number(bitrate)
              << "-threads" << QString::number(threads) << "-c:a" << "aac"
              << "-b:a" << QString::number(m_options.value("audioBitrate").toInt(192000)) << "-af" << "apad"
              << "-map_metadata" << "-1" << "-map_chapters" << "-1"
              << "-movflags" << "+faststart" << "-f" << "mp4" << staging;
    m_frames = request.end - request.start + 1;
    m_busy = true;
    emit stateChanged();
    m_process.start(executable, arguments);
    return true;
}
void ClipExporter::cancel() {
    if (!m_busy || m_cancelled)
        return;
    m_cancelled = true;
    m_process.kill();
}
bool ClipExporter::commit(QString& error) {
    if (!m_temporary || QFileInfo(m_temporary->fileName()).size() == 0) {
        error = tr("The exporter did not produce a clip.");
        return false;
    }
    const QString staging = m_temporary->fileName();
    const auto temporary = staging.toStdWString();
    const auto target = m_output.toLocalFile().toStdWString();
    // QTemporaryFile::close() retains its native handle for reopening. Release
    // that handle before Windows attempts to rename/replace the finished file.
    m_temporary->setAutoRemove(false);
    m_temporary.reset();
    // Both paths are in the same folder/volume. Publish only a complete file;
    // failed or cancelled exports never destroy an existing destination.
    const bool exists = QFileInfo::exists(m_output.toLocalFile());
    const BOOL saved = exists ? ReplaceFileW(target.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr)
                              : MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!saved) {
        const DWORD reason = GetLastError();
        QFile::remove(staging);
        error = tr("Could not save the finished clip (Windows error %1). Check write permissions and whether "
                   "the output is open.")
                    .arg(reason);
        return false;
    }
    return true;
}
void ClipExporter::complete(bool success, const QString& error) {
    if (!m_busy)
        return;
    m_busy = false;
    m_error = error;
    m_progress = success ? 1 : m_progress;
    m_temporary.reset();
    m_sequence.reset();
    emit stateChanged();
    emit finished(success, m_cancelled, m_output);
}
} // namespace tfv

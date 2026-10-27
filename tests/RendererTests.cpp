#include "presentation/VideoItem.h"
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>

class RendererTests : public QObject {
    Q_OBJECT
  private slots:
    void textureStorageRecyclesWithoutChangingPixels() {
        QQuickWindow window;
        window.resize(160, 90);
        tfv::VideoItem surface(window.contentItem());
        surface.setSize(QSizeF(160, 90));
        surface.setGpuCacheMiB(1);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        for (int i = 0; i < 48; ++i) {
            QByteArray y(320 * 180, char(20 + i * 4)), u(160 * 90, char(128)), v = u;
            const uint8_t* pixels[] = {reinterpret_cast<const uint8_t*>(y.constData()),
                                       reinterpret_cast<const uint8_t*>(u.constData()),
                                       reinterpret_cast<const uint8_t*>(v.constData())};
            const int strides[] = {320, 160, 160};
            const auto frame =
                tfv::VideoFrame::copyYuv420(320, 180, pixels, strides, tfv::VideoFrame::Matrix::Bt709, false);
            surface.setFrame(frame);
            const auto rendered = window.grabWindow();
            QVERIFY(!rendered.isNull());
            const auto gpu = rendered.pixelColor(80, 45), cpu = frame.toImage().pixelColor(80, 45);
            QVERIFY(qAbs(gpu.red() - cpu.red()) <= 3 && qAbs(gpu.green() - cpu.green()) <= 3 &&
                    qAbs(gpu.blue() - cpu.blue()) <= 3);
            QVERIFY(surface.gpuCacheUsedMiB() <= 1.);
        }
        QCOMPARE(surface.textureSetsCreated(), quint64(12));
        QCOMPARE(surface.textureSetsReused(), quint64(36));
    }
    void nativeYuvMatchesCpuColorConversion() {
        QQuickWindow window;
        window.resize(129, 65);
        tfv::VideoItem surface(window.contentItem());
        surface.setSize(QSizeF(129, 65));
        surface.setGpuCacheMiB(16);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software);
        tfv::VideoFrame first;
        QColor firstColor;
        double previousBytes = 0;
        for (auto matrix : {tfv::VideoFrame::Matrix::Bt601, tfv::VideoFrame::Matrix::Bt709,
                            tfv::VideoFrame::Matrix::Bt2020}) {
            for (bool full : {false, true}) {
                for (const auto& value :
                     {QList<int>{16, 128, 128}, {235, 128, 128}, {100, 80, 170}, {160, 200, 60}}) {
                    QByteArray y(129 * 65, char(value[0])), u(65 * 33, char(value[1])),
                        v(65 * 33, char(value[2]));
                    const uint8_t* data[] = {reinterpret_cast<const uint8_t*>(y.constData()),
                                             reinterpret_cast<const uint8_t*>(u.constData()),
                                             reinterpret_cast<const uint8_t*>(v.constData())};
                    const int strides[] = {129, 65, 65};
                    const auto frame = tfv::VideoFrame::copyYuv420(129, 65, data, strides, matrix, full);
                    surface.setFrame(frame);
                    const auto rendered = window.grabWindow();
                    QVERIFY(!rendered.isNull());
                    const auto gpu = rendered.pixelColor(64, 32);
                    const auto cpu = frame.toImage().pixelColor(64, 32);
                    if (first.isNull()) {
                        first = frame;
                        firstColor = gpu;
                    }
                    QVERIFY(surface.gpuCacheUsedMiB() > previousBytes);
                    previousBytes = surface.gpuCacheUsedMiB();
                    QVERIFY2(std::abs(gpu.red() - cpu.red()) <= 3 &&
                                 std::abs(gpu.green() - cpu.green()) <= 3 &&
                                 std::abs(gpu.blue() - cpu.blue()) <= 3,
                             qPrintable(QString("GPU %1/%2/%3 CPU %4/%5/%6")
                                            .arg(gpu.red())
                                            .arg(gpu.green())
                                            .arg(gpu.blue())
                                            .arg(cpu.red())
                                            .arg(cpu.green())
                                            .arg(cpu.blue())));
                    // Revisit an existing frame through the GPU texture cache.
                    surface.setFrame(frame);
                    QCOMPARE(window.grabWindow().pixelColor(64, 32), gpu);
                    QCOMPARE(surface.gpuCacheUsedMiB(), previousBytes);
                }
            }
        }
        // The first frame survives multiple distinct uploads, and a budget of
        // zero evicts everything except the current frame.
        surface.setFrame(first);
        QCOMPARE(window.grabWindow().pixelColor(64, 32), firstColor);
        QCOMPARE(surface.gpuCacheUsedMiB(), previousBytes);
        surface.setGpuCacheMiB(0);
        QCOMPARE(window.grabWindow().pixelColor(64, 32), firstColor);
        QCOMPARE(surface.gpuCacheUsedMiB(), first.sizeInBytes() / 1048576.);
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    RendererTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "RendererTests.moc"

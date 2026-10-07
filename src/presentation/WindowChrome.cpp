#include "WindowChrome.h"
#include <QEvent>
#include <QGuiApplication>
#include <QTimer>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#include <windows.h>
#endif

namespace tfv {
WindowChrome::WindowChrome(QObject* parent) : QObject(parent) {
    connect(this, &WindowChrome::styleChanged, this, &WindowChrome::scheduleApply);
}
void WindowChrome::setWindow(QWindow* window) {
    if (m_window == window)
        return;
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (window) {
        window->installEventFilter(this);
        connect(window, &QWindow::activeChanged, this, &WindowChrome::scheduleApply);
    }
    emit styleChanged();
}
void WindowChrome::setDark(bool dark) {
    if (m_dark != dark) {
        m_dark = dark;
        emit styleChanged();
    }
}
void WindowChrome::setCaptionColor(const QColor& color) {
    if (m_caption != color) {
        m_caption = color;
        emit styleChanged();
    }
}
void WindowChrome::setTextColor(const QColor& color) {
    if (m_text != color) {
        m_text = color;
        emit styleChanged();
    }
}
void WindowChrome::setBorderColor(const QColor& color) {
    if (m_border != color) {
        m_border = color;
        emit styleChanged();
    }
}
bool WindowChrome::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_window &&
        (event->type() == QEvent::PlatformSurface || event->type() == QEvent::WinIdChange ||
         event->type() == QEvent::ThemeChange || event->type() == QEvent::WindowStateChange))
        scheduleApply();
    return QObject::eventFilter(watched, event);
}
void WindowChrome::scheduleApply() {
    if (m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        apply();
    });
}
void WindowChrome::apply() {
    bool applied = false;
#ifdef Q_OS_WIN
    if (m_window && m_window->handle() && QGuiApplication::platformName() == "windows") {
        const auto handle = reinterpret_cast<HWND>(m_window->winId());
        const BOOL dark = m_dark;
        DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        const auto nativeColor = [](const QColor& color) -> COLORREF {
            return color.isValid() ? RGB(color.red(), color.green(), color.blue()) : DWMWA_COLOR_DEFAULT;
        };
        const COLORREF caption = nativeColor(m_caption), text = nativeColor(m_text),
                       border = nativeColor(m_border);
        const HRESULT captionResult =
            DwmSetWindowAttribute(handle, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
        const HRESULT textResult = DwmSetWindowAttribute(handle, DWMWA_TEXT_COLOR, &text, sizeof(text));
        DwmSetWindowAttribute(handle, DWMWA_BORDER_COLOR, &border, sizeof(border));
        applied = SUCCEEDED(captionResult) && SUCCEEDED(textResult);
    }
#endif
    if (m_applied != applied) {
        m_applied = applied;
        emit appliedChanged();
    }
}
} // namespace tfv

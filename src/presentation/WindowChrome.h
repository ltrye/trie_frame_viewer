#pragma once
#include <QColor>
#include <QObject>
#include <QPointer>
#include <QWindow>

namespace tfv {
// Uses the Windows window frame; QML owns the theme colors and sizing.
class WindowChrome : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY styleChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY styleChanged)
    Q_PROPERTY(QColor captionColor READ captionColor WRITE setCaptionColor NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY styleChanged)
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY styleChanged)
    Q_PROPERTY(bool applied READ applied NOTIFY appliedChanged)
  public:
    explicit WindowChrome(QObject* parent = nullptr);
    QWindow* window() const {
        return m_window;
    }
    bool dark() const {
        return m_dark;
    }
    QColor captionColor() const {
        return m_caption;
    }
    QColor textColor() const {
        return m_text;
    }
    QColor borderColor() const {
        return m_border;
    }
    bool applied() const {
        return m_applied;
    }
    void setWindow(QWindow* window);
    void setDark(bool dark);
    void setCaptionColor(const QColor& color);
    void setTextColor(const QColor& color);
    void setBorderColor(const QColor& color);
  signals:
    void styleChanged();
    void appliedChanged();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void scheduleApply();
    void apply();
    QPointer<QWindow> m_window;
    QColor m_caption, m_text, m_border;
    bool m_dark = true, m_applied = false, m_scheduled = false;
};
} // namespace tfv

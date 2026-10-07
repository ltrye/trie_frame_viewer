#pragma once
#include <QAbstractListModel>
#include <QJsonArray>

namespace tfv {
struct Bookmark {
    QString id, name, color;
    int frame = 0;
};
class BookmarkModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, ColorRole, FrameRole };
    explicit BookmarkModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void add(int frame, const QString& color);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void rename(const QString& id, const QString& name);
    bool setFrame(const QString& id, int frame, int frameCount);
    const Bookmark* at(int row) const;
    const Bookmark* find(const QString& id) const;
    int adjacentFrame(int current, bool forward) const;
    QJsonArray toJson() const;
    bool load(const QJsonArray& values, int frameCount, QString& error);
    void clear();
  signals:
    void countChanged();
    void edited();

  private:
    int rowForId(const QString& id) const;
    QList<Bookmark> m_items;
};
} // namespace tfv

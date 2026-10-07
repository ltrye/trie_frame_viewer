#pragma once
#include <QAbstractListModel>
#include <QJsonArray>

namespace tfv {
struct Bookmark {
    QString id, name, color;
    int start = 0, end = 0;
    bool range = false;
};
class BookmarkModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
  public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, ColorRole, StartRole, EndRole, RangeRole };
    explicit BookmarkModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void add(int start, int end, bool range, const QString& color);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void rename(int row, const QString& name);
    const Bookmark* at(int row) const;
    int adjacentFrame(int current, bool forward) const;
    QJsonArray toJson() const;
    bool load(const QJsonArray& values, int frameCount, QString& error);
    void clear();
  signals:
    void countChanged();
    void edited();

  private:
    QList<Bookmark> m_items;
};
} // namespace tfv

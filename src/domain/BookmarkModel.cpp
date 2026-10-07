#include "BookmarkModel.h"
#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace tfv {
int BookmarkModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_items.size());
}
QVariant BookmarkModel::data(const QModelIndex& index, int role) const {
    const auto* b = at(index.row());
    if (!b)
        return {};
    switch (role) {
    case IdRole:
        return b->id;
    case NameRole:
        return b->name;
    case ColorRole:
        return b->color;
    case StartRole:
        return b->start;
    case EndRole:
        return b->end;
    case RangeRole:
        return b->range;
    default:
        return {};
    }
}
QHash<int, QByteArray> BookmarkModel::roleNames() const {
    return {{IdRole, "bookmarkId"},    {NameRole, "bookmarkName"}, {ColorRole, "bookmarkColor"},
            {StartRole, "startFrame"}, {EndRole, "endFrame"},      {RangeRole, "isRange"}};
}
const Bookmark* BookmarkModel::at(int row) const {
    return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr;
}
void BookmarkModel::add(int start, int end, bool range, const QString& color) {
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_items.append(
        {QUuid::createUuid().toString(QUuid::WithoutBraces),
         range ? QString("Range %1–%2").arg(start + 1).arg(end + 1) : QString("Pose at %1").arg(start + 1),
         color, start, end, range});
    endInsertRows();
    emit countChanged();
    emit edited();
}
void BookmarkModel::remove(int row) {
    if (!at(row))
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit edited();
}
void BookmarkModel::rename(int row, const QString& name) {
    if (!at(row) || name.trimmed().isEmpty())
        return;
    m_items[row].name = name.trimmed();
    emit dataChanged(index(row), index(row), {NameRole});
    emit edited();
}
int BookmarkModel::adjacentFrame(int current, bool forward) const {
    QList<int> frames;
    for (const auto& b : m_items)
        frames.append(b.start);
    if (frames.isEmpty())
        return current;
    std::sort(frames.begin(), frames.end());
    if (forward) {
        for (int f : frames)
            if (f > current)
                return f;
        return frames.first();
    }
    for (auto it = frames.crbegin(); it != frames.crend(); ++it)
        if (*it < current)
            return *it;
    return frames.last();
}
QJsonArray BookmarkModel::toJson() const {
    QJsonArray result;
    for (const auto& b : m_items)
        result.append(QJsonObject{{"id", b.id},
                                  {"name", b.name},
                                  {"color", b.color},
                                  {"type", b.range ? "range" : "frame"},
                                  {"start", b.start},
                                  {"endInclusive", b.end}});
    return result;
}
bool BookmarkModel::load(const QJsonArray& values, int frameCount, QString& error) {
    QList<Bookmark> parsed;
    QSet<QString> ids;
    for (const auto& value : values) {
        const auto o = value.toObject();
        const auto type = o.value("type").toString();
        const int start = o.value("start").toInt(-1), end = o.value("endInclusive").toInt(-1);
        const QString id = o.value("id").toString(), color = o.value("color").toString();
        if ((type != "frame" && type != "range") || start < 0 || end < start || end >= frameCount ||
            (type == "frame" && end != start) || id.isEmpty() || ids.contains(id) ||
            !QColor(color).isValid()) {
            error = "Project contains an invalid bookmark.";
            return false;
        }
        ids.insert(id);
        parsed.append({id, o.value("name").toString(), color, start, end, type == "range"});
    }
    beginResetModel();
    m_items = parsed;
    endResetModel();
    emit countChanged();
    return true;
}
void BookmarkModel::clear() {
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}
} // namespace tfv

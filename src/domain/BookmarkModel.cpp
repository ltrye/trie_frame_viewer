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
    case FrameRole:
        return b->frame;
    default:
        return {};
    }
}
QHash<int, QByteArray> BookmarkModel::roleNames() const {
    return {{IdRole, "bookmarkId"},
            {NameRole, "bookmarkName"},
            {ColorRole, "bookmarkColor"},
            {FrameRole, "bookmarkFrame"}};
}
const Bookmark* BookmarkModel::at(int row) const {
    return row >= 0 && row < m_items.size() ? &m_items[row] : nullptr;
}
int BookmarkModel::rowForId(const QString& id) const {
    for (int row = 0; row < m_items.size(); ++row)
        if (m_items[row].id == id)
            return row;
    return -1;
}
const Bookmark* BookmarkModel::find(const QString& id) const {
    return at(rowForId(id));
}
void BookmarkModel::add(int frame, const QString& color) {
    const int row = rowCount();
    beginInsertRows({}, row, row);
    m_items.append({QUuid::createUuid().toString(QUuid::WithoutBraces), QString("Pose at %1").arg(frame + 1),
                    color, frame});
    endInsertRows();
    emit countChanged();
    emit edited();
}
void BookmarkModel::remove(const QString& id) {
    const int row = rowForId(id);
    if (!at(row))
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    emit countChanged();
    emit edited();
}
void BookmarkModel::rename(const QString& id, const QString& name) {
    const int row = rowForId(id);
    if (!at(row) || name.trimmed().isEmpty() || m_items[row].name == name.trimmed())
        return;
    m_items[row].name = name.trimmed();
    emit dataChanged(index(row), index(row), {NameRole});
    emit edited();
}
bool BookmarkModel::setFrame(const QString& id, int frame, int frameCount) {
    const int row = rowForId(id);
    if (!at(row) || frame < 0 || frame >= frameCount)
        return false;
    if (m_items[row].frame != frame) {
        m_items[row].frame = frame;
        emit dataChanged(index(row), index(row), {FrameRole});
        emit edited();
    }
    return true;
}
int BookmarkModel::adjacentFrame(int current, bool forward) const {
    QList<int> frames;
    for (const auto& b : m_items)
        frames.append(b.frame);
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
                                  {"type", "frame"},
                                  {"start", b.frame},
                                  {"endInclusive", b.frame}});
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
        // Version 1 range bookmarks migrate to their first frame. Preserve their identity and name.
        parsed.append({id, o.value("name").toString(), color, start});
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

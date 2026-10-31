// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrentmodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSet>
#include <QIcon>
#include <QJsonArray>
#include <QLocale>

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    enum Format { Text, Bytes, Rate, Percentage, Ratio, Count, Time, Date, State, Boolean };
    struct Column { const char *label; const char *key; Format format; };
    // Same column order as the upstream TransferListModel.
    const Column columns[] = {
        {"#", "priority", Count}, {"Name", "name", Text}, {"Size", "size", Bytes},
        {"Total Size", "total_size", Bytes}, {"Progress", "progress", Percentage},
        {"Status", "state", State}, {"Seeds", "num_seeds", Count}, {"Peers", "num_leechs", Count},
        {"Down Speed", "dlspeed", Rate}, {"Up Speed", "upspeed", Rate}, {"ETA", "eta", Time},
        {"Ratio", "ratio", Ratio}, {"Popularity", "popularity", Ratio},
        {"Category", "category", Text}, {"Tags", "tags", Text},
        {"Added On", "added_on", Date}, {"Completed On", "completion_on", Date},
        {"Tracker", "tracker", Text}, {"Down Limit", "dl_limit", Rate}, {"Up Limit", "up_limit", Rate},
        {"Downloaded", "downloaded", Bytes}, {"Uploaded", "uploaded", Bytes},
        {"Session Download", "downloaded_session", Bytes}, {"Session Upload", "uploaded_session", Bytes},
        {"Remaining", "amount_left", Bytes}, {"Time Active", "time_active", Time},
        {"Save Path", "save_path", Text}, {"Completed", "completed", Bytes},
        {"Ratio Limit", "max_ratio", Ratio}, {"Last Seen Complete", "seen_complete", Date},
        {"Last Activity", "last_activity", Date}, {"Availability", "availability", Ratio},
        {"Download Path", "download_path", Text}, {"Info Hash v1", "infohash_v1", Text},
        {"Info Hash v2", "infohash_v2", Text}, {"Reannounce In", "reannounce", Time},
        {"Private", "private", Boolean}, {"Created On", "creation_date", Date}
    };
}

TorrentModel::TorrentModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int TorrentModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_hashes.size();
}

int TorrentModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : std::size(columns);
}

QString TorrentModel::hashAt(const int row) const
{
    return m_hashes.value(row);
}

QJsonObject TorrentModel::torrentAt(const int row) const
{
    return m_torrents.value(hashAt(row));
}

QString TorrentModel::size(const qint64 bytes)
{
    return QLocale().formattedDataSize(qMax<qint64>(bytes, 0), 1);
}

QString TorrentModel::speed(const qint64 bytes)
{
    return size(bytes) + u"/s";
}

QString TorrentModel::duration(const qint64 seconds)
{
    if ((seconds < 0) || (seconds >= 8640000))
        return u"∞"_s;
    if (seconds >= 86400)
        return tr("%1d %2h").arg(seconds / 86400).arg((seconds / 3600) % 24);
    if (seconds >= 3600)
        return tr("%1h %2m").arg(seconds / 3600).arg((seconds / 60) % 60);
    return tr("%1m %2s").arg(seconds / 60).arg(seconds % 60);
}

QString TorrentModel::stateName(const QString &state)
{
    static const QHash<QString, QString> names {
        {u"downloading"_s, tr("Downloading")}, {u"stalledDL"_s, tr("Stalled")},
        {u"metaDL"_s, tr("Downloading metadata")}, {u"forcedMetaDL"_s, tr("[F] Downloading metadata")},
        {u"forcedDL"_s, tr("[F] Downloading")}, {u"uploading"_s, tr("Seeding")},
        {u"stalledUP"_s, tr("Seeding")}, {u"forcedUP"_s, tr("[F] Seeding")},
        {u"queuedDL"_s, tr("Queued")}, {u"queuedUP"_s, tr("Queued")},
        {u"checkingDL"_s, tr("Checking")}, {u"checkingUP"_s, tr("Checking")},
        {u"checkingResumeData"_s, tr("Checking resume data")},
        {u"stoppedDL"_s, tr("Stopped")}, {u"stoppedUP"_s, tr("Completed")},
        {u"moving"_s, tr("Moving")}, {u"missingFiles"_s, tr("Missing Files")},
        {u"error"_s, tr("Errored")}
    };
    return names.value(state, state);
}

QVariant TorrentModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || (index.row() >= rowCount()) || (index.column() >= columnCount()))
        return {};
    const auto torrent = torrentAt(index.row());
    const auto &column = columns[index.column()];
    const auto value = torrent.value(QString::fromLatin1(column.key));
    if (role == Qt::UserRole)
        return value.toVariant();
    if ((role == Qt::DecorationRole) && (index.column() == 1))
    {
        const QString state = torrent.value(u"state"_s).toString();
        const QString icon = (state == u"error" || state == u"missingFiles") ? u"error"_s
            : state.startsWith(u"checking") ? u"force-recheck"_s
            : state.contains(u"DL") || (state == u"downloading") ? u"downloading"_s
            : state.startsWith(u"stopped") ? u"stopped"_s : u"upload"_s;
        return QIcon(u":/icons/" + icon + u".svg");
    }
    if ((role == Qt::TextAlignmentRole) && (column.format != Text) && (column.format != State))
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    if (value.isUndefined())
        return QString();
    const qint64 number = value.toInteger();
    switch (column.format)
    {
    case Bytes: return size(number);
    case Rate: return speed(number);
    case Percentage: return u"%1%"_s.arg(value.toDouble() * 100, 0, 'f', 1);
    case Ratio: return (value.toDouble() < 0) ? u"∞"_s : QString::number(value.toDouble(), 'f', 2);
    case Date: return number > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(number), QLocale::ShortFormat) : QString();
    case Time: return duration(number);
    case State: return stateName(value.toString());
    case Boolean: return value.toBool() ? tr("Yes") : tr("No");
    case Count:
        if (index.column() == 6)
            return u"%1 (%2)"_s.arg(number).arg(torrent.value(u"num_complete"_s).toInteger());
        if (index.column() == 7)
            return u"%1 (%2)"_s.arg(number).arg(torrent.value(u"num_incomplete"_s).toInteger());
        return (number < 0) ? QString() : QString::number(number);
    case Text: return value.toString();
    }
    return {};
}

QVariant TorrentModel::headerData(const int section, const Qt::Orientation orientation, const int role) const
{
    if ((orientation == Qt::Horizontal) && (role == Qt::DisplayRole)
        && (section >= 0) && (section < columnCount()))
        return QCoreApplication::translate("TransferListModel", columns[section].label);
    return {};
}

void TorrentModel::clear()
{
    beginResetModel();
    m_hashes.clear();
    m_torrents.clear();
    endResetModel();
}

void TorrentModel::applySync(const QJsonObject &sync)
{
    const auto updates = sync.value(u"torrents"_s).toObject();
    QSet<QString> removed;
    for (const auto value : sync.value(u"torrents_removed"_s).toArray())
        removed.insert(value.toString());
    // Preserve model indexes and selections, including across a full reconnect snapshot.
    if (sync.value(u"full_update"_s).toBool())
    {
        for (const auto &hash : m_hashes)
        {
            if (!updates.contains(hash))
                removed.insert(hash);
        }
    }
    for (int row = m_hashes.size() - 1; row >= 0; --row)
    {
        if (removed.contains(m_hashes[row]))
        {
            beginRemoveRows({}, row, row);
            m_torrents.remove(m_hashes.takeAt(row));
            endRemoveRows();
        }
    }
    for (auto it = updates.begin(); it != updates.end(); ++it)
    {
        const auto delta = it.value().toObject();
        if (!m_torrents.contains(it.key()))
        {
            const int row = m_hashes.size();
            beginInsertRows({}, row, row);
            m_hashes.append(it.key());
            m_torrents.insert(it.key(), delta);
            endInsertRows();
        }
        else
        {
            auto &torrent = m_torrents[it.key()];
            if (sync.value(u"full_update"_s).toBool())
                torrent = delta;
            else
            {
                for (auto field = delta.begin(); field != delta.end(); ++field)
                    torrent.insert(field.key(), field.value());
            }
            const int row = m_hashes.indexOf(it.key());
            emit dataChanged(index(row, 0), index(row, columnCount() - 1));
        }
    }
}

void TorrentFilter::setFilters(const QString &text, const QString &state, const QString &category,
    const QString &tag)
{
    m_text = text;
    m_state = state;
    m_category = category;
    m_tag = tag;
    invalidateFilter();
}

bool TorrentFilter::filterAcceptsRow(const int row, const QModelIndex &) const
{
    const auto *model = static_cast<const TorrentModel *>(sourceModel());
    const auto data = model->torrentAt(row);
    if (!data.value(u"name"_s).toString().contains(m_text, Qt::CaseInsensitive))
        return false;
    const QString state = data.value(u"state"_s).toString();
    const bool complete = (data.value(u"progress"_s).toDouble() >= 1);
    if ((m_state == u"downloading") && complete)
        return false;
    if ((m_state == u"completed") && !complete)
        return false;
    if ((m_state == u"stopped") && !state.startsWith(u"stopped"))
        return false;
    if ((m_state == u"errored") && (state != u"error") && (state != u"missingFiles"))
        return false;
    if (!m_category.isNull() && (data.value(u"category"_s).toString() != m_category))
        return false;
    if (!m_tag.isNull() && !data.value(u"tags"_s).toString().split(u", "_s).contains(m_tag))
        return false;
    return true;
}

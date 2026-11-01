// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrentmodel.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPalette>
#include "gui/color.h"
#include <QDateTime>
#include <QSet>
#include <QUrl>
#include <QIcon>
#include <QJsonArray>
#include <QLocale>

#include "base/bittorrent/torrent.h"
#include "base/utils/misc.h"
#include "gui/transferlistmodel.h"
#include "gui/uithememanager.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    enum Format { Text, Bytes, Rate, Percentage, Ratio, Count, Time, Date, State, Boolean };
    struct Column { const char *label; const char *key; Format format; const char *comment = nullptr; };
    // Same column order as the upstream TransferListModel.
    const Column columns[] = {
        {"#", "priority", Count}, {"Name", "name", Text, "i.e: torrent name"}, {"Size", "size", Bytes, "i.e: torrent size"},
        {"Total Size", "total_size", Bytes, "i.e. Size including unwanted data"}, {"Progress", "progress", Percentage, "% Done"},
        {"Status", "state", State, "Torrent status (e.g. downloading, seeding, stopped)"}, {"Seeds", "num_seeds", Count, "i.e. full sources (often untranslated)"}, {"Peers", "num_leechs", Count, "i.e. partial sources (often untranslated)"},
        {"Down Speed", "dlspeed", Rate, "i.e: Download speed"}, {"Up Speed", "upspeed", Rate, "i.e: Upload speed"}, {"ETA", "eta", Time, "i.e: Estimated Time of Arrival / Time left"},
        {"Ratio", "ratio", Ratio, "Share ratio"}, {"Popularity", "popularity", Ratio},
        {"Category", "category", Text}, {"Tags", "tags", Text},
        {"Added On", "added_on", Date, "Torrent was added to transfer list on 01/01/2010 08:00"}, {"Completed On", "completion_on", Date, "Torrent was completed on 01/01/2010 08:00"},
        {"Tracker", "tracker", Text}, {"Down Limit", "dl_limit", Rate, "i.e: Download limit"}, {"Up Limit", "up_limit", Rate, "i.e: Upload limit"},
        {"Downloaded", "downloaded", Bytes, "Amount of data downloaded (e.g. in MB)"}, {"Uploaded", "uploaded", Bytes, "Amount of data uploaded (e.g. in MB)"},
        {"Session Downloaded", "downloaded_session", Bytes, "Amount of data downloaded since program open (e.g. in MB)"}, {"Session Uploaded", "uploaded_session", Bytes, "Amount of data uploaded since program open (e.g. in MB)"},
        {"Remaining", "amount_left", Bytes, "Amount of data left to download (e.g. in MB)"}, {"Time Active", "time_active", Time, "Time (duration) the torrent is active (not stopped)"},
        {"Save Path", "save_path", Text, "Torrent save path"}, {"Completed", "completed", Bytes, "Amount of data completed (e.g. in MB)"},
        {"Ratio Limit", "max_ratio", Ratio, "Upload share ratio limit"}, {"Last Seen Complete", "seen_complete", Date, "Indicates the time when the torrent was last seen complete/whole"},
        {"Last Activity", "last_activity", Date, "Time passed since a chunk was downloaded/uploaded"}, {"Availability", "availability", Ratio, "The number of distributed copies of the torrent"},
        {"Incomplete Save Path", "download_path", Text, "Torrent incomplete save path"}, {"Info Hash v1", "infohash_v1", Text, "i.e: torrent info hash v1"},
        {"Info Hash v2", "infohash_v2", Text, "i.e: torrent info hash v2"}, {"Reannounce In", "reannounce", Time, "Indicates the time until next trackers reannounce"},
        {"Private", "private", Boolean, "Flags private torrents"}, {"Created On", "creation_date", Date, "Torrent was initially created on 01/01/2010 08:00"}
    };
}

TorrentModel::TorrentModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    UIThemeManager::initInstance();
    m_stateColors = TransferListModel::stateColors();
    connect(UIThemeManager::instance(), &UIThemeManager::themeChanged, this, [this]
    {
        m_stateColors = TransferListModel::stateColors();
        if (rowCount() > 0)
            emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DecorationRole, Qt::ForegroundRole});
    });
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
    return Utils::Misc::friendlyUnit(bytes);
}

QString TorrentModel::speed(const qint64 bytes)
{
    return Utils::Misc::friendlyUnit(bytes, true);
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

QString TorrentModel::stateName(const QString &value)
{
    return TransferListModel::statusStrings().value(state(value), value);
}

QVariant TorrentModel::data(const QModelIndex &index, const int role) const
{
    if (!index.isValid() || (index.row() >= rowCount()) || (index.column() >= columnCount()))
        return {};
    const auto torrent = torrentAt(index.row());
    const auto &column = columns[index.column()];
    const auto value = torrent.value(QString::fromLatin1(column.key));
    if (role == Qt::UserRole)
    {
        if (index.column() == 4) return value.toDouble() * 100;
        if (index.column() == 5)
        {
            return QVariant::fromValue(state(value.toString()));
        }
        return value.toVariant();
    }
    if ((role == Qt::DecorationRole) && (index.column() == 1))
        return TransferListModel::stateIcon(state(torrent.value(u"state"_s).toString()));
    if (role == Qt::ForegroundRole)
        return m_stateColors.value(state(torrent.value(u"state"_s).toString()));
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
    case Percentage: return value.toDouble() >= 1 ? u"100%"_s : u"%1%"_s.arg(value.toDouble() * 100, 0, 'f', 1);
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
        return QCoreApplication::translate("TransferListModel", columns[section].label, columns[section].comment);
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
    const QString &tag, const int column, const QString &tracker)
{
    m_tracker = tracker;
    m_filterColumn = column;
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
    if (!data.value(m_filterColumn == 26 ? u"save_path"_s : u"name"_s).toString().contains(m_text, Qt::CaseInsensitive))
        return false;
    if (!acceptsState(data, m_state))
        return false;
    if (!m_tracker.isNull() && QUrl(data.value(u"tracker"_s).toString()).host() != m_tracker)
        return false;
    if (!m_category.isNull() && (data.value(u"category"_s).toString() != m_category)
        && (m_category.isEmpty() || !data.value(u"category"_s).toString().startsWith(m_category + u'/')))
        return false;
    if (!m_tag.isNull() && !data.value(u"tags"_s).toString().split(u", "_s).contains(m_tag))
        return false;
    return true;
}

bool TorrentFilter::acceptsState(const QJsonObject &torrent, const QString &filter)
{
    if (filter == u"stalledUP" || filter == u"stalledDL") return torrent.value(u"state"_s).toString() == filter;
    const QString state = torrent.value(u"state"_s).toString();
    const bool complete = torrent.value(u"progress"_s).toDouble() >= 1;
    const bool errored = (state == u"error") || (state == u"missingFiles");
    const bool stopped = state.startsWith(u"stopped");
    const bool active = torrent.value(u"dlspeed"_s).toInteger() > 0 || torrent.value(u"upspeed"_s).toInteger() > 0;
    if (filter == u"downloading") return !complete;
    if (filter == u"seeding") return complete && !stopped && !errored && !state.startsWith(u"checking") && state != u"moving";
    if (filter == u"completed") return complete;
    if (filter == u"running") return !stopped && !errored;
    if (filter == u"stopped") return stopped;
    if (filter == u"active") return active;
    if (filter == u"inactive") return !active;
    if (filter == u"stalled") return state.startsWith(u"stalled");
    if (filter == u"checking") return state.startsWith(u"checking");
    if (filter == u"moving") return state == u"moving";
    if (filter == u"errored") return errored;
    return true;
}

BitTorrent::TorrentState TorrentModel::state(const QString &value)
{
    using BitTorrent::TorrentState;
    static const QHash<QString, TorrentState> states {
        {u"downloading"_s, TorrentState::Downloading}, {u"stalledDL"_s, TorrentState::StalledDownloading},
        {u"forcedDL"_s, TorrentState::ForcedDownloading}, {u"metaDL"_s, TorrentState::DownloadingMetadata},
        {u"forcedMetaDL"_s, TorrentState::ForcedDownloadingMetadata}, {u"uploading"_s, TorrentState::Uploading},
        {u"forcedUP"_s, TorrentState::ForcedUploading}, {u"stalledUP"_s, TorrentState::StalledUploading},
        {u"queuedDL"_s, TorrentState::QueuedDownloading}, {u"queuedUP"_s, TorrentState::QueuedUploading},
        {u"checkingDL"_s, TorrentState::CheckingDownloading}, {u"checkingUP"_s, TorrentState::CheckingUploading},
        {u"checkingResumeData"_s, TorrentState::CheckingResumeData}, {u"stoppedDL"_s, TorrentState::StoppedDownloading},
        {u"stoppedUP"_s, TorrentState::StoppedUploading}, {u"moving"_s, TorrentState::Moving},
        {u"missingFiles"_s, TorrentState::MissingFiles}, {u"error"_s, TorrentState::Error}
    };
    return states.value(value, TorrentState::Unknown);
}

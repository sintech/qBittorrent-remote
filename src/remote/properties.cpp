// SPDX-License-Identifier: GPL-2.0-or-later
#include "properties.h"

#include <QDateTime>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QScopedValueRollback>
#include <QStandardItemModel>
#include <QTreeView>
#include <QSet>
#include <QSortFilterProxyModel>
#include "gui/propertylistviews.h"

#include "gui/properties/proptabbar.h"
#include "gui/properties/speedwidget.h"
#include "gui/properties/downloadedpiecesbar.h"
#include "gui/properties/pieceavailabilitybar.h"
#include "torrentmodel.h"
#include "profiles.h"
#include <QShortcut>
#include <QTimer>
#include "ui_propertieswidget.h"
#include "gui/properties/peerlistwidget.h"
#include "gui/trackerlist/trackerlistwidget.h"
#include "gui/torrentcontentwidget.h"
#include "gui/lineedit.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    QString trProperty(const char *text, const char *comment = nullptr)
    {
        return QCoreApplication::translate("PropertiesWidget", text, comment);
    }

    QString date(const QJsonValue &value)
    {
        return value.toInteger() > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(value.toInteger()), QLocale::ShortFormat) : QString();
    }

    constexpr int KeyRole = Qt::UserRole + 1;

    QStandardItem *ensureRow(QStandardItem *parent, const QString &key, int columns)
    {
        for (int row = 0; row < parent->rowCount(); ++row)
            if (parent->child(row)->data(KeyRole).toString() == key) return parent->child(row);
        QList<QStandardItem *> items;
        for (int column = 0; column < columns; ++column)
        {
            auto *item = new QStandardItem;
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            items.append(item);
        }
        items.first()->setData(key, KeyRole);
        parent->appendRow(items);
        return items.first();
    }

    void setCell(QStandardItem *row, int column, const QVariant &display, const QVariant &sort = {})
    {
        auto *parent = row->parent() ? row->parent() : row->model()->invisibleRootItem();
        auto *item = parent->child(row->row(), column);
        item->setData(display, Qt::DisplayRole);
        item->setData(sort.isValid() ? sort : display, Qt::UserRole);
        if (column > 0 && sort.isValid() && sort.typeId() != QMetaType::QString)
            item->setData(QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    }

    void removeMissing(QStandardItem *parent, const QSet<QString> &wanted)
    {
        for (int row = parent->rowCount() - 1; row >= 0; --row)
            if (!wanted.contains(parent->child(row)->data(KeyRole).toString())) parent->removeRow(row);
    }

    void fillTracker(QStandardItem *row, const QJsonObject &tracker, const QString &name)
    {
        const auto tr = [](const char *text) { return QCoreApplication::translate("TrackerListModel", text); };
        const int state = tracker.value(u"status"_s).toInt();
        const QString status = tracker.value(u"updating"_s).toBool() ? tr("Updating...")
            : state == 0 ? tr("Disabled") : state == 1 ? tr("Not contacted yet")
            : state == 2 ? tr("Working") : state == 5 ? tr("Tracker error")
            : state == 6 ? tr("Unreachable") : tr("Not working");
        setCell(row, TrackerListView::COL_URL, name);
        setCell(row, TrackerListView::COL_TIER, tracker.value(u"tier"_s).isUndefined() ? QString() : QString::number(tracker.value(u"tier"_s).toInt()), tracker.value(u"tier"_s).toVariant());
        const auto btVersion = tracker.value(u"bt_version"_s);
        setCell(row, TrackerListView::COL_PROTOCOL, btVersion.isUndefined() ? QString() : u"v%1"_s.arg(btVersion.toInt()));
        setCell(row, TrackerListView::COL_STATUS, status, state);
        const QList<QPair<int, QString>> counts {{TrackerListView::COL_PEERS, u"num_peers"_s}, {TrackerListView::COL_SEEDS, u"num_seeds"_s},
            {TrackerListView::COL_LEECHES, u"num_leeches"_s}, {TrackerListView::COL_TIMES_DOWNLOADED, u"num_downloaded"_s}};
        for (const auto &field : counts)
        {
            const auto value = tracker.value(field.second);
            setCell(row, field.first, value.isUndefined() || value.toInteger() < 0 ? QString() : QString::number(value.toInteger()), value.toVariant());
        }
        setCell(row, TrackerListView::COL_MSG, tracker.value(u"msg"_s).toString());
        for (const auto &field : {qMakePair(int(TrackerListView::COL_NEXT_ANNOUNCE), u"next_announce"_s), qMakePair(int(TrackerListView::COL_MIN_ANNOUNCE), u"min_announce"_s)})
        {
            const auto value = tracker.value(field.second);
            const qint64 seconds = qMax<qint64>(0, value.toInteger() - QDateTime::currentSecsSinceEpoch());
            setCell(row, field.first, value.isUndefined() || value.toInteger() <= 0 ? QString() : TorrentModel::duration(seconds), seconds);
        }
    }

}

Properties::Properties(QWidget *parent)
    : PropertiesWidget(parent)
{
    setObjectName(u"remoteProperties"_s);
    m_progress = m_downloadedPieces;
    m_progress->setObjectName(u"remotePieceStrip"_s);
    m_availability = m_piecesAvailability;
    m_availability->hide();
    m_ui->labelPiecesAvailability->hide();
    m_ui->labelAverageAvailabilityVal->hide();
    m_speed = static_cast<SpeedWidget *>(m_speedWidget);
    m_speed->setObjectName(u"remoteSpeedWidget"_s);
    std::array<bool, 10> available {};
    available[2] = available[3] = true;
    m_speed->setAvailableGraphs(available);
    m_trackers = m_trackerList;
    m_trackers->setObjectName(u"remoteTrackers"_s);
    m_trackers->setHeaderSettingsKey(u"remote/trackerHeader"_s);
    m_peers = m_peerList;
    m_peers->setObjectName(u"remotePeers"_s);
    m_peers->setHeaderSettingsKey(u"remote/peerHeader"_s);
    m_files = m_ui->filesList->sourceModel();
    m_ui->filesList->setObjectName(u"remoteContent"_s);
    m_ui->filesList->setHeaderSettingsKey(u"remote/contentHeader"_s);
    m_ui->filesList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    const auto openFile = [this](const QModelIndex &index)
    {
        const QString relative = index.siblingAtColumn(0).data(KeyRole).toString();
        const QString path = remoteFilePath(remoteStoragePath(m_torrent, m_filePaths), relative);
        if (!path.isEmpty()) emit openPathRequested(path);
    };
    connect(m_ui->filesList, &QTreeView::doubleClicked, this, openFile);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter})
    {
        auto *shortcut = new QShortcut(QKeySequence(key), m_ui->filesList);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, openFile] { openFile(m_ui->filesList->currentIndex()); });
    }
    const auto allFiles = [this]
    {
        QModelIndexList rows;
        for (int row = 0; row < m_files->rowCount(); ++row) rows.append(m_files->index(row, 0));
        return rows;
    };
    connect(m_ui->selectAllButton, &QPushButton::clicked, this, [this, allFiles] { setFilePriority(allFiles(), 1); });
    connect(m_ui->selectNoneButton, &QPushButton::clicked, this, [this, allFiles] { setFilePriority(allFiles(), 0); });
    connect(m_files, &QStandardItemModel::itemChanged, this, [this](QStandardItem *item)
    {
        if (!m_updatingFiles && item->column() == 0 && item->isCheckable()) setFilePriority({item->index()}, item->checkState() == Qt::Unchecked ? 0 : 1);
    });
    m_tabs = m_tabBar;
    connect(m_tabs, &PropTabBar::tabChanged, this, [this](const int index)
    {
        QSettings().setValue(u"remote/propertiesTab"_s, index);
        emit currentChanged(index);
    });
    m_contentFilterLine->installPatternMenu(static_cast<QSortFilterProxyModel *>(m_ui->filesList->model()), u"remote/contentFilterFormat"_s);
    const int savedTab = QSettings().value(u"remote/propertiesTab"_s, 0).toInt();
    m_tabs->setCurrentIndex((savedTab >= -1 && savedTab <= 5) ? savedTab : 0);
    connect(m_tabs, &PropTabBar::visibilityToggled, this, [](bool visible)
    {
        if (!visible) QSettings().setValue(u"remote/propertiesTab"_s, -1);
    });
    QTimer::singleShot(0, this, [this] { setVisibility(m_tabs->currentIndex() >= 0); });
    installContextMenus();
    clear();
}

Properties::~Properties() = default;

int Properties::currentIndex() const
{
    return m_tabs->currentIndex();
}

bool Properties::isPanelVisible() const
{
    return currentIndex() >= 0;
}

void Properties::clear()
{
    for (auto *menu : findChildren<QMenu *>()) menu->close();
    m_torrent = {};
    m_filePaths.clear();
    for (auto *label : findChildren<QLabel *>())
    {
        if (label->objectName().endsWith(u"Val"))
            label->clear();
    }
    m_progress->clear();
    m_availability->clear();
    m_trackers->sourceModel()->removeRows(0, m_trackers->sourceModel()->rowCount());
    m_peers->sourceModel()->removeRows(0, m_peers->sourceModel()->rowCount());
    m_ui->listWebSeeds->clear();
    m_files->removeRows(0, m_files->rowCount());
    m_ui->selectAllButton->setEnabled(false);
    m_ui->selectNoneButton->setEnabled(false);
    m_ui->filesList->setProperty("initialized", false);
}

void Properties::setTorrent(const QJsonObject &torrent)
{
    if (m_torrent.value(u"hash"_s) != torrent.value(u"hash"_s))
        clear();
    m_torrent = torrent;
    m_ui->labelProgressVal->setText(u"%1%"_s.arg(torrent.value(u"progress"_s).toDouble() * 100, 0, 'f', 1));
    const QString state = torrent.value(u"state"_s).toString();
    const bool showAvailability = torrent.value(u"progress"_s).toDouble() < 1
        && !state.startsWith(u"stopped") && !state.startsWith(u"queued") && !state.startsWith(u"checking");
    m_availability->setVisible(showAvailability);
    m_ui->labelPiecesAvailability->setVisible(showAvailability);
    m_ui->labelAverageAvailabilityVal->setVisible(showAvailability);
    m_ui->labelAverageAvailabilityVal->setText(QString::number(torrent.value(u"availability"_s).toDouble(), 'f', 3));
    m_ui->labelInfohash1Val->setText(torrent.value(u"infohash_v1"_s).toString());
    m_ui->labelInfohash2Val->setText(torrent.value(u"infohash_v2"_s).toString());
}

void Properties::setData(const int tab, const QJsonDocument &data)
{
    if (tab == 0)
        showGeneral(data.object());
    else if (tab == 1)
    {
        auto *root = m_trackers->sourceModel()->invisibleRootItem();
        QSet<QString> wanted;
        for (const auto value : data.array())
        {
            const auto tracker = value.toObject();
            const QString url = tracker.value(u"url"_s).toString();
            wanted.insert(url);
            auto *row = ensureRow(root, url, TrackerListView::COL_COUNT);
            fillTracker(row, tracker, url);
            QSet<QString> endpoints;
            for (const auto value : tracker.value(u"endpoints"_s).toArray())
            {
                const auto endpoint = value.toObject();
                const QString name = endpoint.value(u"name"_s).toString();
                const QString key = name + u'|' + QString::number(endpoint.value(u"bt_version"_s).toInt());
                endpoints.insert(key);
                fillTracker(ensureRow(row, key, TrackerListView::COL_COUNT), endpoint, name);
            }
            removeMissing(row, endpoints);
        }
        removeMissing(root, wanted);
    }
    else if (tab == 2)
    {
        auto *root = m_peers->sourceModel()->invisibleRootItem();
        const auto peers = data.object().value(u"peers"_s).toObject();
        QSet<QString> wanted;
        m_peers->setColumnAvailable(PeerListView::COUNTRY, data.object().value(u"show_flags"_s).toBool());
        for (auto it = peers.begin(); it != peers.end(); ++it)
        {
            wanted.insert(it.key());
            const auto peer = it.value().toObject();
            auto *row = ensureRow(root, it.key(), PeerListView::COL_COUNT);
            const auto country = peer.value(u"country_code"_s).toString().toLower();
            row->setData(country.isEmpty() ? QIcon() : QIcon(u":/icons/flags/" + country + u".svg"), Qt::DecorationRole);
            row->setToolTip(peer.value(u"country"_s).toString());
            setCell(row, PeerListView::IP, peer.value(u"ip"_s).toString());
            setCell(row, PeerListView::PORT, peer.value(u"port"_s).toInt());
            setCell(row, PeerListView::CONNECTION, peer.value(u"connection"_s).toString());
            setCell(row, PeerListView::FLAGS, peer.value(u"flags"_s).toString());
            row->model()->item(row->row(), PeerListView::FLAGS)->setToolTip(peer.value(u"flags_desc"_s).toString());
            setCell(row, PeerListView::CLIENT, peer.value(u"client"_s).toString());
            setCell(row, PeerListView::PEERID_CLIENT, peer.value(u"peer_id_client"_s).toString());
            const auto progress = peer.value(u"progress"_s).toDouble();
            setCell(row, PeerListView::PROGRESS, u"%1%"_s.arg(progress * 100, 0, 'f', 1), progress);
            for (const auto &field : {qMakePair(int(PeerListView::DOWN_SPEED), u"dl_speed"_s), qMakePair(int(PeerListView::UP_SPEED), u"up_speed"_s)})
                setCell(row, field.first, TorrentModel::speed(peer.value(field.second).toInteger()), peer.value(field.second).toVariant());
            for (const auto &field : {qMakePair(int(PeerListView::TOT_DOWN), u"downloaded"_s), qMakePair(int(PeerListView::TOT_UP), u"uploaded"_s)})
                setCell(row, field.first, TorrentModel::size(peer.value(field.second).toInteger()), peer.value(field.second).toVariant());
            setCell(row, PeerListView::RELEVANCE, QString::number(peer.value(u"relevance"_s).toDouble(), 'f', 2), peer.value(u"relevance"_s).toVariant());
            setCell(row, PeerListView::DOWNLOADING_PIECE, peer.value(u"files"_s).toString());
        }
        removeMissing(root, wanted);
    }
    else if (tab == 3)
    {
        m_ui->listWebSeeds->clear();
        for (const auto value : data.array())
            m_ui->listWebSeeds->addItem(value.toObject().value(u"url"_s).toString());
    }
    else if (tab == 4)
        showContent(data.array());
    else if (tab == 6)
    {
        const auto values = data.array();
        QBitArray complete(values.size());
        QBitArray partial(values.size());
        for (int i = 0; i < values.size(); ++i)
        {
            complete.setBit(i, values[i].toInt() == 2);
            partial.setBit(i, values[i].toInt() == 1);
        }
        m_progress->setProgress(complete, partial);
    }
    else if (tab == 7)
    {
        QList<int> values;
        for (const auto value : data.array())
            values.append(value.toInt());
        m_availability->setAvailability(values);
    }
}

void Properties::showGeneral(const QJsonObject &properties)
{
    m_torrent.insert(u"private"_s, properties.value(u"is_private"_s));
    const auto text = [&properties](const QString &key) { return properties.value(key).toVariant().toString(); };
    const auto bytes = [&properties](const QString &key) { return TorrentModel::size(properties.value(key).toInteger()); };
    const auto rate = [&properties](const QString &key) { return TorrentModel::speed(properties.value(key).toInteger()); };
    const auto duration = [&properties](const QString &key) { return TorrentModel::duration(properties.value(key).toInteger()); };
    const auto total = [&](const QString &key, const QString &session)
    {
        return trProperty("%1 (%2 this session)").arg(bytes(key), bytes(session));
    };
    m_ui->labelSavePathVal->setText(text(u"save_path"_s));
    m_ui->labelElapsedVal->setText(duration(u"time_elapsed"_s));
    m_ui->labelETAVal->setText(duration(u"eta"_s));
    m_ui->labelConnectionsVal->setText(trProperty("%1 (%2 max)", "%1 and %2 are numbers, e.g. 3 (10 max)")
        .arg(text(u"nb_connections"_s), properties.value(u"nb_connections_limit"_s).toInt() < 0 ? u"∞"_s : text(u"nb_connections_limit"_s)));
    m_ui->labelDlTotalVal->setText(total(u"total_downloaded"_s, u"total_downloaded_session"_s));
    m_ui->labelUpTotalVal->setText(total(u"total_uploaded"_s, u"total_uploaded_session"_s));
    m_ui->labelSeedsVal->setText(trProperty("%1 (%2 total)", "%1 and %2 are numbers, e.g. 3 (10 total)").arg(text(u"seeds"_s), text(u"seeds_total"_s)));
    m_ui->labelPeersVal->setText(trProperty("%1 (%2 total)", "%1 and %2 are numbers, e.g. 3 (10 total)").arg(text(u"peers"_s), text(u"peers_total"_s)));
    m_ui->labelDlSpeedVal->setText(trProperty("%1 (%2 avg.)", "%1 and %2 are speed rates, e.g. 200KiB/s (100KiB/s avg.)").arg(rate(u"dl_speed"_s), rate(u"dl_speed_avg"_s)));
    m_ui->labelUpSpeedVal->setText(trProperty("%1 (%2 avg.)", "%1 and %2 are speed rates, e.g. 200KiB/s (100KiB/s avg.)").arg(rate(u"up_speed"_s), rate(u"up_speed_avg"_s)));
    m_ui->labelDlLimitVal->setText(properties.value(u"dl_limit"_s).toInteger() <= 0 ? u"∞"_s : rate(u"dl_limit"_s));
    m_ui->labelUpLimitVal->setText(properties.value(u"up_limit"_s).toInteger() <= 0 ? u"∞"_s : rate(u"up_limit"_s));
    m_ui->labelWastedVal->setText(bytes(u"total_wasted"_s));
    m_ui->labelShareRatioVal->setText(QString::number(properties.value(u"share_ratio"_s).toDouble(), 'f', 2));
    m_ui->labelPopularityVal->setText(QString::number(m_torrent.value(u"popularity"_s).toDouble(), 'f', 2));
    m_ui->labelReannounceInVal->setText(duration(u"reannounce"_s));
    m_ui->labelLastSeenCompleteVal->setText(date(properties.value(u"last_seen"_s)));
    m_ui->labelTotalSizeVal->setText(bytes(u"total_size"_s));
    m_ui->labelTotalPiecesVal->setText(trProperty("%1 x %2 (have %3)", "(torrent pieces) eg 152 x 4MB (have 25)")
        .arg(text(u"pieces_num"_s), bytes(u"piece_size"_s), text(u"pieces_have"_s)));
    m_ui->labelCreatedByVal->setText(text(u"created_by"_s));
    m_ui->labelAddedOnVal->setText(date(properties.value(u"addition_date"_s)));
    m_ui->labelCompletedOnVal->setText(date(properties.value(u"completion_date"_s)));
    m_ui->labelCreatedOnVal->setText(date(properties.value(u"creation_date"_s)));
    m_ui->labelPrivateVal->setText(properties.value(u"is_private"_s).toBool() ? trProperty("Yes") : trProperty("No"));
    m_ui->labelCommentVal->setText(text(u"comment"_s));
    // Keep remote metadata plain text, even if it contains HTML or link markup.
    m_ui->labelCommentVal->setTextFormat(Qt::PlainText);
}

void Properties::showContent(const QJsonArray &files)
{
    const QScopedValueRollback<bool> updating(m_updatingFiles, true);
    m_filePaths.clear();
    QFileIconProvider icons;
    QSet<QString> wanted;
    auto *root = m_files->invisibleRootItem();
    for (const auto value : files)
    {
        const auto file = value.toObject();
        m_filePaths.append(file.value(u"name"_s).toString());
        const auto parts = m_filePaths.last().split(u'/');
        QStandardItem *parent = root;
        QString path;
        for (int i = 0; i + 1 < parts.size(); ++i)
        {
            path += parts[i] + u'/';
            auto *folder = ensureRow(parent, path, 6);
            setCell(folder, 0, parts[i]);
            folder->setIcon(icons.icon(QFileIconProvider::Folder));
            folder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            parent = folder;
        }
        path += parts.last();
        wanted.insert(path);
        auto *row = ensureRow(parent, path, 6);
        setCell(row, 0, parts.last());
        row->setIcon(icons.icon(QFileInfo(parts.last())));
        const qint64 size = file.value(u"size"_s).toInteger();
        const qreal progress = file.value(u"progress"_s).toDouble();
        const int priority = file.value(u"priority"_s).toInt();
        row->setData(file.value(u"index"_s).toInt(m_filePaths.size() - 1), Qt::UserRole + 2);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        row->setData(priority ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        setCell(row, 1, TorrentModel::size(size), size);
        setCell(row, 2, u"%1%"_s.arg(progress * 100, 0, 'f', 1), progress * 100);
        const QHash<int, QString> priorities {{0, tr("Do not download")}, {1, tr("Normal")}, {6, tr("High")}, {7, tr("Maximum")}};
        setCell(row, 3, priorities.value(priority, QString::number(priority)), priority);
        const qint64 remaining = qRound64(size * (1 - progress));
        setCell(row, 4, TorrentModel::size(remaining), remaining);
        setCell(row, 5, QString::number(file.value(u"availability"_s).toDouble(), 'f', 2), file.value(u"availability"_s).toVariant());
    }
    const std::function<void(QStandardItem *)> aggregate = [&](QStandardItem *parent)
    {
        for (int i = parent->rowCount() - 1; i >= 0; --i)
        {
            auto *row = parent->child(i);
            if (row->data(KeyRole).toString().endsWith(u'/'))
            {
                aggregate(row);
                if (!row->rowCount()) { parent->removeRow(i); continue; }
                qint64 size = 0, remaining = 0;
                int checked = 0;
                for (int child = 0; child < row->rowCount(); ++child)
                {
                    size += row->child(child, 1)->data(Qt::UserRole).toLongLong();
                    remaining += row->child(child, 4)->data(Qt::UserRole).toLongLong();
                    checked += row->child(child)->data(Qt::CheckStateRole).toInt() != Qt::Unchecked;
                }
                row->setData(checked == 0 ? Qt::Unchecked : checked == row->rowCount() ? Qt::Checked : Qt::PartiallyChecked, Qt::CheckStateRole);
                setCell(row, 1, TorrentModel::size(size), size);
                const qreal progress = size ? 100.0 * (size - remaining) / size : 0;
                setCell(row, 2, u"%1%"_s.arg(progress, 0, 'f', 1), progress);
                setCell(row, 3, tr("Mixed"), -1);
                setCell(row, 4, TorrentModel::size(remaining), remaining);
            }
            else if (!wanted.contains(row->data(KeyRole).toString()))
                parent->removeRow(i);
        }
    };
    aggregate(root);
    m_ui->selectAllButton->setEnabled(!files.isEmpty());
    m_ui->selectNoneButton->setEnabled(!files.isEmpty());
    const std::function<void(QStandardItem *)> align = [&](QStandardItem *parent)
    {
        for (int i = 0; i < parent->rowCount(); ++i)
        {
            for (int column : {2, 3, 5}) parent->child(i, column)->setData({}, Qt::TextAlignmentRole);
            align(parent->child(i));
        }
    };
    align(root);
    if (!m_ui->filesList->property("initialized").toBool())
    {
        m_ui->filesList->expandAll();
        m_ui->filesList->setProperty("initialized", true);
    }
}

void Properties::pushStats(const QJsonObject &state)
{
    std::array<quint64, 10> sample {};
    sample[2] = qMax<qint64>(0, state.value(u"up_info_speed"_s).toInteger());
    sample[3] = qMax<qint64>(0, state.value(u"dl_info_speed"_s).toInteger());
    m_speed->pushSample(sample);
}

void Properties::resetSpeedHistory()
{
    m_speed->clearHistory();
}

void Properties::setFilePriority(const QModelIndexList &rows, int priority)
{
    const auto hash = m_torrent.value(u"hash"_s).toString();
    if (hash.isEmpty()) return;
    QSet<QString> ids;
    const std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &index)
    {
        if (index.data(Qt::UserRole + 2).isValid()) ids.insert(index.data(Qt::UserRole + 2).toString());
        for (int child = 0; child < m_files->rowCount(index); ++child) collect(m_files->index(child, 0, index));
    };
    for (const auto &row : rows) collect(row);
    if (ids.isEmpty()) return;
    QUrlQuery form; form.addQueryItem(u"hash"_s, hash); form.addQueryItem(u"id"_s, QStringList(ids.values()).join(u'|')); form.addQueryItem(u"priority"_s, QString::number(priority));
    emit commandRequested(u"torrents/filePrio"_s, form);
}

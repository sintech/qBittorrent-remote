/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023-2025  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

// Shared presentation extracted from PeerListWidget, TrackerListWidget and TorrentContentWidget.
#include "propertylistviews.h"

#include <QAction>
#include <QCoreApplication>
#include <QHeaderView>
#include <QMenu>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>

#include "base/global.h"
#include "properties/peerlistsortmodel.h"
#include "trackerlist/trackerlistsortmodel.h"
#include "trackerlist/trackerlistitemdelegate.h"
#include "torrentcontentitemdelegate.h"

void PropertyListView::setHeaderSettingsKey(const QString &key)
{
    m_settingsKey = key;
    header()->restoreState(QSettings().value(key).toByteArray());
    const auto save = [this] { QSettings().setValue(m_settingsKey, header()->saveState()); };
    connect(header(), &QHeaderView::sectionMoved, this, save);
    connect(header(), &QHeaderView::sectionResized, this, save);
    connect(header(), &QHeaderView::sortIndicatorChanged, this, save);
}

void PropertyListView::installColumnMenu()
{
    header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header(), &QWidget::customContextMenuRequested, this, [this](const QPoint &position)
    {
        QMenu menu(this);
        for (int column = 0; column < model()->columnCount(); ++column)
        {
            const auto label = model()->headerData(column, Qt::Horizontal).toString();
            if (label.isEmpty()) continue;
            auto *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(!isColumnHidden(column));
            connect(action, &QAction::toggled, this, [this, column](bool visible) { setColumnHidden(column, !visible); });
        }
        menu.exec(header()->mapToGlobal(position));
    });
}

PeerListView::PeerListView(QWidget *parent) : PropertyListView(parent)
{
    // Visual settings
    setUniformRowHeights(true);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setAllColumnsShowFocus(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    header()->setFirstSectionMovable(true);
    header()->setStretchLastSection(false);
    header()->setTextElideMode(Qt::ElideRight);

    m_dataModel = new QStandardItemModel(0, PeerListColumns::COL_COUNT, this);
    m_dataModel->setHeaderData(PeerListColumns::COUNTRY, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Country/Region")); // Country flag column
    m_dataModel->setHeaderData(PeerListColumns::IP, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "IP/Address"));
    m_dataModel->setHeaderData(PeerListColumns::PORT, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Port"));
    m_dataModel->setHeaderData(PeerListColumns::FLAGS, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Flags"));
    m_dataModel->setHeaderData(PeerListColumns::CONNECTION, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Connection"));
    m_dataModel->setHeaderData(PeerListColumns::CLIENT, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Client", "i.e.: Client application"));
    m_dataModel->setHeaderData(PeerListColumns::PEERID_CLIENT, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Peer ID Client", "i.e.: Client resolved from Peer ID"));
    m_dataModel->setHeaderData(PeerListColumns::PROGRESS, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Progress", "i.e: % downloaded"));
    m_dataModel->setHeaderData(PeerListColumns::DOWN_SPEED, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Down Speed", "i.e: Download speed"));
    m_dataModel->setHeaderData(PeerListColumns::UP_SPEED, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Up Speed", "i.e: Upload speed"));
    m_dataModel->setHeaderData(PeerListColumns::TOT_DOWN, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Downloaded", "i.e: total data downloaded"));
    m_dataModel->setHeaderData(PeerListColumns::TOT_UP, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Uploaded", "i.e: total data uploaded"));
    m_dataModel->setHeaderData(PeerListColumns::RELEVANCE, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Relevance", "i.e: How relevant this peer is to us. How many pieces it has that we don't."));
    m_dataModel->setHeaderData(PeerListColumns::DOWNLOADING_PIECE, Qt::Horizontal, QCoreApplication::translate("PeerListWidget", "Files", "i.e. files that are being downloaded right now"));
    // Set header text alignment
    m_dataModel->setHeaderData(PeerListColumns::PORT, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::PROGRESS, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::DOWN_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::UP_SPEED, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::TOT_DOWN, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::TOT_UP, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    m_dataModel->setHeaderData(PeerListColumns::RELEVANCE, Qt::Horizontal, QVariant(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    auto *proxy = new PeerListSortModel(this);
    proxy->setDynamicSortFilter(true);
    proxy->setSourceModel(m_dataModel);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    setModel(proxy);
    hideColumn(IP_HIDDEN);
    hideColumn(PEERID_CLIENT);
    setSortingEnabled(true);
#ifdef QBT_REMOTE_CLIENT
    installColumnMenu();
#endif
}

QStandardItemModel *PeerListView::sourceModel() const { return m_dataModel; }

TrackerListView::TrackerListView(QWidget *parent, const bool createModel) : PropertyListView(parent)
{
    setExpandsOnDoubleClick(false);
    setAllColumnsShowFocus(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSortingEnabled(true);
    setUniformRowHeights(true);
    setContextMenuPolicy(Qt::CustomContextMenu);

    header()->setSortIndicator(0, Qt::AscendingOrder);
    header()->setFirstSectionMovable(true);
    header()->setStretchLastSection(false); // Must be set after loadSettings() in order to work
    header()->setTextElideMode(Qt::ElideRight);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);

    setItemDelegate(new TrackerListItemDelegate(this));
    if (!createModel) return;
    m_dataModel = new QStandardItemModel(0, COL_COUNT, this);
    const char *labels[] {"URL/Announce Endpoint", "Tier", "BT Protocol", "Status", "Peers", "Seeds", "Leeches", "Times Downloaded", "Message", "Next Announce", "Min Announce"};
    for (int column = 0; column < COL_COUNT; ++column)
        m_dataModel->setHeaderData(column, Qt::Horizontal, QCoreApplication::translate("TrackerListModel", labels[column]));
    for (int column : {COL_TIER, COL_PEERS, COL_SEEDS, COL_LEECHES, COL_TIMES_DOWNLOADED, COL_NEXT_ANNOUNCE, COL_MIN_ANNOUNCE})
        m_dataModel->setHeaderData(column, Qt::Horizontal, QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    setModel(new TrackerListSortModel(m_dataModel, this));
    setColumnHidden(COL_PROTOCOL, true);
    setColumnWidth(COL_URL, 350);
    setColumnWidth(COL_TIER, 45);
    installColumnMenu();
}

QStandardItemModel *TrackerListView::sourceModel() const { return m_dataModel; }

TorrentContentView::TorrentContentView(QWidget *parent, const bool createModel) : PropertyListView(parent)
{
    setDragDropMode(QAbstractItemView::DragOnly);
    setDragEnabled(false);
    setSelectionMode(QAbstractItemView::MultiSelection);
    setExpandsOnDoubleClick(false);
    setSortingEnabled(true);
    setUniformRowHeights(true);
    header()->setSortIndicator(0, Qt::AscendingOrder);
    header()->setFirstSectionMovable(true);
    header()->setContextMenuPolicy(Qt::CustomContextMenu);

    if (!createModel) return;
    m_dataModel = new QStandardItemModel(0, 6, this);
    const char *labels[] {"Name", "Size", "Progress", "Download Priority", "Remaining", "Availability"};
    for (int column = 0; column < 6; ++column)
        m_dataModel->setHeaderData(column, Qt::Horizontal, QCoreApplication::translate("TorrentContentModel", labels[column]));
    for (int column : {Size, Remaining})
        m_dataModel->setHeaderData(column, Qt::Horizontal, QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter), Qt::TextAlignmentRole);
    auto *proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(m_dataModel);
    proxy->setSortRole(Qt::UserRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    setModel(proxy);
    setItemDelegate(new TorrentContentItemDelegate(this));
    header()->setStretchLastSection(false);
    installColumnMenu();
}

QStandardItemModel *TorrentContentView::sourceModel() const { return m_dataModel; }

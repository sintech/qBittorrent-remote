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

#pragma once

#include <QTreeView>

class QStandardItemModel;

// Presentation shared by the local session controllers and the Web API frontend.
class PropertyListView : public QTreeView
{
    Q_OBJECT
public:
    using QTreeView::QTreeView;
    void setHeaderSettingsKey(const QString &key);
protected:
    void installColumnMenu();
private:
    QString m_settingsKey;
};

class PeerListView : public PropertyListView
{
    Q_OBJECT
public:
    enum PeerListColumns
    {
        COUNTRY, IP, PORT, CONNECTION, FLAGS, CLIENT, PEERID_CLIENT, PROGRESS,
        DOWN_SPEED, UP_SPEED, TOT_DOWN, TOT_UP, RELEVANCE, DOWNLOADING_PIECE,
        IP_HIDDEN, COL_COUNT
    };
    explicit PeerListView(QWidget *parent = nullptr);
    QStandardItemModel *sourceModel() const;
private:
    QStandardItemModel *m_dataModel = nullptr;
};

class TrackerListView : public PropertyListView
{
    Q_OBJECT
public:
    enum TrackerListColumn
    {
        COL_URL, COL_TIER, COL_PROTOCOL, COL_STATUS, COL_PEERS, COL_SEEDS,
        COL_LEECHES, COL_TIMES_DOWNLOADED, COL_MSG, COL_NEXT_ANNOUNCE,
        COL_MIN_ANNOUNCE, COL_COUNT
    };
    static constexpr int STICKY_ROW_COUNT = 3;
    explicit TrackerListView(QWidget *parent = nullptr, bool createModel = true);
    QStandardItemModel *sourceModel() const;
private:
    QStandardItemModel *m_dataModel = nullptr;
};

class TorrentContentView : public PropertyListView
{
    Q_OBJECT
public:
    enum Column { Name, Size, Progress, Priority, Remaining, Availability };
    explicit TorrentContentView(QWidget *parent = nullptr, bool createModel = true);
    QStandardItemModel *sourceModel() const;
private:
    QStandardItemModel *m_dataModel = nullptr;
};

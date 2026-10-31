// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include "gui/properties/propertieswidget.h"

class PropTabBar;
class SpeedWidget;
class DownloadedPiecesBar;
class PieceAvailabilityBar;
class QStandardItemModel;
class PeerListWidget;
class TrackerListWidget;

namespace Ui { class PropertiesWidget; }

namespace Remote
{
    class Properties final : public PropertiesWidget
    {
        Q_OBJECT
    public:
        explicit Properties(QWidget *parent = nullptr);
        ~Properties() override;
        int currentIndex() const;
        bool isPanelVisible() const;
        void clear();
        void pushStats(const QJsonObject &state);
        void resetSpeedHistory();
        void setTorrent(const QJsonObject &torrent);
        void setData(int tab, const QJsonDocument &data);

    signals:
        void currentChanged(int index);
        void openPathRequested(const QString &path);

    private:
        void showGeneral(const QJsonObject &properties);
        void showContent(const QJsonArray &files);
        SpeedWidget *m_speed = nullptr;
        DownloadedPiecesBar *m_progress = nullptr;
        PieceAvailabilityBar *m_availability = nullptr;
        PropTabBar *m_tabs = nullptr;
        TrackerListWidget *m_trackers = nullptr;
        PeerListWidget *m_peers = nullptr;
        QStandardItemModel *m_files = nullptr;
        QJsonObject m_torrent;
    };
}

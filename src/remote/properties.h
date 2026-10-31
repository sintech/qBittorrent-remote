// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

class PropTabBar;
class SpeedWidget;
class DownloadedPiecesBar;
class PieceAvailabilityBar;
class QStandardItemModel;
class PeerListView;
class TrackerListView;

namespace Ui { class PropertiesWidget; }

namespace Remote
{
    class Properties final : public QWidget
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

    private:
        void showGeneral(const QJsonObject &properties);
        void showContent(const QJsonArray &files);
        std::unique_ptr<Ui::PropertiesWidget> m_ui;
        SpeedWidget *m_speed = nullptr;
        DownloadedPiecesBar *m_progress = nullptr;
        PieceAvailabilityBar *m_availability = nullptr;
        PropTabBar *m_tabs = nullptr;
        TrackerListView *m_trackers = nullptr;
        PeerListView *m_peers = nullptr;
        QStandardItemModel *m_files = nullptr;
        QJsonObject m_torrent;
    };
}

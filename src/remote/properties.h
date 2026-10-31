// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include <QJsonDocument>
#include <QJsonObject>
#include <QWidget>

class PropTabBar;
class QStandardItemModel;
class QTreeWidget;

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
        void setTorrent(const QJsonObject &torrent);
        void setData(int tab, const QJsonDocument &data);

    signals:
        void currentChanged(int index);

    private:
        void showGeneral(const QJsonObject &properties);
        void showContent(const QJsonArray &files);
        std::unique_ptr<Ui::PropertiesWidget> m_ui;
        PropTabBar *m_tabs = nullptr;
        QTreeWidget *m_trackers = nullptr;
        QTreeWidget *m_peers = nullptr;
        QStandardItemModel *m_files = nullptr;
        QJsonObject m_torrent;
    };
}

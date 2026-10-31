// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QScrollArea>
#include <QSet>

class QListWidget;
class TransferListFiltersWidgetItem;

namespace Remote
{
    class TorrentModel;
    class Sidebar final : public QScrollArea
    {
        Q_OBJECT
    public:
        explicit Sidebar(QWidget *parent = nullptr);
        void updateData(const TorrentModel &model, const QJsonObject &categories, const QSet<QString> &tags);
        QString state() const;
        QString category() const;
        QString tag() const;
        QString tracker() const;
    signals:
        void filtersChanged();
    private:
        QListWidget *m_status = nullptr;
        QListWidget *m_categories = nullptr;
        QListWidget *m_tags = nullptr;
        QListWidget *m_trackers = nullptr;
        QList<TransferListFiltersWidgetItem *> m_sections;
    };
}

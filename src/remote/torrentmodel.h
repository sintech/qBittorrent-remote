// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QAbstractTableModel>
#include <QJsonObject>
#include <QSortFilterProxyModel>

namespace Remote
{
    class TorrentModel final : public QAbstractTableModel
    {
        Q_OBJECT

    public:
        explicit TorrentModel(QObject *parent = nullptr);
        int rowCount(const QModelIndex &parent = {}) const override;
        int columnCount(const QModelIndex &parent = {}) const override;
        QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
        QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
        void applySync(const QJsonObject &sync);
        void clear();
        QString hashAt(int row) const;
        QJsonObject torrentAt(int row) const;
        static QString stateName(const QString &state);
        static QString size(qint64 bytes);
        static QString speed(qint64 bytes);
        static QString duration(qint64 seconds);

    private:
        QStringList m_hashes;
        QHash<QString, QJsonObject> m_torrents;
    };

    class TorrentFilter final : public QSortFilterProxyModel
    {
    public:
        using QSortFilterProxyModel::QSortFilterProxyModel;
        void setFilters(const QString &text, const QString &state, const QString &category,
            const QString &tag);

    protected:
        bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

    private:
        QString m_text;
        QString m_state;
        QString m_category;
        QString m_tag;
    };
}

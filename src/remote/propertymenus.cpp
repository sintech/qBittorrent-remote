// SPDX-License-Identifier: GPL-2.0-or-later
#include "properties.h"
#include "profiles.h"
#include <QApplication>
#include <QClipboard>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QStandardItemModel>
#include <QTreeView>
#include "gui/properties/peerlistwidget.h"
#include "gui/trackerlist/trackerlistwidget.h"
#include "gui/torrentcontentwidget.h"
#include "ui_propertieswidget.h"
using namespace Qt::StringLiterals;
using namespace Remote;

void Properties::installContextMenus()
{
    const auto send = [this](const QString &method, QUrlQuery form)
    {
        const QString hash = m_torrent.value(u"hash"_s).toString();
        if (hash.isEmpty()) return;
        if (!form.hasQueryItem(u"hashes"_s)) form.addQueryItem(u"hash"_s, hash);
        emit commandRequested(method, form);
    };
    const auto bind = [this](QWidget *view, auto handler)
    {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this, [this, view, handler](const QPoint &point)
        {
            if (m_torrent.isEmpty()) return;
            handler(point);
        });
    };
    bind(m_trackers, [this, send](const QPoint &point)
    {
        QStringList urls;
        for (auto index : m_trackers->selectionModel()->selectedRows())
        {
            while (index.parent().isValid()) index = index.parent();
            const auto url = index.data(Qt::UserRole + 1).toString();
            if (!url.startsWith(u"**") && !urls.contains(url)) urls.append(url);
        }
        auto *menu = TrackerListWidget::createContextMenu(this, !urls.isEmpty(), m_torrent.value(u"state"_s).toString().startsWith(u"stopped"),
            [this, send, urls](const QString &op, const QVariant &)
        {
            if (op == u"copy") { QApplication::clipboard()->setText(urls.join(u'\n')); return; }
            QUrlQuery form;
            QString method;
            bool ok = false;
            if (op == u"add")
            {
                const auto text = QInputDialog::getMultiLineText(this, TrackerListWidget::tr("Add trackers..."), TrackerListWidget::tr("Tracker URLs:"), {}, &ok);
                if (!ok || text.trimmed().isEmpty()) return;
                method = u"torrents/addTrackers"_s; form.addQueryItem(u"urls"_s, text.trimmed());
            }
            else if (op == u"edit")
            {
                const auto text = QInputDialog::getText(this, TrackerListWidget::tr("Edit tracker URL..."), u"URL"_s, QLineEdit::Normal, urls.first(), &ok);
                if (!ok || text.isEmpty()) return;
                method = u"torrents/editTracker"_s; form.addQueryItem(u"origUrl"_s, urls.first()); form.addQueryItem(u"newUrl"_s, text);
            }
            else if (op == u"remove") { method = u"torrents/removeTrackers"_s; form.addQueryItem(u"urls"_s, urls.join(u'|')); }
            else
            {
                method = u"torrents/reannounce"_s;
                form.addQueryItem(u"hashes"_s, m_torrent.value(u"hash"_s).toString());
                if (op == u"selected")
                {
                    QStringList encoded;
                    for (const auto &url : urls) encoded.append(QString::fromLatin1(QUrl::toPercentEncoding(url)));
                    form.addQueryItem(u"urls"_s, encoded.join(u'|'));
                }
            }
            send(method, form);
        });
        menu->popup(m_trackers->viewport()->mapToGlobal(point));
    });
    bind(m_peers, [this, send](const QPoint &point)
    {
        QStringList peers;
        for (const auto &index : m_peers->selectionModel()->selectedRows())
        {
            auto ip = index.siblingAtColumn(PeerListView::IP).data().toString();
            if (ip.contains(u':')) ip = u'[' + ip + u']';
            peers.append(ip + u':' + index.siblingAtColumn(PeerListView::PORT).data().toString());
        }
        const auto state = m_torrent.value(u"state"_s).toString();
        auto *menu = PeerListWidget::createContextMenu(this, !peers.isEmpty(), m_torrent.value(u"private"_s).toBool(), state.startsWith(u"checking"), state.startsWith(u"queued"),
            [this, send, peers](const QString &op, const QVariant &)
        {
            if (op == u"copy") { QApplication::clipboard()->setText(peers.join(u'\n')); return; }
            QUrlQuery form;
            if (op == u"ban")
            {
                if (QMessageBox::question(this, PeerListWidget::tr("Ban peer permanently"), PeerListWidget::tr("Are you sure you want to permanently ban the selected peers?")) != QMessageBox::Yes) return;
                form.addQueryItem(u"peers"_s, peers.join(u'|')); send(u"transfer/banPeers"_s, form);
            }
            else
            {
                bool ok = false;
                auto text = QInputDialog::getMultiLineText(this, PeerListWidget::tr("Add peers..."), u"IP:port"_s, {}, &ok);
                if (!ok || text.trimmed().isEmpty()) return;
                text.replace(u'\n', u'|');
                form.addQueryItem(u"peers"_s, text.trimmed()); form.addQueryItem(u"hashes"_s, m_torrent.value(u"hash"_s).toString());
                send(u"torrents/addPeers"_s, form);
            }
        });
        menu->popup(m_peers->viewport()->mapToGlobal(point));
    });
    bind(m_ui->listWebSeeds, [this, send](const QPoint &point)
    {
        QStringList urls;
        for (const auto *item : m_ui->listWebSeeds->selectedItems()) urls.append(item->text());
        auto *menu = PropertiesWidget::createContextMenu(this, !urls.isEmpty(), [this, send, urls](const QString &op, const QVariant &)
        {
            if (op == u"copy") { QApplication::clipboard()->setText(urls.join(u'\n')); return; }
            QUrlQuery form;
            QString method;
            if (op == u"remove") { method = u"torrents/removeWebSeeds"_s; form.addQueryItem(u"urls"_s, urls.join(u'|')); }
            else
            {
                bool ok = false;
                const auto url = QInputDialog::getText(this, op == u"add" ? PropertiesWidget::tr("Add web seed...") : PropertiesWidget::tr("Edit web seed URL..."), u"URL"_s, QLineEdit::Normal, op == u"edit" ? urls.first() : QString(), &ok);
                if (!ok || url.isEmpty()) return;
                method = op == u"add" ? u"torrents/addWebSeeds"_s : u"torrents/editWebSeed"_s;
                form.addQueryItem(op == u"add" ? u"urls"_s : u"newUrl"_s, url);
                if (op == u"edit") form.addQueryItem(u"origUrl"_s, urls.first());
            }
            send(method, form);
        });
        menu->popup(m_ui->listWebSeeds->viewport()->mapToGlobal(point));
    });
    bind(m_ui->filesList, [this, send](const QPoint &point)
    {
        const auto rows = m_ui->filesList->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QStringList paths;
        QList<QStringList> groups;
        for (const auto &row : rows)
        {
            paths.append(row.data(Qt::UserRole + 1).toString());
            QStringList ids;
            const std::function<void(const QModelIndex &)> collect = [&](const QModelIndex &index)
            {
                if (index.data(Qt::UserRole + 2).isValid()) ids.append(index.data(Qt::UserRole + 2).toString());
                for (int i = 0; i < index.model()->rowCount(index); ++i) collect(index.model()->index(i, 0, index));
            };
            collect(row); groups.append(ids);
        }
        auto *menu = TorrentContentWidget::createContextMenu(this, rows.size(), true, [this, send, paths, groups](const QString &op, const QVariant &value)
        {
            if (op == u"open" || op == u"parent")
            {
                QString relative = paths.first();
                if (op == u"parent") relative = relative.left(relative.lastIndexOf(u'/') + 1);
                const auto path = relative.isEmpty() ? remoteStoragePath(m_torrent, m_filePaths) : remoteFilePath(remoteStoragePath(m_torrent, m_filePaths), relative);
                if (!path.isEmpty()) emit openPathRequested(path);
                return;
            }
            if (op == u"rename")
            {
                QString oldPath = paths.first();
                const bool folder = oldPath.endsWith(u'/');
                if (folder) oldPath.chop(1);
                const auto slash = oldPath.lastIndexOf(u'/');
                bool ok = false;
                const auto name = QInputDialog::getText(this, TorrentContentWidget::tr("Rename..."), TorrentContentWidget::tr("New name:"), QLineEdit::Normal, oldPath.mid(slash + 1), &ok);
                if (!ok || name.isEmpty() || name == u"." || name == u".." || name.contains(u'/') || name.contains(u'\\')) return;
                QUrlQuery form; form.addQueryItem(u"oldPath"_s, oldPath); form.addQueryItem(u"newPath"_s, oldPath.left(slash + 1) + name);
                send(folder ? u"torrents/renameFolder"_s : u"torrents/renameFile"_s, form); return;
            }
            QMap<int, QSet<QString>> priorities;
            for (int i = 0; i < groups.size(); ++i)
            {
                const int priority = op == u"order" ? (i < qMax<qsizetype>(1, groups.size()/3) ? 7 : i < 2*qMax<qsizetype>(1, groups.size()/3) ? 6 : 1) : value.toInt();
                for (const auto &id : groups[i]) priorities[priority].insert(id);
            }
            for (auto it = priorities.cbegin(); it != priorities.cend(); ++it)
            {
                QUrlQuery form; form.addQueryItem(u"id"_s, QStringList(it.value().values()).join(u'|')); form.addQueryItem(u"priority"_s, QString::number(it.key()));
                send(u"torrents/filePrio"_s, form);
            }
        });
        menu->popup(m_ui->filesList->viewport()->mapToGlobal(point));
    });
}

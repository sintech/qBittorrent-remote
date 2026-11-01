// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QLabel>
#include <QRegularExpression>
#include "base/bittorrent/trackerentry.h"
#include "gui/trackerentriesdialog.h"
#include <QMessageBox>
#include <QSaveFile>
#include "gui/transferlistwidget.h"
using namespace Qt::StringLiterals;
using namespace Remote;

void Window::showContextMenu(const QPoint &position)
{
    if (!m_client.isConnected() || selectedHashes().isEmpty()) return;
    TransferListWidget::MenuState state;
    QList<QJsonObject> torrents;
    const auto hashes = selectedHashes();
    for (const auto &index : m_table->selectionModel()->selectedRows())
    {
        auto torrent = m_model.torrentAt(m_filter.mapToSource(index).row());
        torrent.insert(u"hash"_s, m_model.hashAt(m_filter.mapToSource(index).row()));
        torrents.append(torrent);
    }
    state.selectionCount = torrents.size();
    state.categories = m_categories.keys();
    state.tags = m_tags.values(); state.tags.sort(Qt::CaseInsensitive);
    state.queueing = m_serverState.value(u"queueing"_s).toBool(true);
    bool first = true;
    for (const auto &torrent : torrents)
    {
        const auto status = torrent.value(u"state"_s).toString();
        const bool stopped = status.startsWith(u"stopped") || status.startsWith(u"paused");
        state.needsStart |= stopped || status.startsWith(u"forced") || status.startsWith(u"checking") || status == u"error" || status == u"missingFiles";
        state.needsStop |= !stopped;
        state.needsForce |= !status.startsWith(u"forced");
        state.oneHasMetadata |= !status.endsWith(u"MetaDL");
        state.needsPreview |= !status.endsWith(u"MetaDL");
        state.oneNotFinished |= torrent.value(u"progress"_s).toDouble() < 1;
        state.oneCanForceReannounce |= !stopped && !status.startsWith(u"queued") && !status.startsWith(u"checking") && status != u"error" && status != u"missingFiles";
        state.hasInfohashV1 |= !torrent.value(u"infohash_v1"_s).toString().isEmpty();
        state.hasInfohashV2 |= !torrent.value(u"infohash_v2"_s).toString().isEmpty();
        const auto category = torrent.value(u"category"_s).toString();
        const QSet<QString> tags = [&torrent] { const auto list = torrent.value(u"tags"_s).toString().split(u", "_s, Qt::SkipEmptyParts); return QSet<QString>(list.begin(), list.end()); }();
        state.tagsInAny.unite(tags);
        if (first)
        {
            state.firstCategory = category; state.tagsInAll = tags;
            state.firstAutoTMM = torrent.value(u"auto_tmm"_s).toBool();
            state.superSeedingMode = torrent.value(u"super_seeding"_s).toBool();
            state.sequentialDownloadMode = torrent.value(u"seq_dl"_s).toBool();
            state.prioritizeFirstLast = torrent.value(u"f_l_piece_prio"_s).toBool();
        }
        else
        {
            state.tagsInAll.intersect(tags);
            state.allSameCategory &= category == state.firstCategory;
            state.allSameAutoTMM &= torrent.value(u"auto_tmm"_s).toBool() == state.firstAutoTMM;
            state.allSameSuperSeeding &= torrent.value(u"super_seeding"_s).toBool() == state.superSeedingMode;
            state.allSameSequentialDownloadMode &= torrent.value(u"seq_dl"_s).toBool() == state.sequentialDownloadMode;
            state.allSamePrioFirstlast &= torrent.value(u"f_l_piece_prio"_s).toBool() == state.prioritizeFirstLast;
        }
        first = false;
    }
    const auto server = m_client.serverUrl();
    auto *menu = TransferListWidget::createContextMenu(this, state, [this, torrents, hashes, state, server](const QString &op, const QVariant &value)
    {
        if (!m_client.isConnected() || server != m_client.serverUrl()) return;
        const auto send = [this, hashes](const QString &method, QUrlQuery form = {})
        { form.addQueryItem(u"hashes"_s, hashes.join(u'|')); m_client.command(method, form); };
        const QHash<QString, QString> methods {
            {u"startSelectedTorrents"_s, u"start"_s}, {u"stopSelectedTorrents"_s, u"stop"_s},
            {u"recheckSelectedTorrents"_s,u"recheck"_s},{u"reannounceSelectedTorrents"_s,u"reannounce"_s},
            {u"increaseQueuePosSelectedTorrents"_s,u"increasePrio"_s},{u"decreaseQueuePosSelectedTorrents"_s,u"decreasePrio"_s},
            {u"topQueuePosSelectedTorrents"_s,u"topPrio"_s},{u"bottomQueuePosSelectedTorrents"_s,u"bottomPrio"_s}};
        if (methods.contains(op)) { send(u"torrents/" + methods.value(op)); return; }
        if (op == u"softDeleteSelectedTorrents") { removeSelected(); return; }
        if (op == u"openSelectedTorrentsFolder") { openSelectedFolder(); return; }
        if (op == u"setTorrentOptions") { QDesktopServices::openUrl(m_client.serverUrl()); return; }
        if (op == u"previewSelectedTorrents")
        {
            const auto torrent = torrents.first();
            QUrlQuery query; query.addQueryItem(u"hash"_s, hashes.first());
            m_client.get(u"torrents/files"_s, query, [this, torrent](const QByteArray &body)
            {
                QStringList files; for (const auto &item : QJsonDocument::fromJson(body).array()) files.append(item.toObject().value(u"name"_s).toString());
                if (files.isEmpty()) return;
                bool ok = false;
                const auto name = QInputDialog::getItem(this, TransferListWidget::tr("Pre&view file..."), {}, files, 0, false, &ok);
                if (ok) openRemotePath(remoteFilePath(remoteStoragePath(torrent, files), name));
            }); return;
        }
        if (op.startsWith(u"copy") || op == u"hash1" || op == u"hash2")
        {
            QStringList lines;
            for (const auto &torrent : torrents)
            {
                const QHash<QString, QString> fields {{u"copySelectedNames"_s,u"name"_s},{u"hash1"_s,u"infohash_v1"_s},{u"hash2"_s,u"infohash_v2"_s},{u"copySelectedIDs"_s,u"hash"_s},{u"copySelectedComments"_s,u"comment"_s},{u"copyContentPaths"_s,u"content_path"_s},{u"copySelectedMagnetURIs"_s,u"magnet_uri"_s}};
                lines.append(torrent.value(fields.value(op)).toString());
            }
            QApplication::clipboard()->setText(lines.join(u'\n')); return;
        }
        if (op == u"exportTorrent")
        {
            const auto folder = QFileDialog::getExistingDirectory(this, TransferListWidget::tr("E&xport .torrent..."));
            if (folder.isEmpty()) return;
            for (const auto &torrent : torrents)
            {
                QUrlQuery query; query.addQueryItem(u"hash"_s, torrent.value(u"hash"_s).toString());
                // Hash filenames cannot contain server-provided path separators.
                const auto hash = torrent.value(u"hash"_s).toString();
                if (!QRegularExpression(u"^[0-9a-fA-F]{40,64}$"_s).match(hash).hasMatch()) continue;
                const auto path = folder + u'/' + hash + u".torrent";
                m_client.get(u"torrents/export"_s, query, [this, path](const QByteArray &data)
                { QSaveFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) QMessageBox::warning(this, tr("Export torrent"), tr("Could not save the torrent file.")); });
            }
            return;
        }
        QUrlQuery form;
        if (op == u"forceStartSelectedTorrents") { form.addQueryItem(u"value"_s,u"true"_s); send(u"torrents/setForceStart"_s,form); }
        else if (op == u"setSelectedTorrentsLocation" || op == u"renameSelectedTorrent")
        {
            bool ok = false; const bool location = op == u"setSelectedTorrentsLocation";
            const auto text = QInputDialog::getText(this, location ? TransferListWidget::tr("Set loc&ation...") : TransferListWidget::tr("Re&name..."), location ? tr("Server path:") : tr("New name:"), QLineEdit::Normal, torrents.first().value(location ? u"save_path"_s : u"name"_s).toString(), &ok);
            if (!ok || text.isEmpty()) return;
            form.addQueryItem(location ? u"location"_s : u"name"_s,text);
            if (location) send(u"torrents/setLocation"_s,form);
            else { form.addQueryItem(u"hash"_s,hashes.first()); m_client.command(u"torrents/rename"_s,form); }
        }
        else if (op == u"category" || op == u"newCategory")
        {
            QString category = value.toString();
            if (op == u"newCategory")
            {
                bool ok = false; category = QInputDialog::getText(this, TransferListWidget::tr("&New...", "New category..."), tr("Category:"), QLineEdit::Normal, {}, &ok);
                if (!ok || category.isEmpty()) return;
                // setCategory follows a successful create, preserving mutation ordering.
                m_client.createCategoryAndAssign(category, hashes); return;
            }
            form.addQueryItem(u"category"_s,category); send(u"torrents/setCategory"_s,form);
        }
        else if (op == u"addTags" || op == u"addTag" || op == u"removeTag" || op == u"clearTags")
        {
            QString tags = value.toString();
            if (op == u"addTags") { bool ok = false; tags = QInputDialog::getText(this, TransferListWidget::tr("Ta&gs"), tr("Tags:"), QLineEdit::Normal, {}, &ok); if (!ok || tags.isEmpty()) return; }
            form.addQueryItem(u"tags"_s, tags); send(op == u"removeTag" || op == u"clearTags" ? u"torrents/removeTags"_s : u"torrents/addTags"_s,form);
        }
        else if (op == u"setSelectedAutoTMMEnabled") { form.addQueryItem(u"enable"_s,state.firstAutoTMM && state.allSameAutoTMM ? u"false"_s : u"true"_s); send(u"torrents/setAutoManagement"_s,form); }
        else if (op == u"setSelectedTorrentsSuperSeeding") { form.addQueryItem(u"value"_s,state.superSeedingMode && state.allSameSuperSeeding ? u"false"_s : u"true"_s); send(u"torrents/setSuperSeeding"_s,form); }
        else if (op == u"setSelectedTorrentsSequentialDownload" || op == u"setSelectedFirstLastPiecePrio")
        {
            const auto field = op == u"setSelectedTorrentsSequentialDownload" ? u"seq_dl"_s : u"f_l_piece_prio"_s;
            const bool target = !(field == u"seq_dl" ? state.allSameSequentialDownloadMode && state.sequentialDownloadMode : state.allSamePrioFirstlast && state.prioritizeFirstLast);
            QStringList changed; for (const auto &torrent : torrents) if (torrent.value(u"progress"_s).toDouble() < 1 && torrent.value(field).toBool() != target) changed.append(torrent.value(u"hash"_s).toString());
            if (!changed.isEmpty()) { form.addQueryItem(u"hashes"_s,changed.join(u'|')); m_client.command(field == u"seq_dl" ? u"torrents/toggleSequentialDownload"_s : u"torrents/toggleFirstLastPiecePrio"_s,form); }
        }
        else if (op == u"editTorrentTrackers") editTrackers(hashes);

    });
    menu->popup(m_table->viewport()->mapToGlobal(position));
}

void Window::sidebarCommand(const QString &kind, const QString &op, const QString &value)
{
    if (!m_client.isConnected()) return;
    QStringList hashes;
    for (int row = 0; row < m_filter.rowCount(); ++row) hashes.append(m_model.hashAt(m_filter.mapToSource(m_filter.index(row, 0)).row()));
    if (op.contains(u"Start") || op == u"startVisibleTorrents" || op.contains(u"Stop") || op == u"stopVisibleTorrents")
    {
        if (hashes.isEmpty()) return;
        QUrlQuery form; form.addQueryItem(u"hashes"_s, hashes.join(u'|'));
        m_client.command(op.contains(u"Start") || op == u"startVisibleTorrents" ? u"torrents/start"_s : u"torrents/stop"_s, form); return;
    }
    if (op.contains(u"Delete") || op == u"deleteVisibleTorrents")
    { m_table->selectAll(); removeSelected(); return; }
    QUrlQuery form;
    if (kind == u"category")
    {
        if (op == u"removeCategory" || op == u"removeUnusedCategories")
        {
            QStringList categories;
            if (op == u"removeCategory") categories.append(value);
            else for (const auto &name : m_categories.keys())
            {
                bool used = false;
                for (int i = 0; i < m_model.rowCount(); ++i) used |= m_model.torrentAt(i).value(u"category"_s).toString() == name;
                if (!used) categories.append(name);
            }
            if (categories.isEmpty()) return;
            form.addQueryItem(u"categories"_s, categories.join(u'\n')); m_client.command(u"torrents/removeCategories"_s, form);
        }
        else
        {
            bool ok = false;
            QString name = value;
            if (op != u"editCategory") name = QInputDialog::getText(this, tr("Category"), tr("Name:"), QLineEdit::Normal, op == u"addSubcategory" ? value + u'/' : QString(), &ok);
            else ok = true;
            if (!ok || name.isEmpty()) return;
            const auto path = QInputDialog::getText(this, tr("Category"), tr("Server path:"), QLineEdit::Normal, m_categories.value(name).toObject().value(u"savePath"_s).toString(), &ok);
            if (!ok) return;
            form.addQueryItem(u"category"_s, name); form.addQueryItem(u"savePath"_s, path);
            m_client.command(op == u"editCategory" ? u"torrents/editCategory"_s : u"torrents/createCategory"_s, form);
        }
    }
    else if (kind == u"tag")
    {
        QStringList tags;
        if (op == u"addTag")
        {
            bool ok = false; const auto name = QInputDialog::getText(this, tr("Tags"), tr("Tags:"), QLineEdit::Normal, {}, &ok);
            if (!ok || name.isEmpty()) return;
            form.addQueryItem(u"tags"_s, name); m_client.command(u"torrents/createTags"_s, form); return;
        }
        if (op == u"removeTag") tags.append(value);
        else for (const auto &name : m_tags)
        {
            bool used = false;
            for (int i = 0; i < m_model.rowCount(); ++i) used |= m_model.torrentAt(i).value(u"tags"_s).toString().split(u", "_s).contains(name);
            if (!used) tags.append(name);
        }
        if (tags.isEmpty()) return;
        form.addQueryItem(u"tags"_s, tags.join(u',')); m_client.command(u"torrents/deleteTags"_s, form);
    }
    else if (kind == u"tracker" && op == u"onRemoveTrackerTriggered")
    {
        for (const auto &hash : hashes)
        {
            QUrlQuery query; query.addQueryItem(u"hash"_s, hash);
            m_client.get(u"torrents/trackers"_s, query, [this, value, hash](const QByteArray &body)
            {
                QStringList urls;
                for (const auto &tracker : QJsonDocument::fromJson(body).array())
                { const auto url = tracker.toObject().value(u"url"_s).toString(); if (QUrl(url).host() == value) urls.append(url); }
                if (urls.isEmpty()) return;
                QUrlQuery form; form.addQueryItem(u"hash"_s, hash); form.addQueryItem(u"urls"_s, urls.join(u'|'));
                m_client.command(u"torrents/removeTrackers"_s, form);
            });
        }
    }
}

void Window::editTrackers(const QStringList &hashes)
{
    struct State { QHash<QString, QStringList> trackers; QList<BitTorrent::TrackerEntry> common; int completed = 0; };
    auto state = std::make_shared<State>();
    const auto server = m_client.serverUrl();
    for (const auto &hash : hashes)
    {
        QUrlQuery query; query.addQueryItem(u"hash"_s, hash);
        m_client.get(u"torrents/trackers"_s, query, [this, state, hash, hashes, server](const QByteArray &body)
        {
            QList<BitTorrent::TrackerEntry> entries;
            for (const auto &value : QJsonDocument::fromJson(body).array())
            {
                const auto tracker = value.toObject(); const auto url = tracker.value(u"url"_s).toString();
                if (!url.startsWith(u"**")) { state->trackers[hash].append(url); entries.append({url, tracker.value(u"tier"_s).toInt()}); }
            }
            if (state->completed == 0) state->common = entries;
            else for (int i = state->common.size() - 1; i >= 0; --i) if (!state->trackers[hash].contains(state->common[i].url)) state->common.removeAt(i);
            if (++state->completed != hashes.size() || server != m_client.serverUrl()) return;
            auto *dialog = new TrackerEntriesDialog(this);
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setTrackers(state->common);
            const auto labels = dialog->findChildren<QLabel *>();
            if (!labels.isEmpty()) labels.first()->setText(labels.first()->text() + u"\n\n" + tr("The Web API preserves existing tracker tiers; changing tier order is unavailable."));
            connect(dialog, &QDialog::accepted, this, [this, dialog, state, hashes, server]
            {
                if (server != m_client.serverUrl()) return;
                QStringList edited; for (const auto &entry : dialog->trackers()) if (!edited.contains(entry.url)) edited.append(entry.url);
                QStringList removed; for (const auto &entry : state->common) if (!edited.contains(entry.url)) removed.append(entry.url);
                for (const auto &hash : hashes)
                {
                    QStringList added; for (const auto &url : edited) if (!state->trackers[hash].contains(url)) added.append(url);
                    for (const auto &[method, urls] : {qMakePair(u"torrents/addTrackers"_s, added), qMakePair(u"torrents/removeTrackers"_s, removed)})
                    {
                        if (urls.isEmpty()) continue;
                        QUrlQuery form; form.addQueryItem(u"hash"_s, hash); form.addQueryItem(u"urls"_s, urls.join(method.endsWith(u"addTrackers") ? u'\n' : u'|'));
                        m_client.command(method, form);
                    }
                }
            });
            dialog->open();
        });
    }
}

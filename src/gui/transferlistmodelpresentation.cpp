/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2015-2025  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (C) 2010  Christophe Dumez <chris@qbittorrent.org>
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

#include "transferlistmodel.h"
#include <QCoreApplication>
#include "base/global.h"
#include "uithememanager.h"

QHash<BitTorrent::TorrentState, QColor> TransferListModel::stateColors()
{
    struct TorrentStateColorDescriptor
    {
        const BitTorrent::TorrentState state;
        const QString id;
    };

    const TorrentStateColorDescriptor colorDescriptors[] =
    {
        {BitTorrent::TorrentState::Downloading, u"TransferList.Downloading"_s},
        {BitTorrent::TorrentState::StalledDownloading, u"TransferList.StalledDownloading"_s},
        {BitTorrent::TorrentState::DownloadingMetadata, u"TransferList.DownloadingMetadata"_s},
        {BitTorrent::TorrentState::ForcedDownloadingMetadata, u"TransferList.ForcedDownloadingMetadata"_s},
        {BitTorrent::TorrentState::ForcedDownloading, u"TransferList.ForcedDownloading"_s},
        {BitTorrent::TorrentState::Uploading, u"TransferList.Uploading"_s},
        {BitTorrent::TorrentState::StalledUploading, u"TransferList.StalledUploading"_s},
        {BitTorrent::TorrentState::ForcedUploading, u"TransferList.ForcedUploading"_s},
        {BitTorrent::TorrentState::QueuedDownloading, u"TransferList.QueuedDownloading"_s},
        {BitTorrent::TorrentState::QueuedUploading, u"TransferList.QueuedUploading"_s},
        {BitTorrent::TorrentState::CheckingDownloading, u"TransferList.CheckingDownloading"_s},
        {BitTorrent::TorrentState::CheckingUploading, u"TransferList.CheckingUploading"_s},
        {BitTorrent::TorrentState::CheckingResumeData, u"TransferList.CheckingResumeData"_s},
        {BitTorrent::TorrentState::StoppedDownloading, u"TransferList.StoppedDownloading"_s},
        {BitTorrent::TorrentState::StoppedUploading, u"TransferList.StoppedUploading"_s},
        {BitTorrent::TorrentState::Moving, u"TransferList.Moving"_s},
        {BitTorrent::TorrentState::MissingFiles, u"TransferList.MissingFiles"_s},
        {BitTorrent::TorrentState::Error, u"TransferList.Error"_s}
    };

    QHash<BitTorrent::TorrentState, QColor> colors;
    for (const TorrentStateColorDescriptor &colorDescriptor : colorDescriptors)
    {
        const QColor themeColor = UIThemeManager::instance()->getColor(colorDescriptor.id);
        colors.insert(colorDescriptor.state, themeColor);
    }
    return colors;
}

QHash<BitTorrent::TorrentState, QString> TransferListModel::statusStrings()
{
    return {
        {BitTorrent::TorrentState::Downloading, QCoreApplication::translate("TransferListModel", "Downloading")},
        {BitTorrent::TorrentState::StalledDownloading, QCoreApplication::translate("TransferListModel", "Stalled", "Torrent is waiting for download to begin")},
        {BitTorrent::TorrentState::DownloadingMetadata, QCoreApplication::translate("TransferListModel", "Downloading metadata", "Used when loading a magnet link")},
        {BitTorrent::TorrentState::ForcedDownloadingMetadata, QCoreApplication::translate("TransferListModel", "[F] Downloading metadata", "Used when forced to load a magnet link. You probably shouldn't translate the F.")},
        {BitTorrent::TorrentState::ForcedDownloading, QCoreApplication::translate("TransferListModel", "[F] Downloading", "Used when the torrent is forced started. You probably shouldn't translate the F.")},
        {BitTorrent::TorrentState::Uploading, QCoreApplication::translate("TransferListModel", "Seeding", "Torrent is complete and in upload-only mode")},
        {BitTorrent::TorrentState::StalledUploading, QCoreApplication::translate("TransferListModel", "Seeding", "Torrent is complete and in upload-only mode")},
        {BitTorrent::TorrentState::ForcedUploading, QCoreApplication::translate("TransferListModel", "[F] Seeding", "Used when the torrent is forced started. You probably shouldn't translate the F.")},
        {BitTorrent::TorrentState::QueuedDownloading, QCoreApplication::translate("TransferListModel", "Queued", "Torrent is queued")},
        {BitTorrent::TorrentState::QueuedUploading, QCoreApplication::translate("TransferListModel", "Queued", "Torrent is queued")},
        {BitTorrent::TorrentState::CheckingDownloading, QCoreApplication::translate("TransferListModel", "Checking", "Torrent local data is being checked")},
        {BitTorrent::TorrentState::CheckingUploading, QCoreApplication::translate("TransferListModel", "Checking", "Torrent local data is being checked")},
        {BitTorrent::TorrentState::CheckingResumeData, QCoreApplication::translate("TransferListModel", "Checking resume data", "Used when loading the torrents from disk after qbt is launched. It checks the correctness of the .fastresume file. Normally it is completed in a fraction of a second, unless loading many many torrents.")},
        {BitTorrent::TorrentState::StoppedDownloading, QCoreApplication::translate("TransferListModel", "Stopped")},
        {BitTorrent::TorrentState::StoppedUploading, QCoreApplication::translate("TransferListModel", "Completed")},
        {BitTorrent::TorrentState::Moving, QCoreApplication::translate("TransferListModel", "Moving", "Torrent local data are being moved/relocated")},
        {BitTorrent::TorrentState::MissingFiles, QCoreApplication::translate("TransferListModel", "Missing Files")},
        {BitTorrent::TorrentState::Error, QCoreApplication::translate("TransferListModel", "Errored", "Torrent status, the torrent has an error")}};
}

QIcon TransferListModel::stateIcon(const BitTorrent::TorrentState state)
{
    switch (state)
    {
    case BitTorrent::TorrentState::Downloading:
    case BitTorrent::TorrentState::ForcedDownloading:
    case BitTorrent::TorrentState::DownloadingMetadata:
    case BitTorrent::TorrentState::ForcedDownloadingMetadata:
        return UIThemeManager::instance()->getIcon(u"downloading"_s);
    case BitTorrent::TorrentState::StalledDownloading:
        return UIThemeManager::instance()->getIcon(u"stalledDL"_s);
    case BitTorrent::TorrentState::StalledUploading:
        return UIThemeManager::instance()->getIcon(u"stalledUP"_s);
    case BitTorrent::TorrentState::Uploading:
    case BitTorrent::TorrentState::ForcedUploading:
        return UIThemeManager::instance()->getIcon(u"upload"_s, u"uploading"_s);
    case BitTorrent::TorrentState::StoppedDownloading:
        return UIThemeManager::instance()->getIcon(u"stopped"_s, u"media-playback-pause"_s);
    case BitTorrent::TorrentState::StoppedUploading:
        return UIThemeManager::instance()->getIcon(u"checked-completed"_s, u"completed"_s);
    case BitTorrent::TorrentState::QueuedDownloading:
    case BitTorrent::TorrentState::QueuedUploading:
        return UIThemeManager::instance()->getIcon(u"queued"_s);
    case BitTorrent::TorrentState::CheckingDownloading:
    case BitTorrent::TorrentState::CheckingUploading:
    case BitTorrent::TorrentState::CheckingResumeData:
        return UIThemeManager::instance()->getIcon(u"force-recheck"_s, u"checking"_s);
    case BitTorrent::TorrentState::Moving:
        return UIThemeManager::instance()->getIcon(u"set-location"_s);
    case BitTorrent::TorrentState::Unknown:
    case BitTorrent::TorrentState::MissingFiles:
    case BitTorrent::TorrentState::Error:
        return UIThemeManager::instance()->getIcon(u"error"_s);
    default:
        Q_UNREACHABLE();
        break;
    }

    return {};
}

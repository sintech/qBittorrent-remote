/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023  Vladimir Golovnev <glassez@yandex.ru>
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

#include "basefilterwidget.h"
#include <QMenu>
#include <QCoreApplication>
#include "base/global.h"
#include "gui/uithememanager.h"

#ifndef QBT_REMOTE_CLIENT
#include "base/bittorrent/session.h"
#endif
#include "gui/utils.h"

constexpr int ALL_ROW = 0;

BaseFilterWidget::BaseFilterWidget(QWidget *parent, TransferListWidget *transferList)
    : QListWidget(parent)
    , m_transferList {transferList}
{
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setUniformItemSizes(true);
    setSpacing(0);

    setIconSize(Utils::Gui::smallIconSize());

#if defined(Q_OS_MACOS)
    setAttribute(Qt::WA_MacShowFocusRect, false);
#endif

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &BaseFilterWidget::customContextMenuRequested, this, &BaseFilterWidget::showMenu);
    connect(this, &BaseFilterWidget::currentRowChanged, this, &BaseFilterWidget::applyFilter);

#ifndef QBT_REMOTE_CLIENT
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentsLoaded
            , this, &BaseFilterWidget::handleTorrentsLoaded);
    connect(BitTorrent::Session::instance(), &BitTorrent::Session::torrentAboutToBeRemoved
            , this, &BaseFilterWidget::torrentAboutToBeDeleted);
#endif

}

QSize BaseFilterWidget::sizeHint() const
{
    return {
        // Width should be exactly the width of the content
        sizeHintForColumn(0),
        // Height should be exactly the height of the content
        static_cast<int>((sizeHintForRow(0) + 2 * spacing()) * (count() + 0.5))};
}

QSize BaseFilterWidget::minimumSizeHint() const
{
    QSize size = sizeHint();
    size.setWidth(6);
    return size;
}

TransferListWidget *BaseFilterWidget::transferList() const
{
    return m_transferList;
}

void BaseFilterWidget::toggleFilter(const bool checked)
{
    setVisible(checked);
    if (checked)
        applyFilter(currentRow());
    else
        applyFilter(ALL_ROW);
}

QMenu *BaseFilterWidget::createTrackerContextMenu(QWidget *parent, bool normalItem, const std::function<void(const QString &)> &handler)
{
    QMenu *menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    if (normalItem)
    {
        menu->addAction(UIThemeManager::instance()->getIcon(u"edit-clear"_s, u"list-remove"_s), QCoreApplication::translate("TrackersFilterWidget", "Remove tracker")
            , parent, [handler] { handler(u"onRemoveTrackerTriggered"_s); });
        menu->addSeparator();
    }

    menu->addAction(UIThemeManager::instance()->getIcon(u"torrent-start"_s, u"media-playback-start"_s), QCoreApplication::translate("TrackersFilterWidget", "Start torrents")
        , parent, [handler] { handler(u"startVisibleTorrents"_s); });
    menu->addAction(UIThemeManager::instance()->getIcon(u"torrent-stop"_s, u"media-playback-pause"_s), QCoreApplication::translate("TrackersFilterWidget", "Stop torrents")
        , parent, [handler] { handler(u"stopVisibleTorrents"_s); });
    menu->addAction(UIThemeManager::instance()->getIcon(u"list-remove"_s), QCoreApplication::translate("TrackersFilterWidget", "Remove torrents")
        , parent, [handler] { handler(u"deleteVisibleTorrents"_s); });

    return menu;
}

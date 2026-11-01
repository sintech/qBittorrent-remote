// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"
#include "activation.h"
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include "gui/addnewtorrentdialog.h"
using namespace Qt::StringLiterals;
using namespace Remote;
void Window::openSources(const QStringList &sources)
{
    restoreWindow();
    const auto normalized=normalizeSources(sources,QDir::currentPath());
    if (!normalized.errors.isEmpty()) statusBar()->showMessage(tr("Cannot open these torrent sources: %1").arg(normalized.errors.join(u", "_s)),15000);
    for (const auto &source : normalized.values)
        if (!m_pendingSources.contains(source) && (!m_addDialog || m_addDialog->property("activationSource").toString()!=source)) m_pendingSources.append(source);
    if (m_pendingSources.isEmpty()) return;
    if (m_client.isConnected()) { processPendingSources();return; }
    statusBar()->showMessage(tr("Torrent files are waiting for a server connection."));
    if (!m_sourceConnectionRequested)
    {
        m_sourceConnectionRequested=true;
        QTimer::singleShot(0,this,[this]
        {
            if (m_client.isConnected()) { m_sourceConnectionRequested=false;processPendingSources();return; }
            const auto id=QSettings().value(u"remote/selectedProfile"_s).toString();
            if (!id.isEmpty() && connectProfile(id)) return;
            showConnectionDialog();
            m_sourceConnectionRequested=false;
        });
    }
}
void Window::processPendingSources()
{
    if (!m_client.isConnected() || m_openingSource || m_addDialog || m_pendingSources.isEmpty()) return;
    const auto source=m_pendingSources.first();
    const auto scheme=QUrl(source).scheme().toLower();
    const bool file=scheme!=u"magnet" && scheme!=u"http" && scheme!=u"https";
    if (file && !QFileInfo(source).isFile())
    {
        m_pendingSources.removeFirst();
        statusBar()->showMessage(tr("Torrent file no longer exists: %1").arg(source),15000);
        QTimer::singleShot(0,this,&Window::processPendingSources);return;
    }
    m_openingSource=true;
    showAddDialog(source,file);
}
void Window::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    QStringList sources;for (const auto &url : event->mimeData()->urls()) sources.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!normalizeSources(sources,QDir::currentPath()).values.isEmpty()) event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls()) return;
    QStringList sources;for (const auto &url : event->mimeData()->urls()) sources.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
    openSources(sources);event->acceptProposedAction();
}

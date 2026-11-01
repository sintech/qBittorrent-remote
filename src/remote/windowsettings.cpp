// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"
#include "settingsdialog.h"
#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QWindowStateChangeEvent>
#include <QMenu>
#include <QSettings>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include "gui/transferlistwidget.h"
using namespace Qt::StringLiterals;
using namespace Remote;

void Window::showSettingsDialog()
{
    if (auto *existing = findChild<SettingsDialog *>())
    { existing->show(); existing->raise(); existing->activateWindow(); return; }
    auto *dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&QDialog::accepted,this,[this,dialog]
    {
        applyClientSettings();
        if (dialog->restartRequired()) statusBar()->showMessage(tr("Restart the client to apply language and appearance changes."),15000);
    });
    dialog->open();
}
void Window::openServerWebUi()
{
    if (!m_client.isConnected()) return;
    if (!QDesktopServices::openUrl(m_client.serverUrl()))
        statusBar()->showMessage(tr("Could not open the server Web UI in your browser."),10000);
}
void Window::applyClientSettings()
{
    QSettings settings;
    m_table->setAlternatingRowColors(settings.value(u"remote/list/alternateRows"_s,false).toBool());
    const bool onTop = settings.value(u"remote/desktop/alwaysOnTop"_s,false).toBool();
    if (windowFlags().testFlag(Qt::WindowStaysOnTopHint) != onTop)
    {
        const bool visible = isVisible();
        captureWindowGeometry();
        setWindowFlag(Qt::WindowStaysOnTopHint,onTop);
        if (visible) restoreWindow();
    }
    const bool enabled = settings.value(u"remote/desktop/tray"_s,false).toBool() && QSystemTrayIcon::isSystemTrayAvailable();
    if (enabled && !m_tray)
    {
        m_tray = new QSystemTrayIcon(windowIcon(),this);
        m_tray->setToolTip(u"qBittorrent Remote"_s);
        auto *menu = new QMenu(this);
        menu->addAction(tr("Show window"),this,[this] { restoreWindow(); });
        menu->addAction(tr("Client settings…"),this,&Window::showSettingsDialog);
        menu->addAction(m_webUiAction);
        menu->addSeparator();
        menu->addAction(tr("Exit"),this,[this] { m_forceExit = true; close(); QCoreApplication::quit(); });
        m_tray->setContextMenu(menu);
        connect(m_tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason)
        { if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) { restoreWindow(); } });
    }
    if (m_tray)
    {
        const bool hiddenToTray = m_tray->isVisible() && !isVisible();
        m_tray->setVisible(enabled);
        if (!enabled && hiddenToTray) restoreWindow();
    }
}
void Window::changeEvent(QEvent *event)
{
    MainWindow::changeEvent(event);
    if (!m_geometryReady || m_restoringWindow || event->type()!=QEvent::WindowStateChange) return;
    if (isMinimized())
    {
        const auto *change=static_cast<QWindowStateChangeEvent *>(event);
        if (!change->oldState().testFlag(Qt::WindowMinimized))
            m_restoreWindowState=change->oldState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
        saveWindowState();
        if (m_tray && m_tray->isVisible() && QSettings().value(u"remote/desktop/minimizeToTray"_s,false).toBool())
            QTimer::singleShot(0,this,[this] { if(isMinimized() && m_tray && m_tray->isVisible()) hide(); });
    }
    else
    {
        captureWindowGeometry();
        m_geometrySaveTimer.start();
    }
}
void Window::torrentDoubleClicked()
{
    if (!m_client.isConnected() || selectedHashes().isEmpty()) return;
    const auto action = QSettings().value(u"remote/list/doubleClick"_s,u"none"_s).toString();
    if (action == u"folder") openSelectedFolder();
    else if (action == u"webui") openServerWebUi();
    else if (action == u"toggle")
    {
        const auto rows = m_table->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        const auto torrent = m_model.torrentAt(m_filter.mapToSource(rows.first()).row());
        const auto state = torrent.value(u"state"_s).toString();
        runCommand(state.startsWith(u"stopped") || state.startsWith(u"paused") ? u"torrents/start"_s : u"torrents/stop"_s);
    }
}

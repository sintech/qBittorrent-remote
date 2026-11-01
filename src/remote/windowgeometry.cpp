// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QSettings>
#include <QSplitter>
#include <QScopedValueRollback>
#include "gui/transferlistwidget.h"
#include <QHeaderView>
using namespace Qt::StringLiterals;
using namespace Remote;
void Window::captureWindowGeometry()
{
    if (!m_geometryReady || m_restoringWindow || !isVisible() || isMinimized()) return;
    m_windowGeometry=saveGeometry();
    if (normalGeometry().isValid()) m_normalWindowRect=normalGeometry();
    m_restoreWindowState=windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
}
void Window::saveWindowState()
{
    if (!m_geometryReady || m_restoringWindow) return;
    captureWindowGeometry();
    QSettings settings;
    if (!m_windowGeometry.isEmpty()) settings.setValue(u"remote/geometry"_s,m_windowGeometry);
    settings.setValue(u"remote/nativeHeader"_s,m_table->header()->saveState());
    settings.setValue(u"remote/nativeVertical"_s,m_vertical->saveState());
    settings.setValue(u"remote/nativeHorizontal"_s,m_horizontal->saveState());
}
void Window::restoreWindow()
{
    if (!isVisible() || isMinimized())
    {
        m_geometrySaveTimer.stop();
        const QScopedValueRollback<bool> restoring(m_restoringWindow,true);
        if (m_restoreWindowState.testFlag(Qt::WindowFullScreen)) showFullScreen();
        else if (m_restoreWindowState.testFlag(Qt::WindowMaximized)) showMaximized();
        else
        {
            showNormal();
            if (m_normalWindowRect.isValid()) setGeometry(m_normalWindowRect);
        }
    }
    raise();activateWindow();
    captureWindowGeometry();
    QTimer::singleShot(0,this,&Window::saveWindowState);
}
void Window::moveEvent(QMoveEvent *event)
{
    MainWindow::moveEvent(event);
    if (m_geometryReady && !m_restoringWindow && isVisible() && !isMinimized())
    { captureWindowGeometry();m_geometrySaveTimer.start(); }
}
void Window::resizeEvent(QResizeEvent *event)
{
    MainWindow::resizeEvent(event);
    if (m_geometryReady && !m_restoringWindow && isVisible() && !isMinimized())
    { captureWindowGeometry();m_geometrySaveTimer.start(); }
}
void Window::hideEvent(QHideEvent *event)
{
    saveWindowState();
    MainWindow::hideEvent(event);
}

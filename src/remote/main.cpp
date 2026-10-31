// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QSettings>
#include <QTimer>

#include "window.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qBittorrent"_s);
    QCoreApplication::setApplicationName(u"qBittorrent-remote"_s);
    QCoreApplication::setApplicationVersion(u"5.2.4-remote-preview"_s);
    Remote::Window window;
    window.show();
    QTimer::singleShot(0, &window, &Remote::Window::showConnectionDialog);
    return app.exec();
}

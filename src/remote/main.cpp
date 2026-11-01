// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QSettings>
#include <QLocale>
#include <QTranslator>
#include <QTimer>

#include "window.h"
#include "settingsdialog.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qBittorrent"_s);
    QCoreApplication::setApplicationName(u"qBittorrent-remote"_s);
    QCoreApplication::setApplicationVersion(u"5.2.4-remote-preview"_s);
    QTranslator translator;
    QString language = QSettings().value(u"remote/language"_s, QLocale::system().name()).toString();
    if (language.isEmpty() || language == u"system") language = QLocale::system().name();
    Remote::SettingsDialog::applyStartupAppearance();
    if (language.startsWith(u"ru") && translator.load(u":/translations/qbittorrent_ru.qm"_s))
        app.installTranslator(&translator);
    Remote::Window window;
    if (QSettings().value(u"remote/desktop/startMinimized"_s,false).toBool()) window.showMinimized();
    else window.show();
    QTimer::singleShot(0, &window, &Remote::Window::startLastProfile);
    return app.exec();
}

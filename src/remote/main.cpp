// SPDX-License-Identifier: GPL-2.0-or-later
#include <QApplication>
#include <QSettings>
#include <QLocale>
#include <QTranslator>
#include <QTimer>
#include <QCommandLineParser>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include "activation.h"
#include "app/applicationinstancemanager.h"
#include "base/path.h"

#include "window.h"
#include "settingsdialog.h"

using namespace Qt::StringLiterals;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(u"qBittorrent"_s);
    QCoreApplication::setApplicationName(u"qBittorrent-remote"_s);
    QCoreApplication::setApplicationVersion(u"5.2.4-remote-preview"_s);
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Native qBittorrent remote client"_s);
    parser.addHelpOption();parser.addVersionOption();
    parser.addPositionalArgument(u"sources"_s,u"Torrent files, magnet links or HTTP(S) URLs"_s,u"[sources…]"_s);
    parser.process(app);
    const auto sources=Remote::normalizeSources(parser.positionalArguments(),QDir::currentPath());
    if (!sources.errors.isEmpty()) QMessageBox::warning(nullptr,Remote::Window::tr("Add torrent"),Remote::Window::tr("Cannot open these torrent sources: %1").arg(sources.errors.join(u", "_s)));
    if (sources.values.isEmpty() && !sources.errors.isEmpty()) return 1;
    const auto instancePath=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!QDir().mkpath(instancePath)) { QMessageBox::critical(nullptr,u"qBittorrent Remote"_s,Remote::Window::tr("Cannot create the client configuration directory."));return 1; }
    ApplicationInstanceManager instance{Path(instancePath)};
    if (!instance.isFirstInstance())
    {
        QStringList batch;
        const auto send = [&instance](const QStringList &values) { return instance.sendMessage(Remote::activationMessage(values)); };
        bool delivered=true;
        for (const auto &source : sources.values)
        {
            auto proposed=batch;proposed.append(source);
            if (Remote::activationMessage(proposed).toUtf8().size()>60000)
            {
                if (batch.isEmpty() || !send(batch)) { delivered=false;break; }
                batch.clear();
            }
            batch.append(source);
        }
        if (delivered) delivered=send(batch);
        if (!delivered) QMessageBox::warning(nullptr,u"qBittorrent Remote"_s,Remote::Window::tr("Could not pass the request to the running client. Check that instance before trying again."));
        return delivered ? 0 : 1;
    }
    QTranslator translator;
    QString language = QSettings().value(u"remote/language"_s, QLocale::system().name()).toString();
    if (language.isEmpty() || language == u"system") language = QLocale::system().name();
    Remote::SettingsDialog::applyStartupAppearance();
    if (language.startsWith(u"ru") && translator.load(u":/translations/qbittorrent_ru.qm"_s))
        app.installTranslator(&translator);
    Remote::Window window;
    QObject::connect(&instance,&ApplicationInstanceManager::messageReceived,&window,[&window](const QString &message)
    { QStringList files;if(Remote::decodeActivation(message,files)) window.openSources(files); });
    if (sources.values.isEmpty() && QSettings().value(u"remote/desktop/startMinimized"_s,false).toBool()) window.showMinimized();
    else window.show();
    QTimer::singleShot(0,&window,[&window,files=sources.values]
    { if(files.isEmpty()) window.startLastProfile();else window.openSources(files); });
    return app.exec();
}

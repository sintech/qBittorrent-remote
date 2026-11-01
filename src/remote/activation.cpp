// SPDX-License-Identifier: GPL-2.0-or-later
#include "activation.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSet>
#include <QUrlQuery>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif
using namespace Qt::StringLiterals;
Remote::Sources Remote::normalizeSources(const QStringList &values, const QString &workingDirectory)
{
    Sources result;
    QSet<QString> seen;
    for (const auto &value : values)
    {
        const QUrl url(value);
        QString source;
        if (url.scheme().compare(u"magnet",Qt::CaseInsensitive)==0 || url.scheme().compare(u"http",Qt::CaseInsensitive)==0 || url.scheme().compare(u"https",Qt::CaseInsensitive)==0)
        {
            if (!url.isValid()) { result.errors.append(value); continue; }
            source=value;
        }
        else
        {
            const auto path=url.isLocalFile() ? url.toLocalFile() : value;
            const QFileInfo info(QDir(workingDirectory).absoluteFilePath(path));
            if (info.suffix().compare(u"torrent",Qt::CaseInsensitive)!=0 || !info.isFile() || !info.isReadable())
            { result.errors.append(value); continue; }
            source=info.canonicalFilePath();
        }
        auto key=source;
#ifdef Q_OS_WIN
        if (QUrl(source).scheme()!=u"magnet" && !source.startsWith(u"http",Qt::CaseInsensitive)) key=key.toCaseFolded();
#endif
        if (!seen.contains(key)) { seen.insert(key);result.values.append(source); }
    }
    return result;
}
QString Remote::activationMessage(const QStringList &sources)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{u"version"_s,1},{u"sources"_s,QJsonArray::fromStringList(sources)}}).toJson(QJsonDocument::Compact));
}
bool Remote::decodeActivation(const QString &message, QStringList &sources)
{
    if (message.toUtf8().size()>65535) return false;
    const auto document=QJsonDocument::fromJson(message.toUtf8());
    const auto object=document.object();
    if (!document.isObject() || object.value(u"version"_s).toInt()!=1 || !object.value(u"sources"_s).isArray()) return false;
    QStringList result;
    for (const auto &value : object.value(u"sources"_s).toArray())
    { if (!value.isString() || value.toString().isEmpty()) return false;result.append(value.toString()); }
    sources=result;return true;
}
bool Remote::registerTorrentHandler(QSettings &registry, const QString &executable)
{
    if (!QFileInfo(executable).isAbsolute() || executable.contains(u'"')) return false;
    const auto exe=QDir::toNativeSeparators(executable);
    const auto progId=u"qBittorrent.Remote.Torrent"_s;
    const auto app=u"qBittorrent Remote"_s;
    const auto command=QString(u'"'+exe+u"\" -- \"%1\"");
    const QString prog=u"Software/Classes/"+progId;
    registry.setValue(prog+u"/Default",u"BitTorrent file (qBittorrent Remote)"_s);
    registry.setValue(prog+u"/DefaultIcon/Default",QString(u'"'+exe+u"\",-102"));
    registry.setValue(prog+u"/shell/open/command/Default",command);
    registry.setValue(u"Software/Classes/.torrent/OpenWithProgids/"+progId,QStringLiteral(""));
    const QString application=u"Software/Classes/Applications/"+QFileInfo(executable).fileName();
    registry.setValue(application+u"/FriendlyAppName",app);
    registry.setValue(application+u"/SupportedTypes/.torrent",QStringLiteral(""));
    registry.setValue(application+u"/shell/open/command/Default",command);
    const auto capability=u"Software/qBittorrent/qBittorrent-remote/Capabilities"_s;
    registry.setValue(capability+u"/ApplicationName",app);
    registry.setValue(capability+u"/ApplicationDescription",u"qBittorrent desktop remote client"_s);
    registry.setValue(capability+u"/ApplicationIcon",QString(u'"'+exe+u"\",-101"));
    registry.setValue(capability+u"/FileAssociations/.torrent",progId);
    const auto magnet=u"qBittorrent.Remote.Magnet"_s;
    const QString magnetClass=u"Software/Classes/"+magnet;
    registry.setValue(magnetClass+u"/Default",u"Magnet link (qBittorrent Remote)"_s);
    registry.setValue(magnetClass+u"/URL Protocol",QStringLiteral(""));
    registry.setValue(magnetClass+u"/DefaultIcon/Default",QString(u'"'+exe+u"\",-101"));
    registry.setValue(magnetClass+u"/shell/open/command/Default",command);
    registry.setValue(u"Software/Classes/magnet/URL Protocol"_s,QStringLiteral(""));
    registry.setValue(u"Software/Classes/magnet/OpenWithProgids/"+magnet,QStringLiteral(""));
    registry.setValue(capability+u"/URLAssociations/magnet",magnet);
    registry.setValue(u"Software/RegisteredApplications/"+app,QString(capability).replace(u'/',u'\\'));
    registry.sync();return registry.status()==QSettings::NoError;
}
bool Remote::registerTorrentHandler()
{
#ifdef Q_OS_WIN
    QSettings registry(u"HKEY_CURRENT_USER"_s,QSettings::NativeFormat);
    const bool result=registerTorrentHandler(registry,QCoreApplication::applicationFilePath());
    if (result) SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);
    return result;
#else
    return false;
#endif
}
QUrl Remote::defaultApplicationsUrl()
{
    QUrl url(u"ms-settings:defaultapps"_s);
    QUrlQuery query;query.addQueryItem(u"registeredAppUser"_s,u"qBittorrent Remote"_s);url.setQuery(query);return url;
}

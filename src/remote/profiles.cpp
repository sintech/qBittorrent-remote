// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles.h"
#include "base/path.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    const QString StoreKey = u"remote/profiles/v1"_s;
    QString tr(const char *message) { return QCoreApplication::translate("Remote::Profiles", message); }

    bool windowsPath(const QString &path)
    {
        return path.startsWith(u"//") || (path.size() >= 3 && path[0].isLetter() && path[1] == u':' && path[2] == u'/');
    }

    bool absolutePath(const QString &path)
    {
        return path.startsWith(u'/') || windowsPath(path);
    }

    bool absoluteLocalPath(const QString &path)
    {
#ifdef Q_OS_WIN
        return windowsPath(path);
#else
        return absolutePath(path);
#endif
    }

    QByteArray protect(const QByteArray &plain, QString *error)
    {
#ifdef Q_OS_WIN
        DATA_BLOB input {static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()))};
        DATA_BLOB output {};
        if (CryptProtectData(&input, L"qBittorrent remote profile", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            const QByteArray result(reinterpret_cast<const char *>(output.pbData), output.cbData);
            LocalFree(output.pbData);
            return result;
        }
        *error = tr("Windows could not encrypt profile credentials (%1).").arg(GetLastError());
#else
        Q_UNUSED(plain)
        *error = tr("Saving credentials is supported by the Windows build only.");
#endif
        return {};
    }

    QByteArray unprotect(const QByteArray &encrypted, QString *error)
    {
#ifdef Q_OS_WIN
        DATA_BLOB input {static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()))};
        DATA_BLOB output {};
        if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        {
            const QByteArray result(reinterpret_cast<const char *>(output.pbData), output.cbData);
            SecureZeroMemory(output.pbData, output.cbData);
            LocalFree(output.pbData);
            return result;
        }
        *error = tr("Credentials cannot be decrypted under this Windows account (%1). Enter them again.").arg(GetLastError());
#else
        Q_UNUSED(encrypted)
        *error = tr("These credentials can only be read by the Windows account that saved them.");
#endif
        return {};
    }
}

bool Remote::validServerUrl(const QUrl &url)
{
    return url.isValid() && !url.host().isEmpty() && (url.scheme() == u"http" || url.scheme() == u"https")
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment();
}

QString Remote::normalizedPath(const QString &value)
{
    QString path = value;
    path.replace(u'\\', u'/');
    const bool unc = path.startsWith(u"//");
    const bool driveAbsolute = path.size() >= 3 && path[0].isLetter() && path[1] == u':' && path[2] == u'/';
    path = QDir::cleanPath(path);
    if (unc && !path.startsWith(u"//")) path.prepend(u'/');
    if (driveAbsolute && path.size() == 2 && path[1] == u':') path += u'/';
    return path;
}

QString Remote::remoteStoragePath(const QJsonObject &torrent, const QStringList &files)
{
    const QString fallback = torrent.value(u"save_path"_s).toString();
    const QString actual = torrent.value(u"content_path"_s).toString();
    if (actual.isEmpty() || files.isEmpty()) return fallback;
    PathList paths;
    for (const auto &file : files) paths.append(Path(file));
    // The server computes content_path with the same upstream root-folder helper.
    const QString relative = files.size() == 1 ? files.first() : Path::findRootFolder(paths).toString();
    const QString content = normalizedPath(actual);
    if (relative.isEmpty()) return content;
    const QString suffix = u'/' + normalizedPath(relative);
    const auto sensitivity = windowsPath(content) ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (!content.endsWith(suffix, sensitivity)) return fallback;
    QString root = content.first(content.size() - suffix.size());
    if (root.isEmpty()) root = u"/"_s;
    if (root.size() == 2 && root[1] == u':') root += u'/';
    return root;
}

QString Remote::remoteFilePath(const QString &directory, const QString &relativePath)
{
    QString relative = relativePath;
    relative.replace(u'\\', u'/');
    if (directory.isEmpty() || relative.isEmpty() || absolutePath(relative)
        || relative.split(u'/').contains(u".."_s)) return {};
    return normalizedPath(directory + u'/' + relative);
}

Connection Profile::connection() const
{
    Connection result;
    result.url = url;
    if (!result.url.path().endsWith(u'/')) result.url.setPath(result.url.path() + u'/');
    if (apiKeyAuthentication) result.apiKey = apiKey;
    else { result.username = username; result.password = password; }
    return result;
}

QString Profile::localPath(const QString &remotePath) const
{
    if (remotePath.isEmpty()) return {};
    const QString path = normalizedPath(remotePath);
    if (!absolutePath(path)) return {};
    QString result;
    qsizetype best = -1;
    for (const auto &mapping : mappings)
    {
        const QString root = normalizedPath(mapping.remote);
        const QString local = normalizedPath(mapping.local);
        if (!absolutePath(root) || !absoluteLocalPath(local)) continue;
        const auto sensitivity = windowsPath(root) ? Qt::CaseInsensitive : Qt::CaseSensitive;
        const QString prefix = root.endsWith(u'/') ? root : root + u'/';
        QString suffix;
        if (path.compare(root, sensitivity) == 0) suffix.clear();
        else if (path.startsWith(prefix, sensitivity)) suffix = path.mid(prefix.size());
        else continue;
        if (root.size() <= best) continue;
        best = root.size();
        result = normalizedPath(local + u'/' + suffix);
    }
    return result;
}

Profile ProfileStore::create()
{
    Profile profile;
    profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile.name = tr("New server");
    return profile;
}

QString ProfileStore::validate(const Profile &profile)
{
    if (profile.id.isEmpty() || profile.name.trimmed().isEmpty()) return tr("Enter a profile name.");
    if (!validServerUrl(profile.url)) return tr("Enter an HTTP(S) server URL without credentials, query or fragment.");
    QSet<QString> roots;
    for (const auto &mapping : profile.mappings)
    {
        const QString root = normalizedPath(mapping.remote);
        const QString local = normalizedPath(mapping.local);
        if (mapping.remote.isEmpty() || mapping.local.isEmpty() || !absolutePath(root) || !absoluteLocalPath(local))
            return tr("Both mapping paths must be absolute and non-empty.");
        const QString key = windowsPath(root) ? root.toLower() : root;
        if (roots.contains(key)) return tr("A remote folder is mapped more than once.");
        roots.insert(key);
    }
    return {};
}

QList<Profile> ProfileStore::load(QString *error)
{
    if (error) error->clear();
    QSettings settings;
    const auto bytes = settings.value(StoreKey).toByteArray();
    if (bytes.isEmpty())
    {
        // Earlier previews stored only the address and username, never secrets.
        const QUrl previous(settings.value(u"remote/url"_s).toString());
        if (!validServerUrl(previous)) return {};
        auto legacy = create();
        legacy.id = u"legacy"_s;
        legacy.name = previous.host();
        legacy.url = previous;
        legacy.username = settings.value(u"remote/username"_s, u"admin"_s).toString();
        legacy.apiKeyAuthentication = settings.value(u"remote/auth"_s, 0).toInt() == 0;
        return {legacy};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()
        || document.object().value(u"schema"_s).toInt() != 1
        || !document.object().value(u"profiles"_s).isArray())
    {
        if (error) *error = tr("The profile configuration is invalid. It has not been overwritten.");
        return {};
    }
    QList<Profile> result;
    QSet<QString> ids;
    for (const auto &value : document.object().value(u"profiles"_s).toArray())
    {
        const auto object = value.toObject();
        Profile profile;
        profile.id = object.value(u"id"_s).toString();
        profile.name = object.value(u"name"_s).toString();
        profile.url = QUrl(object.value(u"url"_s).toString());
        profile.username = object.value(u"username"_s).toString();
        profile.apiKeyAuthentication = object.value(u"api_key_auth"_s).toBool();
        for (const auto &entry : object.value(u"mappings"_s).toArray())
        {
            const auto mapping = entry.toObject();
            profile.mappings.append({mapping.value(u"remote"_s).toString(), mapping.value(u"local"_s).toString()});
        }
        if (!validate(profile).isEmpty() || ids.contains(profile.id))
        {
            if (error) *error = tr("The profile configuration is invalid. It has not been overwritten.");
            return {};
        }
        ids.insert(profile.id);
        const auto encoded = object.value(u"secrets_dpapi"_s).toString().toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
        {
            if (error) *error = tr("The profile configuration is invalid. It has not been overwritten.");
            return {};
        }
        profile.protectedSecrets = decoded.decoded;
        if (!profile.protectedSecrets.isEmpty())
        {
            const auto plain = unprotect(profile.protectedSecrets, &profile.secretError);
            const auto secrets = QJsonDocument::fromJson(plain);
            profile.secretsReadable = profile.secretError.isEmpty() && secrets.isObject();
            if (profile.secretsReadable)
            {
                profile.password = secrets.object().value(u"password"_s).toString();
                profile.apiKey = secrets.object().value(u"api_key"_s).toString();
            }
            else if (profile.secretError.isEmpty()) profile.secretError = tr("Saved credentials are invalid. Enter them again.");
        }
        result.append(profile);
    }
    return result;
}

bool ProfileStore::save(const QList<Profile> &profiles, QString *error)
{
    QString failure;
    QJsonArray entries;
    QSet<QString> ids;
    for (const auto &profile : profiles)
    {
        failure = validate(profile);
        if (ids.contains(profile.id)) failure = tr("Duplicate profile identifier.");
        if (!failure.isEmpty()) { if (error) *error = failure; return false; }
        ids.insert(profile.id);
        QByteArray encrypted = profile.protectedSecrets;
        if (profile.secretsReadable)
        {
            encrypted.clear();
            if (!profile.password.isEmpty() || !profile.apiKey.isEmpty())
            {
                const auto plain = QJsonDocument(QJsonObject {{u"password"_s, profile.password}, {u"api_key"_s, profile.apiKey}}).toJson(QJsonDocument::Compact);
                encrypted = protect(plain, &failure);
                if (!failure.isEmpty()) { if (error) *error = failure; return false; }
            }
        }
        QJsonArray mappings;
        for (const auto &mapping : profile.mappings)
            mappings.append(QJsonObject {{u"remote"_s, normalizedPath(mapping.remote)}, {u"local"_s, normalizedPath(mapping.local)}});
        entries.append(QJsonObject {{u"id"_s, profile.id}, {u"name"_s, profile.name.trimmed()}, {u"url"_s, profile.url.toString()},
            {u"username"_s, profile.username}, {u"api_key_auth"_s, profile.apiKeyAuthentication},
            {u"secrets_dpapi"_s, QString::fromLatin1(encrypted.toBase64())}, {u"mappings"_s, mappings}});
    }
    QSettings settings;
    settings.setValue(StoreKey, QJsonDocument(QJsonObject {{u"schema"_s, 1}, {u"profiles"_s, entries}}).toJson(QJsonDocument::Compact));
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        if (error) *error = tr("Could not save the local profile configuration.");
        return false;
    }
    if (error) error->clear();
    return true;
}

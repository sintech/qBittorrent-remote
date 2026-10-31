// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QList>
#include <QString>
#include "client.h"

namespace Remote
{
    struct PathMapping { QString remote; QString local; };

    struct Profile
    {
        QString id;
        QString name;
        QUrl url;
        bool apiKeyAuthentication = false;
        QString username = QStringLiteral("admin");
        QString password;
        QString apiKey;
        QList<PathMapping> mappings;
        QByteArray protectedSecrets;
        bool secretsReadable = true;
        QString secretError;

        Connection connection() const;
        QString localPath(const QString &remotePath) const;
    };

    QString normalizedPath(const QString &path);
    QString remoteFilePath(const QString &directory, const QString &relativePath);
    bool validServerUrl(const QUrl &url);

    class ProfileStore
    {
    public:
        static QList<Profile> load(QString *error = nullptr);
        static bool save(const QList<Profile> &profiles, QString *error = nullptr);
        static QString validate(const Profile &profile);
        static Profile create();
    };
}

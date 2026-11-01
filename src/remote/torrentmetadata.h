// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDateTime>
#include <QStringList>
namespace Remote
{
    struct TorrentMetadata
    {
        QString name, comment, hashV1, hashV2;
        QDateTime created;
        QStringList files;
        QList<qint64> sizes;
        QString error;
        bool valid() const { return error.isEmpty() && !files.isEmpty(); }
    };
    TorrentMetadata readTorrentMetadata(const QString &path);
}

// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QStringList>
#include <QUrl>
class QSettings;
namespace Remote
{
    struct Sources { QStringList values, errors; };
    Sources normalizeSources(const QStringList &values, const QString &workingDirectory);
    QString activationMessage(const QStringList &sources);
    bool decodeActivation(const QString &message, QStringList &sources);
    bool registerTorrentHandler(QSettings &userRegistry, const QString &executable);
    bool registerTorrentHandler();
    QUrl defaultApplicationsUrl();
}

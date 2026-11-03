// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <functional>

#include <QJsonObject>
#include <QMap>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

class QHttpMultiPart;
class QNetworkReply;

namespace Remote
{
    struct Connection
    {
        QUrl url;
        QString username;
        QString password;
        QString apiKey;
    };

    class Client final : public QObject
    {
        Q_OBJECT

    public:
        explicit Client(QObject *parent = nullptr, QNetworkAccessManager *network = nullptr);
        void connectToServer(const Connection &connection);
        void disconnectFromServer();
        bool isConnected() const;
        quint64 connectionGeneration() const { return m_generation; }
        QUrl serverUrl() const;
        void command(const QString &method, const QUrlQuery &parameters);
        void post(const QString &method, const QUrlQuery &parameters, std::function<void(const QByteArray &)> callback);
        QString serverVersion() const { return m_serverVersion; }
        QString webApiVersion() const { return m_webApiVersion; }
        static bool supportsVersions(const QString &serverVersion, const QString &webApiVersion);
        void createCategoryAndAssign(const QString &category, const QStringList &hashes);
        void addTorrentFile(const QString &path, const QUrlQuery &parameters = {}, const QString &hash = {}, const QMap<int, QStringList> &priorities = {});
        void probe(const QString &method, const QUrlQuery &parameters, bool post,
            std::function<void(const QByteArray &)> callback);
        void get(const QString &method, const QUrlQuery &parameters,
            std::function<void(const QByteArray &)> callback);

    signals:
        void connectedChanged(bool connected);
        void statusChanged(const QString &message);
        void errorOccurred(const QString &message);
        void requestFailed(const QString &method);
        void syncReceived(const QJsonObject &data);
        void reset();

    private:
        using Callback = std::function<void(const QByteArray &)>;
        void request(const QString &method, const QUrlQuery &parameters, bool post,
            Callback callback, bool mutation = false, QHttpMultiPart *multipart = nullptr, bool optional = false);
        void applyFilePriorities(const QString &hash, QMap<int, QStringList> priorities, bool start);
        void invalidateRequests();
        void authenticate();
        void poll();
        void retry();
        void setConnected(bool connected);

        QNetworkAccessManager *m_network = nullptr;
        QTimer m_pollTimer;
        Connection m_connection;
        QString m_serverVersion;
        QString m_webApiVersion;
        QSet<QNetworkReply *> m_pending;
        qint64 m_revision = 0;
        quint64 m_generation = 0;
        int m_retryDelay = 1000;
        bool m_pollInFlight = false;
        bool m_connected = false;
        bool m_running = false;
        bool m_authenticated = false;
    };
}

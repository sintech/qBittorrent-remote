// SPDX-License-Identifier: GPL-2.0-or-later
#include "client.h"
#include "profiles.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>

using namespace Qt::StringLiterals;
using namespace Remote;

Client::Client(QObject *parent, QNetworkAccessManager *network)
    : QObject(parent)
    , m_network(network ? network : new QNetworkAccessManager(this))
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]
    {
        if (m_authenticated)
            poll();
        else
            authenticate();
    });
}

void Client::connectToServer(const Connection &connection)
{
    disconnectFromServer();
    m_connection = connection;
    auto &url = m_connection.url;
    if (!validServerUrl(url))
    {
        emit errorOccurred(tr("Enter an HTTP(S) server URL without credentials, query or fragment."));
        return;
    }
    QString path = url.path();
    if (!path.endsWith(u'/'))
        path += u'/';
    url.setPath(path);
    m_running = true;
    authenticate();
}

void Client::disconnectFromServer()
{
    m_running = false;
    m_authenticated = false;
    m_revision = 0;
    m_retryDelay = 1000;
    m_pollTimer.stop();
    invalidateRequests();
    // Do not carry a cookie or cached credentials to another profile.
    m_network->setCookieJar(new QNetworkCookieJar(m_network));
    m_network->clearAccessCache();
    m_connection = {};
    setConnected(false);
    emit reset();
}

void Client::invalidateRequests()
{
    ++m_generation;
    m_pollInFlight = false;
    const auto pending = m_pending;
    m_pending.clear();
    for (auto *reply : pending)
    {
        QObject::disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

bool Client::isConnected() const
{
    return m_connected;
}

QUrl Client::serverUrl() const
{
    return m_connection.url;
}

void Client::setConnected(const bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged(connected);
}

void Client::request(const QString &method, const QUrlQuery &parameters, const bool post,
    Callback callback, const bool mutation, QHttpMultiPart *multipart)
{
    if (!m_running)
    {
        delete multipart;
        return;
    }
    QUrl url = m_connection.url;
    url.setPath(url.path() + u"api/v2/" + method);
    if (!post)
        url.setQuery(parameters);
    QNetworkRequest request(url);
    // Never forward the password, cookie or API key to a redirect destination.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(15000);
    QUrl origin = m_connection.url;
    origin.setPath({});
    request.setRawHeader("Referer", origin.toEncoded());
    if (!m_connection.apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_connection.apiKey.toUtf8());
    QNetworkReply *reply = nullptr;
    if (multipart)
    {
        reply = m_network->post(request, multipart);
        multipart->setParent(reply);
    }
    else if (post)
    {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-www-form-urlencoded"_s);
        reply = m_network->post(request, parameters.toString(QUrl::FullyEncoded).replace(u"+"_s, u"%2B"_s).toUtf8());
    }
    else
        reply = m_network->get(request);

    m_pending.insert(reply);
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, mutation, method, generation]
    {
        m_pending.remove(reply);
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (method == u"sync/maindata")
            m_pollInFlight = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if ((reply->error() != QNetworkReply::NoError) || (status < 200) || (status >= 300))
        {
            // Do not include response bodies/URLs: a proxy may echo credentials.
            QString message = tr("Request %1 failed (HTTP %2).").arg(method).arg(status);
            if (mutation)
                message += u' ' + tr("It was not retried. Refresh the list before repeating the operation.");
            emit errorOccurred(message);
            if (((status == 401) || (status == 403))
                && (!m_connection.apiKey.isEmpty() || (method == u"auth/login")))
            {
                invalidateRequests();
                m_running = false;
                m_authenticated = false;
                m_pollTimer.stop();
                setConnected(false);
            }
            else if ((status == 401) || (status == 403) || (status == 0)
                || (method == u"sync/maindata") || (method == u"app/version"))
            {
                invalidateRequests();
                m_authenticated = false;
                setConnected(false);
                retry();
            }
            return;
        }
        callback(reply->readAll());
    });
}

void Client::authenticate()
{
    emit statusChanged(tr("Connecting…"));
    const auto checkVersion = [this]
    {
        request(u"app/version"_s, {}, false, [this](const QByteArray &version)
        {
            if (!version.startsWith("v5.2."))
            {
                m_running = false;
                emit errorOccurred(tr("This prototype requires qBittorrent 5.2.x."));
                return;
            }
            m_authenticated = true;
            m_revision = 0;
            poll();
        });
    };
    if (!m_connection.apiKey.isEmpty())
    {
        checkVersion();
        return;
    }
    QUrlQuery form;
    form.addQueryItem(u"username"_s, m_connection.username);
    form.addQueryItem(u"password"_s, m_connection.password);
    request(u"auth/login"_s, form, true, [this, checkVersion](const QByteArray &body)
    {
        if (body.trimmed() != "Ok.")
        {
            m_running = false; // Wrong credentials must not cause an IP ban through retries.
            emit errorOccurred(tr("Authentication failed. Check the username and password."));
            return;
        }
        checkVersion();
    });
}

void Client::poll()
{
    if (m_pollInFlight)
        return;
    m_pollInFlight = true;
    QUrlQuery query;
    query.addQueryItem(u"rid"_s, QString::number(m_revision));
    request(u"sync/maindata"_s, query, false, [this](const QByteArray &body)
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(body, &error);
        const QJsonObject data = document.object();
        if ((error.error != QJsonParseError::NoError) || !document.isObject()
            || !data.value(u"rid"_s).isDouble()
            || ((m_revision == 0) && !data.value(u"full_update"_s).toBool()))
        {
            m_authenticated = false;
            setConnected(false);
            emit errorOccurred(tr("The server returned an invalid synchronization response."));
            retry();
            return;
        }
        m_revision = data.value(u"rid"_s).toInteger();
        m_retryDelay = 1000;
        setConnected(true);
        emit statusChanged(tr("Connected"));
        emit syncReceived(data);
        m_pollTimer.start(1500);
    });
}

void Client::retry()
{
    if (!m_running)
        return;
    m_revision = 0;
    emit statusChanged(tr("Disconnected — reconnecting…"));
    m_pollTimer.start(m_retryDelay);
    m_retryDelay = qMin(m_retryDelay * 2, 30000);
}

void Client::command(const QString &method, const QUrlQuery &parameters)
{
    if (!m_connected)
    {
        emit errorOccurred(tr("The server is disconnected. The command was not sent."));
        return;
    }
    request(method, parameters, true, [this, method](const QByteArray &body)
    {
        if ((method == u"torrents/add") && (body.trimmed() == "Fails."))
            emit errorOccurred(tr("The server could not add the torrent."));
        m_pollTimer.start(0);
    }, true);
}

void Client::addTorrentFile(const QString &path, const QUrlQuery &parameters, const QString &hash, const QMap<int, QStringList> &priorities)
{
    if (!m_connected)
        return;
    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto *file = new QFile(path, multipart);
    if (!file->open(QIODevice::ReadOnly))
    {
        delete multipart;
        emit errorOccurred(tr("Could not read the torrent file."));
        return;
    }
    QHttpPart part;
    // A constant filename avoids quoting/encoding ambiguities in MIME headers.
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        u"form-data; name=\"torrents\"; filename=\"upload.torrent\""_s);
    part.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-bittorrent"_s);
    part.setBodyDevice(file);
    multipart->append(part);
    auto options = parameters;
    const bool stagePriorities = !hash.isEmpty() && !priorities.isEmpty();
    const bool startAfterPriorities = stagePriorities && parameters.queryItemValue(u"stopped"_s) != u"true";
    if (stagePriorities)
    {
        options.removeAllQueryItems(u"stopped"_s);
        options.addQueryItem(u"stopped"_s, u"true"_s);
    }
    for (const auto &[name, value] : options.queryItems(QUrl::FullyDecoded))
    {
        QHttpPart option;
        option.setHeader(QNetworkRequest::ContentDispositionHeader, QString(u"form-data; name=\""_s + name + u"\""_s));
        option.setBody(value.toUtf8());
        multipart->append(option);
    }
    request(u"torrents/add"_s, {}, true, [this, hash, priorities, stagePriorities, startAfterPriorities](const QByteArray &body)
    {
        if (body.trimmed() == "Fails.")
            emit errorOccurred(tr("The server could not add the torrent."));
        if (body.trimmed() == "Ok." && stagePriorities)
            applyFilePriorities(hash, priorities, startAfterPriorities);
        m_pollTimer.start(0);
    }, true, multipart);
}

void Client::get(const QString &method, const QUrlQuery &parameters, Callback callback)
{
    if (m_connected)
        request(method, parameters, false, std::move(callback));
}

void Client::createCategoryAndAssign(const QString &category, const QStringList &hashes)
{
    if (!m_connected) return;
    QUrlQuery form; form.addQueryItem(u"category"_s, category); form.addQueryItem(u"savePath"_s, QString());
    request(u"torrents/createCategory"_s, form, true, [this, category, hashes](const QByteArray &)
    {
        QUrlQuery assign; assign.addQueryItem(u"category"_s, category); assign.addQueryItem(u"hashes"_s, hashes.join(u'|'));
        command(u"torrents/setCategory"_s, assign);
    }, true);
}

void Client::applyFilePriorities(const QString &hash, QMap<int, QStringList> priorities, const bool start)
{
    if (priorities.isEmpty())
    {
        if (start)
        {
            QUrlQuery form; form.addQueryItem(u"hashes"_s, hash);
            command(u"torrents/start"_s, form);
        }
        return;
    }
    const auto first = priorities.cbegin();
    QUrlQuery form;
    form.addQueryItem(u"hash"_s, hash);
    form.addQueryItem(u"id"_s, first.value().join(u'|'));
    form.addQueryItem(u"priority"_s, QString::number(first.key()));
    priorities.remove(first.key());
    request(u"torrents/filePrio"_s, form, true, [this, hash, priorities, start](const QByteArray &)
    { applyFilePriorities(hash, priorities, start); }, true);
}

// SPDX-License-Identifier: GPL-2.0-or-later
#include "logcontroller.h"
#include "client.h"
#include "gui/executionlogwidget.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
using namespace Qt::StringLiterals;
using namespace Remote;
LogController::LogController(Client *client,ExecutionLogWidget *widget)
    : QObject(widget),m_client(client),m_widget(widget)
{
    connect(client,&Client::connectedChanged,this,[this](bool connected)
    {
        if(!connected)
        {
            ++m_generation;m_messageId=m_peerId=-1;m_messagesPending=m_peersPending=false;
            m_widget->clearRemote();
        }
        else refresh();
    });
    connect(client,&Client::reset,this,[this]
    { ++m_generation;m_messageId=m_peerId=-1;m_messagesPending=m_peersPending=false;m_widget->clearRemote(); });
    connect(client,&Client::syncReceived,this,[this](const QJsonObject &data)
    {
        // A fresh full snapshot can follow a server restart without an observed
        // transport failure (e.g. API-key auth). Old log IDs then no longer apply.
        if(data[u"full_update"].toBool())
        {
            ++m_generation;m_messageId=m_peerId=-1;m_messagesPending=m_peersPending=false;
            m_widget->clearRemote();refresh();
        }
    });
    connect(client,&Client::requestFailed,this,[this](const QString &method)
    { if(method==u"log/main") m_messagesPending=false;if(method==u"log/peers") m_peersPending=false; });
    m_timer.setInterval(2500);connect(&m_timer,&QTimer::timeout,this,&LogController::refresh);m_timer.start();
}
void LogController::refresh()
{
    if(!m_client->isConnected() || !m_widget->isVisible()) return;
    const auto generation=m_generation;
    QPointer<LogController> guard(this);
    if(!m_messagesPending)
    {
        m_messagesPending=true;QUrlQuery q;q.addQueryItem(u"last_known_id"_s,QString::number(m_messageId));
        m_client->get(u"log/main"_s,q,[this,guard,generation](const QByteArray &body)
        {
            if(!guard || generation!=m_generation) return;m_messagesPending=false;
            const auto doc=QJsonDocument::fromJson(body);if(!doc.isArray()) return;
            for(const auto &entry:doc.array())
            {
                const auto p=entry.toObject();const int id=p[u"id"].toInt(-1);if(id<=m_messageId) continue;
                Log::Msg message;message.id=id;message.timestamp=p[u"timestamp"].toInteger();message.type=static_cast<Log::MsgType>(p[u"type"].toInt());message.message=p[u"message"].toString();
                m_widget->appendRemoteMessage(message);m_messageId=id;
            }
        });
    }
    if(!m_peersPending)
    {
        m_peersPending=true;QUrlQuery q;q.addQueryItem(u"last_known_id"_s,QString::number(m_peerId));
        m_client->get(u"log/peers"_s,q,[this,guard,generation](const QByteArray &body)
        {
            if(!guard || generation!=m_generation) return;m_peersPending=false;
            const auto doc=QJsonDocument::fromJson(body);if(!doc.isArray()) return;
            for(const auto &entry:doc.array())
            {
                const auto p=entry.toObject();const int id=p[u"id"].toInt(-1);if(id<=m_peerId) continue;
                Log::Peer peer;peer.id=id;peer.timestamp=p[u"timestamp"].toInteger();peer.blocked=p[u"blocked"].toBool();peer.ip=p[u"ip"].toString();peer.reason=p[u"reason"].toString();
                m_widget->appendRemotePeer(peer);m_peerId=id;
            }
        });
    }
}

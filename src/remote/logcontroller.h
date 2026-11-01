// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QTimer>
class ExecutionLogWidget;
namespace Remote {
class Client;
class LogController final : public QObject
{
    Q_OBJECT
public:
    LogController(Client *client, ExecutionLogWidget *widget);
    void refresh();
private:
    Client *m_client;
    ExecutionLogWidget *m_widget;
    QTimer m_timer;
    int m_messageId=-1, m_peerId=-1;
    bool m_messagesPending=false, m_peersPending=false;
    quint64 m_generation=0;
};
}

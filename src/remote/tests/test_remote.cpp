// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <QAbstractItemModelTester>
#include <QAction>
#include <QBuffer>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTreeView>
#include <QLabel>
#include <QGroupBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QTest>
#include <QTranslator>
#include <QTemporaryDir>
#include <QSettings>

#include "client.h"
#include "properties.h"
#include "torrentmodel.h"
#include "window.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    struct Response
    {
        QByteArray body;
        int status = 200;
        int delay = 0;
    };

    class Reply final : public QNetworkReply
    {
    public:
        Reply(const QNetworkRequest &request, const Response &response, QObject *parent)
            : QNetworkReply(parent)
            , m_body(response.body)
        {
            setRequest(request);
            setUrl(request.url());
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
            open(QIODevice::ReadOnly);
            QTimer::singleShot(response.delay, this, [this, response]
            {
                if (m_done)
                    return;
                m_done = true;
                if (response.status >= 400)
                    setError(QNetworkReply::ContentAccessDenied, u"HTTP error"_s);
                setFinished(true);
                emit readyRead();
                emit finished();
            });
        }
        void abort() override
        {
            m_done = true;
            setError(OperationCanceledError, u"Cancelled"_s);
            setFinished(true);
            emit finished();
        }
        qint64 bytesAvailable() const override
        {
            return m_body.size() - m_offset + QNetworkReply::bytesAvailable();
        }
    protected:
        qint64 readData(char *data, const qint64 maximum) override
        {
            const qint64 count = qMin(maximum, m_body.size() - m_offset);
            if (count == 0)
                return -1;
            memcpy(data, m_body.constData() + m_offset, count);
            m_offset += count;
            return count;
        }
    private:
        QByteArray m_body;
        qint64 m_offset = 0;
        bool m_done = false;
    };

    class Network final : public QNetworkAccessManager
    {
    public:
        struct Sent { Operation operation; QNetworkRequest request; QByteArray body; };
        QList<Sent> sent;
        QHash<QString, Response> responses;
        QByteArray snapshot = R"({"rid":1,"full_update":true,"torrents":{}})";
        QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoing) override
        {
            sent.append({operation, request, outgoing ? outgoing->readAll() : QByteArray()});
            const QString path = request.url().path();
            const auto response = responses.value(path,
                path.endsWith(u"app/version") ? Response {"v5.2.4"}
                : path.endsWith(u"auth/login") ? Response {"Ok."}
                : path.endsWith(u"sync/maindata") ? Response {snapshot}
                : Response {""});
            return new Reply(request, response, this);
        }
    };

    Connection keyConnection()
    {
        Connection result;
        result.url = QUrl(u"https://example.test/qbt/"_s);
        result.apiKey = u"test-key"_s;
        return result;
    }
}

class TestRemote final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDirectory.isValid());
        QCoreApplication::setOrganizationName(u"qBittorrentRemoteTests"_s);
        QCoreApplication::setApplicationName(u"isolated"_s);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
    }

    void init()
    {
        QSettings().clear();
    }

    void modelMergesDeltasAndPreservesSelection()
    {
        TorrentModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.applySync(QJsonObject {{u"full_update"_s, true}, {u"torrents"_s, QJsonObject {
            {u"hash1"_s, QJsonObject {{u"name"_s, u"First"_s}, {u"dlspeed"_s, 100}}},
            {u"hash2"_s, QJsonObject {{u"name"_s, u"Second"_s}}}}}});
        QCOMPARE(model.rowCount(), 2);
        QPersistentModelIndex selected(model.index(0, 1));
        model.applySync(QJsonObject {{u"torrents"_s, QJsonObject {
            {u"hash1"_s, QJsonObject {{u"dlspeed"_s, 300}}}}}});
        QCOMPARE(selected.data().toString(), u"First"_s);
        QCOMPARE(model.index(0, 8).data(Qt::UserRole).toInt(), 300);
        model.applySync(QJsonObject {{u"full_update"_s, true}, {u"torrents"_s, QJsonObject {
            {u"hash1"_s, QJsonObject {{u"name"_s, u"Renamed"_s}}}}}});
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(selected.isValid());
        QCOMPARE(selected.data().toString(), u"Renamed"_s);
        QVERIFY(!model.index(0, 8).data(Qt::UserRole).isValid());
        model.applySync(QJsonObject {{u"torrents_removed"_s, QJsonArray {u"hash1"_s}}});
        QCOMPARE(model.rowCount(), 0);
        QVERIFY(!selected.isValid());
    }

    void authenticationAndReverseProxyPath()
    {
        Network network;
        Client client(nullptr, &network);
        QSignalSpy connected(&client, &Client::connectedChanged);
        client.connectToServer(keyConnection());
        QTRY_VERIFY(client.isConnected());
        QCOMPARE(connected.size(), 1);
        QCOMPARE(network.sent[0].request.url().path(), u"/qbt/api/v2/app/version"_s);
        QCOMPARE(network.sent[0].request.rawHeader("Authorization"), QByteArray("Bearer test-key"));
        QCOMPARE(network.sent[0].request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
            int(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(network.sent[1].request.url().query(), u"rid=0"_s);
        client.disconnectFromServer();
        QVERIFY(!client.isConnected());
    }

    void passwordEncoding()
    {
        Network network;
        Client client(nullptr, &network);
        auto connection = keyConnection();
        connection.apiKey.clear();
        connection.username = u"a+b@example.test"_s;
        connection.password = u"p+&= пароль"_s;
        client.connectToServer(connection);
        QTRY_VERIFY(client.isConnected());
        const QByteArray body = network.sent.first().body;
        QVERIFY(body.contains("a%2Bb"));
        QVERIFY(body.contains("p%2B%26%3D"));
        QVERIFY(!network.sent.first().request.hasRawHeader("Authorization"));
    }

    void wrongPasswordDoesNotRetry()
    {
        Network network;
        network.responses.insert(u"/qbt/api/v2/auth/login"_s, {"Fails."});
        Client client(nullptr, &network);
        QSignalSpy errors(&client, &Client::errorOccurred);
        auto connection = keyConnection();
        connection.apiKey.clear();
        client.connectToServer(connection);
        QTRY_COMPARE(errors.size(), 1);
        QTest::qWait(1200);
        QCOMPARE(network.sent.size(), 1);
        QVERIFY(!client.isConnected());
    }

    void invalidSnapshotDoesNotConnect()
    {
        Network network;
        network.snapshot = "<html>login</html>";
        Client client(nullptr, &network);
        QSignalSpy errors(&client, &Client::errorOccurred);
        client.connectToServer(keyConnection());
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!client.isConnected());
    }

    void failedMutationIsNeverReplayed()
    {
        Network network;
        network.responses.insert(u"/qbt/api/v2/torrents/stop"_s, {"error", 500});
        Client client(nullptr, &network);
        QSignalSpy errors(&client, &Client::errorOccurred);
        client.connectToServer(keyConnection());
        QTRY_VERIFY(client.isConnected());
        QUrlQuery form;
        form.addQueryItem(u"hashes"_s, u"abc|def"_s);
        client.command(u"torrents/stop"_s, form);
        QTRY_COMPARE(errors.size(), 1);
        QTest::qWait(1800);
        int count = 0;
        for (const auto &sent : network.sent)
        {
            if (sent.request.url().path().endsWith(u"torrents/stop"))
            {
                ++count;
                QCOMPARE(sent.operation, QNetworkAccessManager::PostOperation);
                QVERIFY(sent.body.contains("hashes=abc"));
            }
        }
        QCOMPARE(count, 1);
        QVERIFY(errors.first().first().toString().contains(u"not retried"));
    }

    void switchingServersDiscardsOldResponses()
    {
        Network network;
        network.responses.insert(u"/qbt/api/v2/app/version"_s, {"v5.2.4", 100});
        Client client(nullptr, &network);
        QSignalSpy sync(&client, &Client::syncReceived);
        client.connectToServer(keyConnection());
        client.disconnectFromServer();
        QTest::qWait(150);
        QCOMPARE(sync.size(), 0);
        QVERIFY(!client.isConnected());
    }

    void reconnectRestartsWithFullSnapshot()
    {
        Network network;
        Client client(nullptr, &network);
        client.connectToServer(keyConnection());
        QTRY_VERIFY(client.isConnected());
        network.responses.insert(u"/qbt/api/v2/sync/maindata"_s, {"unavailable", 503});
        QTRY_VERIFY_WITH_TIMEOUT(!client.isConnected(), 3000);
        network.responses.remove(u"/qbt/api/v2/sync/maindata"_s);
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 3000);
        QCOMPARE(network.sent.last().request.url().query(), u"rid=0"_s);
    }

    void invalidApiKeyDoesNotRetry()
    {
        Network network;
        network.responses.insert(u"/qbt/api/v2/app/version"_s, {"forbidden", 403});
        Client client(nullptr, &network);
        QSignalSpy errors(&client, &Client::errorOccurred);
        client.connectToServer(keyConnection());
        QTRY_COMPARE(errors.size(), 1);
        QTest::qWait(1200);
        QCOMPARE(network.sent.size(), 1);
        QVERIFY(!client.isConnected());
    }

    void redirectsAreNotFollowed()
    {
        Network network;
        network.responses.insert(u"/qbt/api/v2/app/version"_s, {"", 302});
        Client client(nullptr, &network);
        QSignalSpy errors(&client, &Client::errorOccurred);
        client.connectToServer(keyConnection());
        QTRY_COMPARE(errors.size(), 1);
        QVERIFY(!client.isConnected());
        QCOMPARE(network.sent.size(), 1);
    }

    void nativeWindowControlsRemoteSelection()
    {
        Network network;
        network.snapshot = R"({"rid":1,"full_update":true,"torrents":{
            "abc":{"name":"Example Linux ISO","size":2147483648,"progress":0.42,"state":"downloading","dlspeed":4194304,"upspeed":524288,"eta":300,"ratio":0.12,"priority":1,"num_seeds":12,"num_complete":24,"num_leechs":3,"num_incomplete":7},
            "def":{"name":"Completed archive","size":104857600,"progress":1,"state":"uploading","dlspeed":0,"upspeed":2097152,"ratio":2.1,"priority":2}},
            "server_state":{"dl_info_speed":4194304,"up_info_speed":2621440,"free_space_on_disk":107374182400}})";
        network.responses.insert(u"/qbt/api/v2/torrents/properties"_s,
            {R"({"save_path":"/srv/downloads","total_size":104857600,"total_downloaded":104857600,"total_uploaded":220200960,"dl_speed":0,"up_speed":2097152,"share_ratio":2.1,"time_elapsed":7200,"nb_connections":8,"piece_size":262144,"created_by":"Example","comment":"Remote properties"})"});
        Window window(nullptr, &network);
        window.resize(1150, 760);
        window.show();
        window.client()->connectToServer(keyConnection());
        QTRY_VERIFY(window.client()->isConnected());
        auto *table = window.findChild<QTreeView *>(u"remoteTorrentTable"_s);
        QTRY_COMPARE(table->model()->rowCount(), 2);
        table->selectionModel()->select(table->model()->index(0, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        auto *stop = window.findChild<QAction *>(u"actionStop"_s);
        QVERIFY(stop->isEnabled());
        stop->trigger();
        QTRY_VERIFY(std::any_of(network.sent.cbegin(), network.sent.cend(), [](const auto &sent)
        { return sent.request.url().path().endsWith(u"torrents/stop"); }));
        for (const auto &sent : network.sent)
        {
            if (sent.request.url().path().endsWith(u"torrents/stop"))
                QCOMPARE(sent.body, QByteArray("hashes=def")); // Sorted by name, not source row.
        }
        const auto *savePath = window.findChild<QLabel *>(u"labelSavePathVal"_s);
        QVERIFY(savePath);
        QTRY_COMPARE(savePath->text(), u"/srv/downloads"_s);
        QVERIFY(window.findChild<QGroupBox *>(u"groupTransferBox"_s));
        QVERIFY(window.findChild<QGroupBox *>(u"groupInfosBox"_s));
        QVERIFY(!table->rootIsDecorated());
        const QString screenshot = qEnvironmentVariable("REMOTE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty())
            QVERIFY(window.grab().save(screenshot));
        window.client()->disconnectFromServer();
        QVERIFY(!stop->isEnabled());
        QCOMPARE(table->model()->rowCount(), 0);
    }

    void propertiesReuseOriginalGroupsAndHierarchicalContent()
    {
        Properties panel;
        panel.resize(1000, 500);
        panel.show();
        panel.setTorrent(QJsonObject {{u"hash"_s, u"first"_s}, {u"progress"_s, 0.5}});
        panel.setData(0, QJsonDocument(QJsonObject {{u"save_path"_s, u"/srv/first"_s}, {u"comment"_s, u"<b>Literal metadata</b>"_s}}));
        QCOMPARE(panel.findChild<QLabel *>(u"labelSavePathVal"_s)->text(), u"/srv/first"_s);
        QCOMPARE(panel.findChild<QLabel *>(u"labelCommentVal"_s)->textFormat(), Qt::PlainText);
        auto *stack = panel.findChild<QStackedWidget *>(u"stackedProperties"_s);
        QVERIFY(stack);
        auto buttons = panel.findChildren<QPushButton *>();
        QPushButton *content = nullptr;
        for (auto *button : buttons)
        {
            if (button->text() == u"Content")
                content = button;
        }
        QVERIFY(content);
        QVERIFY(content->mapTo(&panel, QPoint()).y() >= stack->geometry().bottom());
        content->click();
        QCOMPARE(panel.currentIndex(), 4);
        const auto files = QJsonDocument::fromJson(R"([
            {"name":"root/video/a.mkv","size":100,"progress":0.5,"priority":1},
            {"name":"root/video/b.mkv","size":200,"progress":1,"priority":0}])");
        panel.setData(4, files);
        auto *tree = panel.findChild<QTreeView *>(u"remoteContent"_s);
        QVERIFY(tree);
        QCOMPARE(tree->model()->rowCount(), 1);
        const auto root = tree->model()->index(0, 0);
        QCOMPARE(root.data().toString(), u"root"_s);
        const auto folder = tree->model()->index(0, 0, root);
        QCOMPARE(folder.data().toString(), u"video"_s);
        QCOMPARE(tree->model()->rowCount(folder), 2);
        QCOMPARE(tree->model()->index(0, 0, folder).data().toString(), u"a.mkv"_s);
        panel.setTorrent(QJsonObject {{u"hash"_s, u"second"_s}});
        QVERIFY(panel.findChild<QLabel *>(u"labelSavePathVal"_s)->text().isEmpty());
    }

    void stateAndPathFiltersWorkTogether()
    {
        TorrentModel model;
        model.applySync(QJsonObject {{u"full_update"_s, true}, {u"torrents"_s, QJsonObject {
            {u"a"_s, QJsonObject {{u"name"_s, u"First"_s}, {u"save_path"_s, u"/srv/video"_s}, {u"progress"_s, 0.5}, {u"state"_s, u"downloading"_s}}},
            {u"b"_s, QJsonObject {{u"name"_s, u"Second"_s}, {u"save_path"_s, u"/srv/music"_s}, {u"progress"_s, 1}, {u"state"_s, u"stoppedUP"_s}}}}}});
        TorrentFilter filter;
        filter.setSourceModel(&model);
        filter.setFilters(u"video"_s, u"downloading"_s, {}, {}, 26);
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.index(0, 1).data().toString(), u"First"_s);
        filter.setFilters({}, u"stopped"_s, {}, {});
        QCOMPARE(filter.rowCount(), 1);
        QCOMPARE(filter.index(0, 1).data().toString(), u"Second"_s);
    }

#ifdef REMOTE_HAS_TRANSLATIONS
    void originalRussianCatalogTranslatesNativeForms()
    {
        QTranslator translator;
        QVERIFY(translator.load(u":/translations/qbittorrent_ru.qm"_s));
        QCoreApplication::installTranslator(&translator);
        {
            Properties panel;
            const auto *group = panel.findChild<QGroupBox *>(u"groupTransferBox"_s);
            QVERIFY(group);
            QCOMPARE(group->title(), u"Торрент"_s);
            TorrentModel model;
            QCOMPARE(model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), u"Имя"_s);
        }
        QCoreApplication::removeTranslator(&translator);
    }
#endif

    void nativeWindowStartsDisconnected()
    {
        Window window;
        auto *table = window.findChild<QTreeView *>(u"remoteTorrentTable"_s);
        QVERIFY(table);
        QCOMPARE(table->model()->columnCount(), 38);
        auto *start = window.findChild<QAction *>(u"actionStart"_s);
        auto *options = window.findChild<QAction *>(u"actionConnectRemote"_s);
        QVERIFY(start);
        QVERIFY(options);
        QVERIFY(!start->isEnabled());
        QVERIFY(options->isEnabled());
    }
};

QTEST_MAIN(TestRemote)
#include "test_remote.moc"

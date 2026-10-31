// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <QAbstractItemModelTester>
#include <QAction>
#include <QDesktopServices>
#include <QBuffer>
#include <QJsonArray>
#include <QNetworkReply>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTreeView>
#include <QLabel>
#include <QLineEdit>
#include <QGroupBox>
#include <QStackedWidget>
#include <QPushButton>
#include <QTest>
#include <QTranslator>
#include <QComboBox>
#include <QHeaderView>
#include <QMenu>
#include <QLocale>
#include <QStandardItemModel>
#include <QAbstractItemView>
#include <QTemporaryDir>
#include <QCheckBox>
#include <QTextEdit>
#include <QStyle>
#include <QTableWidget>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QFile>
#include <QDir>
#include "profiles.h"
#include "profileeditor.h"
#include "gui/transferlistwidget.h"
#include "gui/transferlistdelegate.h"
#include "gui/properties/peerlistwidget.h"
#include "gui/trackerlist/trackerlistwidget.h"
#include "gui/torrentcontentwidget.h"
#include "gui/transferlistfilterswidget.h"
#include "gui/transferlistfilters/statusfilterwidget.h"
#include "gui/transferlistfilters/categoryfilterwidget.h"
#include "gui/transferlistfilters/tagfilterwidget.h"
#include "gui/statusbar.h"
#include "gui/lineedit.h"
#include "gui/downloadfromurldialog.h"
#include "gui/deletionconfirmationdialog.h"
#include <QSettings>

#include "client.h"
#include "properties.h"
#include "gui/propertylistviews.h"
#include "gui/properties/speedwidget.h"
#include "gui/properties/speedplotview.h"
#include "gui/properties/downloadedpiecesbar.h"
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

class BrowserCapture final : public QObject
{
    Q_OBJECT
public:
    BrowserCapture()
    {
        QDesktopServices::setUrlHandler(u"file"_s, this, "capture");
        QDesktopServices::setUrlHandler(u"http"_s, this, "capture");
        QDesktopServices::setUrlHandler(u"https"_s, this, "capture");
    }
    ~BrowserCapture() override
    {
        QDesktopServices::unsetUrlHandler(u"file"_s);
        QDesktopServices::unsetUrlHandler(u"http"_s);
        QDesktopServices::unsetUrlHandler(u"https"_s);
    }
    QList<QUrl> urls;
public slots:
    void capture(const QUrl &url) { urls.append(url); }
};

class TestRemote final : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settingsDirectory;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDirectory.isValid());
        QLocale::setDefault(QLocale(u"en_US"_s));
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
        network.responses.insert(u"/qbt/api/v2/torrents/pieceStates"_s, {"[2,2,2,2]"});
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

    void nativePiecesStripExpandsAndSpeedUsesServerSamples()
    {
        Properties panel;
        panel.resize(1000, 600);
        panel.show();
        auto *strip = panel.findChild<DownloadedPiecesBar *>(u"remotePieceStrip"_s);
        QVERIFY(strip);
        QCOMPARE(strip->sizePolicy().horizontalPolicy(), QSizePolicy::Expanding);
        QTRY_VERIFY(strip->width() > panel.width() * 0.7);
        panel.setData(6, QJsonDocument(QJsonArray {2, 2, 1, 0, 0}));
        auto *speed = panel.findChild<SpeedWidget *>(u"remoteSpeedWidget"_s);
        QVERIFY(speed);
        auto *plot = speed->findChild<SpeedPlotView *>();
        QVERIFY(plot);
        QPushButton *button = nullptr;
        for (auto *candidate : panel.findChildren<QPushButton *>())
            if (candidate->text() == u"Speed") button = candidate;
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        button->click();
        QCOMPARE(panel.currentIndex(), 5);
        panel.pushStats(QJsonObject {{u"up_info_speed"_s, 1024}, {u"dl_info_speed"_s, 4096}});
        QTest::qWait(1100);
        panel.pushStats(QJsonObject {{u"up_info_speed"_s, 1024}, {u"dl_info_speed"_s, 4096}});
        QTest::qWait(1100);
        panel.pushStats(QJsonObject {{u"up_info_speed"_s, 1024}, {u"dl_info_speed"_s, 4096}});
        QVERIFY(plot->sampleCount() >= 2);
        QCOMPARE(plot->peakValue(), quint64(4096));
        const auto before = plot->sampleCount();
        panel.setTorrent(QJsonObject {{u"hash"_s, u"another"_s}});
        QCOMPARE(plot->sampleCount(), before); // Session graph survives torrent selection.
        auto *period = speed->findChild<QComboBox *>(u"speedPeriod"_s);
        QVERIFY(period);
        QCOMPARE(period->count(), 7);
        period->setCurrentIndex(1);
        QCOMPARE(plot->peakValue(), quint64(4096));
        const auto screenshot = qEnvironmentVariable("REMOTE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) QVERIFY(panel.grab().save(screenshot + u".speed.png"));
        panel.resetSpeedHistory();
        QCOMPARE(plot->sampleCount(), qsizetype(0));
    }

    void sharedPropertyViewsHaveNativeColumnsAndStableRows()
    {
        Properties panel;
        auto *trackers = panel.findChild<TrackerListView *>(u"remoteTrackers"_s);
        auto *peers = panel.findChild<PeerListView *>(u"remotePeers"_s);
        auto *content = panel.findChild<TorrentContentView *>(u"remoteContent"_s);
        QVERIFY(trackers);
        QVERIFY(peers);
        QVERIFY(content);
        QCOMPARE(trackers->model()->columnCount(), 11);
        QCOMPARE(trackers->model()->headerData(0, Qt::Horizontal).toString(), u"URL/Announce Endpoint"_s);
        QCOMPARE(peers->model()->columnCount(), 15);
        QCOMPARE(peers->model()->headerData(1, Qt::Horizontal).toString(), u"IP/Address"_s);
        QCOMPARE(content->model()->columnCount(), 6);
        panel.setData(1, QJsonDocument::fromJson("[\n            {\"url\":\"** [DHT] **\",\"status\":2}, {\"url\":\"** [PeX] **\",\"status\":2}, {\"url\":\"** [LSD] **\",\"status\":2},\n            {\"url\":\"https://tracker.test/announce\",\"tier\":0,\"status\":2,\"endpoints\":[{\"name\":\"192.0.2.1:443\",\"status\":2,\"bt_version\":1}]}]"));
        QCOMPARE(trackers->model()->rowCount(), 4);
        QCOMPARE(trackers->model()->rowCount(trackers->model()->index(3, 0)), 1);
        panel.setData(2, QJsonDocument::fromJson("{\"peers\":{\n            \"a\":{\"ip\":\"10.0.0.10\",\"port\":100,\"client\":\"A\",\"dl_speed\":20},\n            \"b\":{\"ip\":\"10.0.0.2\",\"port\":20,\"client\":\"B\",\"dl_speed\":100}}}"));
        peers->sortByColumn(PeerListView::PORT, Qt::AscendingOrder);
        QCOMPARE(peers->model()->index(0, PeerListView::PORT).data(Qt::UserRole).toInt(), 20);
        peers->sortByColumn(PeerListView::IP, Qt::AscendingOrder);
        QCOMPARE(peers->model()->index(0, PeerListView::IP).data().toString(), u"10.0.0.2"_s);
        QPersistentModelIndex selected(peers->model()->index(0, PeerListView::IP));
        panel.setData(2, QJsonDocument::fromJson("{\"peers\":{\n            \"a\":{\"ip\":\"10.0.0.10\",\"port\":100,\"client\":\"A\",\"dl_speed\":30},\n            \"b\":{\"ip\":\"10.0.0.2\",\"port\":20,\"client\":\"B\",\"dl_speed\":200}}}"));
        QVERIFY(selected.isValid());
        QCOMPARE(selected.data().toString(), u"10.0.0.2"_s);
        QCOMPARE(peers->header()->stretchLastSection(), false);
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
            ProfileEditor profiles({}, {}, nullptr);
            QCOMPARE(profiles.windowTitle(), u"Профили подключений"_s);
            QCOMPARE(profiles.findChild<QTableWidget *>(u"profileMappings"_s)->horizontalHeaderItem(0)->text(), u"Удалённая папка"_s);
            Window window;
            QCOMPARE(window.findChild<QAction *>(u"actionConnectionProfiles"_s)->text(), u"Профили подключений…"_s);
        }
        QCoreApplication::removeTranslator(&translator);
    }
#endif

    void canonicalWidgetsAndNativeModelRoles()
    {
        Window window;
        QVERIFY(qobject_cast<MainWindow *>(&window));
        auto *table = window.findChild<TransferListWidget *>(u"remoteTorrentTable"_s);
        QVERIFY(table);
        QVERIFY(qobject_cast<TransferListDelegate *>(table->itemDelegate()));
        QVERIFY(window.findChild<StatusBar *>());
        QVERIFY(window.findChild<LineEdit *>());
        auto *panel = window.findChild<PropertiesWidget *>(u"remoteProperties"_s);
        QVERIFY(panel);
        QVERIFY(panel->findChild<PeerListWidget *>());
        QVERIFY(panel->findChild<TrackerListWidget *>());
        QVERIFY(panel->findChild<TorrentContentWidget *>());
        auto *sidebar = window.findChild<TransferListFiltersWidget *>(u"remoteSidebar"_s);
        QVERIFY(sidebar);
        auto *status = sidebar->findChild<StatusFilterWidget *>();
        QCOMPARE(status->count(), 14);
        QVERIFY(sidebar->findChild<CategoryFilterWidget *>()->currentIndex().data(Qt::UserRole).isNull());
        QVERIFY(sidebar->findChild<TagFilterWidget *>()->currentIndex().data(Qt::UserRole).isNull());
        TorrentModel model;
        model.applySync(QJsonObject {{u"full_update"_s, true}, {u"torrents"_s, QJsonObject {
            {u"a"_s, QJsonObject {{u"progress"_s, 0.42}, {u"state"_s, u"stoppedDL"_s}}}}}});
        QCOMPARE(model.index(0, 4).data(Qt::UserRole).toDouble(), 42.0);
        QCOMPARE(model.index(0, 5).data(Qt::UserRole).value<BitTorrent::TorrentState>(), BitTorrent::TorrentState::StoppedDownloading);
#ifdef Q_OS_WIN
        QCOMPARE(QApplication::style()->objectName().toLower(), u"fusion"_s);
#endif
    }

    void originalUrlAndRemovalDialogs()
    {
        DownloadFromURLDialog urls(nullptr);
        auto *input = urls.findChild<QTextEdit *>(u"textUrls"_s);
        QVERIFY(input);
        input->setPlainText(u" https://example.test/a.torrent\nhttps://example.test/a.torrent\nmagnet:?xt=urn:btih:example\n"_s);
        QSignalSpy requested(&urls, &DownloadFromURLDialog::urlsReadyToBeDownloaded);
        QVERIFY(QMetaObject::invokeMethod(&urls, "onSubmit"));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toStringList(), (QStringList {u"https://example.test/a.torrent"_s, u"magnet:?xt=urn:btih:example"_s}));
        QCOMPARE(urls.result(), int(QDialog::Accepted));
        DeletionConfirmationDialog removal(nullptr, 1, u"Example torrent"_s, false);
        QVERIFY(!removal.isRemoveContentSelected());
        auto *check = removal.findChild<QCheckBox *>(u"checkRemoveContent"_s);
        QVERIFY(check);
        check->setChecked(true);
        QVERIFY(removal.isRemoveContentSelected());
    }

    void optionsOpensCurrentServerWebUiWithoutCredentials()
    {
        BrowserCapture browser;
        Network network;
        Window window(nullptr, &network);
        auto *options = window.findChild<QAction *>(u"actionOptions"_s);
        QVERIFY(options);
        QVERIFY(!options->isEnabled());
        auto first = keyConnection();
        first.url = QUrl(u"https://example.test/qbt"_s);
        window.client()->connectToServer(first);
        QTRY_VERIFY(options->isEnabled());
        options->trigger();
        QCOMPARE(browser.urls, (QList<QUrl> {QUrl(u"https://example.test/qbt/"_s)}));
        Connection second;
        second.url = QUrl(u"http://second.test:8080/reverse/proxy/"_s);
        second.username = u"admin"_s;
        second.password = u"browser-must-not-receive-this"_s;
        window.client()->connectToServer(second);
        QVERIFY(!options->isEnabled());
        QTRY_VERIFY(options->isEnabled());
        options->trigger();
        QCOMPARE(browser.urls.size(), 2);
        QCOMPARE(browser.urls.last(), second.url);
        QVERIFY(browser.urls.last().userInfo().isEmpty());
        QVERIFY(!browser.urls.last().hasQuery());
        window.client()->disconnectFromServer();
        QVERIFY(!options->isEnabled());
    }

    void pathMappingsUseBoundariesAndLongestPrefix()
    {
        Profile profile;
        profile.mappings = {{u"/srv"_s, u"X:/Downloads"_s}, {u"/srv/video"_s, u"Y:/Video"_s}};
        QCOMPARE(profile.localPath(u"/srv/video/movie.mkv"_s), u"Y:/Video/movie.mkv"_s);
        QCOMPARE(profile.localPath(u"/srv/video-old/a.mkv"_s), u"X:/Downloads/video-old/a.mkv"_s);
        QVERIFY(profile.localPath(u"/srv2/movie.mkv"_s).isEmpty());
        QCOMPARE(profile.localPath(u"/srv/video/"_s), u"Y:/Video"_s);
        QVERIFY(profile.localPath(u"srv/video/movie.mkv"_s).isEmpty());
        profile.mappings = {{u"/srv/video"_s, u"//nas/media"_s}};
        QVERIFY(profile.localPath(u"/srv/video/../../private/key"_s).isEmpty());
        QVERIFY(profile.localPath(u"/SRV/video/movie.mkv"_s).isEmpty());
        QCOMPARE(profile.localPath(u"/srv/video/movie.mkv"_s), u"//nas/media/movie.mkv"_s);
        profile.mappings = {{u"C:\\Downloads"_s, u"D:\\Local"_s}};
        QCOMPARE(profile.localPath(u"c:/downloads/movie.mkv"_s), u"D:/Local/movie.mkv"_s);
        profile.mappings = {{u"/"_s, u"//nas/share"_s}};
        QCOMPARE(profile.localPath(u"/video/a.mkv"_s), u"//nas/share/video/a.mkv"_s);
        QCOMPARE(remoteFilePath(u"/srv/downloads"_s, u"video/a.mkv"_s), u"/srv/downloads/video/a.mkv"_s);
        QVERIFY(remoteFilePath(u"/srv/downloads"_s, u"../private/key"_s).isEmpty());
        QVERIFY(remoteFilePath(u"/srv/downloads"_s, u"/etc/passwd"_s).isEmpty());
        QCOMPARE(normalizedPath(u"C:"_s), u"C:"_s);
    }

    void profilesPersistAndRejectInvalidReplacements()
    {
        auto profile = ProfileStore::create();
        profile.name = u"NAS"_s;
        profile.url = QUrl(u"https://example.test/qbt/"_s);
        profile.username = u"admin"_s;
        profile.mappings = {{u"/srv/downloads"_s, u"X:/Downloads"_s}};
        QString error;
        QVERIFY2(ProfileStore::save({profile}, &error), qPrintable(error));
        const auto loaded = ProfileStore::load(&error);
        QVERIFY(error.isEmpty());
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().id, profile.id);
        QCOMPARE(loaded.first().url, profile.url);
        QCOMPARE(loaded.first().mappings.first().local, u"X:/Downloads"_s);
        const auto previous = QSettings().value(u"remote/profiles/v1"_s);
        auto invalid = profile;
        invalid.mappings.append({u"/srv/downloads/"_s, u"Y:/Other"_s});
        QVERIFY(!ProfileStore::save({invalid}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(QSettings().value(u"remote/profiles/v1"_s), previous);
        invalid = profile;
        invalid.url = QUrl(u"https://admin:secret@example.test/qbt/"_s);
        QVERIFY(!ProfileStore::save({invalid}, &error));
        QCOMPARE(QSettings().value(u"remote/profiles/v1"_s), previous);
        invalid = profile;
        invalid.mappings = {{u"/srv"_s, u"C:"_s}};
        QVERIFY(!ProfileStore::save({invalid}, &error));
        QSettings().setValue(u"remote/profiles/v1"_s, QByteArray("invalid json"));
        QVERIFY(ProfileStore::load(&error).isEmpty());
        QVERIFY(!error.isEmpty());
        QCOMPARE(QSettings().value(u"remote/profiles/v1"_s).toByteArray(), QByteArray("invalid json"));
    }

    void profileSecretsUseWindowsProtection()
    {
        auto profile = ProfileStore::create();
        profile.url = QUrl(u"https://example.test/"_s);
        profile.password = u"fake-test-password-never-plaintext"_s;
        profile.apiKey = u"fake-test-api-key-never-plaintext"_s;
        QString error;
#ifdef Q_OS_WIN
        QVERIFY2(ProfileStore::save({profile}, &error), qPrintable(error));
        const auto raw = QSettings().value(u"remote/profiles/v1"_s).toByteArray();
        QVERIFY(!raw.contains(profile.password.toUtf8()));
        QVERIFY(!raw.contains(profile.apiKey.toUtf8()));
        auto loaded = ProfileStore::load(&error);
        QCOMPARE(loaded.size(), 1);
        QVERIFY(loaded.first().secretsReadable);
        QCOMPARE(loaded.first().password, profile.password);
        QCOMPARE(loaded.first().apiKey, profile.apiKey);
        QVERIFY(!loaded.first().protectedSecrets.isEmpty());
        // An unreadable blob is preserved during unrelated mapping edits.
        loaded.first().secretsReadable = false;
        loaded.first().protectedSecrets = QByteArray("unreadable-dpapi-test-blob");
        loaded.first().mappings = {{u"/srv"_s, u"X:/Data"_s}};
        QVERIFY(ProfileStore::save(loaded, &error));
        loaded = ProfileStore::load(&error);
        QVERIFY(error.isEmpty());
        QVERIFY(!loaded.first().secretsReadable);
        QCOMPARE(loaded.first().protectedSecrets, QByteArray("unreadable-dpapi-test-blob"));
        QVERIFY(ProfileStore::save(loaded, &error));
        QCOMPARE(ProfileStore::load().first().protectedSecrets, QByteArray("unreadable-dpapi-test-blob"));
#else
        // The portable test target must not silently persist plaintext secrets.
        QVERIFY(!ProfileStore::save({profile}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QSettings().contains(u"remote/profiles/v1"_s));
#endif
    }

    void profileEditorKeepsDraftsAndCancelDoesNotSave()
    {
        auto first = ProfileStore::create();
        first.name = u"First"_s;
        first.url = QUrl(u"https://first.test/qbt/"_s);
        first.mappings = {{u"/srv/downloads"_s, u"X:/Downloads"_s}};
        QVERIFY(ProfileStore::save({first}));
        {
            ProfileEditor editor(ProfileStore::load(), first.id);
            auto *name = editor.findChild<QLineEdit *>(u"profileName"_s);
            QVERIFY(name);
            name->setText(u"Edited first"_s);
            editor.findChild<QPushButton *>(u"addProfile"_s)->click();
            name->setText(u"Second"_s);
            editor.findChild<QLineEdit *>(u"profileUrl"_s)->setText(u"http://second.test:8080/"_s);
            editor.findChild<QListWidget *>(u"profileList"_s)->setCurrentRow(0);
            QCOMPARE(name->text(), u"Edited first"_s);
            QCOMPARE(editor.findChild<QLineEdit *>(u"profilePassword"_s)->echoMode(), QLineEdit::Password);
            editor.reject();
        }
        QCOMPARE(ProfileStore::load().size(), 1);
        QCOMPARE(ProfileStore::load().first().name, u"First"_s);
        ProfileEditor editor(ProfileStore::load(), first.id);
        editor.findChild<QLineEdit *>(u"profileName"_s)->setText(u"Saved name"_s);
        auto *table = editor.findChild<QTableWidget *>(u"profileMappings"_s);
        QCOMPARE(table->rowCount(), 1);
        table->item(0, 1)->setText(u"Y:/New location"_s);
        editor.show();
        const auto screenshot = qEnvironmentVariable("REMOTE_SCREENSHOT_PATH");
        if (!screenshot.isEmpty()) QVERIFY(editor.grab().save(screenshot + u".profiles.png"));
        editor.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        QCOMPARE(editor.result(), int(QDialog::Accepted));
        QCOMPARE(ProfileStore::load().first().name, u"Saved name"_s);
        QCOMPARE(ProfileStore::load().first().mappings.first().local, u"Y:/New location"_s);
    }

    void contentActivationOpensMappedLocalFileForActiveProfile()
    {
        QTemporaryDir local;
        QVERIFY(local.isValid());
        QVERIFY(QDir(local.path()).mkpath(u"video"_s));
        QFile file(local.filePath(u"video/movie.txt"_s));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
        file.close();
        auto profile = ProfileStore::create();
        profile.name = u"Mapped server"_s;
        profile.url = QUrl(u"https://example.test/qbt/"_s);
        profile.mappings = {{u"/srv/downloads"_s, local.path()}};
        QVERIFY(ProfileStore::save({profile}));
        BrowserCapture browser;
        Network network;
        Window window(nullptr, &network);
        QVERIFY(window.connectProfile(profile.id));
        QTRY_VERIFY(window.client()->isConnected());
        QVERIFY(window.findChild<QAction *>(u"actionConnectionProfiles"_s)->isEnabled());
        auto *panel = window.findChild<Properties *>(u"remoteProperties"_s);
        panel->setTorrent(QJsonObject {{u"hash"_s, u"abc"_s}, {u"save_path"_s, u"/srv/downloads"_s}});
        panel->setData(4, QJsonDocument(QJsonArray {QJsonObject {{u"name"_s, u"video/movie.txt"_s}, {u"size"_s, 4}, {u"progress"_s, 1}, {u"priority"_s, 1}}}));
        auto *tree = panel->findChild<TorrentContentWidget *>(u"remoteContent"_s);
        const auto folder = tree->model()->index(0, 0);
        const auto index = tree->model()->index(0, 0, folder);
        QVERIFY(index.isValid());
        QVERIFY(QMetaObject::invokeMethod(tree, "doubleClicked", Q_ARG(QModelIndex, index)));
        QCOMPARE(browser.urls.size(), 1);
        QCOMPARE(browser.urls.first(), QUrl::fromLocalFile(file.fileName()));
        // Switching profiles changes the root used for the same remote path.
        QTemporaryDir other;
        QVERIFY(other.isValid());
        auto second = profile;
        second.id = ProfileStore::create().id;
        second.url = QUrl(u"http://other.test/"_s);
        second.mappings = {{u"/srv/downloads"_s, other.path()}};
        QVERIFY(ProfileStore::save({profile, second}));
        QVERIFY(window.connectProfile(second.id));
        QTRY_VERIFY(window.client()->isConnected());
        window.openRemotePath(u"/srv/downloads"_s);
        QCOMPARE(browser.urls.size(), 2);
        QCOMPARE(browser.urls.last(), QUrl::fromLocalFile(other.path()));
    }

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

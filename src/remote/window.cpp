// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"

#include <QAction>
#include <QApplication>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeView>
#include <QToolBar>

#include "ui_mainwindow.h"
#include "properties.h"
#include "sidebar.h"
#include "gui/transferlistwidget.h"
#include "gui/lineedit.h"
#include "gui/statusbar.h"
#include "gui/downloadfromurldialog.h"
#include "gui/deletionconfirmationdialog.h"
#include "gui/uithememanager.h"

using namespace Qt::StringLiterals;
using namespace Remote;

Window::Window(QWidget *parent, QNetworkAccessManager *network)
    : MainWindow(parent)
    , m_client(this, network)
    , m_model(this)
    , m_filter(this)
{
    UIThemeManager::initInstance();
    // Reuse the unmodified upstream window, menus and toolbar, not a web view.
    setWindowTitle(u"qBittorrent Remote — 5.2.4"_s);
    setWindowIcon(QIcon(u":/icons/qbittorrent.ico"_s));
    // An unfinished action must never look operational or act on a local session.
    for (auto *action : findChildren<QAction *>())
    {
        if (!action->isSeparator() && !action->menu())
            action->setEnabled(false);
    }
    auto *connectAction = new QAction(QIcon(u":/icons/network-server.svg"_s), tr("Connect to Server…"), this);
    connectAction->setObjectName(u"actionConnectRemote"_s);
    connectAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
    m_ui->menuFile->insertAction(m_ui->actionCloseWindow, connectAction);
    m_ui->menuFile->insertSeparator(m_ui->actionCloseWindow);
    connect(connectAction, &QAction::triggered, this, &Window::showConnectionDialog);
    m_ui->menuAutoShutdownOnDownloadsCompletion->setEnabled(false);
    m_ui->menuLog->setEnabled(false);
    auto *languageMenu = m_ui->menuView->addMenu(tr("Language"));
    for (const auto &language : {u"en"_s, u"ru"_s})
    {
        auto *action = languageMenu->addAction(language == u"ru" ? u"Русский"_s : u"English"_s);
        connect(action, &QAction::triggered, this, [this, language]
        {
            QSettings().setValue(u"remote/language"_s, language);
            QMessageBox::information(this, tr("Language"), u"Restart the application to apply the language.\nПерезапустите приложение для смены языка."_s);
        });
    }

    m_filter.setSourceModel(&m_model);
    m_filter.setSortRole(Qt::UserRole);
    m_filter.setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table = new TransferListWidget(&m_filter, this);
    m_table->setObjectName(u"remoteTorrentTable"_s);
    m_table->setAcceptDrops(false);
    m_table->sortByColumn(1, Qt::AscendingOrder);
    m_properties = new Properties(this);
    const auto view = setupTransferView(m_table, m_properties);
    m_horizontal = view.horizontal;
    m_vertical = view.vertical;
    m_search = view.filter;
    m_filterColumn = view.filterColumn;
    m_sidebar = new Sidebar(m_horizontal);
    m_horizontal->insertWidget(0, m_sidebar);
    m_horizontal->setStretchFactor(1, 1);

    m_nativeStatus = new StatusBar(this);
    setStatusBar(m_nativeStatus);

    const auto enableAction = [](QAction *action) { action->setEnabled(true); };
    for (auto *action : {m_ui->actionExit, m_ui->actionCloseWindow,
        m_ui->actionAbout, m_ui->actionDocumentation, m_ui->actionTopToolBar,
        m_ui->actionShowStatusbar, m_ui->actionShowFiltersSidebar})
        enableAction(action);
    connect(m_ui->actionExit, &QAction::triggered, this, &QWidget::close);
    connect(m_ui->actionCloseWindow, &QAction::triggered, this, &QWidget::close);
    connect(m_ui->actionAbout, &QAction::triggered, this, [this]
    { QMessageBox::about(this, tr("qBittorrent Remote"), tr("Remote frontend prototype for qBittorrent 5.2.x.\nUses the original qBittorrent window form. GPL v2 or later.")); });
    connect(m_ui->actionDocumentation, &QAction::triggered, this, []
    { QDesktopServices::openUrl(QUrl(u"https://github.com/sintech/qBittorrent-remote/blob/feature/remote-api-5.2.4/doc/remote-client.md"_s)); });
    connect(m_ui->actionTopToolBar, &QAction::toggled, m_ui->toolBar, &QWidget::setVisible);
    connect(m_ui->actionShowStatusbar, &QAction::toggled, statusBar(), &QWidget::setVisible);
    connect(m_ui->actionShowFiltersSidebar, &QAction::toggled, m_sidebar, &QWidget::setVisible);
    connect(m_ui->actionOpen, &QAction::triggered, this, &Window::addFiles);
    connect(m_ui->actionDownloadFromURL, &QAction::triggered, this, &Window::addLinks);
    connect(m_ui->actionDelete, &QAction::triggered, this, &Window::removeSelected);
    m_ui->actionOptions->setToolTip(tr("Open server Web UI in the default browser; use its gear icon for settings."));
    connect(m_ui->actionOptions, &QAction::triggered, this, [this]
    {
        if (!m_client.isConnected())
            return;
        // The stock Web UI has no standalone settings route. Open its actual
        // root, retaining the reverse-proxy prefix and the browser's own login.
        if (!QDesktopServices::openUrl(m_client.serverUrl()))
            statusBar()->showMessage(tr("Could not open the server Web UI in your browser."), 10000);
    });
    const QMap<QAction *, QString> commands {
        {m_ui->actionStart, u"start"_s}, {m_ui->actionStop, u"stop"_s},
        {m_ui->actionTopQueuePos, u"topPrio"_s}, {m_ui->actionIncreaseQueuePos, u"increasePrio"_s},
        {m_ui->actionDecreaseQueuePos, u"decreasePrio"_s}, {m_ui->actionBottomQueuePos, u"bottomPrio"_s}
    };
    for (auto it = commands.begin(); it != commands.end(); ++it)
    {
        connect(it.key(), &QAction::triggered, this, [this, method = it.value()]
        { runCommand(u"torrents/" + method); });
    }
    m_ui->actionOpen->setShortcut(QKeySequence::Open);
    m_ui->actionDownloadFromURL->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
    m_ui->actionDelete->setShortcut(QKeySequence::Delete);
    m_ui->actionExit->setShortcut(QKeySequence::Quit);
    m_ui->actionStart->setShortcut(Qt::CTRL | Qt::Key_S);
    m_ui->actionStop->setShortcut(Qt::CTRL | Qt::Key_P);
    m_ui->actionOptions->setShortcut(Qt::ALT | Qt::Key_O);

    connect(&m_client, &Client::connectedChanged, this, [this](const bool connected)
    {
        updateActions();
        if (connected)
            statusBar()->clearMessage();
    });
    connect(&m_client, &Client::statusChanged, this, [this](const QString &message)
    {
        m_nativeStatus->setConnectionMessage(message);
        if (!m_client.isConnected())
            statusBar()->showMessage(message, 4000);
    });
    connect(&m_client, &Client::errorOccurred, this, [this](const QString &message)
    {
        statusBar()->showMessage(message, 15000);
        m_nativeStatus->setConnectionMessage(message);
    });
    connect(&m_client, &Client::syncReceived, this, &Window::applySync);
    connect(&m_client, &Client::reset, this, [this]
    {
        ++m_detailGeneration;
        m_model.clear();
        m_categories = {};
        m_tags.clear();
        m_serverState = {};
        m_nativeStatus->setSnapshot({});
        m_properties->clear();
        m_properties->resetSpeedHistory();
        updateFilters();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]
    { updateActions(); refreshDetails(); });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &Window::showContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this] { updateFilters(); });
    connect(m_sidebar, &Sidebar::filtersChanged, this, [this]
    {
        m_filter.setFilters(m_search->text(), m_sidebar->state(), m_sidebar->category(),
            m_sidebar->tag(), m_filterColumn->currentData().toInt(), m_sidebar->tracker());
    });
    connect(m_properties, &Properties::currentChanged, this, [this] { refreshDetails(); });
    connect(m_filterColumn, &QComboBox::currentIndexChanged, this, [this] { updateFilters(); });
    m_detailsTimer.setInterval(5000);
    connect(&m_detailsTimer, &QTimer::timeout, this, &Window::refreshDetails);
    m_detailsTimer.start();

    QSettings settings;
    restoreGeometry(settings.value(u"remote/geometry"_s).toByteArray());
    const auto header = settings.value(u"remote/nativeHeader"_s).toByteArray();
    if (!header.isEmpty())
        m_table->header()->restoreState(header);
    m_vertical->restoreState(settings.value(u"remote/nativeVertical"_s).toByteArray());
    m_horizontal->restoreState(settings.value(u"remote/nativeHorizontal"_s).toByteArray());
    updateActions();
    updateFilters();
}

Window::~Window() = default;

Client *Window::client()
{
    return &m_client;
}

void Window::closeEvent(QCloseEvent *event)
{
    QSettings settings;
    settings.setValue(u"remote/geometry"_s, saveGeometry());
    settings.setValue(u"remote/nativeHeader"_s, m_table->header()->saveState());
    settings.setValue(u"remote/nativeVertical"_s, m_vertical->saveState());
    settings.setValue(u"remote/nativeHorizontal"_s, m_horizontal->saveState());
    m_client.disconnectFromServer();
    MainWindow::closeEvent(event);
}

void Window::showConnectionDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Connect to qBittorrent"));
    auto *form = new QFormLayout(&dialog);
    auto *url = new QLineEdit(&dialog);
    url->setPlaceholderText(u"https://server.example/qbittorrent/"_s);
    auto *auth = new QComboBox(&dialog);
    auth->addItems({tr("API key"), tr("Username and password")});
    auto *key = new QLineEdit(&dialog);
    key->setEchoMode(QLineEdit::Password);
    auto *username = new QLineEdit(&dialog);
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    QSettings settings;
    url->setText(settings.value(u"remote/url"_s).toString());
    username->setText(settings.value(u"remote/username"_s, u"admin"_s).toString());
    form->addRow(tr("Server URL:"), url);
    form->addRow(tr("Authentication:"), auth);
    form->addRow(tr("API key:"), key);
    form->addRow(tr("Username:"), username);
    form->addRow(tr("Password:"), password);
    auto *hint = new QLabel(tr("Use the Web UI address. Download paths belong to the server.\nPasswords and API keys are kept in memory for this session only."), &dialog);
    hint->setWordWrap(true);
    form->addRow(hint);
    const auto selectAuth = [auth, key, username, password]
    {
        const bool apiKey = (auth->currentIndex() == 0);
        key->setEnabled(apiKey);
        username->setEnabled(!apiKey);
        password->setEnabled(!apiKey);
    };
    connect(auth, &QComboBox::currentIndexChanged, &dialog, selectAuth);
    auth->setCurrentIndex(settings.value(u"remote/auth"_s, 0).toInt());
    selectAuth();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QUrl serverUrl(url->text().trimmed());
    if (!serverUrl.isValid() || serverUrl.host().isEmpty()
        || ((serverUrl.scheme() != u"http") && (serverUrl.scheme() != u"https"))
        || !serverUrl.userInfo().isEmpty() || serverUrl.hasQuery() || serverUrl.hasFragment())
    {
        QMessageBox::warning(this, tr("Invalid server URL"), tr("Enter an HTTP(S) URL without embedded credentials, query or fragment."));
        return;
    }
    Connection connection;
    connection.url = serverUrl;
    if (auth->currentIndex() == 0)
    {
        if (key->text().trimmed().isEmpty())
            return;
        connection.apiKey = key->text().trimmed();
    }
    else
    {
        connection.username = username->text();
        connection.password = password->text();
    }
    settings.setValue(u"remote/url"_s, serverUrl.toString());
    settings.setValue(u"remote/username"_s, username->text());
    settings.setValue(u"remote/auth"_s, auth->currentIndex());
    setWindowTitle(u"qBittorrent Remote — " + serverUrl.host());
    m_client.connectToServer(connection);
}

QStringList Window::selectedHashes() const
{
    QStringList hashes;
    for (const auto &index : m_table->selectionModel()->selectedRows())
        hashes.append(m_model.hashAt(m_filter.mapToSource(index).row()));
    return hashes;
}

void Window::updateActions()
{
    const bool connected = m_client.isConnected();
    const bool selected = connected && !selectedHashes().isEmpty();
    for (auto *action : {m_ui->actionOpen, m_ui->actionDownloadFromURL, m_ui->actionOptions})
        action->setEnabled(connected);
    for (auto *action : {m_ui->actionStart, m_ui->actionStop, m_ui->actionDelete,
        m_ui->actionTopQueuePos, m_ui->actionIncreaseQueuePos, m_ui->actionDecreaseQueuePos, m_ui->actionBottomQueuePos})
        action->setEnabled(selected);
}

void Window::applySync(const QJsonObject &sync)
{
    m_model.applySync(sync);
    if (sync.value(u"full_update"_s).toBool())
    {
        m_categories = {};
        m_tags.clear();
        m_serverState = {};
    }
    const auto categories = sync.value(u"categories"_s).toObject();
    for (auto it = categories.begin(); it != categories.end(); ++it)
        m_categories.insert(it.key(), it.value());
    for (const auto value : sync.value(u"categories_removed"_s).toArray())
        m_categories.remove(value.toString());
    for (const auto value : sync.value(u"tags"_s).toArray())
        m_tags.insert(value.toString());
    for (const auto value : sync.value(u"tags_removed"_s).toArray())
        m_tags.remove(value.toString());
    const auto state = sync.value(u"server_state"_s).toObject();
    for (auto it = state.begin(); it != state.end(); ++it)
        m_serverState.insert(it.key(), it.value());
    m_properties->pushStats(m_serverState);
    StatusBar::Snapshot status;
    status.listening = m_serverState.value(u"connection_status"_s).toString() != u"disconnected";
    status.incoming = m_serverState.value(u"connection_status"_s).toString() == u"connected";
    status.alternative = m_serverState.value(u"use_alt_speed_limits"_s).toBool();
    status.dhtNodes = m_serverState.value(u"dht_nodes"_s).toInt();
    status.downloadRate = m_serverState.value(u"dl_info_speed"_s).toInteger();
    status.uploadRate = m_serverState.value(u"up_info_speed"_s).toInteger();
    status.downloaded = m_serverState.value(u"dl_info_data"_s).toInteger();
    status.uploaded = m_serverState.value(u"up_info_data"_s).toInteger();
    status.downloadLimit = m_serverState.value(u"dl_rate_limit"_s).toInt();
    status.uploadLimit = m_serverState.value(u"up_rate_limit"_s).toInt();
    status.freeSpace = m_serverState.value(u"free_space_on_disk"_s).toInteger();
    m_nativeStatus->setSnapshot(status);
    updateFilters();
}

void Window::updateFilters()
{
    m_sidebar->updateData(m_model, m_categories, m_tags);
    m_filter.setFilters(m_search->text(), m_sidebar->state(), m_sidebar->category(),
        m_sidebar->tag(), m_filterColumn->currentData().toInt(), m_sidebar->tracker());
}

void Window::runCommand(const QString &method, QUrlQuery parameters)
{
    const auto hashes = selectedHashes();
    if (hashes.isEmpty())
        return;
    parameters.addQueryItem(u"hashes"_s, hashes.join(u'|'));
    m_client.command(method, parameters);
}

void Window::removeSelected()
{
    const auto hashes = selectedHashes();
    if (hashes.isEmpty())
        return;
    const auto selected = m_table->selectionModel()->selectedRows();
    const QString name = selected.size() == 1 ? selected.first().siblingAtColumn(1).data().toString() : QString();
    DeletionConfirmationDialog dialog(this, hashes.size(), name, false);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QUrlQuery form;
    form.addQueryItem(u"hashes"_s, hashes.join(u'|'));
    form.addQueryItem(u"deleteFiles"_s, dialog.isRemoveContentSelected() ? u"true"_s : u"false"_s);
    m_client.command(u"torrents/delete"_s, form);
}

void Window::addFiles()
{
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Add Torrent File"), {}, tr("Torrent files (*.torrent)"));
    if (paths.isEmpty())
        return;
    bool accepted = false;
    const QString savePath = QInputDialog::getText(this, tr("Server download directory"),
        tr("Server path (leave empty for the server default):"), QLineEdit::Normal, {}, &accepted);
    if (!accepted)
        return;
    for (const auto &path : paths)
        m_client.addTorrentFile(path, savePath);
}

void Window::addLinks()
{
    DownloadFromURLDialog dialog(this);
    connect(&dialog, &DownloadFromURLDialog::urlsReadyToBeDownloaded, this, [this](const QStringList &urls)
    {
        QUrlQuery form;
        form.addQueryItem(u"urls"_s, urls.join(u'\n'));
        m_client.command(u"torrents/add"_s, form);
    });
    dialog.exec();
}

void Window::showContextMenu(const QPoint &position)
{
    if (!m_client.isConnected() || selectedHashes().isEmpty())
        return;
    QMenu menu(this);
    using Command = TransferListWidget::Command;
    const auto add = [this, &menu](Command command, const auto &callback)
    {
        auto *action = TransferListWidget::createAction(command, &menu);
        connect(action, &QAction::triggered, this, callback);
        menu.addAction(action);
    };
    add(Command::Start, [this] { runCommand(u"torrents/start"_s); });
    add(Command::Stop, [this] { runCommand(u"torrents/stop"_s); });
    add(Command::ForceStart, [this]
    {
        QUrlQuery form;
        form.addQueryItem(u"value"_s, u"true"_s);
        runCommand(u"torrents/setForceStart"_s, form);
    });
    menu.addSeparator();
    add(Command::Remove, [this] { removeSelected(); });
    menu.addSeparator();
    add(Command::Recheck, [this] { runCommand(u"torrents/recheck"_s); });
    add(Command::Reannounce, [this] { runCommand(u"torrents/reannounce"_s); });
    menu.exec(m_table->viewport()->mapToGlobal(position));
}

void Window::refreshDetails()
{
    const auto hashes = selectedHashes();
    const quint64 generation = ++m_detailGeneration;
    if ((hashes.size() != 1) || !m_client.isConnected())
    {
        m_properties->clear();
        return;
    }
    const auto rows = m_table->selectionModel()->selectedRows();
    auto torrent = m_model.torrentAt(m_filter.mapToSource(rows.first()).row());
    torrent.insert(u"hash"_s, hashes.first());
    m_properties->setTorrent(torrent);
    const int tab = m_properties->currentIndex();
    if (tab < 0 || tab > 4)
        return;
    const QStringList methods {u"torrents/properties"_s, u"torrents/trackers"_s,
        u"sync/torrentPeers"_s, u"torrents/webseeds"_s, u"torrents/files"_s};
    QUrlQuery query;
    query.addQueryItem(u"hash"_s, hashes.first());
    if (tab == 2)
        query.addQueryItem(u"rid"_s, u"0"_s);
    const auto apply = [this, generation](int target, const QByteArray &body)
    {
        if (generation != m_detailGeneration)
            return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body, &error);
        if (error.error == QJsonParseError::NoError)
            m_properties->setData(target, document);
    };
    m_client.get(methods[tab], query, [apply, tab](const QByteArray &body) { apply(tab, body); });
    if (tab == 0)
    {
        m_client.get(u"torrents/pieceStates"_s, query, [apply](const QByteArray &body) { apply(6, body); });
        m_client.get(u"torrents/pieceAvailability"_s, query, [apply](const QByteArray &body) { apply(7, body); });
    }
}

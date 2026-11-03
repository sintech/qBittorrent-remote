// SPDX-License-Identifier: GPL-2.0-or-later
#include "window.h"
#include "activation.h"
#include "torrentmetadata.h"
#include "gui/fspathedit.h"
#include <QCompleter>
#include <QStringListModel>
#include "settingsdialog.h"
#include <QSystemTrayIcon>
#include "gui/addnewtorrentdialog.h"
#include "gui/trackerentriesdialog.h"

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
#include <QFileInfo>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <algorithm>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTreeView>
#include <QToolBar>

#include "ui_mainwindow.h"
#include "gui/search/searchwidget.h"
#include "gui/executionlogwidget.h"
#include "logcontroller.h"
#include <QTabWidget>
#include <QShortcut>
#include "properties.h"
#include "sidebar.h"
#include "profileeditor.h"
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
    setWindowTitle(u"qBittorrent Remote — %1"_s.arg(QString::fromLatin1(QBT_REMOTE_VERSION)));
    setWindowIcon(QIcon(u":/remote-icons/application.ico"_s));
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
    auto *profilesAction = new QAction(UIThemeManager::instance()->getIcon(u"network-server"_s), tr("Connection profiles…"), this);
    profilesAction->setObjectName(u"actionConnectionProfiles"_s);
    m_ui->menuOptions->insertAction(m_ui->actionOptions, profilesAction);
    connect(profilesAction, &QAction::triggered, this, &Window::showProfilesDialog);
    m_webUiAction = m_ui->menuOptions->addAction(UIThemeManager::instance()->getIcon(u"network-server"_s),tr("Server Web UI…"));
    m_webUiAction->setObjectName(u"actionServerWebUi"_s);
    connect(m_webUiAction,&QAction::triggered,this,&Window::openServerWebUi);
    for (auto *action : {m_ui->actionPauseSession, m_ui->actionResumeSession,
        m_ui->actionCheckForUpdates, m_ui->actionDonateMoney, m_ui->actionCreateTorrent,
        m_ui->actionManageCookies, m_ui->actionSpeedInTitleBar, m_ui->actionRSSReader,
        m_ui->actionStatistics, m_ui->actionLock,
        m_ui->menuAutoShutdownOnDownloadsCompletion->menuAction()})
        action->setVisible(false);
    m_ui->menuLog->setEnabled(true);

    m_filter.setSourceModel(&m_model);
    m_filter.setSortRole(Qt::UserRole);
    m_filter.setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table = new TransferListWidget(&m_filter, this);
    m_table->setObjectName(u"remoteTorrentTable"_s);
    m_table->setAcceptDrops(false);
    setAcceptDrops(true);
    m_table->sortByColumn(1, Qt::AscendingOrder);
    m_properties = new Properties(this);
    const auto view = setupTransferView(m_table, m_properties);
    m_tabs = view.tabs;
    m_tabs->setObjectName(u"remoteMainTabs"_s);
    m_transferFilterAction = view.filterAction;
    m_horizontal = view.horizontal;
    m_vertical = view.vertical;
    m_search = view.filter;
    m_search->setObjectName(u"remoteTransferFilter"_s);
    m_filterColumn = view.filterColumn;
    m_sidebar = new Sidebar(m_horizontal);
    m_horizontal->insertWidget(0, m_sidebar);
    m_horizontal->setStretchFactor(1, 1);

    m_searchPanel = new SearchWidget(&m_client,m_tabs);
    m_logPanel = new ExecutionLogWidget(Log::MsgType::ALL,m_tabs);
    auto *logs = new LogController(&m_client,m_logPanel);
    const auto showSearch = [this](bool show)
    {
        if(show && m_tabs->indexOf(m_searchPanel)<0) m_tabs->insertTab(1,m_searchPanel,UIThemeManager::instance()->getIcon(u"edit-find"_s),tr("Search"));
        if(!show) { m_tabs->removeTab(m_tabs->indexOf(m_searchPanel));m_searchPanel->hide(); }
        QSettings().setValue(u"remote/search/visible"_s,show);
    };
    const auto showLogs = [this](bool show)
    {
        if(show && m_tabs->indexOf(m_logPanel)<0) m_tabs->addTab(m_logPanel,UIThemeManager::instance()->getIcon(u"help-contents"_s),tr("Execution Log"));
        if(!show) { m_tabs->removeTab(m_tabs->indexOf(m_logPanel));m_logPanel->hide(); }
        QSettings().setValue(u"remote/log/visible"_s,show);
    };
    m_ui->actionSearchWidget->setEnabled(true);m_ui->actionExecutionLogs->setEnabled(true);
    connect(m_ui->actionSearchWidget,&QAction::toggled,this,showSearch);
    connect(m_ui->actionExecutionLogs,&QAction::toggled,this,showLogs);
    const bool searchVisible=QSettings().value(u"remote/search/visible"_s,true).toBool();
    const bool logVisible=QSettings().value(u"remote/log/visible"_s,true).toBool();
    m_ui->actionSearchWidget->setChecked(searchVisible);showSearch(searchVisible);
    m_ui->actionExecutionLogs->setChecked(logVisible);showLogs(logVisible);
    const auto updateLogTypes=[this]
    {
        Log::MsgTypes types;
        if(m_ui->actionNormalMessages->isChecked()) types|=Log::NORMAL;
        if(m_ui->actionInformationMessages->isChecked()) types|=Log::INFO;
        if(m_ui->actionWarningMessages->isChecked()) types|=Log::WARNING;
        if(m_ui->actionCriticalMessages->isChecked()) types|=Log::CRITICAL;
        m_logPanel->setMessageTypes(types);QSettings().setValue(u"remote/log/types"_s,int(types));
    };
    const int types=QSettings().value(u"remote/log/types"_s,15).toInt();
    for(const auto &[action,type]:QList<QPair<QAction *,int>>{{m_ui->actionNormalMessages,1},{m_ui->actionInformationMessages,2},{m_ui->actionWarningMessages,4},{m_ui->actionCriticalMessages,8}})
    { action->setEnabled(true);action->setChecked(types&type);connect(action,&QAction::toggled,this,updateLogTypes); }
    updateLogTypes();
    connect(m_tabs,&QTabWidget::currentChanged,this,[this,logs]
    {
        const bool transfers=m_tabs->currentIndex()==0;
        m_transferFilterAction->setVisible(transfers);
        if(m_tabs->currentWidget()==m_searchPanel) m_searchPanel->giveFocusToSearchInput();
        if(m_tabs->currentWidget()==m_logPanel) QTimer::singleShot(0,logs,&LogController::refresh);
        updateActions();
    });
    auto *transfersShortcut=new QShortcut(Qt::ALT|Qt::Key_1,this);
    connect(transfersShortcut,&QShortcut::activated,this,[this] { m_tabs->setCurrentIndex(0); });
    auto *searchShortcut=new QShortcut(Qt::ALT|Qt::Key_2,this);
    connect(searchShortcut,&QShortcut::activated,this,[this] { m_ui->actionSearchWidget->setChecked(true);m_tabs->setCurrentWidget(m_searchPanel); });
    auto *logShortcut=new QShortcut(Qt::ALT|Qt::Key_4,this);
    connect(logShortcut,&QShortcut::activated,this,[this] { m_ui->actionExecutionLogs->setChecked(true);m_tabs->setCurrentWidget(m_logPanel); });
    connect(m_searchPanel,&SearchWidget::sourceRequested,this,[this](const QString &source,AddTorrentOption option)
    {
        if(!m_client.isConnected()) return;
        const auto normalized = normalizeSources({source}, QDir::currentPath()).values;
        for (const auto &value : normalized)
        {
            if (option == AddTorrentOption::SkipDialog) m_skipDialogSources.insert(value);
            if (option == AddTorrentOption::ShowDialog) m_forceDialogSources.insert(value);
        }
        openSources(normalized);
    });

    m_nativeStatus = new StatusBar(this);
    setStatusBar(m_nativeStatus);

    const auto enableAction = [](QAction *action) { action->setEnabled(true); };
    for (auto *action : {m_ui->actionExit, m_ui->actionCloseWindow,
        m_ui->actionAbout, m_ui->actionDocumentation, m_ui->actionTopToolBar,
        m_ui->actionShowStatusbar, m_ui->actionShowFiltersSidebar})
        enableAction(action);
    connect(m_ui->actionExit, &QAction::triggered, this, [this] { m_forceExit = true; close(); QCoreApplication::quit(); });
    connect(m_ui->actionCloseWindow, &QAction::triggered, this, &QWidget::close);
    connect(m_ui->actionAbout, &QAction::triggered, this, [this]
    { QMessageBox::about(this, tr("qBittorrent Remote"), tr("Remote frontend for qBittorrent 5.2.x / 5.3.x.\nUses the original qBittorrent window form. GPL v2 or later.")); });
    connect(m_ui->actionDocumentation, &QAction::triggered, this, []
    { QDesktopServices::openUrl(QUrl(u"https://github.com/sintech/qBittorrent-remote/blob/master/doc/remote-client.md"_s)); });
    const auto bindVisibility = [this](QAction *action, QWidget *widget)
    {
        const QString key = u"remote/view/"_s + action->objectName();
        widget->setVisible(QSettings().value(key, true).toBool());
        const auto synchronize = [action, widget]
        {
            const QSignalBlocker blocker(action);
            action->setChecked(!widget->isHidden());
        };
        synchronize();
        connect(m_ui->menuView, &QMenu::aboutToShow, this, synchronize);
        connect(action, &QAction::toggled, widget, [widget, key](bool visible)
        {
            widget->setVisible(visible);
            QSettings().setValue(key, visible);
        });
    };
    bindVisibility(m_ui->actionTopToolBar, m_ui->toolBar);
    bindVisibility(m_ui->actionShowStatusbar, statusBar());
    bindVisibility(m_ui->actionShowFiltersSidebar, m_sidebar);
    connect(m_ui->actionOpen, &QAction::triggered, this, &Window::addFiles);
    connect(m_ui->actionDownloadFromURL, &QAction::triggered, this, &Window::addLinks);
    connect(m_ui->actionDelete, &QAction::triggered, this, &Window::removeSelected);
    m_ui->actionOptions->setToolTip(tr("Client settings"));
    connect(m_ui->actionOptions,&QAction::triggered,this,&Window::showSettingsDialog);
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
        if (!connected) m_openingSource=false;
        if (connected)
        {
            m_sourceConnectionRequested = false;
            statusBar()->clearMessage();
            QTimer::singleShot(0,this,&Window::processPendingSources);
        }
    });
    connect(&m_client, &Client::statusChanged, this, [this](const QString &message)
    {
        m_nativeStatus->setConnectionMessage(message);
        if (!m_client.isConnected())
            statusBar()->showMessage(message, 4000);
    });
    connect(&m_client, &Client::errorOccurred, this, [this](const QString &message)
    {
        if (!m_client.isConnected()) m_sourceConnectionRequested=false;
        statusBar()->showMessage(message, 15000);
        m_nativeStatus->setConnectionMessage(message);
    });
    connect(&m_client,&Client::requestFailed,this,[this](const QString &method)
    { if (method==u"app/preferences") m_openingSource=false; });
    connect(&m_client, &Client::syncReceived, this, &Window::applySync);
    connect(&m_client, &Client::reset, this, [this]
    {
        for (auto *menu : findChildren<QMenu *>()) menu->close();
        m_openingSource = false;
        for (auto *dialog : findChildren<AddNewTorrentDialog *>())
        {
            const auto source=dialog->property("activationSource").toString();
            if (dialog->result()!=QDialog::Accepted && !source.isEmpty() && !m_pendingSources.contains(source)) m_pendingSources.prepend(source);
            dialog->reject();
        }
        for (auto *dialog : findChildren<TrackerEntriesDialog *>()) dialog->reject();
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
    connect(m_table,&QTreeView::doubleClicked,this,[this] { torrentDoubleClicked(); });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &Window::showContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this] { updateFilters(); });
    connect(m_sidebar, &Sidebar::filtersChanged, this, [this]
    {
        m_filter.setFilters(m_search->text(), m_sidebar->state(), m_sidebar->category(),
            m_sidebar->tag(), m_filterColumn->currentData().toInt(), m_sidebar->tracker());
    });
    connect(m_properties, &Properties::openPathRequested, this, &Window::openRemotePath);
    connect(m_sidebar, &Sidebar::contextCommand, this, &Window::sidebarCommand);
    connect(m_properties, &Properties::commandRequested, &m_client, &Client::command);
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
    m_windowGeometry=saveGeometry();
    m_normalWindowRect=normalGeometry().isValid() ? normalGeometry() : geometry();
    m_restoreWindowState=windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen);
    m_geometryReady=true;
    m_geometrySaveTimer.setSingleShot(true);
    m_geometrySaveTimer.setInterval(200);
    connect(&m_geometrySaveTimer,&QTimer::timeout,this,&Window::saveWindowState);
    applyClientSettings();
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
    saveWindowState();
    if (!m_forceExit && m_tray && m_tray->isVisible() && QSettings().value(u"remote/desktop/closeToTray"_s,false).toBool())
    { event->ignore(); hide(); return; }
    m_client.disconnectFromServer();
    MainWindow::closeEvent(event);
}

bool Window::reloadProfiles(QString *error)
{
    QString failure;
    const auto profiles = ProfileStore::load(&failure);
    if (error) *error = failure;
    if (!failure.isEmpty()) return false;
    m_profiles = profiles;
    return true;
}

bool Window::connectProfile(const QString &id, QString *error)
{
    if (!reloadProfiles(error)) return false;
    for (const auto &profile : m_profiles)
    {
        if (profile.id != id) continue;
        QString failure;
        if (!profile.secretsReadable) failure = profile.secretError;
        else if (profile.apiKeyAuthentication && profile.apiKey.isEmpty()) failure = tr("Enter the API key in Tools → Connection profiles.");
        if (!failure.isEmpty()) { if (error) *error = failure; return false; }
        m_activeProfile = profile;
        QSettings().setValue(u"remote/selectedProfile"_s, profile.id);
        setWindowTitle(u"qBittorrent Remote — %1 (%2)"_s.arg(profile.name, profile.url.host()));
        m_client.connectToServer(profile.connection());
        return true;
    }
    if (error) *error = tr("The selected connection profile no longer exists.");
    return false;
}

void Window::showProfilesDialog() { editProfiles(false); }

void Window::editProfiles(const bool offerConnection)
{
    QString error;
    if (!reloadProfiles(&error))
    {
        QMessageBox::warning(this, tr("Connection profiles"), error);
        return;
    }
    const QString selected = m_activeProfile.id.isEmpty() ? QSettings().value(u"remote/selectedProfile"_s).toString() : m_activeProfile.id;
    ProfileEditor editor(m_profiles, selected, this);
    if (offerConnection) editor.setWindowTitle(tr("Configure a connection profile"));
    if (editor.exec() != QDialog::Accepted) return;
    if (!reloadProfiles(&error))
    {
        QMessageBox::warning(this, tr("Connection profiles"), error);
        return;
    }
    if (editor.connectRequested())
    {
        if (!connectProfile(editor.selectedId(), &error)) QMessageBox::warning(this, tr("Cannot connect"), error);
        return;
    }
    if (!m_activeProfile.id.isEmpty())
    {
        const auto active = std::find_if(m_profiles.cbegin(), m_profiles.cend(), [this](const auto &profile) { return profile.id == m_activeProfile.id; });
        if (active == m_profiles.cend())
        {
            m_client.disconnectFromServer();
            m_activeProfile = {};
            setWindowTitle(u"qBittorrent Remote — %1"_s.arg(QString::fromLatin1(QBT_REMOTE_VERSION)));
            QSettings().remove(u"remote/selectedProfile"_s);
        }
        else
        {
            const auto previous = m_activeProfile.connection(), next = active->connection();
            if (previous.url != next.url || previous.username != next.username || previous.password != next.password || previous.apiKey != next.apiKey)
            {
                m_client.disconnectFromServer();
                statusBar()->showMessage(tr("Connection settings changed. Reconnect to apply them."), 10000);
            }
            m_activeProfile = *active; // Mapping-only edits take effect immediately.
            setWindowTitle(u"qBittorrent Remote — %1 (%2)"_s.arg(active->name, active->url.host()));
        }
    }
}

void Window::startLastProfile()
{
    if (!QSettings().value(u"remote/desktop/autoConnect"_s,true).toBool()) return;
    const QString id = QSettings().value(u"remote/selectedProfile"_s).toString();
    if (!id.isEmpty() && connectProfile(id)) return;
    showConnectionDialog();
}

void Window::showConnectionDialog()
{
    QString error;
    if (!reloadProfiles(&error))
    {
        QMessageBox::warning(this, tr("Connection profiles"), error);
        return;
    }
    if (m_profiles.isEmpty()) { editProfiles(true); return; }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Connect to qBittorrent"));
    auto *form = new QFormLayout(&dialog);
    auto *profiles = new QComboBox(&dialog);
    const QString selected = QSettings().value(u"remote/selectedProfile"_s).toString();
    for (const auto &profile : m_profiles)
    {
        profiles->addItem(profile.name, profile.id);
        if (profile.id == selected) profiles->setCurrentIndex(profiles->count() - 1);
    }
    auto *address = new QLabel(&dialog);
    address->setTextFormat(Qt::PlainText);
    const auto showAddress = [this, profiles, address] { address->setText(m_profiles.value(profiles->currentIndex()).url.toDisplayString()); };
    connect(profiles, &QComboBox::currentIndexChanged, &dialog, showAddress);
    showAddress();
    form->addRow(tr("Profile:"), profiles);
    form->addRow(tr("Server:"), address);
    auto *manage = new QPushButton(tr("Edit profiles…"), &dialog);
    form->addRow(manage);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Connect"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(manage, &QPushButton::clicked, &dialog, [this, &dialog] { dialog.reject(); editProfiles(true); });
    if (dialog.exec() == QDialog::Accepted && !connectProfile(profiles->currentData().toString(), &error))
        QMessageBox::warning(this, tr("Cannot connect"), error);
}

void Window::openRemotePath(const QString &remotePath)
{
    const QString local = m_activeProfile.localPath(remotePath);
    if (local.isEmpty())
    {
        QMessageBox::information(this, tr("No folder mapping"), tr("Add a matching remote → local folder rule in Tools → Connection profiles."));
        return;
    }
    if (!QFileInfo::exists(local))
    {
        QMessageBox::information(this, tr("Local path not found"), tr("The mapped local path does not exist:\n%1").arg(QDir::toNativeSeparators(local)));
        return;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(local)))
        QMessageBox::warning(this, tr("Cannot open local path"), QDir::toNativeSeparators(local));
}

void Window::openSelectedFolder()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    const auto torrent = m_model.torrentAt(m_filter.mapToSource(rows.first()).row());
    QString path = torrent.value(u"content_path"_s).toString();
    if (path.isEmpty()) path = torrent.value(u"save_path"_s).toString();
    const QString local = m_activeProfile.localPath(path);
    if (!local.isEmpty() && QFileInfo(local).isFile())
    {
        const QString directory = QFileInfo(local).absolutePath();
        QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    }
    else openRemotePath(path);
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
    const bool selected = connected && (!m_tabs || m_tabs->currentIndex()==0) && !selectedHashes().isEmpty();
    m_ui->actionOptions->setEnabled(true);
    m_webUiAction->setEnabled(connected);
    for (auto *action : {m_ui->actionOpen, m_ui->actionDownloadFromURL})
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
    bool deleteFiles = false;
    if (QSettings().value(u"remote/list/confirmRemoval"_s,true).toBool())
    {
        DeletionConfirmationDialog dialog(this, hashes.size(), name, false);
        if (dialog.exec() != QDialog::Accepted) return;
        deleteFiles = dialog.isRemoveContentSelected();
    }
    QUrlQuery form;
    form.addQueryItem(u"hashes"_s, hashes.join(u'|'));
    form.addQueryItem(u"deleteFiles"_s, deleteFiles ? u"true"_s : u"false"_s);
    m_client.command(u"torrents/delete"_s, form);
}

void Window::addFiles()
{
    const auto paths = QFileDialog::getOpenFileNames(this, tr("Add Torrent File"), {}, tr("Torrent files (*.torrent)"));
    if (paths.isEmpty())
        return;
    openSources(paths);
}

void Window::addLinks()
{
    DownloadFromURLDialog dialog(this);
    connect(&dialog, &DownloadFromURLDialog::urlsReadyToBeDownloaded, this, [this](const QStringList &urls)
    {
        openSources(urls);
    });
    dialog.exec();
}

void Window::showAddDialog(const QString &source, bool file, bool forceDialog, const QJsonObject &metadata)
{
    const auto server = m_client.serverUrl();
    m_client.get(u"app/preferences"_s, {}, [this, source, file, server, forceDialog, metadata](const QByteArray &body)
    {
        if (server != m_client.serverUrl()) return;
        const auto document = QJsonDocument::fromJson(body);
        if (!document.isObject()) { m_openingSource = false; QMessageBox::warning(this, tr("Add torrent"), tr("Could not load server download defaults.")); return; }
        auto preferences = document.object();
        preferences.insert(u"remote_server"_s, server.toString());
        const auto connection = m_client.connectionGeneration();
        if (handleDuplicateSource(source, file, preferences, metadata))
        {
            if (connection != m_client.connectionGeneration()) return;
            m_openingSource = false;
            m_pendingSources.removeOne(source);
            m_forceDialogSources.remove(source);
            m_skipDialogSources.remove(source);
            QTimer::singleShot(0, this, &Window::processPendingSources);
            return;
        }
        m_openingSource = false;
        if (!forceDialog && m_skipDialogSources.remove(source))
        {
            QUrlQuery query;
            query.addQueryItem(u"urls"_s, QString(source).replace(u'%', u"%25"_s));
            m_client.command(u"torrents/add"_s, query);
            m_pendingSources.removeOne(source);
            QTimer::singleShot(0, this, &Window::processPendingSources);
            return;
        }
        auto *dialog = new AddNewTorrentDialog(source, preferences, m_categories, this, file);
        m_pendingSources.removeOne(source);
        m_addDialog=dialog;
        for (auto *path : dialog->findChildren<FileSystemPathEdit *>())
            setupPathCompletion(path);
        dialog->setProperty("activationSource",source);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog,&QDialog::finished,this,[this,source]
        {
            m_forceDialogSources.remove(source);
            m_skipDialogSources.remove(source);
            m_addDialog=nullptr;
            QTimer::singleShot(0,this,&Window::processPendingSources);
        });
        connect(dialog, &QDialog::accepted, this, [this, dialog, source, file, server]
        {
            if (!m_client.isConnected() || server != m_client.serverUrl())
            { if (!m_pendingSources.contains(source)) m_pendingSources.prepend(source); return; }
            auto form = dialog->parameters();
            if (file) m_client.addTorrentFile(source, form, dialog->torrentHash(), dialog->filePriorities());
            else { form.addQueryItem(u"urls"_s, QString(source).replace(u'%',u"%25"_s)); m_client.command(u"torrents/add"_s, form); }
        });
        if (!forceDialog && (!file || !dialog->torrentHash().isEmpty()) && QSettings().value(u"remote/addDialog/skip"_s, false).toBool())
            QTimer::singleShot(0, dialog, &QDialog::accept);
        else dialog->open();
    });
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

void Window::setupPathCompletion(FileSystemPathEdit *path)
{
    auto *combo = path->findChild<QComboBox *>();
    if (!combo || !combo->lineEdit()) return;
    auto *line = combo->lineEdit();
    auto *completer = new QCompleter(combo);
    auto *suggestions = new QStringListModel(completer);
    completer->setModel(suggestions);
    completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    combo->setCompleter(completer);
    auto *timer = new QTimer(combo);
    timer->setSingleShot(true);
    timer->setInterval(250);
    connect(line, &QLineEdit::textEdited, timer, [timer, suggestions, completer]
    {
        completer->popup()->hide();
        suggestions->setStringList({});
        timer->start();
    });
    connect(timer, &QTimer::timeout, combo, [this, line, suggestions, completer]
    {
        const auto input = line->text();
        const auto separator = qMax(input.lastIndexOf(u'/'), input.lastIndexOf(u'\\'));
        if (separator < 0 || !line->hasFocus()) return;
        const auto directory = input.left(separator + 1);
        QUrlQuery query;
        query.addQueryItem(u"dirPath"_s, QString(directory).replace(u'%', u"%25"_s));
        query.addQueryItem(u"mode"_s, u"dirs"_s);
        const QPointer<QLineEdit> guard(line);
        m_client.probe(u"app/getDirectoryContent"_s, query, false,
            [guard, input, suggestions, completer](const QByteArray &body)
        {
            if (!guard || !guard->hasFocus() || guard->text() != input) return;
            QStringList matches;
            for (const auto &entry : QJsonDocument::fromJson(body).array())
            {
                auto value = entry.toString();
                if (input.contains(u'\\'))
                    value.replace(u'/', u'\\');
                if (!value.isEmpty() && value.startsWith(input, input.startsWith(u'/')
                        ? Qt::CaseSensitive : Qt::CaseInsensitive))
                    matches.append(value);
            }
            matches.sort(Qt::CaseInsensitive);
            suggestions->setStringList(matches);
            if (!matches.isEmpty())
            {
                completer->setCompletionPrefix(input);
                completer->complete();
            }
        });
    });
}

bool Window::handleDuplicateSource(const QString &source, bool file, const QJsonObject &preferences, const QJsonObject &metadata)
{
    QStringList hashes;
    QStringList trackers;
    bool privateSource = false;
    if (file)
    {
        const auto metadata = readTorrentMetadata(source);
        if (!metadata.valid()) return false;
        hashes = {metadata.hashV1, metadata.hashV2};
        trackers = metadata.trackers;
        privateSource = metadata.isPrivate;
    }
    else
    {
        const QUrl url(source);
        if (url.scheme() != u"magnet")
        {
            hashes = {metadata[u"infohash_v1"].toString(), metadata[u"infohash_v2"].toString(),
                metadata[u"hash"].toString()};
            privateSource = metadata[u"info"].toObject()[u"private"].toBool();
            for (const auto &tracker : metadata[u"trackers"].toArray())
                trackers.append(tracker.toObject()[u"url"].toString());
        }
        else
        {
            const QUrlQuery query(url);
            for (const auto &topic : query.allQueryItemValues(u"xt"_s, QUrl::FullyDecoded))
            {
                if (topic.startsWith(u"urn:btih:", Qt::CaseInsensitive))
                {
                    const auto hash = topic.mid(9);
                    if (hash.size() == 40)
                        hashes.append(hash);
                    else if (hash.size() == 32)
                    {
                        QByteArray bytes;
                        unsigned accumulator = 0;
                        int bits = 0;
                        for (const auto character : hash.toUpper())
                        {
                            const auto value = QStringView(u"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567").indexOf(character);
                            if (value < 0) break;
                            accumulator = (accumulator << 5) | unsigned(value);
                            bits += 5;
                            if (bits >= 8)
                            {
                                bits -= 8;
                                bytes.append(char((accumulator >> bits) & 0xff));
                            }
                        }
                        if (bytes.size() == 20)
                            hashes.append(QString::fromLatin1(bytes.toHex()));
                    }
                }
                else if (topic.startsWith(u"urn:btmh:1220", Qt::CaseInsensitive))
                    hashes.append(topic.mid(13));
            }
            trackers = query.allQueryItemValues(u"tr"_s, QUrl::FullyDecoded);
        }
    }
    hashes.removeAll(QString());
    if (hashes.isEmpty()) return false;

    for (int row = 0; row < m_model.rowCount(); ++row)
    {
        const auto torrent = m_model.torrentAt(row);
        if (!hashes.contains(m_model.hashAt(row), Qt::CaseInsensitive)
            && !hashes.contains(torrent[u"infohash_v1"].toString(), Qt::CaseInsensitive)
            && !hashes.contains(torrent[u"infohash_v2"].toString(), Qt::CaseInsensitive))
            continue;
        const auto title = tr("Torrent is already present");
        if (privateSource || torrent[u"private"].toBool())
        {
            QMessageBox::warning(this, title, tr("Trackers cannot be merged because it is a private torrent."));
            return true;
        }
        trackers.removeIf([](const QString &tracker)
        {
            return tracker.trimmed().isEmpty() || tracker.contains(u'\n') || tracker.contains(u'\r');
        });
        trackers.removeDuplicates();
        if (trackers.isEmpty())
        {
            QMessageBox::information(this, title, tr("The new source has no trackers to merge."));
            return true;
        }
        const auto hash = m_model.hashAt(row);
        const auto connection = m_client.connectionGeneration();
        const auto answer = QMessageBox::question(this, title,
            tr("Torrent '%1' is already in the transfer list. Do you want to merge trackers from new source?")
                .arg(torrent[u"name"].toString()), QMessageBox::Yes | QMessageBox::No,
            preferences[u"merge_trackers"].toBool() ? QMessageBox::Yes : QMessageBox::No);
        if (answer == QMessageBox::Yes && m_client.isConnected()
            && connection == m_client.connectionGeneration())
        {
            QUrlQuery query;
            query.addQueryItem(u"hash"_s, hash);
            query.addQueryItem(u"urls"_s, trackers.join(u'\n').replace(u'%', u"%25"_s));
            m_client.command(u"torrents/addTrackers"_s, query);
        }
        return true;
    }
    return false;
}

void Window::inspectUrlSource(const QString &source, bool forceDialog, int attempts)
{
    QUrlQuery query;
    // fetchMetadata decodes its source parameter once after form decoding.
    query.addQueryItem(u"source"_s, QString::fromLatin1(QUrl::toPercentEncoding(source))
        .replace(u'%', u"%25"_s));
    const auto connection = m_client.connectionGeneration();
    m_client.probe(u"torrents/fetchMetadata"_s, query, true,
        [this, source, forceDialog, attempts, connection](const QByteArray &body)
    {
        const auto document = QJsonDocument::fromJson(body);
        const auto metadata = document.object();
        if (document.isObject() && !metadata.contains(u"info"_s) && attempts < 4)
        {
            QTimer::singleShot(400, this, [this, source, forceDialog, attempts, connection]
            {
                if (m_client.isConnected() && m_openingSource && m_pendingSources.value(0) == source
                    && connection == m_client.connectionGeneration())
                    inspectUrlSource(source, forceDialog, attempts + 1);
            });
            return;
        }
        showAddDialog(source, false, forceDialog, metadata);
    });
}

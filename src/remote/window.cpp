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
#include <QPainter>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QStyleOptionProgressBar>
#include <QTreeView>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui_mainwindow.h"
#include "properties.h"
#include "sidebar.h"
#include "gui/hidabletabwidget.h"
#include "gui/progressbarpainter.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    class ProgressDelegate final : public QStyledItemDelegate
    {
    public:
        explicit ProgressDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            if (index.column() != 4)
            {
                QStyledItemDelegate::paint(painter, option, index);
                return;
            }
            QStyleOptionViewItem customOption(option);
            const QString state = index.siblingAtColumn(5).data(Qt::UserRole).toString();
            customOption.state.setFlag(QStyle::State_Enabled, state != u"error" && state != u"stoppedDL" && state != u"unknown");
            m_painter.paint(painter, customOption, index.data().toString(),
                int(index.data(Qt::UserRole).toDouble() * 100));
        }
    private:
        ProgressBarPainter m_painter;
    };

}

Window::Window(QWidget *parent, QNetworkAccessManager *network)
    : QMainWindow(parent)
    , m_ui(std::make_unique<Ui::MainWindow>())
    , m_client(this, network)
    , m_model(this)
    , m_filter(this)
{
    // Reuse the unmodified upstream window, menus and toolbar, not a web view.
    m_ui->setupUi(this);
    setWindowTitle(u"qBittorrent Remote — 5.2.4"_s);
    setWindowIcon(QIcon(u":/icons/qbittorrent.ico"_s));
    const QMap<QAction *, QString> icons {
        {m_ui->actionOpen, u"list-add"_s}, {m_ui->actionDownloadFromURL, u"insert-link"_s},
        {m_ui->actionDelete, u"list-remove"_s}, {m_ui->actionStart, u"torrent-start"_s},
        {m_ui->actionStop, u"torrent-stop"_s}, {m_ui->actionTopQueuePos, u"go-top"_s},
        {m_ui->actionIncreaseQueuePos, u"go-up"_s}, {m_ui->actionDecreaseQueuePos, u"go-down"_s},
        {m_ui->actionBottomQueuePos, u"go-bottom"_s}, {m_ui->actionOptions, u"configure"_s},
        {m_ui->actionCreateTorrent, u"torrent-creator"_s}, {m_ui->actionLock, u"object-locked"_s},
        {m_ui->actionExit, u"application-exit"_s}
    };
    // An unfinished action must never look operational or act on a local session.
    for (auto *action : findChildren<QAction *>())
    {
        if (!action->isSeparator() && !action->menu())
            action->setEnabled(false);
    }
    for (auto it = icons.begin(); it != icons.end(); ++it)
        it.key()->setIcon(QIcon(u":/icons/" + it.value() + u".svg"));
    m_ui->toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
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

    auto *tabs = new HidableTabWidget(this);
    m_horizontal = new QSplitter(Qt::Horizontal, tabs);
    m_sidebar = new Sidebar(m_horizontal);
    m_vertical = new QSplitter(Qt::Vertical, m_horizontal);
    m_vertical->setChildrenCollapsible(false);
    m_vertical->setFrameShape(QFrame::NoFrame);
    auto *listArea = new QWidget(m_vertical);
    auto *listLayout = new QVBoxLayout(listArea);
    listLayout->setContentsMargins(0, 0, 0, 0);
    auto *searchArea = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(searchArea);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(searchArea);
    m_search->setFixedWidth(200);
    m_search->setPlaceholderText(QCoreApplication::translate("MainWindow", "Filter torrents..."));
    m_search->setClearButtonEnabled(true);
    m_filterColumn = new QComboBox(searchArea);
    m_filterColumn->addItem(m_model.headerData(1, Qt::Horizontal, Qt::DisplayRole).toString(), 1);
    m_filterColumn->addItem(m_model.headerData(26, Qt::Horizontal, Qt::DisplayRole).toString(), 26);
    searchLayout->addWidget(m_search);
    searchLayout->addWidget(new QLabel(QCoreApplication::translate("MainWindow", "Filter by:"), searchArea));
    searchLayout->addWidget(m_filterColumn);
    auto *spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->toolBar->insertWidget(m_ui->actionLock, spacer);
    m_ui->toolBar->insertWidget(m_ui->actionLock, searchArea);
    m_table = new QTreeView(listArea);
    m_table->setObjectName(u"remoteTorrentTable"_s);
    m_filter.setSourceModel(&m_model);
    m_filter.setSortRole(Qt::UserRole);
    m_filter.setSortCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(&m_filter);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setUniformRowHeights(true);
    m_table->setAlternatingRowColors(true);
    m_table->setRootIsDecorated(false);
    m_table->setAllColumnsShowFocus(true);
    m_table->setItemsExpandable(false);
    m_table->setAutoScroll(true);
    m_table->header()->setSectionsMovable(true);
    m_table->header()->setFirstSectionMovable(true);
    m_table->header()->setStretchLastSection(false);
    m_table->setItemDelegate(new ProgressDelegate(m_table));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(1, Qt::AscendingOrder);
    const QList<int> widths {35, 260, 85, 85, 110, 120, 80, 80, 90, 90, 80, 65};
    for (int column = 0; column < widths.size(); ++column)
        m_table->setColumnWidth(column, widths[column]);
    m_table->hideColumn(3);
    for (int column = 12; column < m_model.columnCount(); ++column)
        m_table->hideColumn(column);
    m_table->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->header(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos)
    {
        QMenu menu(this);
        for (int column = 0; column < m_model.columnCount(); ++column)
        {
            auto *action = menu.addAction(m_model.headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
            action->setCheckable(true);
            action->setChecked(!m_table->isColumnHidden(column));
            connect(action, &QAction::toggled, this, [this, column](const bool checked)
            { m_table->setColumnHidden(column, !checked); });
        }
        menu.exec(m_table->header()->mapToGlobal(pos));
    });
    listLayout->addWidget(m_table);
    m_horizontal->setStretchFactor(1, 1);
    m_horizontal->setSizes({170, 800});

    m_properties = new Properties(m_vertical);
    m_vertical->setSizes({380, 300});
    tabs->addTab(m_horizontal, QIcon(u":/icons/folder-remote.svg"_s), tr("Transfers"));
    m_ui->centralWidgetLayout->addWidget(tabs);
    m_status = new QLabel(this);
    m_status->setPixmap(QIcon(u":/icons/disconnected.svg"_s).pixmap(16, 16));
    m_freeSpace = new QLabel(tr("Free space: N/A"), this);
    m_dht = new QLabel(tr("DHT: %1 nodes").arg(0), this);
    statusBar()->addWidget(m_freeSpace);
    statusBar()->addWidget(m_dht);
    statusBar()->addPermanentWidget(m_status);
    const auto addRate = [this](const QString &icon, QLabel **label)
    {
        auto *area = new QWidget(this);
        auto *layout = new QHBoxLayout(area);
        layout->setContentsMargins(4, 0, 4, 0);
        auto *image = new QLabel(area);
        image->setPixmap(QIcon(u":/icons/" + icon + u".svg").pixmap(16, 16));
        *label = new QLabel(u"0 B/s"_s, area);
        layout->addWidget(image);
        layout->addWidget(*label);
        area->setMinimumWidth(155);
        statusBar()->addPermanentWidget(area);
    };
    addRate(u"downloading"_s, &m_rates);
    addRate(u"upload"_s, &m_uploadRate);

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
        m_status->setPixmap(QIcon(m_client.isConnected() ? u":/icons/connected.svg"_s : u":/icons/disconnected.svg"_s).pixmap(16, 16));
        m_status->setToolTip(message);
        if (!m_client.isConnected())
            statusBar()->showMessage(message, 4000);
    });
    connect(&m_client, &Client::errorOccurred, this, [this](const QString &message)
    {
        statusBar()->showMessage(message, 15000);
        m_status->setToolTip(message);
    });
    connect(&m_client, &Client::syncReceived, this, &Window::applySync);
    connect(&m_client, &Client::reset, this, [this]
    {
        ++m_detailGeneration;
        m_model.clear();
        m_categories = {};
        m_tags.clear();
        m_serverState = {};
        m_rates->setText(u"0 B/s"_s);
        m_uploadRate->setText(u"0 B/s"_s);
        m_freeSpace->setText(tr("Free space: N/A"));
        m_dht->setText(tr("DHT: %1 nodes").arg(0));
        m_properties->clear();
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
    QMainWindow::closeEvent(event);
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
    m_rates->setText(TorrentModel::speed(m_serverState.value(u"dl_info_speed"_s).toInteger()));
    m_uploadRate->setText(TorrentModel::speed(m_serverState.value(u"up_info_speed"_s).toInteger()));
    m_freeSpace->setText(QCoreApplication::translate("StatusBar", "Free space: %1").arg(TorrentModel::size(m_serverState.value(u"free_space_on_disk"_s).toInteger())));
    m_dht->setText(QCoreApplication::translate("StatusBar", "DHT: %1 nodes").arg(m_serverState.value(u"dht_nodes"_s).toInteger()));
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
    QMessageBox dialog(QMessageBox::Question, tr("Remove torrents"),
        tr("Remove %1 torrent(s) from the server?").arg(hashes.size()),
        QMessageBox::Yes | QMessageBox::No, this);
    dialog.setDefaultButton(QMessageBox::No);
    auto *deleteFiles = new QCheckBox(tr("Also delete the files on the server"), &dialog);
    dialog.setCheckBox(deleteFiles);
    if (dialog.exec() != QMessageBox::Yes)
        return;
    QUrlQuery form;
    form.addQueryItem(u"hashes"_s, hashes.join(u'|'));
    form.addQueryItem(u"deleteFiles"_s, deleteFiles->isChecked() ? u"true"_s : u"false"_s);
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
    bool accepted = false;
    const QString links = QInputDialog::getMultiLineText(this, tr("Add Torrent Link"),
        tr("Magnet links or HTTP(S) torrent URLs, one per line:"), {}, &accepted);
    if (!accepted || links.trimmed().isEmpty())
        return;
    const QString savePath = QInputDialog::getText(this, tr("Server download directory"),
        tr("Server path (leave empty for the server default):"), QLineEdit::Normal, {}, &accepted);
    if (!accepted)
        return;
    QUrlQuery form;
    form.addQueryItem(u"urls"_s, links.trimmed());
    if (!savePath.isEmpty())
        form.addQueryItem(u"savepath"_s, savePath);
    m_client.command(u"torrents/add"_s, form);
}

void Window::showContextMenu(const QPoint &position)
{
    if (!m_client.isConnected() || selectedHashes().isEmpty())
        return;
    QMenu menu(this);
    menu.addAction(m_ui->actionStart);
    menu.addAction(m_ui->actionStop);
    menu.addAction(tr("Force Start"), this, [this]
    {
        QUrlQuery form;
        form.addQueryItem(u"value"_s, u"true"_s);
        runCommand(u"torrents/setForceStart"_s, form);
    });
    menu.addSeparator();
    menu.addAction(tr("Force Recheck"), this, [this] { runCommand(u"torrents/recheck"_s); });
    menu.addAction(tr("Force Reannounce"), this, [this] { runCommand(u"torrents/reannounce"_s); });
    menu.addSeparator();
    menu.addAction(m_ui->actionDelete);
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
        m_client.get(u"torrents/pieceStates"_s, query, [apply](const QByteArray &body) { apply(6, body); });
}

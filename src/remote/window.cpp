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
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui_mainwindow.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    class ProgressDelegate final : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QStyleOptionProgressBar bar;
            bar.rect = option.rect.adjusted(2, 3, -2, -3);
            bar.minimum = 0;
            bar.maximum = 1000;
            bar.progress = qRound(index.data(Qt::UserRole).toDouble() * 1000);
            bar.text = index.data().toString();
            bar.textVisible = true;
            bar.state = option.state;
            bar.palette = option.palette;
            QApplication::style()->drawControl(QStyle::CE_ProgressBar, &bar, painter);
        }
    };

    void fillTable(QTableWidget *table, const QStringList &headers, const QList<QStringList> &rows)
    {
        table->clear();
        table->setColumnCount(headers.size());
        table->setHorizontalHeaderLabels(headers);
        table->setRowCount(rows.size());
        for (int row = 0; row < rows.size(); ++row)
        {
            for (int column = 0; column < headers.size(); ++column)
                table->setItem(row, column, new QTableWidgetItem(rows[row].value(column)));
        }
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
    }
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
    m_ui->actionOptions->setText(tr("Connect to Server…"));
    m_ui->menuAutoShutdownOnDownloadsCompletion->setEnabled(false);
    m_ui->menuLog->setEnabled(false);

    auto *tabs = new QTabWidget(this);
    m_vertical = new QSplitter(Qt::Vertical, tabs);
    m_horizontal = new QSplitter(Qt::Horizontal, m_vertical);
    m_sidebar = new QTreeWidget(m_horizontal);
    m_sidebar->setObjectName(u"remoteSidebar"_s);
    m_sidebar->setHeaderHidden(true);
    auto *listArea = new QWidget(m_horizontal);
    auto *listLayout = new QVBoxLayout(listArea);
    listLayout->setContentsMargins(0, 0, 0, 0);
    m_search = new QLineEdit(listArea);
    m_search->setPlaceholderText(tr("Filter torrent names…"));
    m_search->setClearButtonEnabled(true);
    listLayout->addWidget(m_search);
    m_table = new QTableView(listArea);
    m_table->setObjectName(u"remoteTorrentTable"_s);
    m_filter.setSourceModel(&m_model);
    m_filter.setSortRole(Qt::UserRole);
    m_table->setModel(&m_filter);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->setItemDelegateForColumn(4, new ProgressDelegate(m_table));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(1, Qt::AscendingOrder);
    const QList<int> widths {35, 260, 85, 85, 110, 120, 80, 80, 90, 90, 80, 65};
    for (int column = 0; column < widths.size(); ++column)
        m_table->setColumnWidth(column, widths[column]);
    m_table->hideColumn(3);
    for (int column = 12; column < m_model.columnCount(); ++column)
        m_table->hideColumn(column);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos)
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
        menu.exec(m_table->horizontalHeader()->mapToGlobal(pos));
    });
    listLayout->addWidget(m_table);
    m_horizontal->setStretchFactor(1, 1);
    m_horizontal->setSizes({170, 800});

    m_properties = new QTabWidget(m_vertical);
    const QStringList propertyNames {tr("General"), tr("Trackers"), tr("Peers"), tr("Content")};
    for (const auto &name : propertyNames)
    {
        auto *table = new QTableWidget(m_properties);
        table->setObjectName(u"remoteDetails%1"_s.arg(m_details.size()));
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->verticalHeader()->hide();
        m_details.append(table);
        m_properties->addTab(table, name);
    }
    m_vertical->setSizes({400, 180});
    tabs->addTab(m_vertical, QIcon(u":/icons/folder-remote.svg"_s), tr("Transfers"));
    m_ui->centralWidgetLayout->addWidget(tabs);
    m_status = new QLabel(tr("Disconnected"), this);
    m_rates = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    statusBar()->addPermanentWidget(m_rates);

    const auto enableAction = [](QAction *action) { action->setEnabled(true); };
    for (auto *action : {m_ui->actionOptions, m_ui->actionExit, m_ui->actionCloseWindow,
        m_ui->actionAbout, m_ui->actionDocumentation, m_ui->actionTopToolBar,
        m_ui->actionShowStatusbar, m_ui->actionShowFiltersSidebar})
        enableAction(action);
    connect(m_ui->actionOptions, &QAction::triggered, this, &Window::showConnectionDialog);
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

    connect(&m_client, &Client::connectedChanged, this, [this](bool) { updateActions(); });
    connect(&m_client, &Client::statusChanged, m_status, &QLabel::setText);
    connect(&m_client, &Client::errorOccurred, this, [this](const QString &message)
    {
        m_status->setText(message);
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
        m_rates->clear();
        for (auto *table : m_details)
            table->setRowCount(0);
        updateFilters();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this]
    { updateActions(); refreshDetails(); });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, &Window::showContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, [this] { updateFilters(); });
    connect(m_sidebar, &QTreeWidget::currentItemChanged, this, [this]
    {
        const auto *item = m_sidebar->currentItem();
        m_filter.setFilters(m_search->text(),
            item ? item->data(0, Qt::UserRole).toString() : QString(),
            item ? item->data(0, Qt::UserRole + 1).toString() : QString(),
            item ? item->data(0, Qt::UserRole + 2).toString() : QString());
    });
    connect(m_properties, &QTabWidget::currentChanged, this, [this] { refreshDetails(); });
    m_detailsTimer.setInterval(5000);
    connect(&m_detailsTimer, &QTimer::timeout, this, &Window::refreshDetails);
    m_detailsTimer.start();

    QSettings settings;
    restoreGeometry(settings.value(u"remote/geometry"_s).toByteArray());
    const auto header = settings.value(u"remote/header"_s).toByteArray();
    if (!header.isEmpty())
        m_table->horizontalHeader()->restoreState(header);
    m_vertical->restoreState(settings.value(u"remote/vertical"_s).toByteArray());
    m_horizontal->restoreState(settings.value(u"remote/horizontal"_s).toByteArray());
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
    settings.setValue(u"remote/header"_s, m_table->horizontalHeader()->saveState());
    settings.setValue(u"remote/vertical"_s, m_vertical->saveState());
    settings.setValue(u"remote/horizontal"_s, m_horizontal->saveState());
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
    m_rates->setText(tr("D: %1   U: %2   Free: %3")
        .arg(TorrentModel::speed(m_serverState.value(u"dl_info_speed"_s).toInteger()),
            TorrentModel::speed(m_serverState.value(u"up_info_speed"_s).toInteger()),
            TorrentModel::size(m_serverState.value(u"free_space_on_disk"_s).toInteger())));
    updateFilters();
}

void Window::updateFilters()
{
    QString kind;
    QString value;
    if (const auto *item = m_sidebar->currentItem())
    {
        kind = item->data(0, Qt::UserRole + 3).toString();
        value = item->data(0, Qt::UserRole + 4).toString();
    }
    const QSignalBlocker blocker(m_sidebar);
    m_sidebar->clear();
    const auto add = [this, &kind, &value](QTreeWidgetItem *parent, const QString &label,
        const QString &type, const QString &filterValue, const int role)
    {
        auto *item = new QTreeWidgetItem(parent, {label});
        item->setData(0, role, filterValue);
        item->setData(0, Qt::UserRole + 3, type);
        item->setData(0, Qt::UserRole + 4, filterValue);
        if ((kind == type) && (value == filterValue))
            m_sidebar->setCurrentItem(item);
        return item;
    };
    auto *states = new QTreeWidgetItem(m_sidebar, {tr("Status")});
    const QList<QPair<QString, QString>> filters {{tr("All"), u"all"_s},
        {tr("Downloading"), u"downloading"_s}, {tr("Completed"), u"completed"_s},
        {tr("Stopped"), u"stopped"_s}, {tr("Errored"), u"errored"_s}};
    for (const auto &filter : filters)
        add(states, filter.first, u"status"_s, filter.second, Qt::UserRole);
    auto *categories = new QTreeWidgetItem(m_sidebar, {tr("Categories")});
    add(categories, tr("Uncategorized"), u"category"_s, u""_s, Qt::UserRole + 1);
    for (auto it = m_categories.begin(); it != m_categories.end(); ++it)
        add(categories, it.key(), u"category"_s, it.key(), Qt::UserRole + 1);
    auto *tags = new QTreeWidgetItem(m_sidebar, {tr("Tags")});
    auto tagNames = m_tags.values();
    tagNames.sort();
    for (const auto &tag : tagNames)
        add(tags, tag, u"tag"_s, tag, Qt::UserRole + 2);
    m_sidebar->expandAll();
    const auto *item = m_sidebar->currentItem();
    m_filter.setFilters(m_search->text(), item ? item->data(0, Qt::UserRole).toString() : QString(),
        item ? item->data(0, Qt::UserRole + 1).toString() : QString(),
        item ? item->data(0, Qt::UserRole + 2).toString() : QString());
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
        for (auto *table : m_details)
            table->setRowCount(0);
        return;
    }
    const int tab = m_properties->currentIndex();
    const QStringList methods {u"torrents/properties"_s, u"torrents/trackers"_s,
        u"sync/torrentPeers"_s, u"torrents/files"_s};
    QUrlQuery query;
    query.addQueryItem(u"hash"_s, hashes.first());
    if (tab == 2)
        query.addQueryItem(u"rid"_s, u"0"_s);
    m_client.get(methods[tab], query, [this, generation, tab](const QByteArray &body)
    {
        if (generation != m_detailGeneration)
            return;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(body, &error);
        if (error.error == QJsonParseError::NoError)
            fillDetails(tab, document);
    });
}

void Window::fillDetails(const int tab, const QJsonDocument &document)
{
    QList<QStringList> rows;
    QStringList headers;
    if (tab == 0)
    {
        headers = {tr("Property"), tr("Value")};
        const auto object = document.object();
        const QList<QPair<QString, QString>> fields {
            {tr("Save Path"), u"save_path"_s}, {tr("Total Size"), u"total_size"_s},
            {tr("Downloaded"), u"total_downloaded"_s}, {tr("Uploaded"), u"total_uploaded"_s},
            {tr("Down Speed"), u"dl_speed"_s}, {tr("Up Speed"), u"up_speed"_s},
            {tr("Share Ratio"), u"share_ratio"_s}, {tr("Time Active"), u"time_elapsed"_s},
            {tr("Connections"), u"nb_connections"_s}, {tr("Piece Size"), u"piece_size"_s},
            {tr("Created By"), u"created_by"_s}, {tr("Comment"), u"comment"_s}
        };
        for (const auto &field : fields)
        {
            const auto value = object.value(field.second);
            QString text = value.toVariant().toString();
            if (field.second.contains(u"size") || field.second.startsWith(u"total_"))
                text = TorrentModel::size(value.toInteger());
            else if (field.second.endsWith(u"speed"))
                text = TorrentModel::speed(value.toInteger());
            else if (field.second == u"time_elapsed")
                text = TorrentModel::duration(value.toInteger());
            rows.append({field.first, text});
        }
    }
    else if (tab == 1)
    {
        headers = {tr("URL"), tr("Status"), tr("Tier"), tr("Peers"), tr("Message")};
        const QStringList statuses {tr("Disabled"), tr("Not contacted"), tr("Working"), tr("Updating"), tr("Not working")};
        for (const auto value : document.array())
        {
            const auto item = value.toObject();
            rows.append({item.value(u"url"_s).toString(), statuses.value(item.value(u"status"_s).toInt()),
                QString::number(item.value(u"tier"_s).toInt()), QString::number(item.value(u"num_peers"_s).toInt()),
                item.value(u"msg"_s).toString()});
        }
    }
    else if (tab == 2)
    {
        headers = {tr("IP"), tr("Port"), tr("Client"), tr("Progress"), tr("Down Speed"), tr("Up Speed")};
        const auto peers = document.object().value(u"peers"_s).toObject();
        for (const auto value : peers)
        {
            const auto item = value.toObject();
            rows.append({item.value(u"ip"_s).toString(), QString::number(item.value(u"port"_s).toInt()),
                item.value(u"client"_s).toString(), u"%1%"_s.arg(item.value(u"progress"_s).toDouble() * 100, 0, 'f', 1),
                TorrentModel::speed(item.value(u"dl_speed"_s).toInteger()), TorrentModel::speed(item.value(u"up_speed"_s).toInteger())});
        }
    }
    else
    {
        headers = {tr("Name"), tr("Size"), tr("Progress"), tr("Priority"), tr("Availability")};
        for (const auto value : document.array())
        {
            const auto item = value.toObject();
            rows.append({item.value(u"name"_s).toString(), TorrentModel::size(item.value(u"size"_s).toInteger()),
                u"%1%"_s.arg(item.value(u"progress"_s).toDouble() * 100, 0, 'f', 1),
                QString::number(item.value(u"priority"_s).toInt()), QString::number(item.value(u"availability"_s).toDouble(), 'f', 2)});
        }
    }
    fillTable(m_details[tab], headers, rows);
}

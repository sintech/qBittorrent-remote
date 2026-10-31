// SPDX-License-Identifier: GPL-2.0-or-later
#include "properties.h"

#include <QDateTime>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QStandardItemModel>
#include <QTreeView>
#include <QTreeWidget>

#include "gui/properties/proptabbar.h"
#include "torrentmodel.h"
#include "ui_remoteproperties.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    QString trProperty(const char *text, const char *comment = nullptr)
    {
        return QCoreApplication::translate("PropertiesWidget", text, comment);
    }

    QString date(const QJsonValue &value)
    {
        return value.toInteger() > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(value.toInteger()), QLocale::ShortFormat) : QString();
    }

    class PieceStrip final : public QWidget
    {
    public:
        using QWidget::QWidget;
        QJsonArray pieces;
        qreal fraction = 0;
        void paintEvent(QPaintEvent *) override
        {
            QPainter painter(this);
            const QRect area = rect().adjusted(1, 1, -1, -1);
            const QColor complete(50, 110, 220);
            painter.fillRect(area, QColor(230, 230, 230));
            if (pieces.isEmpty())
                painter.fillRect(QRect(area.topLeft(), QSize(qRound(area.width() * fraction), area.height())), complete);
            else
            {
                for (int x = 0; x < area.width(); ++x)
                {
                    const int index = qMin(int(pieces.size()) - 1, int(qint64(x) * pieces.size() / qMax(1, area.width())));
                    const int state = pieces[index].toInt();
                    if (state > 0)
                    {
                        painter.setPen(state == 2 ? complete : QColor(120, 170, 245));
                        painter.drawLine(area.left() + x, area.top(), area.left() + x, area.bottom());
                    }
                }
            }
            painter.setPen(palette().mid().color());
            painter.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    };

    QTreeWidget *createList(QWidget *parent, const QStringList &headers)
    {
        auto *view = new QTreeWidget(parent);
        view->setHeaderLabels(headers);
        view->setRootIsDecorated(false);
        view->setUniformRowHeights(true);
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        view->setAllColumnsShowFocus(true);
        view->header()->setStretchLastSection(true);
        view->header()->setSectionsMovable(true);
        return view;
    }
}

Properties::Properties(QWidget *parent)
    : QWidget(parent)
    , m_ui(std::make_unique<Ui::PropertiesWidget>())
{
    m_ui->setupUi(this);
    setObjectName(u"remoteProperties"_s);
    auto *progress = new PieceStrip(this);
    progress->setObjectName(u"remotePieceStrip"_s);
    progress->setFixedHeight(18);
    m_ui->tempProgressBarArea->hide();
    m_ui->groupBarLayout->addWidget(progress, 0, 1);
    m_ui->labelPiecesAvailability->hide();
    m_ui->tempAvailabilityBarArea->hide();
    m_ui->labelAverageAvailabilityVal->hide();

    m_trackers = createList(this, {tr("#"), tr("URL"), tr("Status"), tr("Peers"), tr("Seeds"), tr("Leeches"), tr("Downloaded"), tr("Message")});
    m_trackers->setObjectName(u"remoteTrackers"_s);
    m_ui->hBoxLayoutTrackers->insertWidget(0, m_trackers);
    m_ui->trackerUpButton->setIcon(QIcon(u":/icons/go-up.svg"_s));
    m_ui->trackerDownButton->setIcon(QIcon(u":/icons/go-down.svg"_s));
    m_ui->trackerUpButton->setEnabled(false);
    m_ui->trackerDownButton->setEnabled(false);
    m_peers = createList(this, {tr("IP/Address"), tr("Port"), tr("Connection"), tr("Flags"), tr("Client"), tr("Progress"), tr("Down Speed"), tr("Up Speed"), tr("Downloaded"), tr("Uploaded"), tr("Relevance")});
    m_peers->setObjectName(u"remotePeers"_s);
    m_ui->vBoxLayoutPeerPage->addWidget(m_peers);
    m_files = new QStandardItemModel(this);
    m_ui->filesList->setObjectName(u"remoteContent"_s);
    m_ui->filesList->setModel(m_files);
    m_ui->filesList->setUniformRowHeights(true);
    m_ui->filesList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ui->selectAllButton->setEnabled(false);
    m_ui->selectNoneButton->setEnabled(false);
    m_tabs = new PropTabBar;
    m_tabs->setContentsMargins(0, 5, 0, 5);
    m_ui->verticalLayout->addLayout(m_tabs);
    connect(m_tabs, &PropTabBar::tabChanged, this, [this](const int index)
    {
        m_ui->stackedProperties->setCurrentIndex(index);
        QSettings().setValue(u"remote/propertiesTab"_s, index);
        emit currentChanged(index);
    });
    connect(m_tabs, &PropTabBar::visibilityToggled, m_ui->stackedProperties, &QWidget::setVisible);
    // A speed graph has not been ported yet; retain its position without a dead page.
    if (auto *item = m_tabs->itemAt(m_tabs->count() - 1))
        item->widget()->setEnabled(false);
    const int savedTab = QSettings().value(u"remote/propertiesTab"_s, 0).toInt();
    m_tabs->setCurrentIndex((savedTab >= 0 && savedTab <= 4) ? savedTab : 0);
    clear();
}

Properties::~Properties() = default;

int Properties::currentIndex() const
{
    return m_tabs->currentIndex();
}

bool Properties::isPanelVisible() const
{
    return currentIndex() >= 0;
}

void Properties::clear()
{
    m_torrent = {};
    for (auto *label : findChildren<QLabel *>())
    {
        if (label->objectName().endsWith(u"Val"))
            label->clear();
    }
    auto *progress = static_cast<PieceStrip *>(findChild<QWidget *>(u"remotePieceStrip"_s));
    // PieceStrip intentionally has no Q_OBJECT; locate it as QWidget and cast.
    if (progress)
    {
        progress->pieces = {};
        progress->fraction = 0;
        progress->update();
    }
    m_trackers->clear();
    m_peers->clear();
    m_ui->listWebSeeds->clear();
    m_files->clear();
}

void Properties::setTorrent(const QJsonObject &torrent)
{
    if (m_torrent.value(u"hash"_s) != torrent.value(u"hash"_s))
        clear();
    m_torrent = torrent;
    m_ui->labelProgressVal->setText(u"%1%"_s.arg(torrent.value(u"progress"_s).toDouble() * 100, 0, 'f', 1));
    auto *progress = static_cast<PieceStrip *>(findChild<QWidget *>(u"remotePieceStrip"_s));
    progress->fraction = torrent.value(u"progress"_s).toDouble();
    progress->update();
    m_ui->labelInfohash1Val->setText(torrent.value(u"infohash_v1"_s).toString());
    m_ui->labelInfohash2Val->setText(torrent.value(u"infohash_v2"_s).toString());
}

void Properties::setData(const int tab, const QJsonDocument &data)
{
    if (tab == 0)
        showGeneral(data.object());
    else if (tab == 1)
    {
        m_trackers->clear();
        const QStringList statuses {tr("Disabled"), tr("Not contacted yet"), tr("Working"), tr("Updating..."), tr("Not working")};
        for (const auto value : data.array())
        {
            const auto item = value.toObject();
            const auto number = [&item](const QString &key) { const auto v = item.value(key); return v.toInteger() < 0 ? QString() : QString::number(v.toInteger()); };
            new QTreeWidgetItem(m_trackers, {number(u"tier"_s), item.value(u"url"_s).toString(), statuses.value(item.value(u"status"_s).toInt()),
                number(u"num_peers"_s), number(u"num_seeds"_s), number(u"num_leeches"_s), number(u"num_downloaded"_s), item.value(u"msg"_s).toString()});
        }
        m_trackers->setColumnWidth(0, 35);
        m_trackers->setColumnWidth(1, 350);
    }
    else if (tab == 2)
    {
        m_peers->clear();
        const auto peers = data.object().value(u"peers"_s).toObject();
        for (const auto value : peers)
        {
            const auto peer = value.toObject();
            new QTreeWidgetItem(m_peers, {peer.value(u"ip"_s).toString(), QString::number(peer.value(u"port"_s).toInt()),
                peer.value(u"connection"_s).toString(), peer.value(u"flags"_s).toString(), peer.value(u"client"_s).toString(),
                u"%1%"_s.arg(peer.value(u"progress"_s).toDouble() * 100, 0, 'f', 1),
                TorrentModel::speed(peer.value(u"dl_speed"_s).toInteger()), TorrentModel::speed(peer.value(u"up_speed"_s).toInteger()),
                TorrentModel::size(peer.value(u"downloaded"_s).toInteger()), TorrentModel::size(peer.value(u"uploaded"_s).toInteger()),
                QString::number(peer.value(u"relevance"_s).toDouble(), 'f', 2)});
        }
    }
    else if (tab == 3)
    {
        m_ui->listWebSeeds->clear();
        for (const auto value : data.array())
            m_ui->listWebSeeds->addItem(value.toObject().value(u"url"_s).toString());
    }
    else if (tab == 4)
        showContent(data.array());
    else if (tab == 6)
    {
        auto *progress = static_cast<PieceStrip *>(findChild<QWidget *>(u"remotePieceStrip"_s));
        progress->pieces = data.array();
        progress->update();
    }
}

void Properties::showGeneral(const QJsonObject &properties)
{
    const auto text = [&properties](const QString &key) { return properties.value(key).toVariant().toString(); };
    const auto bytes = [&properties](const QString &key) { return TorrentModel::size(properties.value(key).toInteger()); };
    const auto rate = [&properties](const QString &key) { return TorrentModel::speed(properties.value(key).toInteger()); };
    const auto duration = [&properties](const QString &key) { return TorrentModel::duration(properties.value(key).toInteger()); };
    const auto total = [&](const QString &key, const QString &session)
    {
        return trProperty("%1 (%2 this session)").arg(bytes(key), bytes(session));
    };
    m_ui->labelSavePathVal->setText(text(u"save_path"_s));
    m_ui->labelElapsedVal->setText(duration(u"time_elapsed"_s));
    m_ui->labelETAVal->setText(duration(u"eta"_s));
    m_ui->labelConnectionsVal->setText(trProperty("%1 (%2 max)", "%1 and %2 are numbers, e.g. 3 (10 max)")
        .arg(text(u"nb_connections"_s), properties.value(u"nb_connections_limit"_s).toInt() < 0 ? u"∞"_s : text(u"nb_connections_limit"_s)));
    m_ui->labelDlTotalVal->setText(total(u"total_downloaded"_s, u"total_downloaded_session"_s));
    m_ui->labelUpTotalVal->setText(total(u"total_uploaded"_s, u"total_uploaded_session"_s));
    m_ui->labelSeedsVal->setText(trProperty("%1 (%2 total)", "%1 and %2 are numbers, e.g. 3 (10 total)").arg(text(u"seeds"_s), text(u"seeds_total"_s)));
    m_ui->labelPeersVal->setText(trProperty("%1 (%2 total)", "%1 and %2 are numbers, e.g. 3 (10 total)").arg(text(u"peers"_s), text(u"peers_total"_s)));
    m_ui->labelDlSpeedVal->setText(trProperty("%1 (%2 avg.)", "%1 and %2 are speed rates, e.g. 200KiB/s (100KiB/s avg.)").arg(rate(u"dl_speed"_s), rate(u"dl_speed_avg"_s)));
    m_ui->labelUpSpeedVal->setText(trProperty("%1 (%2 avg.)", "%1 and %2 are speed rates, e.g. 200KiB/s (100KiB/s avg.)").arg(rate(u"up_speed"_s), rate(u"up_speed_avg"_s)));
    m_ui->labelDlLimitVal->setText(properties.value(u"dl_limit"_s).toInteger() <= 0 ? u"∞"_s : rate(u"dl_limit"_s));
    m_ui->labelUpLimitVal->setText(properties.value(u"up_limit"_s).toInteger() <= 0 ? u"∞"_s : rate(u"up_limit"_s));
    m_ui->labelWastedVal->setText(bytes(u"total_wasted"_s));
    m_ui->labelShareRatioVal->setText(QString::number(properties.value(u"share_ratio"_s).toDouble(), 'f', 2));
    m_ui->labelPopularityVal->setText(QString::number(m_torrent.value(u"popularity"_s).toDouble(), 'f', 2));
    m_ui->labelReannounceInVal->setText(duration(u"reannounce"_s));
    m_ui->labelLastSeenCompleteVal->setText(date(properties.value(u"last_seen"_s)));
    m_ui->labelTotalSizeVal->setText(bytes(u"total_size"_s));
    m_ui->labelTotalPiecesVal->setText(trProperty("%1 x %2 (have %3)", "(torrent pieces) eg 152 x 4MB (have 25)")
        .arg(text(u"pieces_num"_s), bytes(u"piece_size"_s), text(u"pieces_have"_s)));
    m_ui->labelCreatedByVal->setText(text(u"created_by"_s));
    m_ui->labelAddedOnVal->setText(date(properties.value(u"addition_date"_s)));
    m_ui->labelCompletedOnVal->setText(date(properties.value(u"completion_date"_s)));
    m_ui->labelCreatedOnVal->setText(date(properties.value(u"creation_date"_s)));
    m_ui->labelPrivateVal->setText(properties.value(u"is_private"_s).toBool() ? trProperty("Yes") : trProperty("No"));
    m_ui->labelCommentVal->setText(text(u"comment"_s));
    // Keep remote metadata plain text, even if it contains HTML or link markup.
    m_ui->labelCommentVal->setTextFormat(Qt::PlainText);
}

void Properties::showContent(const QJsonArray &files)
{
    m_files->clear();
    m_files->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Progress"), tr("Download Priority"), tr("Remaining"), tr("Availability")});
    QHash<QString, QStandardItem *> folders;
    folders.insert({}, m_files->invisibleRootItem());
    for (const auto value : files)
    {
        const auto file = value.toObject();
        // Preserve literal server-side path components; never normalize with host rules.
        const auto parts = file.value(u"name"_s).toString().split(u'/');
        QStandardItem *parent = m_files->invisibleRootItem();
        QString path;
        for (int i = 0; i + 1 < parts.size(); ++i)
        {
            path += parts[i] + u'/';
            if (!folders.contains(path))
            {
                auto *folder = new QStandardItem(QIcon(u":/icons/directory.svg"_s), parts[i]);
                parent->appendRow(folder);
                folders.insert(path, folder);
            }
            parent = folders.value(path);
        }
        const qint64 size = file.value(u"size"_s).toInteger();
        const qreal progress = file.value(u"progress"_s).toDouble();
        const int priority = file.value(u"priority"_s).toInt();
        const QHash<int, QString> priorities {{0, tr("Do not download")}, {1, tr("Normal")}, {6, tr("High")}, {7, tr("Maximum")}};
        auto *name = new QStandardItem(QIcon(u":/icons/fileicon.svg"_s), parts.last());
        QList<QStandardItem *> row {name,
            new QStandardItem(TorrentModel::size(size)), new QStandardItem(u"%1%"_s.arg(progress * 100, 0, 'f', 1)),
            new QStandardItem(priorities.value(priority, QString::number(priority))), new QStandardItem(TorrentModel::size(qRound64(size * (1 - progress)))),
            new QStandardItem(QString::number(file.value(u"availability"_s).toDouble(), 'f', 2))};
        parent->appendRow(row);
    }
    m_ui->filesList->setColumnWidth(0, 360);
    m_ui->filesList->setColumnWidth(1, 100);
    m_ui->filesList->setColumnWidth(2, 100);
    m_ui->filesList->expandAll();
}

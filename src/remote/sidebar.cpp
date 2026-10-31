// SPDX-License-Identifier: GPL-2.0-or-later
#include "sidebar.h"

#include <QCoreApplication>
#include <QListWidget>
#include <QSignalBlocker>
#include <QUrl>
#include <QStyle>
#include <QVBoxLayout>

#include "gui/transferlistfilterswidgetitem.h"
#include "torrentmodel.h"

using namespace Qt::StringLiterals;
using namespace Remote;

namespace
{
    class FilterList final : public QListWidget
    {
    public:
        using QListWidget::QListWidget;
        QSize sizeHint() const override
        {
            return {sizeHintForColumn(0), int((count() + 0.5) * qMax(1, sizeHintForRow(0)))};
        }
    };

    QString selection(const QListWidget *list)
    {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
    }

    QString caption(const QString &source, const QString &count)
    {
        return source + u" (" + count + u')';
    }
}

Sidebar::Sidebar(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName(u"remoteSidebar"_s);
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setMinimumWidth(150);
    auto *container = new QWidget(this);
    container->setBackgroundRole(QPalette::Base);
    container->setAutoFillBackground(true);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 2, 0, 0);
    layout->setSpacing(2);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    const QList<QPair<const char *, QListWidget **>> sections {
        {"Status", &m_status}, {"Categories", &m_categories}, {"Tags", &m_tags}, {"Trackers", &m_trackers}};
    for (const auto &entry : sections)
    {
        auto *list = new FilterList(container);
        *entry.second = list;
        list->setFrameShape(QFrame::NoFrame);
        list->setUniformItemSizes(true);
        list->setSpacing(0);
        const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, list);
        list->setIconSize(QSize(iconSize, iconSize));
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        list->setMinimumWidth(145);
        auto *section = new TransferListFiltersWidgetItem(QCoreApplication::translate("TransferListFiltersWidget", entry.first), list, container);
        section->setChecked(true);
        m_sections.append(section);
        layout->addWidget(section);
        connect(list, &QListWidget::currentRowChanged, this, [this] { emit filtersChanged(); });
        connect(section, &TransferListFiltersWidgetItem::toggled, this, [this] { emit filtersChanged(); });
    }
    m_status->setObjectName(u"remoteStatusFilter"_s);
    setWidget(container);
}

QString Sidebar::state() const { return m_sections[0]->isChecked() ? selection(m_status) : QString(); }
QString Sidebar::category() const { return m_sections[1]->isChecked() ? selection(m_categories) : QString(); }
QString Sidebar::tag() const { return m_sections[2]->isChecked() ? selection(m_tags) : QString(); }
QString Sidebar::tracker() const { return m_sections[3]->isChecked() ? selection(m_trackers) : QString(); }

void Sidebar::updateData(const TorrentModel &model, const QJsonObject &categories, const QSet<QString> &tags)
{
    struct Status { const char *label; const char16_t *value; const char16_t *icon; };
    const Status states[] {
        {"All", u"all", u"filter-all"}, {"Downloading", u"downloading", u"downloading"},
        {"Seeding", u"seeding", u"upload"}, {"Completed", u"completed", u"checked-completed"},
        {"Running", u"running", u"torrent-start"}, {"Stopped", u"stopped", u"stopped"},
        {"Active", u"active", u"filter-active"}, {"Inactive", u"inactive", u"filter-inactive"},
        {"Stalled", u"stalled", u"filter-stalled"}, {"Checking", u"checking", u"force-recheck"},
        {"Moving", u"moving", u"set-location"}, {"Errored", u"errored", u"error"}
    };
    const QSignalBlocker statusBlocker(m_status);
    if (m_status->count() == 0)
    {
        for (const auto &state : states)
        {
            auto *item = new QListWidgetItem(QIcon(u":/icons/"_s + QString::fromUtf16(state.icon) + u".svg"), {}, m_status);
            item->setData(Qt::UserRole, QString::fromUtf16(state.value));
        }
        m_status->setCurrentRow(0);
    }
    for (int index = 0; index < int(std::size(states)); ++index)
    {
        int count = 0;
        for (int row = 0; row < model.rowCount(); ++row)
            count += TorrentFilter::acceptsState(model.torrentAt(row), QString::fromUtf16(states[index].value));
        const QByteArray source = QByteArray(states[index].label) + " (%1)";
        m_status->item(index)->setText(QCoreApplication::translate("StatusFilterWidget", source.constData()).arg(count));
    }
    m_status->setFixedHeight(m_status->sizeHint().height());

    QHash<QString, int> categoryCounts;
    QHash<QString, int> tagCounts;
    QHash<QString, int> trackerCounts;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        const auto torrent = model.torrentAt(row);
        ++categoryCounts[torrent.value(u"category"_s).toString()];
        for (const auto &tag : torrent.value(u"tags"_s).toString().split(u", "_s, Qt::SkipEmptyParts))
            ++tagCounts[tag];
        const QString host = QUrl(torrent.value(u"tracker"_s).toString()).host();
        if (!host.isEmpty())
            ++trackerCounts[host];
    }
    const auto updateList = [this, &model](QListWidget *list, const QStringList &values, const QHash<QString, int> &counts, const char *context, const char *emptyName)
    {
        const QSignalBlocker blocker(list);
        const QString selected = selection(list);
        list->clear();
        auto *all = new QListWidgetItem(QIcon(u":/icons/filter-all.svg"_s), QCoreApplication::translate(context, "All (%1)").arg(model.rowCount()), list);
        all->setData(Qt::UserRole, QString());
        if (emptyName)
        {
            auto *empty = new QListWidgetItem(QCoreApplication::translate(context, emptyName).arg(counts.value(u""_s)), list);
            empty->setData(Qt::UserRole, u""_s);
        }
        for (const auto &value : values)
        {
            auto *item = new QListWidgetItem(caption(value, QString::number(counts.value(value))), list);
            item->setData(Qt::UserRole, value);
            item->setIcon(QIcon(list == m_categories ? u":/icons/directory.svg"_s : u":/icons/network-server.svg"_s));
        }
        list->setCurrentRow(0);
        if (!selected.isNull())
        {
            for (int row = 1; row < list->count(); ++row)
            {
                if (list->item(row)->data(Qt::UserRole).toString() == selected)
                    list->setCurrentRow(row);
            }
        }
        list->setFixedHeight(list->sizeHint().height());
    };
    updateList(m_categories, categories.keys(), categoryCounts, "CategoryFilterWidget", "Uncategorized (%1)");
    auto tagNames = tags.values();
    tagNames.sort();
    updateList(m_tags, tagNames, tagCounts, "TagFilterWidget", nullptr);
    auto trackerNames = trackerCounts.keys();
    trackerNames.sort();
    updateList(m_trackers, trackerNames, trackerCounts, "TrackersFilterWidget", nullptr);
}

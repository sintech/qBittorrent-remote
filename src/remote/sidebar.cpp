// SPDX-License-Identifier: GPL-2.0-or-later
#include "sidebar.h"

#include <QCoreApplication>
#include <QListWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include "gui/transferlistfilters/basefilterwidget.h"
#include "gui/transferlistfilters/statusfilterwidget.h"
#include "gui/transferlistfilters/categoryfilterwidget.h"
#include "gui/transferlistfilters/tagfilterwidget.h"
#include "gui/uithememanager.h"
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
    QString selection(const QListWidget *list)
    {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toString() : QString();
    }

    QString selection(const QTreeView *tree)
    {
        return tree->currentIndex().data(Qt::UserRole).toString();
    }

    QString caption(const QString &source, const QString &count)
    {
        return source + u" (" + count + u')';
    }
}

Sidebar::Sidebar(QWidget *parent)
    : TransferListFiltersWidget(parent)
{
    setObjectName(u"remoteSidebar"_s);
    m_status = new StatusFilterWidget(this, nullptr);
    m_categories = new CategoryFilterWidget(this, new QStandardItemModel(this));
    m_tags = new TagFilterWidget(this, new QStandardItemModel(this));
    m_trackers = new BaseFilterWidget(this, nullptr);
    const QList<QPair<const char *, QWidget *>> filters {
        {"Status", m_status}, {"Categories", m_categories}, {"Tags", m_tags}, {"Trackers", m_trackers}};
    for (const auto &entry : filters)
    {
        auto *section = appendFilter(QCoreApplication::translate("TransferListFiltersWidget", entry.first), entry.second);
        m_sections.append(section);
        connect(section, &TransferListFiltersWidgetItem::toggled, this, [this] { emit filtersChanged(); });
    }
    for (auto *list : {m_status, m_trackers})
        connect(list, &QListWidget::currentRowChanged, this, [this] { emit filtersChanged(); });
    for (auto *tree : {m_categories, m_tags})
        connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] { emit filtersChanged(); });
    m_status->setObjectName(u"remoteStatusFilter"_s);
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
        {"Stalled", u"stalled", u"filter-stalled"}, {"Stalled Uploading", u"stalledUP", u"stalledUP"},
        {"Stalled Downloading", u"stalledDL", u"stalledDL"}, {"Checking", u"checking", u"force-recheck"},
        {"Moving", u"moving", u"set-location"}, {"Errored", u"errored", u"error"}
    };
    const QSignalBlocker statusBlocker(m_status);
    for (int i = 0; i < int(std::size(states)); ++i)
        m_status->item(i)->setData(Qt::UserRole, QString::fromUtf16(states[i].value));
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
        if (torrent.value(u"tags"_s).toString().isEmpty()) ++tagCounts[u""_s];
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
            item->setIcon(UIThemeManager::instance()->getIcon(u"trackers"_s, u"network-server"_s));
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
    const auto updateTree = [this, &model](QTreeView *tree, const QStringList &values, const QHash<QString, int> &counts, const char *context, const char *emptyName)
    {
        const QString previous = selection(tree);
        const QSignalBlocker blocker(tree->selectionModel());
        auto *data = static_cast<QStandardItemModel *>(tree->model());
        data->clear();
        auto *all = new QStandardItem(UIThemeManager::instance()->getIcon(u"filter-all"_s), QCoreApplication::translate(context, "All (%1)").arg(model.rowCount()));
        data->appendRow(all);
        if (emptyName)
        {
            auto *empty = new QStandardItem(QCoreApplication::translate(context, emptyName).arg(counts.value(u""_s)));
            empty->setData(u""_s, Qt::UserRole);
            data->appendRow(empty);
        }
        for (const auto &value : values)
        {
            auto *row = new QStandardItem(UIThemeManager::instance()->getIcon(tree == m_categories ? u"directory"_s : u"tag"_s), caption(value, QString::number(counts.value(value))));
            row->setData(value, Qt::UserRole);
            row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            data->appendRow(row);
        }
        tree->setCurrentIndex(data->index(0, 0));
        for (int row = 1; !previous.isNull() && row < data->rowCount(); ++row)
            if (data->index(row, 0).data(Qt::UserRole).toString() == previous)
                tree->setCurrentIndex(data->index(row, 0));
        tree->updateGeometry();
    };
    updateTree(m_categories, categories.keys(), categoryCounts, "CategoryFilterWidget", "Uncategorized (%1)");
    auto tagNames = tags.values();
    tagNames.sort();
    updateTree(m_tags, tagNames, tagCounts, "TagFilterWidget", "Untagged (%1)");
    auto trackerNames = trackerCounts.keys();
    trackerNames.sort();
    updateList(m_trackers, trackerNames, trackerCounts, "TrackersFilterWidget", nullptr);
}

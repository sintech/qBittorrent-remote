// SPDX-License-Identifier: GPL-2.0-or-later
#include "sidebar.h"
#include <QMenu>
#include <functional>

#include <QCoreApplication>
#include <QListWidget>
#include <QTreeView>
#include <QMap>
#include "gui/transferlistfilters/categoryfiltermodel.h"
#include "gui/transferlistfilters/categoryfilterproxymodel.h"
#include "gui/transferlistfilters/tagfiltermodel.h"
#include "gui/transferlistfilters/tagfilterproxymodel.h"
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
        const auto index = tree->currentIndex();
        if (!index.isValid() || (!index.parent().isValid() && index.row()==0)) return {};
        if (!index.parent().isValid() && index.row()==1) return u""_s;
        if (auto *proxy = dynamic_cast<CategoryFilterProxyModel *>(tree->model())) return proxy->categoryName(index);
        return static_cast<TagFilterProxyModel *>(tree->model())->tag(index).toString();
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
    auto *categories = new CategoryFilterProxyModel(this);
    categories->setSortCaseSensitivity(Qt::CaseInsensitive);
    categories->setSourceModel(new CategoryFilterModel(categories));
    m_categories = new CategoryFilterWidget(this,categories);
    m_categories->setObjectName(u"remoteCategoryFilter"_s);
    auto *tags = new TagFilterProxyModel(this);
    tags->setSortCaseSensitivity(Qt::CaseInsensitive);
    tags->setSourceModel(new TagFilterModel(tags));
    m_tags = new TagFilterWidget(this,tags);
    m_tags->setObjectName(u"remoteTagFilter"_s);
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
    const auto bind = [this](QWidget *view, const QString &kind, auto builder, auto selectedValue)
    {
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(view, &QWidget::customContextMenuRequested, this, [this, view, kind, builder, selectedValue](const QPoint &point)
        {
            const QString value = selectedValue();
            const bool normal = !value.isEmpty();
            auto *menu = builder(this, normal, [this, kind, value](const QString &op) { emit contextCommand(kind, op, value); });
            menu->popup(view->mapToGlobal(point));
        });
    };
    bind(m_status, u"status"_s, &StatusFilterWidget::createContextMenu, [this] { return state(); });
    bind(m_categories, u"category"_s, &CategoryFilterWidget::createContextMenu, [this] { return category(); });
    bind(m_tags, u"tag"_s, &TagFilterWidget::createContextMenu, [this] { return tag(); });
    bind(m_trackers, u"tracker"_s, &BaseFilterWidget::createTrackerContextMenu, [this] { return tracker(); });
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
    // Bind API counts to the original models, preserving their icons, labels,
    // special rows, natural sorting and category hierarchy.
    {
        const auto previous = selection(m_categories);
        auto *proxy = static_cast<CategoryFilterProxyModel *>(m_categories->model());
        auto *source = static_cast<CategoryFilterModel *>(proxy->sourceModel());
        QSet<QString> expanded;
        const std::function<void(const QModelIndex &)> remember = [&](const QModelIndex &parent)
        {
            for (int row=0;row<proxy->rowCount(parent);++row)
            {
                const auto index=m_categories->model()->index(row,0,parent);
                if (m_categories->isExpanded(index)) expanded.insert(proxy->categoryName(index));
                remember(index);
            }
        };
        remember({});
        QMap<QString,int> counts;
        for (const auto &name : categories.keys()) counts.insert(name,categoryCounts.value(name));
        for (auto it=categoryCounts.cbegin();it!=categoryCounts.cend();++it) counts.insert(it.key(),it.value());
        const QSignalBlocker blocker(m_categories->selectionModel());
        source->setSnapshot(counts,model.rowCount());
        for (const auto &name : expanded) m_categories->expand(proxy->index(name));
        m_categories->setCurrentIndex(previous.isNull() ? m_categories->model()->index(0,0) : proxy->index(previous));
        if (!m_categories->currentIndex().isValid()) m_categories->setCurrentIndex(m_categories->model()->index(0,0));
        m_categories->updateGeometry();
    }
    {
        const auto previous=selection(m_tags);
        auto *proxy=static_cast<TagFilterProxyModel *>(m_tags->model());
        auto *source=static_cast<TagFilterModel *>(proxy->sourceModel());
        QMap<QString,int> counts;
        for (const auto &tag : tags) counts.insert(tag,tagCounts.value(tag));
        for (auto it=tagCounts.cbegin();it!=tagCounts.cend();++it) if (!it.key().isEmpty()) counts.insert(it.key(),it.value());
        const QSignalBlocker blocker(m_tags->selectionModel());
        source->setSnapshot(counts,model.rowCount(),tagCounts.value(QString()));
        auto selected = previous.isNull() ? m_tags->model()->index(0,0) : previous.isEmpty() ? m_tags->model()->index(1,0) : proxy->index(Tag(previous));
        m_tags->setCurrentIndex(selected.isValid() ? selected : m_tags->model()->index(0,0));
        m_tags->updateGeometry();
    }
    auto trackerNames = trackerCounts.keys();
    trackerNames.sort();
    updateList(m_trackers, trackerNames, trackerCounts, "TrackersFilterWidget", nullptr);
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Server controller for the original SearchWidget form and SearchJobWidget.
#include "gui/search/searchwidget.h"
#include "client.h"
#include "gui/search/searchjobwidget.h"
#include "gui/lineedit.h"
#include "gui/uithememanager.h"
#include "base/search/searchhandler.h"
#include "ui_searchwidget.h"
#include "ui_pluginselectdialog.h"
#include <QCompleter>
#include <QDateTime>
#include <QDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTimer>
#include <QTabBar>

using namespace Qt::StringLiterals;

struct SearchWidget::RemoteState
{
    Ui::SearchWidget ui;
    Remote::Client *client;
    QTimer timer;
    QJsonArray plugins;
    quint64 generation = 0;
    bool starting = false;
    bool loadingPlugins = false;
    struct Job { QPointer<SearchJobWidget> widget; int offset = 0; bool pending = false; quint64 request = 0; };
    QHash<int, Job> jobs;
    QPointer<QDialog> pluginDialog;
};

SearchWidget::SearchWidget(Remote::Client *client, QWidget *parent)
    : GUIApplicationComponent(nullptr,parent), m_remote(new RemoteState)
{
    auto &s=*m_remote;
    s.client=client;
    s.ui.setupUi(this);
    s.ui.lineEditSearchPattern->setPlaceholderText(tr("Search torrents..."));
    s.ui.selectPlugin->addItem(tr("Only enabled"),u"enabled"_s);
    s.ui.comboCategory->addItem(tr("All categories"),u"all"_s);
    s.ui.stackedPages->setCurrentWidget(s.ui.emptyPage);
    s.ui.tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(s.ui.tabWidget->tabBar(),&QWidget::customContextMenuRequested,this,[this](const QPoint &point)
    {
        auto *menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);
        const int index=m_remote->ui.tabWidget->tabBar()->tabAt(point);
        auto *close=menu->addAction(tr("Close"),this,[this,index] { closeRemoteTab(index); });close->setEnabled(index>=0);
        menu->addAction(tr("Close all"),this,[this] { while(m_remote->ui.tabWidget->count()) closeRemoteTab(0); });
        menu->popup(m_remote->ui.tabWidget->tabBar()->mapToGlobal(point));
    });
    connect(s.ui.searchButton,&QPushButton::clicked,this,&SearchWidget::startSearch);
    connect(s.ui.lineEditSearchPattern,&QLineEdit::returnPressed,this,&SearchWidget::startSearch);
    connect(s.ui.lineEditSearchPattern,&QLineEdit::textChanged,this,&SearchWidget::refreshControls);
    connect(s.ui.tabWidget,&QTabWidget::currentChanged,this,&SearchWidget::refreshControls);
    connect(s.ui.tabWidget,&QTabWidget::tabCloseRequested,this,&SearchWidget::closeRemoteTab);
    connect(s.ui.pluginsButton,&QPushButton::clicked,this,&SearchWidget::showPlugins);
    connect(s.ui.stopButton,&QPushButton::clicked,this,[this]
    {
        auto *job=qobject_cast<SearchJobWidget *>(m_remote->ui.tabWidget->currentWidget());
        if (!job) return;
        const int id=job->id().toInt();
        QUrlQuery q;q.addQueryItem(u"id"_s,QString::number(id));
        QPointer<SearchJobWidget> guard(job);
        m_remote->client->post(u"search/stop"_s,q,[guard](const QByteArray &) { if(guard) guard->setRemoteStatus(SearchJobWidget::Status::Aborted); });
    });
    connect(s.ui.selectPlugin,&QComboBox::currentIndexChanged,this,[this]
    {
        auto &s=*m_remote;const auto previous=s.ui.comboCategory->currentData().toString();
        s.ui.comboCategory->clear();s.ui.comboCategory->addItem(tr("All categories"),u"all"_s);
        QSet<QString> ids {u"all"_s};const auto selected=s.ui.selectPlugin->currentData().toString();
        for(const auto &entry:s.plugins)
        {
            const auto p=entry.toObject();
            if(selected!=u"all" && selected!=u"enabled" && p[u"name"].toString()!=selected) continue;
            if(selected==u"enabled" && !p[u"enabled"].toBool()) continue;
            for(const auto &value:p[u"supportedCategories"].toArray())
            {
                const auto cat=value.toObject();const auto id=cat[u"id"].toString();
                if(!ids.contains(id)) { ids.insert(id);s.ui.comboCategory->addItem(cat[u"name"].toString(),id); }
            }
        }
        const int index=s.ui.comboCategory->findData(previous);if(index>=0) s.ui.comboCategory->setCurrentIndex(index);
    });
    connect(client,&Remote::Client::connectedChanged,this,[this](bool connected)
    {
        if(connected) refreshPlugins();
        else
        {
            ++m_remote->generation;m_remote->starting=false;m_remote->loadingPlugins=false;
            for(auto &job:m_remote->jobs) { job.pending=false;job.widget->setRemoteStatus(SearchJobWidget::Status::Aborted); }
            // Do not reuse server search IDs across reconnects/profile changes.
            m_remote->jobs.clear();
        }
        refreshControls();
    });
    connect(client,&Remote::Client::reset,this,[this]
    {
        ++m_remote->generation;m_remote->starting=false;m_remote->loadingPlugins=false;
        m_remote->jobs.clear();m_remote->plugins={};
        while(auto *w=m_remote->ui.tabWidget->widget(0)) { m_remote->ui.tabWidget->removeTab(0);delete w; }
        if(m_remote->pluginDialog) m_remote->pluginDialog->reject();
        refreshControls();
    });
    connect(client,&Remote::Client::requestFailed,this,[this](const QString &method)
    {
        if(method==u"search/start") m_remote->starting=false;
        if(method==u"search/plugins") m_remote->loadingPlugins=false;
        if(method==u"search/results")
            for(auto &job:m_remote->jobs) { job.pending=false;++job.request; }
        refreshControls();
    });
    s.timer.setInterval(1500);connect(&s.timer,&QTimer::timeout,this,&SearchWidget::pollSearches);s.timer.start();
    auto *shortcut=new QShortcut(QKeySequence::Find,this);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(shortcut,&QShortcut::activated,this,&SearchWidget::giveFocusToSearchInput);
    if(client->isConnected()) refreshPlugins();
    refreshControls();
}

SearchWidget::~SearchWidget()
{
    for(auto *child:findChildren<QObject *>()) QObject::disconnect(child,nullptr,this,nullptr);
    m_remote->timer.stop();
    delete m_remote;
}
void SearchWidget::giveFocusToSearchInput() { m_remote->ui.lineEditSearchPattern->setFocus(); }

void SearchWidget::refreshControls()
{
    auto &s=*m_remote;const bool connected=s.client->isConnected();
    s.ui.searchButton->setEnabled(connected && !s.starting && !s.ui.lineEditSearchPattern->text().trimmed().isEmpty() && !s.plugins.isEmpty());
    s.ui.pluginsButton->setEnabled(connected);
    auto *job=qobject_cast<SearchJobWidget *>(s.ui.tabWidget->currentWidget());
    s.ui.stopButton->setEnabled(connected && job && s.jobs.contains(job->id().toInt()) && job->status()==SearchJobWidget::Status::Ongoing);
    s.ui.stackedPages->setCurrentWidget(s.ui.tabWidget->count() || !s.plugins.isEmpty() ? s.ui.searchPage:s.ui.emptyPage);
}

void SearchWidget::refreshPlugins()
{
    auto &s=*m_remote;if(!s.client->isConnected() || s.loadingPlugins) return;
    s.loadingPlugins=true;const auto generation=s.generation;QPointer<SearchWidget> self(this);
    s.client->get(u"search/plugins"_s,{},[self,generation](const QByteArray &body)
    {
        if(!self || self->m_remote->generation!=generation) return;
        auto &s=*self->m_remote;s.loadingPlugins=false;
        const auto doc=QJsonDocument::fromJson(body);
        if(!doc.isArray()) return;
        s.plugins=doc.array();const auto selected=s.ui.selectPlugin->currentData();
        s.ui.selectPlugin->clear();s.ui.selectPlugin->addItem(tr("Only enabled"),u"enabled"_s);s.ui.selectPlugin->addItem(tr("All plugins"),u"all"_s);
        for(const auto &value:s.plugins) { const auto p=value.toObject();s.ui.selectPlugin->addItem(p[u"fullName"].toString(),p[u"name"].toString()); }
        const int index=s.ui.selectPlugin->findData(selected);s.ui.selectPlugin->setCurrentIndex(index>=0?index:0);
        self->refreshControls();
    });
}

void SearchWidget::startSearch()
{
    auto &s=*m_remote;if(!s.ui.searchButton->isEnabled()) return;
    const auto pattern=s.ui.lineEditSearchPattern->text().trimmed();
    QUrlQuery q;q.addQueryItem(u"pattern"_s,pattern);q.addQueryItem(u"category"_s,s.ui.comboCategory->currentData().toString());q.addQueryItem(u"plugins"_s,s.ui.selectPlugin->currentData().toString());
    s.starting=true;refreshControls();const auto generation=s.generation;QPointer<SearchWidget> self(this);
    s.client->post(u"search/start"_s,q,[self,generation,pattern](const QByteArray &body)
    {
        if(!self || self->m_remote->generation!=generation) return;
        auto &s=*self->m_remote;s.starting=false;
        const auto data=QJsonDocument::fromJson(body).object();
        if(!data[u"id"].isDouble() || data[u"id"].toInt()<=0) { self->refreshControls();return; }
        const int id=data[u"id"].toInt();
        auto *job=new SearchJobWidget(QString::number(id),pattern,QList<SearchResult>{},nullptr,self);
        job->setRemoteStatus(SearchJobWidget::Status::Ongoing);
        s.jobs.insert(id,{job});
        const int tab=s.ui.tabWidget->addTab(job,pattern);s.ui.tabWidget->setCurrentIndex(tab);
        connect(job,&SearchJobWidget::statusChanged,self,[self,job]
        {
            if(!self) return;const int index=self->m_remote->ui.tabWidget->indexOf(job);
            if(index>=0) self->m_remote->ui.tabWidget->setTabToolTip(index,job->statusTip());
            self->refreshControls();
            if(job->status()==SearchJobWidget::Status::Finished || job->status()==SearchJobWidget::Status::NoResults) emit self->searchFinished(false);
        });
        connect(job,&SearchJobWidget::resultsCountUpdated,self,[self,job]
        {
            if(!self) return;const int index=self->m_remote->ui.tabWidget->indexOf(job);
            if(index>=0) self->m_remote->ui.tabWidget->setTabText(index,u"%1 (%2)"_s.arg(job->searchPattern()).arg(job->visibleResultsCount()));
        });
        connect(job,&SearchJobWidget::remoteDownloadRequested,self,[self](const QString &url,const QString &engine,AddTorrentOption option)
        {
            if(!self || !self->m_remote->client->isConnected()) return;
            if(option==AddTorrentOption::ShowDialog || QUrl(url).scheme().compare(u"magnet",Qt::CaseInsensitive)==0) { emit self->sourceRequested(url,option);return; }
            // Plugin-specific downloads must execute on the server. The API adds
            // them using server defaults and cannot return a pre-add .torrent.
            QUrlQuery q;q.addQueryItem(u"torrentUrl"_s,QString(url).replace(u'%',u"%25"_s));q.addQueryItem(u"pluginName"_s,engine);
            self->m_remote->client->command(u"search/downloadTorrent"_s,q);
        });
        self->refreshControls();self->pollSearches();
    });
}

void SearchWidget::pollSearches()
{
    auto &s=*m_remote;if(!s.client->isConnected()) return;
    const auto ids=s.jobs.keys();
    for(int id:ids)
    {
        auto &j=s.jobs[id];if(!j.widget || j.pending) continue;
        // Aborted jobs still drain the final results once after Stop.
        if(j.widget->status()!=SearchJobWidget::Status::Ongoing && j.widget->property("remoteDrained").toBool()) continue;
        j.pending=true;const auto token=++j.request;const auto generation=s.generation;QPointer<SearchWidget> self(this);
        QUrlQuery q;q.addQueryItem(u"id"_s,QString::number(id));q.addQueryItem(u"offset"_s,QString::number(j.offset));q.addQueryItem(u"limit"_s,u"500"_s);
        s.client->get(u"search/results"_s,q,[self,id,generation,token](const QByteArray &body)
        {
            if(!self || self->m_remote->generation!=generation || !self->m_remote->jobs.contains(id)) return;
            auto &j=self->m_remote->jobs[id];if(j.request!=token || !j.widget) return;j.pending=false;
            const auto d=QJsonDocument::fromJson(body).object();if(!d[u"results"].isArray() || !d[u"total"].isDouble()) { j.widget->setRemoteStatus(SearchJobWidget::Status::Error);return; }
            QList<SearchResult> results;
            for(const auto &entry:d[u"results"].toArray())
            {
                const auto p=entry.toObject();SearchResult r;
                r.fileName=p[u"fileName"].toString();r.fileUrl=p[u"fileUrl"].toString();r.fileSize=p[u"fileSize"].toInteger();r.nbSeeders=p[u"nbSeeders"].toInteger();r.nbLeechers=p[u"nbLeechers"].toInteger();r.engineName=p[u"engineName"].toString();r.siteUrl=p[u"siteUrl"].toString();r.descrLink=p[u"descrLink"].toString();
                if(const auto date=p[u"pubDate"].toInteger();date>0) r.pubDate=QDateTime::fromSecsSinceEpoch(date);
                results.append(r);
            }
            j.offset+=results.size();j.widget->appendRemoteResults(results);
            if(d[u"status"].toString()==u"Stopped" && j.offset>=d[u"total"].toInt())
            {
                j.widget->setProperty("remoteDrained",true);
                if(j.widget->status()!=SearchJobWidget::Status::Aborted) j.widget->setRemoteStatus(j.offset?SearchJobWidget::Status::Finished:SearchJobWidget::Status::NoResults);
            }
        });
    }
}

void SearchWidget::closeRemoteTab(int index)
{
    auto &s=*m_remote;auto *job=qobject_cast<SearchJobWidget *>(s.ui.tabWidget->widget(index));if(!job) return;
    const int id=job->id().toInt();const bool owned=s.jobs.contains(id) && s.jobs[id].widget==job;
    if(owned) s.jobs.remove(id);
    if(owned && s.client->isConnected()) { QUrlQuery q;q.addQueryItem(u"id"_s,QString::number(id));s.client->command(u"search/delete"_s,q); }
    s.ui.tabWidget->removeTab(index);delete job;refreshControls();
}

void SearchWidget::showPlugins()
{
    auto &s=*m_remote;if(s.pluginDialog) { s.pluginDialog->raise();return; }
    auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);s.pluginDialog=dialog;
    auto ui=std::make_shared<Ui::PluginSelectDialog>();ui->setupUi(dialog);dialog->setAcceptDrops(false);
    ui->pluginsTree->setRootIsDecorated(false);ui->pluginsTree->hideColumn(4);
    ui->pluginsTree->header()->setFirstSectionMovable(true);
    ui->pluginsTree->header()->restoreState(QSettings().value(u"remote/search/pluginsHeader"_s).toByteArray());
    dialog->resize(QSettings().value(u"remote/search/pluginsSize"_s,QSize(600,345)).toSize());
    auto populate=[this,ui,dialog]
    {
        if(!dialog->isVisible()) return;
        const auto selection=ui->pluginsTree->selectedItems();QStringList selected;for(auto *i:selection) selected<<i->text(4);
        ui->pluginsTree->setSortingEnabled(false);ui->pluginsTree->clear();
        for(const auto &entry:m_remote->plugins)
        {
            const auto p=entry.toObject();auto *i=new QTreeWidgetItem(ui->pluginsTree,{p[u"fullName"].toString(),p[u"version"].toString(),p[u"url"].toString(),p[u"enabled"].toBool()?tr("Yes"):tr("No"),p[u"name"].toString()});
            i->setData(0,Qt::UserRole,p[u"enabled"].toBool());i->setSelected(selected.contains(i->text(4)));
        }
        ui->pluginsTree->setSortingEnabled(true);
    };
    auto *timer=new QTimer(dialog);timer->setInterval(2500);
    connect(timer,&QTimer::timeout,dialog,[this,populate] { refreshPlugins();populate(); });timer->start();
    connect(ui->closeButton,&QPushButton::clicked,dialog,&QDialog::close);
    connect(dialog,&QDialog::finished,this,[this,dialog,ui]
    {
        QSettings().setValue(u"remote/search/pluginsSize"_s,dialog->size());QSettings().setValue(u"remote/search/pluginsHeader"_s,ui->pluginsTree->header()->saveState());m_remote->pluginDialog=nullptr;refreshPlugins();
    });
    auto selectedNames=[ui] { QStringList names;for(auto *i:ui->pluginsTree->selectedItems()) names<<i->text(4);return names.join(u'|'); };
    auto enable=[this,selectedNames](bool enabled) { const auto names=selectedNames();if(names.isEmpty()) return;QUrlQuery q;q.addQueryItem(u"names"_s,names);q.addQueryItem(u"enable"_s,enabled?u"true"_s:u"false"_s);m_remote->client->post(u"search/enablePlugin"_s,q,[this](const QByteArray &) { refreshPlugins(); }); };
    connect(ui->pluginsTree,&QTreeWidget::itemDoubleClicked,dialog,[enable](QTreeWidgetItem *item,int) { enable(!item->data(0,Qt::UserRole).toBool()); });
    connect(ui->pluginsTree,&QWidget::customContextMenuRequested,dialog,[ui,dialog,enable,this,selectedNames](const QPoint &point)
    {
        if(ui->pluginsTree->selectedItems().isEmpty()) return;
        auto *menu=new QMenu(dialog);menu->setAttribute(Qt::WA_DeleteOnClose);
        auto *enabled=menu->addAction(tr("Enabled"));enabled->setCheckable(true);enabled->setChecked(ui->pluginsTree->selectedItems().first()->data(0,Qt::UserRole).toBool());connect(enabled,&QAction::triggered,dialog,enable);
        menu->addSeparator();menu->addAction(UIThemeManager::instance()->getIcon(u"list-remove"_s),tr("Uninstall"),dialog,[this,selectedNames] { QUrlQuery q;q.addQueryItem(u"names"_s,selectedNames());m_remote->client->post(u"search/uninstallPlugin"_s,q,[this](const QByteArray &) { refreshPlugins(); }); });
        menu->popup(ui->pluginsTree->viewport()->mapToGlobal(point));
    });
    connect(ui->updateButton,&QPushButton::clicked,dialog,[this] { m_remote->client->post(u"search/updatePlugins"_s,{},[this](const QByteArray &) { refreshPlugins(); }); });
    ui->installButton->setToolTip(tr("Install a Python plugin on the server from an HTTP(S) URL."));
    connect(ui->installButton,&QPushButton::clicked,dialog,[this,dialog]
    {
        bool ok=false;const auto source=QInputDialog::getText(dialog,tr("New search engine plugin URL"),tr("URL:"),QLineEdit::Normal,{},&ok).trimmed();
        if(!ok || source.isEmpty() || !m_remote->client->isConnected()) return;
        const QUrl url(source);if(!url.isValid() || (url.scheme()!=u"https" && url.scheme()!=u"http")) { QMessageBox::warning(dialog,tr("Invalid link"),tr("Enter an HTTP(S) plugin URL accessible to the server."));return; }
        QUrlQuery q;q.addQueryItem(u"sources"_s,QString(source).replace(u'%',u"%25"_s));m_remote->client->post(u"search/installPlugin"_s,q,[this](const QByteArray &) { refreshPlugins(); });
    });
    dialog->show();populate();refreshPlugins();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// The API controller uses the same Designer form and promoted widgets as the local controller.
#include "addnewtorrentdialog.h"
#include <QDateTime>
#include <QFile>
#include <QFileIconProvider>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QScopedValueRollback>
#include <QPushButton>
#include "remote/torrentmetadata.h"
#include "remote/torrentmodel.h"
#include <QJsonArray>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include "gui/lineedit.h"
#include "ui_addnewtorrentdialog.h"
using namespace Qt::StringLiterals;

AddNewTorrentDialog::AddNewTorrentDialog(const QString &source, const QJsonObject &preferences,
    const QJsonObject &categories, QWidget *parent, bool localFile) : QDialog(parent), m_ui(new Ui::AddNewTorrentDialog)
{
    m_ui->setupUi(this);
    setWindowTitle(tr("Add torrent") + u" — " + source);
    for (auto *path : {m_ui->savePath, m_ui->downloadPath})
    {
        path->setMode(FileSystemPathEdit::Mode::DirectorySave);
        path->setServerPathMode();
        path->setMaxVisibleItems(20);
    }
    const QString historyKey = u"remote/addDialog/"_s + QString::fromLatin1(QUrl(preferences.value(u"remote_server"_s).toString()).toEncoded().toBase64());
    setProperty("historyKey", historyKey);
    QSettings settings;
    m_ui->savePath->setPathText(settings.value(historyKey + u"/savePath", preferences.value(u"save_path"_s).toString()).toString());
    m_ui->downloadPath->setPathText(preferences.value(u"temp_path"_s).toString());
    m_ui->groupBoxDownloadPath->setChecked(preferences.value(u"temp_path_enabled"_s).toBool());
    m_ui->checkBoxRememberLastSavePath->setChecked(settings.value(historyKey + u"/remember", false).toBool());
    m_ui->categoryComboBox->addItem(QString());
    m_ui->categoryComboBox->addItems(categories.keys());
    m_ui->categoryComboBox->setCurrentText(settings.value(historyKey + u"/category").toString());
    m_ui->comboTMM->setCurrentIndex(preferences.value(u"auto_tmm_enabled"_s).toBool() ? 1 : 0);
    const auto updatePaths = [this, categories, preferences]
    {
        const bool manual = m_ui->comboTMM->currentIndex() == 0;
        m_ui->groupBoxSavePath->setEnabled(manual);
        m_ui->groupBoxDownloadPath->setEnabled(manual);
        if (!manual)
            m_ui->savePath->setPathText(categories.value(m_ui->categoryComboBox->currentText()).toObject().value(u"savePath"_s).toString(preferences.value(u"save_path"_s).toString()));
    };
    connect(m_ui->comboTMM, &QComboBox::currentIndexChanged, this, updatePaths);
    connect(m_ui->categoryComboBox, &QComboBox::currentTextChanged, this, updatePaths);
    updatePaths();
    m_ui->tagsLineEdit->setReadOnly(false);
    m_ui->tagsLineEdit->setPlaceholderText({});
    connect(m_ui->tagsEditButton, &QToolButton::clicked, this, [this]
    {
        bool ok = false;
        const auto tags = QInputDialog::getText(this, tr("Add/remove tags"), tr("Tags:"), QLineEdit::Normal, m_ui->tagsLineEdit->text(), &ok);
        if (ok) m_ui->tagsLineEdit->setText(tags);
    });
    m_ui->startTorrentCheckBox->setChecked(!preferences.value(u"add_stopped_enabled"_s).toBool());
    m_ui->addToQueueTopCheckBox->setChecked(preferences.value(u"add_to_top_of_queue"_s).toBool());
    m_ui->stopConditionComboBox->addItem(tr("None"), u"None"_s);
    if (source.startsWith(u"magnet:")) m_ui->stopConditionComboBox->addItem(tr("Metadata received"), u"MetadataReceived"_s);
    m_ui->stopConditionComboBox->addItem(tr("Files checked"), u"FilesChecked"_s);
    m_ui->stopConditionComboBox->setCurrentIndex(qMax(0, m_ui->stopConditionComboBox->findData(preferences.value(u"torrent_stop_condition"_s).toString())));
    connect(m_ui->startTorrentCheckBox, &QCheckBox::toggled, m_ui->stopConditionComboBox, &QWidget::setEnabled);
    m_ui->stopConditionComboBox->setEnabled(m_ui->startTorrentCheckBox->isChecked());
    const auto layout = preferences.value(u"torrent_content_layout"_s).toString();
    m_ui->contentLayoutComboBox->setCurrentIndex(layout == u"Subfolder" ? 1 : layout == u"NoSubfolder" ? 2 : 0);
    m_ui->doNotDeleteTorrentCheckBox->hide();
    m_ui->progMetaLoading->hide(); m_ui->buttonSave->hide();
    m_ui->lblMetaLoading->setText(tr("Metadata will be available after adding the torrent to the server."));
    m_ui->buttonSelectAll->setEnabled(false); m_ui->buttonSelectNone->setEnabled(false);
    if (localFile)
    {
        const auto metadata = Remote::readTorrentMetadata(source);
        if (!metadata.valid())
        {
            m_ui->lblMetaLoading->setText(tr("Could not read the torrent file."));
            m_ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        }
        else
        {
            setWindowTitle(metadata.name);
            setProperty("torrentHash", metadata.hashV1.isEmpty() ? metadata.hashV2.left(40) : metadata.hashV1);
            m_ui->lblMetaLoading->hide();
            m_ui->labelInfohash1Data->setText(metadata.hashV1.isEmpty() ? tr("N/A") : metadata.hashV1);
            m_ui->labelInfohash2Data->setText(metadata.hashV2.isEmpty() ? tr("N/A") : metadata.hashV2);
            m_ui->labelDateData->setText(metadata.created.isValid() ? QLocale().toString(metadata.created, QLocale::ShortFormat) : tr("Not available"));
            m_ui->labelCommentData->setTextFormat(Qt::PlainText);
            m_ui->labelCommentData->setText(metadata.comment);
            auto *model = m_ui->contentTreeView->sourceModel();
            QFileIconProvider icons;
            qint64 total = 0;
            for (int i = 0; i < metadata.files.size(); ++i)
            {
                const auto parts = metadata.files[i].split(u'/');
                auto *parent = model->invisibleRootItem();
                for (int j = 0; j + 1 < parts.size(); ++j)
                {
                    QStandardItem *folder = nullptr;
                    for (int k = 0; k < parent->rowCount(); ++k) if (parent->child(k)->text() == parts[j]) folder = parent->child(k);
                    if (!folder)
                    {
                        QList<QStandardItem *> row;
                        for (int c = 0; c < 6; ++c) row.append(new QStandardItem);
                        folder = row.first(); folder->setText(parts[j]); folder->setIcon(icons.icon(QFileIconProvider::Folder)); folder->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                        parent->appendRow(row);
                    }
                    parent = folder;
                }
                QList<QStandardItem *> row;
                for (int c = 0; c < 6; ++c) { auto *item = new QStandardItem; item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); row.append(item); }
                row[0]->setText(parts.last()); row[0]->setIcon(icons.icon(QFileIconProvider::File)); row[0]->setData(i, Qt::UserRole + 2); row[0]->setCheckable(true); row[0]->setCheckState(Qt::Checked);
                row[1]->setText(Remote::TorrentModel::size(metadata.sizes[i])); row[1]->setData(metadata.sizes[i], Qt::UserRole);
                row[3]->setText(TorrentContentWidget::tr("Normal")); row[3]->setData(1, Qt::UserRole);
                parent->appendRow(row); total += metadata.sizes[i];
            }
            m_ui->labelSizeData->setText(Remote::TorrentModel::size(total));
            m_ui->contentTreeView->expandAll();
            const auto apply = [this, model](const QModelIndexList &rows, int priority)
            {
                const QScopedValueRollback<bool> updating(m_updatingPriorities, true);
                const QString label = priority == 0 ? TorrentContentWidget::tr("Do not download") : priority == 7 ? TorrentContentWidget::tr("Maximum") : priority == 6 ? TorrentContentWidget::tr("High") : TorrentContentWidget::tr("Normal");
                const std::function<void(const QModelIndex &)> visit = [&](const QModelIndex &index)
                {
                    auto *item = model->itemFromIndex(index);
                    if (item->data(Qt::UserRole + 2).isValid())
                    {
                        item->setCheckState(priority ? Qt::Checked : Qt::Unchecked);
                        auto *cell = model->itemFromIndex(index.siblingAtColumn(3)); cell->setText(label); cell->setData(priority, Qt::UserRole);
                    }
                    for (int child = 0; child < model->rowCount(index); ++child) visit(model->index(child, 0, index));
                };
                for (const auto &index : rows) visit(index);
            };
            m_ui->buttonSelectAll->setEnabled(true); m_ui->buttonSelectNone->setEnabled(true);
            const auto allRows = [model] { QModelIndexList rows; for (int i = 0; i < model->rowCount(); ++i) rows.append(model->index(i, 0)); return rows; };
            connect(m_ui->buttonSelectAll, &QPushButton::clicked, this, [apply, allRows] { apply(allRows(), 1); });
            connect(m_ui->buttonSelectNone, &QPushButton::clicked, this, [apply, allRows] { apply(allRows(), 0); });
            connect(model, &QStandardItemModel::itemChanged, this, [this, apply](QStandardItem *item)
            { if (!m_updatingPriorities && item->column() == 0 && item->isCheckable()) apply({item->index()}, item->checkState() == Qt::Unchecked ? 0 : 1); });
            m_ui->contentTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(m_ui->contentTreeView, &QWidget::customContextMenuRequested, this, [this, model, apply](const QPoint &point)
            {
                auto *proxy = static_cast<QSortFilterProxyModel *>(m_ui->contentTreeView->model());
                QModelIndexList rows; for (const auto &index : m_ui->contentTreeView->selectionModel()->selectedRows()) rows.append(proxy->mapToSource(index));
                if (rows.isEmpty()) return;
                auto *menu = TorrentContentWidget::createContextMenu(this, rows.size(), false, [rows, apply](const QString &op, const QVariant &value)
                {
                    if (op == u"priority") apply(rows, value.toInt());
                    else if (op == u"order") for (int i = 0; i < rows.size(); ++i) apply({rows[i]}, i < qMax<qsizetype>(1, rows.size()/3) ? 7 : i < 2*qMax<qsizetype>(1, rows.size()/3) ? 6 : 1);
                });
                for (auto *action : menu->actions()) if (action->text() == TorrentContentWidget::tr("Rename...")) action->setEnabled(false);
                menu->popup(m_ui->contentTreeView->viewport()->mapToGlobal(point));
            });
        }
    }
    m_ui->contentTreeView->setColumnAvailable(TorrentContentWidget::Progress, false);
    m_ui->contentTreeView->setColumnAvailable(TorrentContentWidget::Remaining, false);
    m_ui->contentTreeView->setColumnAvailable(TorrentContentWidget::Availability, false);
    if (!settings.contains(u"remote/addContentHeader"_s))
        for (int column = 0; column < m_ui->contentTreeView->model()->columnCount(); ++column)
            m_ui->contentTreeView->setColumnWidth(column, m_ui->contentTreeView->header()->defaultSectionSize());
    m_ui->contentTreeView->setHeaderSettingsKey(u"remote/addContentHeader"_s);
    m_ui->checkBoxNeverShow->hide();
    auto *filter = new LineEdit(this);
    filter->setPlaceholderText(tr("Filter files..."));
    m_ui->contentFilterLayout->insertWidget(0, filter);
    filter->installPatternMenu(static_cast<QSortFilterProxyModel *>(m_ui->contentTreeView->model()), u"remote/addContentFilterFormat"_s);
    connect(m_ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(this, &QDialog::accepted, this, [this, historyKey]
    {
        QSettings settings;
        settings.setValue(historyKey + u"/remember", m_ui->checkBoxRememberLastSavePath->isChecked());
        if (m_ui->checkBoxRememberLastSavePath->isChecked()) settings.setValue(historyKey + u"/savePath", m_ui->savePath->pathText());
        else settings.remove(historyKey + u"/savePath");
        if (m_ui->defaultCategoryCheckbox->isChecked()) settings.setValue(historyKey + u"/category", m_ui->categoryComboBox->currentText());
    });
    resize(settings.value(u"remote/addDialog/size", size()).toSize());
}
AddNewTorrentDialog::~AddNewTorrentDialog()
{
    QSettings().setValue(u"remote/addDialog/size"_s, size());
    delete m_ui;
}
QUrlQuery AddNewTorrentDialog::parameters() const
{
    QUrlQuery form;
    const auto flag = [&form](const QString &key, bool value) { form.addQueryItem(key, value ? u"true"_s : u"false"_s); };
    if (m_ui->comboTMM->currentIndex() == 0)
    {
        form.addQueryItem(u"savepath"_s, m_ui->savePath->pathText());
        if (m_ui->groupBoxDownloadPath->isChecked()) form.addQueryItem(u"downloadPath"_s, m_ui->downloadPath->pathText());
        flag(u"useDownloadPath"_s, m_ui->groupBoxDownloadPath->isChecked());
    }
    flag(u"autoTMM"_s, m_ui->comboTMM->currentIndex() == 1);
    form.addQueryItem(u"category"_s, m_ui->categoryComboBox->currentText());
    form.addQueryItem(u"tags"_s, m_ui->tagsLineEdit->text());
    flag(u"stopped"_s, !m_ui->startTorrentCheckBox->isChecked());
    flag(u"addToTopOfQueue"_s, m_ui->addToQueueTopCheckBox->isChecked());
    flag(u"skip_checking"_s, m_ui->skipCheckingCheckBox->isChecked());
    flag(u"sequentialDownload"_s, m_ui->sequentialCheckBox->isChecked());
    flag(u"firstLastPiecePrio"_s, m_ui->firstLastCheckBox->isChecked());
    form.addQueryItem(u"stopCondition"_s, m_ui->startTorrentCheckBox->isChecked() ? m_ui->stopConditionComboBox->currentData().toString() : u"None"_s);
    form.addQueryItem(u"contentLayout"_s, QStringList{u"Original"_s, u"Subfolder"_s, u"NoSubfolder"_s}.value(m_ui->contentLayoutComboBox->currentIndex()));
    return form;
}

QString AddNewTorrentDialog::torrentHash() const { return property("torrentHash").toString(); }
QMap<int, QStringList> AddNewTorrentDialog::filePriorities() const
{
    QMap<int, QStringList> result;
    const auto *model = m_ui->contentTreeView->sourceModel();
    const std::function<void(const QStandardItem *)> visit = [&](const QStandardItem *parent)
    {
        for (int row = 0; row < parent->rowCount(); ++row)
        {
            const auto *item = parent->child(row);
            if (item->data(Qt::UserRole + 2).isValid())
            {
                const int priority = parent->child(row, 3)->data(Qt::UserRole).toInt();
                if (priority != 1) result[priority].append(item->data(Qt::UserRole + 2).toString());
            }
            visit(item);
        }
    };
    visit(model->invisibleRootItem()); return result;
}

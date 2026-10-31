// SPDX-License-Identifier: GPL-2.0-or-later
#include "profileeditor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;
using namespace Remote;

ProfileEditor::ProfileEditor(QList<Profile> profiles, const QString &selectedId, QWidget *parent)
    : QDialog(parent), m_profiles(std::move(profiles))
{
    setObjectName(u"remoteProfileEditor"_s);
    setWindowTitle(tr("Connection profiles"));
    resize(900, 620);
    setMinimumSize(760, 520);
    auto *outer = new QVBoxLayout(this);
    auto *body = new QHBoxLayout;
    auto *left = new QVBoxLayout;
    m_list = new QListWidget(this);
    m_list->setObjectName(u"profileList"_s);
    m_list->setMinimumWidth(160);
    m_list->setMaximumWidth(240);
    left->addWidget(m_list);
    auto *profileButtons = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add"), this);
    add->setObjectName(u"addProfile"_s);
    auto *remove = new QPushButton(tr("Remove"), this);
    remove->setObjectName(u"removeProfile"_s);
    profileButtons->addWidget(add);
    profileButtons->addWidget(remove);
    left->addLayout(profileButtons);
    body->addLayout(left);
    auto *right = new QVBoxLayout;
    auto *form = new QFormLayout;
    const auto field = [this](const QString &name)
    {
        auto *edit = new QLineEdit(this);
        edit->setObjectName(name);
        return edit;
    };
    m_name = field(u"profileName"_s);
    m_url = field(u"profileUrl"_s);
    m_url->setPlaceholderText(u"https://server.example/qbittorrent/"_s);
    m_auth = new QComboBox(this);
    m_auth->setObjectName(u"profileAuthentication"_s);
    m_auth->addItems({tr("Username and password"), tr("API key")});
    m_username = field(u"profileUsername"_s);
    m_password = field(u"profilePassword"_s);
    m_password->setEchoMode(QLineEdit::Password);
    m_key = field(u"profileApiKey"_s);
    m_key->setEchoMode(QLineEdit::Password);
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Server Web UI URL:"), m_url);
    form->addRow(tr("Authentication:"), m_auth);
    form->addRow(tr("Username:"), m_username);
    form->addRow(tr("Password:"), m_password);
    form->addRow(tr("API key:"), m_key);
    right->addLayout(form);
    m_secretError = new QLabel(this);
    m_secretError->setWordWrap(true);
    right->addWidget(m_secretError);
    auto *group = new QGroupBox(tr("Folder mappings"), this);
    auto *mappingLayout = new QVBoxLayout(group);
    auto *hint = new QLabel(tr("Example: /srv/downloads → D:\\Downloads or \\\\nas\\downloads. The most specific remote folder wins."), group);
    hint->setWordWrap(true);
    mappingLayout->addWidget(hint);
    m_mappings = new QTableWidget(0, 2, group);
    m_mappings->setObjectName(u"profileMappings"_s);
    m_mappings->setHorizontalHeaderLabels({tr("Remote folder"), tr("Local folder")});
    m_mappings->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_mappings->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mappings->setSelectionMode(QAbstractItemView::SingleSelection);
    mappingLayout->addWidget(m_mappings);
    auto *mappingButtons = new QHBoxLayout;
    auto *addMapping = new QPushButton(tr("Add mapping"), group);
    addMapping->setObjectName(u"addMapping"_s);
    auto *removeMapping = new QPushButton(tr("Remove mapping"), group);
    auto *browse = new QPushButton(tr("Choose local folder…"), group);
    mappingButtons->addWidget(addMapping);
    mappingButtons->addWidget(removeMapping);
    mappingButtons->addStretch();
    mappingButtons->addWidget(browse);
    mappingLayout->addLayout(mappingButtons);
    right->addWidget(group, 1);
    auto *storage = new QLabel(tr("Passwords and API keys are saved locally with Windows account encryption (DPAPI). Folder mappings only affect opening local files; server paths stay unchanged."), this);
    storage->setWordWrap(true);
    right->addWidget(storage);
    body->addLayout(right, 1);
    outer->addLayout(body, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Save)->setText(tr("Save"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    auto *connectButton = buttons->addButton(tr("Save and connect"), QDialogButtonBox::ActionRole);
    connectButton->setObjectName(u"saveAndConnectProfile"_s);
    outer->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, &ProfileEditor::addProfile);
    connect(remove, &QPushButton::clicked, this, &ProfileEditor::removeProfile);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row)
    {
        if (m_loading) return;
        storeCurrent();
        loadCurrent(row);
    });
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &name)
    {
        if (!m_loading && m_list->currentItem()) m_list->currentItem()->setText(name);
    });
    connect(m_auth, &QComboBox::currentIndexChanged, this, &ProfileEditor::updateAuthentication);
    connect(addMapping, &QPushButton::clicked, this, [this]
    {
        if (m_index < 0) return;
        const int row = m_mappings->rowCount();
        m_mappings->insertRow(row);
        m_mappings->setItem(row, 0, new QTableWidgetItem);
        m_mappings->setItem(row, 1, new QTableWidgetItem);
        m_mappings->setCurrentCell(row, 0);
        m_mappings->editItem(m_mappings->item(row, 0));
    });
    connect(removeMapping, &QPushButton::clicked, this, [this]
    {
        if (m_mappings->currentRow() >= 0) m_mappings->removeRow(m_mappings->currentRow());
    });
    connect(browse, &QPushButton::clicked, this, [this]
    {
        const int row = m_mappings->currentRow();
        if (row < 0) return;
        const QString current = m_mappings->item(row, 1) ? m_mappings->item(row, 1)->text() : QString();
        const QString path = QFileDialog::getExistingDirectory(this, tr("Local folder"), current);
        if (!path.isEmpty()) m_mappings->setItem(row, 1, new QTableWidgetItem(path));
    });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { finish(false); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(connectButton, &QPushButton::clicked, this, [this] { finish(true); });
    if (m_profiles.isEmpty()) m_profiles.append(ProfileStore::create());
    int selected = 0;
    for (int i = 0; i < m_profiles.size(); ++i) if (m_profiles[i].id == selectedId) selected = i;
    rebuildList(selected);
}

void ProfileEditor::updateAuthentication()
{
    const bool enabled = m_index >= 0;
    const bool api = m_auth->currentIndex() == 1;
    m_username->setEnabled(enabled && !api);
    m_password->setEnabled(enabled && !api);
    m_key->setEnabled(enabled && api);
}

void ProfileEditor::storeCurrent()
{
    if (m_index < 0 || m_index >= m_profiles.size()) return;
    auto &profile = m_profiles[m_index];
    profile.name = m_name->text().trimmed();
    profile.url = QUrl(m_url->text().trimmed());
    profile.apiKeyAuthentication = m_auth->currentIndex() == 1;
    profile.username = m_username->text();
    if (profile.secretsReadable || !m_password->text().isEmpty() || !m_key->text().isEmpty())
    {
        profile.password = m_password->text();
        profile.apiKey = m_key->text().trimmed();
        profile.secretsReadable = true;
        profile.secretError.clear();
    }
    profile.mappings.clear();
    for (int row = 0; row < m_mappings->rowCount(); ++row)
    {
        const auto text = [this, row](int column) { return m_mappings->item(row, column) ? m_mappings->item(row, column)->text().trimmed() : QString(); };
        const QString remote = text(0), local = text(1);
        if (!remote.isEmpty() || !local.isEmpty()) profile.mappings.append({remote, local});
    }
}

void ProfileEditor::loadCurrent(int row)
{
    m_loading = true;
    m_index = row;
    const bool enabled = row >= 0 && row < m_profiles.size();
    const auto profile = enabled ? m_profiles[row] : Profile();
    m_name->setText(profile.name);
    m_url->setText(profile.url.toString());
    m_auth->setCurrentIndex(profile.apiKeyAuthentication ? 1 : 0);
    m_username->setText(profile.username);
    m_password->setText(profile.password);
    m_key->setText(profile.apiKey);
    m_secretError->setText(profile.secretError);
    m_secretError->setVisible(!profile.secretError.isEmpty());
    m_name->setEnabled(enabled);
    m_url->setEnabled(enabled);
    m_auth->setEnabled(enabled);
    m_mappings->setEnabled(enabled);
    m_mappings->setRowCount(profile.mappings.size());
    for (int i = 0; i < profile.mappings.size(); ++i)
    {
        m_mappings->setItem(i, 0, new QTableWidgetItem(profile.mappings[i].remote));
        m_mappings->setItem(i, 1, new QTableWidgetItem(profile.mappings[i].local));
    }
    updateAuthentication();
    m_loading = false;
}

void ProfileEditor::rebuildList(int selected)
{
    m_loading = true;
    m_list->clear();
    for (const auto &profile : m_profiles) m_list->addItem(profile.name);
    m_list->setCurrentRow(selected);
    m_loading = false;
    loadCurrent(selected);
}

void ProfileEditor::addProfile()
{
    storeCurrent();
    m_profiles.append(ProfileStore::create());
    rebuildList(m_profiles.size() - 1);
    m_name->setFocus();
    m_name->selectAll();
}

void ProfileEditor::removeProfile()
{
    if (m_index < 0) return;
    const int previous = m_index;
    m_profiles.removeAt(m_index);
    m_index = -1;
    rebuildList(m_profiles.isEmpty() ? -1 : qMin(previous, int(m_profiles.size() - 1)));
}

QString ProfileEditor::selectedId() const
{
    return m_index >= 0 && m_index < m_profiles.size() ? m_profiles[m_index].id : QString();
}

bool ProfileEditor::connectRequested() const { return m_connectRequested; }

void ProfileEditor::finish(bool connect)
{
    storeCurrent();
    for (int i = 0; i < m_profiles.size(); ++i)
    {
        const QString error = ProfileStore::validate(m_profiles[i]);
        if (!error.isEmpty())
        {
            rebuildList(i);
            QMessageBox::warning(this, tr("Invalid profile"), error);
            return;
        }
    }
    if (connect && m_index < 0) return;
    QString error;
    if (!ProfileStore::save(m_profiles, &error))
    {
        QMessageBox::warning(this, tr("Cannot save profiles"), error);
        return;
    }
    m_connectRequested = connect;
    accept();
}

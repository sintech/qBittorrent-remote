// SPDX-License-Identifier: GPL-2.0-or-later
#include "settingsdialog.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTabWidget>
#include <QVBoxLayout>
#include "base/uidefaults.h"
#include "gui/fspathedit.h"
using namespace Qt::StringLiterals;
using namespace Remote;
namespace
{
    QStringList appearance()
    {
        QSettings settings;
        return {settings.value(u"remote/language"_s,u"system"_s).toString(),
            settings.value(u"remote/style"_s,Defaults::GUI::styleName()).toString(),
            settings.value(u"remote/colorScheme"_s,u"system"_s).toString(),
            settings.value(u"remote/theme"_s).toString()};
    }
}
SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), m_originalAppearance(appearance())
{
    setObjectName(u"remoteSettingsDialog"_s);
    setWindowTitle(tr("Client settings"));
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs);
    const auto page = [tabs](const QString &name)
    {
        auto *widget = new QWidget(tabs);
        auto *form = new QFormLayout(widget);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        tabs->addTab(widget,name);
        return form;
    };
    QSettings settings;
    const auto check = [this, &settings](QFormLayout *form, const QString &label, const QString &key, bool fallback, bool inverse = false)
    {
        auto *box = new QCheckBox(label,this);
        box->setObjectName(key);
        box->setChecked(settings.value(key,fallback).toBool() != inverse);
        form->addRow(box);
        m_save.append([box,key,inverse](QSettings &target) { target.setValue(key,box->isChecked() != inverse); });
        return box;
    };
    const auto combo = [this, &settings](QFormLayout *form, const QString &label, const QString &key,
        const QList<QPair<QString,QString>> &items, const QString &fallback)
    {
        auto *box = new QComboBox(this); box->setObjectName(key);
        for (const auto &[title,value] : items) box->addItem(title,value);
        box->setCurrentIndex(qMax(0,box->findData(settings.value(key,fallback))));
        form->addRow(label,box);
        m_save.append([box,key](QSettings &target) { target.setValue(key,box->currentData()); });
        return box;
    };
    auto *ui = page(tr("Interface"));
    combo(ui,tr("Language:"),u"remote/language"_s,{{tr("System"),u"system"_s},{u"English"_s,u"en"_s},{u"Русский"_s,u"ru"_s}},u"system"_s);
    QList<QPair<QString,QString>> styles {{tr("System"),u"system"_s}};
    for (const auto &style : QStyleFactory::keys()) styles.append({style,style});
    auto *style = combo(ui,tr("Widget style:"),u"remote/style"_s,styles,Defaults::GUI::styleName());
    // Existing configurations use a lower-case style name on some platforms.
    for (int i = 0; i < style->count(); ++i) if (style->itemData(i).toString().compare(settings.value(u"remote/style"_s,Defaults::GUI::styleName()).toString(),Qt::CaseInsensitive)==0) style->setCurrentIndex(i);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    combo(ui,tr("Color scheme:"),u"remote/colorScheme"_s,{{tr("System"),u"system"_s},{tr("Light"),u"light"_s},{tr("Dark"),u"dark"_s}},u"system"_s);
#endif
    m_theme = new FileSystemPathLineEdit(this); m_theme->setObjectName(u"remote/theme"_s);
    m_theme->setMode(FileSystemPathEdit::Mode::FileOpen);
    m_theme->setFileNameFilter(u"qBittorrent UI theme (*.qbtheme)"_s);
    m_theme->setPathText(settings.value(u"remote/theme"_s).toString());
    ui->addRow(tr("Custom UI theme (.qbtheme):"),m_theme);
    auto *note = new QLabel(tr("Leave the theme path empty for the built-in theme. Language and appearance changes take effect after restarting the client."),this);
    note->setWordWrap(true);ui->addRow(note);
    auto *adding = page(tr("Adding torrents"));
    check(adding,tr("Show torrent files and options before adding"),u"remote/addDialog/skip"_s,false,true);
    auto *serverNote = new QLabel(tr("Download folders, categories, limits and network settings belong to the server. Open Tools > Server Web UI to change server settings."),this);
    serverNote->setWordWrap(true);adding->addRow(serverNote);
    auto *list = page(tr("Transfer list"));
    combo(list,tr("Double-click a torrent:"),u"remote/list/doubleClick"_s,
        {{tr("Do nothing"),u"none"_s},{tr("Start / stop"),u"toggle"_s},{tr("Open destination folder"),u"folder"_s},{tr("Open server Web UI"),u"webui"_s}},u"none"_s);
    check(list,tr("Alternate row colors"),u"remote/list/alternateRows"_s,false);
    check(list,tr("Confirm torrent removal"),u"remote/list/confirmRemoval"_s,true);
    auto *desktop = page(tr("Desktop"));
    check(desktop,tr("Connect to the last selected profile on startup"),u"remote/desktop/autoConnect"_s,true);
    check(desktop,tr("Start minimized"),u"remote/desktop/startMinimized"_s,false);
    check(desktop,tr("Keep window on top"),u"remote/desktop/alwaysOnTop"_s,false);
    auto *tray = check(desktop,tr("Show icon in the notification area"),u"remote/desktop/tray"_s,false);
    auto *closeTray = check(desktop,tr("Close window to the notification area"),u"remote/desktop/closeToTray"_s,false);
    auto *minimizeTray = check(desktop,tr("Minimize to the notification area"),u"remote/desktop/minimizeToTray"_s,false);
    for (auto *box : {closeTray,minimizeTray}) { box->setEnabled(tray->isChecked()); connect(tray,&QCheckBox::toggled,box,&QWidget::setEnabled); }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
    connect(buttons,&QDialogButtonBox::accepted,this,&SettingsDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    layout->addWidget(buttons);
    resize(650,370);
}
void SettingsDialog::accept()
{
    const auto theme = m_theme->pathText().trimmed();
    if (!theme.isEmpty() && (!QFileInfo(theme).isFile() || !theme.endsWith(u".qbtheme",Qt::CaseInsensitive)))
    { QMessageBox::warning(this,tr("Client settings"),tr("Choose an existing .qbtheme file, or clear the theme path.")); return; }
    QSettings settings;
    for (const auto &save : m_save) save(settings);
    settings.setValue(u"remote/theme"_s,theme);
    m_restartRequired = appearance() != m_originalAppearance;
    QDialog::accept();
}
bool SettingsDialog::restartRequired() const { return m_restartRequired; }
void SettingsDialog::applyStartupAppearance()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    const auto scheme = QSettings().value(u"remote/colorScheme"_s,u"system"_s).toString();
    qApp->styleHints()->setColorScheme(scheme == u"dark" ? Qt::ColorScheme::Dark : scheme == u"light" ? Qt::ColorScheme::Light : Qt::ColorScheme::Unknown);
#endif
}

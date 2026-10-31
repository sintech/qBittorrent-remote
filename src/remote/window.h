// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include <QJsonObject>
#include "gui/mainwindow.h"
#include <QTimer>

#include "client.h"
#include "profiles.h"
#include "torrentmodel.h"

class StatusBar;
class QJsonDocument;
class QCloseEvent;
class QLineEdit;
class QTreeView;
class QComboBox;
class QTreeWidget;
class QLabel;
class QSplitter;
class QTabWidget;

namespace Ui { class MainWindow; }

namespace Remote
{
    class Properties;
    class Sidebar;
    class Window final : public MainWindow
    {
        Q_OBJECT

    public:
        explicit Window(QWidget *parent = nullptr, QNetworkAccessManager *network = nullptr);
        ~Window() override;
        Client *client();
        bool connectProfile(const QString &id, QString *error = nullptr);

    public slots:
        void showConnectionDialog();
        void showProfilesDialog();
        void openRemotePath(const QString &path);

    protected:
        void closeEvent(QCloseEvent *event) override;

    private:
        QStringList selectedHashes() const;
        void applySync(const QJsonObject &sync);
        void updateActions();
        void updateFilters();
        void refreshDetails();
        void runCommand(const QString &method, QUrlQuery parameters = {});
        void removeSelected();
        void addFiles();
        void addLinks();
        void showContextMenu(const QPoint &position);
        void editProfiles(bool offerConnection);
        bool reloadProfiles(QString *error = nullptr);
        void openSelectedFolder();

        QList<Profile> m_profiles;
        Profile m_activeProfile;
        Client m_client;
        TorrentModel m_model;
        TorrentFilter m_filter;
        TransferListWidget *m_table = nullptr;
        LineEdit *m_search = nullptr;
        Sidebar *m_sidebar = nullptr;
        QSplitter *m_horizontal = nullptr;
        QSplitter *m_vertical = nullptr;
        Properties *m_properties = nullptr;
        QComboBox *m_filterColumn = nullptr;
        StatusBar *m_nativeStatus = nullptr;
        QTimer m_detailsTimer;
        QJsonObject m_categories;
        QSet<QString> m_tags;
        QJsonObject m_serverState;
        quint64 m_detailGeneration = 0;
    };
}

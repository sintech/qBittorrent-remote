// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <memory>

#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>

#include "client.h"
#include "torrentmodel.h"

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
    class Window final : public QMainWindow
    {
        Q_OBJECT

    public:
        explicit Window(QWidget *parent = nullptr, QNetworkAccessManager *network = nullptr);
        ~Window() override;
        Client *client();

    public slots:
        void showConnectionDialog();

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

        std::unique_ptr<Ui::MainWindow> m_ui;
        Client m_client;
        TorrentModel m_model;
        TorrentFilter m_filter;
        QTreeView *m_table = nullptr;
        QLineEdit *m_search = nullptr;
        Sidebar *m_sidebar = nullptr;
        QSplitter *m_horizontal = nullptr;
        QSplitter *m_vertical = nullptr;
        Properties *m_properties = nullptr;
        QComboBox *m_filterColumn = nullptr;
        QLabel *m_status = nullptr;
        QLabel *m_rates = nullptr;
        QLabel *m_uploadRate = nullptr;
        QLabel *m_freeSpace = nullptr;
        QLabel *m_dht = nullptr;
        QTimer m_detailsTimer;
        QJsonObject m_categories;
        QSet<QString> m_tags;
        QJsonObject m_serverState;
        quint64 m_detailGeneration = 0;
    };
}

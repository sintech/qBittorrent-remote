// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDialog>
#include "profiles.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;

namespace Remote
{
    class ProfileEditor final : public QDialog
    {
        Q_OBJECT
    public:
        ProfileEditor(QList<Profile> profiles, const QString &selectedId, QWidget *parent = nullptr);
        QString selectedId() const;
        bool connectRequested() const;

    private:
        void rebuildList(int selected);
        void storeCurrent();
        void loadCurrent(int row);
        void addProfile();
        void removeProfile();
        void finish(bool connect);
        void updateAuthentication();

        QList<Profile> m_profiles;
        int m_index = -1;
        bool m_loading = false;
        bool m_connectRequested = false;
        QListWidget *m_list = nullptr;
        QLineEdit *m_name = nullptr;
        QLineEdit *m_url = nullptr;
        QComboBox *m_auth = nullptr;
        QLineEdit *m_username = nullptr;
        QLineEdit *m_password = nullptr;
        QLineEdit *m_key = nullptr;
        QLabel *m_secretError = nullptr;
        QTableWidget *m_mappings = nullptr;
    };
}

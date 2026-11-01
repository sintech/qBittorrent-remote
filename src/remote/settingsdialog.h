// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <functional>
#include <QDialog>
#include <QList>
class QSettings;
class FileSystemPathLineEdit;
namespace Remote
{
    class SettingsDialog final : public QDialog
    {
        Q_OBJECT
    public:
        explicit SettingsDialog(QWidget *parent = nullptr);
        bool restartRequired() const;
        static void applyStartupAppearance();
        void accept() override;
    private:
        QList<std::function<void(QSettings &)>> m_save;
        FileSystemPathLineEdit *m_theme = nullptr;
        QStringList m_originalAppearance;
        bool m_restartRequired = false;
    };
}

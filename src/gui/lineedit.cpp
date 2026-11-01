/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2023  Vladimir Golovnev <glassez@yandex.ru>
 * Copyright (c) 2007  Trolltech ASA <info@trolltech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * In addition, as a special exception, the copyright holders give permission to
 * link this program with the OpenSSL project's "OpenSSL" library (or with
 * modified versions of it that use the same license as the "OpenSSL" library),
 * and distribute the linked executables. You must obey the GNU General Public
 * License in all respects for all of the code used other than "OpenSSL".  If you
 * modify file(s), you may extend this exception to your version of the file(s),
 * but you are not obligated to do so. If you do not wish to do so, delete this
 * exception statement from your version.
 */

#include "lineedit.h"
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QSettings>
#include <QMenu>
#include "filterpatternformatmenu.h"

#include <chrono>

#include <QAction>
#include <QKeyEvent>
#include <QTimer>

#include "base/global.h"
#include "uithememanager.h"

using namespace std::chrono_literals;

namespace
{
    const std::chrono::milliseconds FILTER_INPUT_DELAY {400};
}

LineEdit::LineEdit(QWidget *parent)
    : QLineEdit(parent)
    , m_delayedTextChangedTimer {new QTimer(this)}
{
    const auto icon = UIThemeManager::instance()->getIcon(u"edit-find"_s);
    auto *action = new QAction(icon, QString(), this);
    addAction(action, QLineEdit::LeadingPosition);

    setClearButtonEnabled(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_delayedTextChangedTimer->setSingleShot(true);
    connect(m_delayedTextChangedTimer, &QTimer::timeout, this, [this]
    {
        emit textUpdated(text());
    });
    connect(this, &QLineEdit::textChanged, this, [this]
    {
        m_delayedTextChangedTimer->start(FILTER_INPUT_DELAY);
    });
}

void LineEdit::keyPressEvent(QKeyEvent *event)
{
    if ((event->modifiers() == Qt::NoModifier) && (event->key() == Qt::Key_Escape))
    {
        clear();
    }

    QLineEdit::keyPressEvent(event);
}

void LineEdit::installPatternMenu(QSortFilterProxyModel *filter, const QString &settingsKey)
{
    setContextMenuPolicy(Qt::CustomContextMenu);
    const auto apply = [this, filter, settingsKey]
    {
        filter->setRecursiveFilteringEnabled(true);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        const auto format = static_cast<FilterPatternFormat>(QSettings().value(settingsKey, int(FilterPatternFormat::PlainText)).toInt());
        if (format == FilterPatternFormat::Regex) filter->setFilterRegularExpression(QRegularExpression(text(), QRegularExpression::CaseInsensitiveOption));
        else if (format == FilterPatternFormat::Wildcards) filter->setFilterRegularExpression(QRegularExpression(QRegularExpression::wildcardToRegularExpression(text(), QRegularExpression::UnanchoredWildcardConversion), QRegularExpression::CaseInsensitiveOption));
        else filter->setFilterFixedString(text());
    };
    connect(this, &LineEdit::textUpdated, filter, apply);
    connect(this, &QWidget::customContextMenuRequested, this, [this, settingsKey, apply](const QPoint &point)
    {
        auto *menu = createStandardContextMenu();
        menu->setAttribute(Qt::WA_DeleteOnClose);
        auto *formats = new FilterPatternFormatMenu(static_cast<FilterPatternFormat>(QSettings().value(settingsKey, int(FilterPatternFormat::PlainText)).toInt()), menu);
        connect(formats, &FilterPatternFormatMenu::patternFormatChanged, this, [settingsKey, apply](FilterPatternFormat format)
        { QSettings().setValue(settingsKey, int(format)); apply(); });
        menu->addSeparator(); menu->addMenu(formats); menu->popup(mapToGlobal(point));
    });
    apply();
}

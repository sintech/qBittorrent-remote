/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
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

#include "misc.h"
#include "string.h"
#include <cmath>

#include <QCoreApplication>
#include <QString>
#include <QLocale>
#include <optional>

namespace
{
    const struct { const char *source; const char *comment; } units[] =
    {
        QT_TRANSLATE_NOOP3("misc", "B", "bytes"),
        QT_TRANSLATE_NOOP3("misc", "KiB", "kibibytes (1024 bytes)"),
        QT_TRANSLATE_NOOP3("misc", "MiB", "mebibytes (1024 kibibytes)"),
        QT_TRANSLATE_NOOP3("misc", "GiB", "gibibytes (1024 mibibytes)"),
        QT_TRANSLATE_NOOP3("misc", "TiB", "tebibytes (1024 gibibytes)"),
        QT_TRANSLATE_NOOP3("misc", "PiB", "pebibytes (1024 tebibytes)"),
        QT_TRANSLATE_NOOP3("misc", "EiB", "exbibytes (1024 pebibytes)")
    };

}

QString Utils::Misc::unitString(const SizeUnit unit, const bool isSpeed)
{
    const auto &unitString = units[static_cast<int>(unit)];
    QString ret = QCoreApplication::translate("misc", unitString.source, unitString.comment);
    if (isSpeed)
        ret += QCoreApplication::translate("misc", "/s", "per second");
    return ret;
}

int Utils::Misc::friendlyUnitPrecision(const SizeUnit unit)
{
    // friendlyUnit's number of digits after the decimal point
    switch (unit)
    {
    case SizeUnit::Byte:
        return 0;
    case SizeUnit::KibiByte:
    case SizeUnit::MebiByte:
        return 1;
    case SizeUnit::GibiByte:
        return 2;
    default:
        return 3;
    }
}

qlonglong Utils::Misc::sizeInBytes(qreal size, const Utils::Misc::SizeUnit unit)
{
    for (int i = 0; i < static_cast<int>(unit); ++i)
        size *= 1024;
    return size;
}

namespace
{
    // return best userfriendly storage unit (B, KiB, MiB, GiB, TiB, ...)
    // use Binary prefix standards from IEC 60027-2
    // see http://en.wikipedia.org/wiki/Kilobyte
    // value must be given in bytes
    // to send numbers instead of strings with suffixes
    struct SplitToFriendlyUnitResult
    {
        qreal value;
        Utils::Misc::SizeUnit unit;
    };

    std::optional<SplitToFriendlyUnitResult> splitToFriendlyUnit(const qint64 bytes, const int unitThreshold = 1024)
    {
        if (bytes < 0)
            return std::nullopt;

        int i = 0;
        auto value = static_cast<qreal>(bytes);

        while ((value >= unitThreshold) && (i < static_cast<int>(Utils::Misc::SizeUnit::ExbiByte)))
        {
            value /= 1024;
            ++i;
        }
        return {{value, static_cast<Utils::Misc::SizeUnit>(i)}};
    }
}

QString Utils::Misc::friendlyUnit(const qint64 bytes, const bool isSpeed, const int precision)
{
    const std::optional<SplitToFriendlyUnitResult> result = splitToFriendlyUnit(bytes);
    if (!result)
        return QCoreApplication::translate("misc", "Unknown", "Unknown (size)");

    const int digitPrecision = (precision >= 0) ? precision : friendlyUnitPrecision(result->unit);
    return Utils::String::fromDouble(result->value, digitPrecision)
           + QChar::Nbsp + unitString(result->unit, isSpeed);
}

QString Utils::Misc::friendlyUnitCompact(const qint64 bytes)
{
    // avoid 1000-1023 values, use next larger unit instead
    const std::optional<SplitToFriendlyUnitResult> result = splitToFriendlyUnit(bytes, 1000);
    if (!result)
        return QCoreApplication::translate("misc", "Unknown", "Unknown (size)");

    int precision = 0;          // >= 100
    if (result->value < 10)
        precision = 2;          // 0 - 9.99
    else if (result->value < 100)
        precision = 1;          // 10 - 99.9

    return Utils::String::fromDouble(result->value, precision)
           // use only one character for unit representation
           + QChar::Nbsp + unitString(result->unit, false)[0];
}

QString Utils::String::fromDouble(const double n, const int precision)
{
    /* HACK because QString rounds up. Eg QString::number(0.999*100.0, 'f', 1) == 99.9
    ** but QString::number(0.9999*100.0, 'f' ,1) == 100.0 The problem manifests when
    ** the number has more digits after the decimal than we want AND the digit after
    ** our 'wanted' is >= 5. In this case our last digit gets rounded up. So for each
    ** precision we add an extra 0 behind 1 in the below algorithm. */

    const double prec = std::pow(10.0, precision);
    return QLocale::system().toString(std::floor(n * prec) / prec, 'f', precision);
}

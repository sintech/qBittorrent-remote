// SPDX-License-Identifier: GPL-2.0-or-later
#include "torrentmetadata.h"
#include <QCryptographicHash>
#include <QFile>
#include <QVariant>
#include <QVariantMap>
#include <stdexcept>
#include <functional>
#include <limits>
using namespace Qt::StringLiterals;
namespace
{
    class Bencode
    {
    public:
        QByteArray data, info;
        qsizetype position = 0;
        int nodes = 0;
        [[noreturn]] void fail() const { throw std::runtime_error("Invalid torrent metadata"); }
        QVariant read(int depth = 0)
        {
            if (depth > 64 || ++nodes > 1000000 || position >= data.size()) fail();
            const char token = data[position++];
            if (token == 'd')
            {
                QVariantMap result;
                while (position < data.size() && data[position] != 'e')
                {
                    const auto keyValue = read(depth + 1);
                    if (keyValue.metaType().id() != QMetaType::QByteArray) fail();
                    const auto key = QString::fromUtf8(keyValue.toByteArray());
                    if (result.contains(key)) fail();
                    const auto begin = position;
                    result.insert(key, read(depth + 1));
                    if (depth == 0 && key == u"info") info = data.mid(begin, position - begin);
                }
                if (position >= data.size()) fail();
                ++position; return result;
            }
            if (token == 'l')
            {
                QVariantList result;
                while (position < data.size() && data[position] != 'e') result.append(read(depth + 1));
                if (position >= data.size()) fail();
                ++position; return result;
            }
            if (token == 'i')
            {
                const auto end = data.indexOf('e', position);
                if (end < position || end - position > 20) fail();
                bool ok = false; const auto value = data.mid(position, end-position).toLongLong(&ok);
                if (!ok) fail();
                position = end + 1; return value;
            }
            if (token < '0' || token > '9') fail();
            --position;
            const auto colon = data.indexOf(':', position);
            if (colon < position || colon - position > 10) fail();
            bool ok = false; const auto length = data.mid(position, colon-position).toLongLong(&ok);
            if (!ok || length < 0 || length > data.size() - colon - 1) fail();
            position = colon + 1 + length; return data.mid(colon + 1, length);
        }
    };
    bool componentValid(const QString &name)
    { return !name.isEmpty() && name != u"." && name != u".." && !name.contains(u'/') && !name.contains(u'\\') && !name.contains(QChar(0)); }
}
Remote::TorrentMetadata Remote::readTorrentMetadata(const QString &path)
{
    TorrentMetadata result;
    try
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024) throw std::runtime_error("Cannot read torrent file");
        Bencode parser; parser.data = file.readAll();
        const auto root = parser.read().toMap();
        if (parser.position != parser.data.size() || parser.info.isEmpty()) parser.fail();
        const auto info = root.value(u"info"_s).toMap();
        result.isPrivate = info.value(u"private"_s).toInt() == 1;
        const auto tiers = root.value(u"announce-list"_s).toList();
        for (const auto &tier : tiers)
        {
            for (const auto &tracker : tier.toList())
                result.trackers.append(QString::fromUtf8(tracker.toByteArray()));
        }
        result.trackers.append(QString::fromUtf8(root.value(u"announce"_s).toByteArray()));
        result.name = QString::fromUtf8(info.value(u"name.utf-8"_s, info.value(u"name"_s)).toByteArray());
        if (!componentValid(result.name)) parser.fail();
        result.comment = QString::fromUtf8(root.value(u"comment.utf-8"_s, root.value(u"comment"_s)).toByteArray());
        if (root.contains(u"creation date"_s)) result.created = QDateTime::fromSecsSinceEpoch(root.value(u"creation date"_s).toLongLong());
        qint64 totalSize = 0;
        const auto add = [&](const QString &name, const QVariantMap &entry)
        {
            bool ok = false; const auto size = entry.value(u"length"_s).toLongLong(&ok);
            if (!ok || size < 0) parser.fail();
            // qBittorrent exposes logical file indices with padding files removed.
            if (entry.value(u"attr"_s).toByteArray().contains('p')) return;
            if (size > std::numeric_limits<qint64>::max() - totalSize) parser.fail();
            totalSize += size;
            result.files.append(name); result.sizes.append(size);
        };
        if (info.contains(u"pieces"_s))
        {
            result.hashV1 = QString::fromLatin1(QCryptographicHash::hash(parser.info, QCryptographicHash::Sha1).toHex());
            const auto files = info.value(u"files"_s).toList();
            if (files.isEmpty()) add(result.name, info);
            for (const auto &item : files)
            {
                const auto entry = item.toMap(); QStringList parts {result.name};
                for (const auto &value : entry.value(u"path.utf-8"_s, entry.value(u"path"_s)).toList())
                { const auto name = QString::fromUtf8(value.toByteArray()); if (!componentValid(name)) parser.fail(); parts.append(name); }
                if (parts.size() < 2) parser.fail();
                add(parts.join(u'/'), entry);
            }
        }
        if (info.value(u"meta version"_s).toInt() == 2)
        {
            result.hashV2 = QString::fromLatin1(QCryptographicHash::hash(parser.info, QCryptographicHash::Sha256).toHex());
            if (result.files.isEmpty())
            {
                const std::function<void(const QVariantMap &, const QString &, int)> walk = [&](const QVariantMap &tree, const QString &prefix, int depth)
                {
                    if (depth > 64) parser.fail();
                    for (auto it = tree.cbegin(); it != tree.cend(); ++it)
                    {
                        if (it.key().isEmpty()) { add(prefix, it.value().toMap()); continue; }
                        if (!componentValid(it.key())) parser.fail();
                        walk(it.value().toMap(), prefix.isEmpty() ? it.key() : prefix + u'/' + it.key(), depth + 1);
                    }
                };
                const auto tree = info.value(u"file tree"_s).toMap();
                // A single-file v2 tree already contains its file name.
                walk(tree, tree.size() == 1 && tree.first().toMap().contains(QString()) ? QString() : result.name, 0);
            }
        }
        if (result.files.isEmpty()) parser.fail();
    }
    catch (const std::exception &error) { result = {}; result.error = QString::fromUtf8(error.what()); }
    return result;
}

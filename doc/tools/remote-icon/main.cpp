// SPDX-License-Identifier: GPL-2.0-or-later
// Render the approved SVG with Qt and package PNG frames into a Windows ICO.
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

int main(int argc,char **argv)
{
    QGuiApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=3) return 1; // SVG source, ICO destination
    QSvgRenderer renderer(args[1]);
    if(!renderer.isValid()) return 2;
    const QList<int> sizes {16,20,24,32,40,48,64,128,256};
    QList<QByteArray> frames;
    for(const int size:sizes)
    {
        QImage image(size,size,QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        { QPainter painter(&image);renderer.render(&painter); }
        QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG")) return 3;
        frames.append(bytes);
    }
    QFile file(args[2]);if(!file.open(QIODevice::WriteOnly)) return 4;
    QDataStream out(&file);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint16(0)<<quint16(1)<<quint16(sizes.size());
    quint32 offset=6+16*sizes.size();
    for(int i=0;i<sizes.size();++i)
    {
        const quint8 size=sizes[i]==256?0:sizes[i];
        out<<size<<size<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)
           <<quint32(frames[i].size())<<offset;
        offset+=frames[i].size();
    }
    for(const auto &frame:frames) out.writeRawData(frame.constData(),frame.size());
    return out.status()==QDataStream::Ok?0:5;
}

#include "mh3g_model.hpp"
#include "game_resource_manager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtEndian>

#include <cstring>
#include <iostream>

template<typename T> void put(QByteArray &data, int offset, T value)
{
    if (data.size() < offset + int(sizeof(T))) data.resize(offset + int(sizeof(T)));
    qToLittleEndian<T>(value, reinterpret_cast<uchar *>(data.data() + offset));
}

void putFloat(QByteArray &data, int offset, float value)
{
    quint32 bits = 0; std::memcpy(&bits, &value, sizeof(bits)); put(data, offset, bits);
}

QByteArray fixtureMod()
{
    QByteArray data(206, '\0'); std::memcpy(data.data(), "MOD\0", 4);
    put<quint16>(data, 4, 0xE6); put<quint16>(data, 8, 1); put<quint32>(data, 12, 3);
    put<quint32>(data, 0x34, 64); put<quint32>(data, 0x38, 116); put<quint32>(data, 0x3c, 200);
    put<quint16>(data, 66, 3); data[74] = char(28); put<quint32>(data, 76, 0);
    put<quint32>(data, 80, 0); put<quint32>(data, 88, 0); put<quint32>(data, 92, 3);
    const float positions[3][3] = {{-1,0,0},{1,0,0},{0,1,0}};
    for (int vertex = 0; vertex < 3; ++vertex)
    {
        const int base = 116 + vertex * 28;
        putFloat(data, base, positions[vertex][0]); putFloat(data, base + 4, positions[vertex][1]);
        putFloat(data, base + 8, positions[vertex][2]); data[base + 14] = 127;
        putFloat(data, base + 16, vertex == 1 ? 1.0f : 0.0f); putFloat(data, base + 20, vertex == 2 ? 1.0f : 0.0f);
    }
    put<quint16>(data, 200, 0); put<quint16>(data, 202, 1); put<quint16>(data, 204, 2);
    return data;
}

QByteArray fixtureTex()
{
    QByteArray data(52, '\0'); std::memcpy(data.data(), "TEX\0", 4); put<quint16>(data, 4, 0xA5);
    put<quint32>(data, 8, 1U | (8U << 6) | (8U << 19)); data[13] = char(0x0b); put<quint32>(data, 16, 0);
    return data;
}

QByteArray fixtureArc()
{
    struct Entry { QByteArray name; quint32 hash; QByteArray data; };
    QList<Entry> entries;
    entries << Entry{"fixture/model",0x58a15856U,fixtureMod()}
            << Entry{"fixture/model_BM",0x241f5debU,fixtureTex()}
            << Entry{"fixture/model",0x2749c8a8U,QByteArray("MRL\0\x20\0\0\0\0\0\0\0\0\0\0\0",16)};
    QByteArray arc(12 + entries.size() * 80, '\0'); std::memcpy(arc.data(), "ARC\0", 4);
    put<quint16>(arc, 4, 0x10); put<quint16>(arc, 6, quint16(entries.size()));
    for (int index = 0; index < entries.size(); ++index)
    {
        const int base = 12 + index * 80;
        std::memcpy(arc.data() + base, entries[index].name.constData(), qMin(63, entries[index].name.size()));
        const QByteArray compressedWithSize = qCompress(entries[index].data, 9);
        const QByteArray compressed = compressedWithSize.mid(4);
        put<quint32>(arc, base + 64, entries[index].hash);
        put<quint32>(arc, base + 68, compressed.size());
        put<quint32>(arc, base + 72, 0x40000000U | quint32(entries[index].data.size()));
        put<quint32>(arc, base + 76, arc.size()); arc.append(compressed);
    }
    return arc;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QString error; Mh3gCpuModel model;
    if (!Mh3gModelLoader::parseMod(fixtureMod(), &model, &error) || model.vertices.size() != 3 || model.indices.size() != 3)
    { std::cerr << "synthetic MOD failed: " << error.toStdString() << '\n'; return 1; }
    QImage image;
    if (!Mh3gModelLoader::decodeTex(fixtureTex(), &image, &error) || image.size() != QSize(8, 8))
    { std::cerr << "synthetic TEX failed: " << error.toStdString() << '\n'; return 1; }
    const QString fixture = QDir::temp().filePath("mh3g-model-fixture.arc");
    QFile output(fixture); if (!output.open(QIODevice::WriteOnly) || output.write(fixtureArc()) < 0) return 1; output.close();
    if (!Mh3gArchiveLoader::validateWeaponArchive(fixture, &error))
    { std::cerr << "synthetic ARC failed: " << error.toStdString() << '\n'; return 1; }
    QFile::remove(fixture);
    QByteArray corruptArc = fixtureArc(); put<quint32>(corruptArc, 12 + 76, 0xffffffffU);
    output.setFileName(fixture); if (!output.open(QIODevice::WriteOnly) || output.write(corruptArc) < 0) return 1; output.close();
    QVector<Mh3gArchiveEntry> corruptEntries;
    if (Mh3gArchiveLoader::read(fixture, &corruptEntries, &error)) { std::cerr << "corrupt ARC accepted\n"; return 1; }
    QFile::remove(fixture);
    QByteArray corrupt = fixtureMod(); put<quint32>(corrupt, 0x38, 0xffffffffU);
    if (Mh3gModelLoader::parseMod(corrupt, &model, &error)) { std::cerr << "corrupt MOD accepted\n"; return 1; }

    if (app.arguments().size() > 1)
    {
        const QDir root(app.arguments().at(1));
        const QStringList folders = QStringList() << "w00" << "w01" << "w02" << "w03" << "w04" << "w06"
            << "w07" << "w08" << "w09" << "w10" << "w11" << "w12";
        int count = 0, failed = 0, environments = 0;
        for (const QString &folder : folders)
        {
            QDir directory(root.filePath(folder));
            for (const QString &file : directory.entryList(QStringList() << "*.arc", QDir::Files, QDir::Name))
            {
                ++count; const QString path = directory.filePath(file);
                QSharedPointer<Mh3gCpuModel> loaded = Mh3gModelLoader::load(QFileInfo(file).baseName(), path);
                if (!loaded->valid() || loaded->diffuse.isNull())
                {
                    ++failed; std::cerr << file.toStdString() << ": "
                        << (loaded->error.isEmpty() ? "missing primary texture" : loaded->error.toStdString()) << '\n';
                }
                if (!loaded->environment.isNull()) ++environments;
            }
        }
        std::cout << "archives=" << count << " failed=" << failed << " environments=" << environments << '\n';
        if (count != 558 || failed != 0 || environments != 462) return 1;

        GameResourceManager resources;
        resources.clearWeaponResources(&error);
        if (!resources.importWeaponResources(root.absolutePath(), &error) || !resources.available())
        { std::cerr << "resource import failed: " << error.toStdString() << '\n'; return 1; }
        if (resources.archivePath("w00/w00_01.arc").isEmpty())
        { std::cerr << "imported ARC lookup failed\n"; return 1; }
        if (!resources.clearWeaponResources(&error) || resources.available())
        { std::cerr << "resource clear failed: " << error.toStdString() << '\n'; return 1; }
        if (app.arguments().size() > 2)
        {
            const QSharedPointer<Mh3gCpuModel> sample = Mh3gModelLoader::load("w00_01", root.filePath("w00/w00_01.arc"));
            if (!sample->valid() || sample->diffuse.isNull() || !sample->diffuse.save(app.arguments().at(2)))
            { std::cerr << "texture preview export failed\n"; return 1; }
        }
    }
    std::cout << "mh3g model tests passed\n";
    return 0;
}

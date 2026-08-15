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
    put<quint16>(data, 4, 0xE6); put<quint16>(data, 8, 1); put<quint16>(data, 0x0a, 1); put<quint32>(data, 12, 3);
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

QByteArray fixtureMrl()
{
    QByteArray data(0xbc, '\0'); std::memcpy(data.data(), "MRL\0", 4);
    put<quint32>(data, 4, 0x20); put<quint32>(data, 8, 1); put<quint32>(data, 0x0c, 1);
    put<quint32>(data, 0x14, 0x1c); put<quint32>(data, 0x18, 0x68);
    put<quint32>(data, 0x1c, 0x241f5debU);
    const QByteArray textureName("fixture/model_BM");
    std::memcpy(data.data() + 0x28, textureName.constData(), textureName.size());
    put<quint32>(data, 0x6c, 0x12345678U);
    put<quint32>(data, 0x80, 2); // One command encoded in eight-byte units.
    put<quint32>(data, 0x9c, 0xa4);
    put<quint32>(data, 0xa4, 3); // Texture command.
    put<quint32>(data, 0xa8, 1); // One-based MRL texture reference.
    return data;
}

QByteArray fixtureArc()
{
    struct Entry { QByteArray name; quint32 hash; QByteArray data; };
    QList<Entry> entries;
    entries << Entry{"fixture/model",0x58a15856U,fixtureMod()}
            << Entry{"fixture/model_BM",0x241f5debU,fixtureTex()}
            << Entry{"fixture/model",0x2749c8a8U,fixtureMrl()};
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
    if (app.arguments().size() == 2 && app.arguments().at(1) == "--probe-bundled")
    {
        GameResourceManager resources;
        QString bundledError;
        if (!resources.available() || resources.archivePath("w00/w00_01.arc").isEmpty()
            || !resources.statusText().contains(QString::fromUtf8("整合包")))
        {
            bundledError = resources.statusText();
            std::cerr << "bundled resource lookup failed: " << bundledError.toStdString() << '\n';
            return 1;
        }
        std::cout << "bundled resources loaded directly\n";
        return 0;
    }
    QString error; Mh3gCpuModel model;
    if (!Mh3gModelLoader::parseMod(fixtureMod(), &model, &error) || model.vertices.size() != 3
        || model.indices.size() != 3 || model.drawCalls.size() != 1)
    { std::cerr << "synthetic MOD failed: " << error.toStdString() << '\n'; return 1; }
    QImage image;
    if (!Mh3gModelLoader::decodeTex(fixtureTex(), &image, &error) || image.size() != QSize(8, 8))
    { std::cerr << "synthetic TEX failed: " << error.toStdString() << '\n'; return 1; }
    const QString fixture = QDir::temp().filePath("mh3g-model-fixture.arc");
    QFile output(fixture); if (!output.open(QIODevice::WriteOnly) || output.write(fixtureArc()) < 0) return 1; output.close();
    if (!Mh3gArchiveLoader::validateWeaponArchive(fixture, &error))
    { std::cerr << "synthetic ARC failed: " << error.toStdString() << '\n'; return 1; }
    const QSharedPointer<Mh3gCpuModel> fixtureModel = Mh3gModelLoader::load("fixture", fixture);
    if (!fixtureModel->valid() || fixtureModel->materials.size() != 1
        || fixtureModel->materials.first().albedo.isNull() || fixtureModel->drawCalls.size() != 1)
    { std::cerr << "synthetic material binding failed: " << fixtureModel->error.toStdString() << '\n'; return 1; }
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
                bool hasAlbedo = false, hasEnvironment = false, usedFallbackMaterial = false;
                for (const Mh3gMaterial &material : loaded->materials)
                {
                    if (!material.albedo.isNull()) hasAlbedo = true;
                    if (!material.environment.isNull()) hasEnvironment = true;
                    if (material.name == QString::fromUtf8("默认材质")) usedFallbackMaterial = true;
                }
                bool materialRangesValid = true;
                for (const Mh3gDrawCall &draw : loaded->drawCalls)
                    if (draw.materialIndex < 0 || draw.materialIndex >= loaded->materials.size()
                        || draw.firstIndex < 0 || draw.indexCount <= 0
                        || draw.firstIndex + draw.indexCount > loaded->indices.size()) materialRangesValid = false;
                if (!loaded->valid() || !hasAlbedo || loaded->drawCalls.isEmpty()
                    || !materialRangesValid || usedFallbackMaterial)
                {
                    ++failed; std::cerr << file.toStdString() << ": "
                        << (loaded->error.isEmpty() ? "missing primary texture" : loaded->error.toStdString()) << '\n';
                }
                if (hasEnvironment) ++environments;
            }
        }
        std::cout << "archives=" << count << " failed=" << failed << " environments=" << environments << '\n';
        if (count != 558 || failed != 0 || environments != 462) return 1;

        if (app.arguments().size() > 2)
        {
            const QSharedPointer<Mh3gCpuModel> sample = Mh3gModelLoader::load("w00_01", root.filePath("w00/w00_01.arc"));
            if (!sample->valid() || sample->materials.isEmpty() || sample->materials.first().albedo.isNull()
                || !sample->materials.first().albedo.save(app.arguments().at(2)))
            { std::cerr << "texture preview export failed\n"; return 1; }
        }
    }
    std::cout << "mh3g model tests passed\n";
    return 0;
}

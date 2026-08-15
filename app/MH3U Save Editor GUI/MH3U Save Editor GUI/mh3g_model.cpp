#include "mh3g_model.hpp"

#include <QFile>
#include <QtEndian>
#include <QtMath>

#include <cstring>
#include <limits>

namespace
{
const quint32 HashTex = 0x241f5debU;
const quint32 HashMrl = 0x2749c8a8U;
const quint32 HashMod = 0x58a15856U;

template<typename T> bool readLe(const QByteArray &data, int offset, T *value)
{
    if (!value || offset < 0 || offset > data.size() - int(sizeof(T))) return false;
    *value = qFromLittleEndian<T>(reinterpret_cast<const uchar *>(data.constData() + offset));
    return true;
}

bool rangeOk(qint64 offset, qint64 count, qint64 size)
{
    return offset >= 0 && count >= 0 && offset <= size && count <= size - offset;
}

float readFloat(const QByteArray &data, int offset, bool *ok)
{
    quint32 bits = 0;
    if (!readLe(data, offset, &bits)) { *ok = false; return 0.0f; }
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if (!qIsFinite(value)) { *ok = false; return 0.0f; }
    return value;
}

int clampByte(int value) { return qBound(0, value, 255); }
int extend3(int value) { return (value & 4) ? value - 8 : value; }

QRgb etcPixel(quint64 block, int x, int y, int alpha)
{
    static const int modifiers[8][4] = {
        {2,8,-2,-8},{5,17,-5,-17},{9,29,-9,-29},{13,42,-13,-42},
        {18,60,-18,-60},{24,80,-24,-80},{33,106,-33,-106},{47,183,-47,-183}
    };
    const bool differential = ((block >> 33) & 1U) != 0;
    const bool flip = ((block >> 32) & 1U) != 0;
    int r1, g1, b1, r2, g2, b2;
    if (differential)
    {
        const int rb = int((block >> 59) & 31U);
        const int gb = int((block >> 51) & 31U);
        const int bb = int((block >> 43) & 31U);
        r1 = (rb << 3) | (rb >> 2); g1 = (gb << 3) | (gb >> 2); b1 = (bb << 3) | (bb >> 2);
        const int rd = qBound(0, rb + extend3(int((block >> 56) & 7U)), 31);
        const int gd = qBound(0, gb + extend3(int((block >> 48) & 7U)), 31);
        const int bd = qBound(0, bb + extend3(int((block >> 40) & 7U)), 31);
        r2 = (rd << 3) | (rd >> 2); g2 = (gd << 3) | (gd >> 2); b2 = (bd << 3) | (bd >> 2);
    }
    else
    {
        r1 = int((block >> 60) & 15U) * 17; r2 = int((block >> 56) & 15U) * 17;
        g1 = int((block >> 52) & 15U) * 17; g2 = int((block >> 48) & 15U) * 17;
        b1 = int((block >> 44) & 15U) * 17; b2 = int((block >> 40) & 15U) * 17;
    }
    const bool second = flip ? y >= 2 : x >= 2;
    const int table = int((block >> (second ? 34 : 37)) & 7U);
    const int bit = x * 4 + y;
    const int selector = int(((block >> bit) & 1U) | (((block >> (bit + 16)) & 1U) << 1));
    const int delta = modifiers[table][selector];
    return qRgba(clampByte((second ? r2 : r1) + delta), clampByte((second ? g2 : g1) + delta),
                 clampByte((second ? b2 : b1) + delta), alpha);
}

}

qint64 Mh3gCpuModel::memoryBytes() const
{
    return qint64(vertices.size()) * qint64(sizeof(Mh3gVertex))
        + qint64(indices.size()) * qint64(sizeof(quint32)) + diffuse.sizeInBytes() + environment.sizeInBytes();
}

bool Mh3gArchiveLoader::read(const QString &path, QVector<Mh3gArchiveEntry> *entries, QString *error)
{
    if (!entries) return false;
    entries->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    const QByteArray bytes = file.readAll();
    quint16 version = 0, count = 0;
    if (bytes.size() < 12 || std::memcmp(bytes.constData(), "ARC\0", 4) != 0
        || !readLe(bytes, 4, &version) || version != 0x10 || !readLe(bytes, 6, &count))
    {
        if (error) *error = QString::fromUtf8("不是受支持的 ARC v0x10 文件：%1").arg(path);
        return false;
    }
    if (!rangeOk(12, qint64(count) * 80, bytes.size()))
    {
        if (error) *error = QString::fromUtf8("ARC 目录越界：%1").arg(path);
        return false;
    }
    for (int index = 0; index < count; ++index)
    {
        const int base = 12 + index * 80;
        const int zero = bytes.indexOf('\0', base);
        const int nameEnd = zero >= base && zero < base + 64 ? zero : base + 64;
        Mh3gArchiveEntry entry;
        entry.name = QString::fromLatin1(bytes.constData() + base, nameEnd - base).replace('\\', '/');
        quint32 packedSize = 0, compressedSize = 0, offset = 0;
        if (!readLe(bytes, base + 64, &entry.typeHash) || !readLe(bytes, base + 68, &compressedSize)
            || !readLe(bytes, base + 72, &packedSize) || !readLe(bytes, base + 76, &offset))
        { if (error) *error = QString::fromUtf8("ARC 目录损坏"); return false; }
        const quint32 unpackedSize = packedSize & 0x3fffffffU;
        if (!rangeOk(offset, compressedSize, bytes.size()) || unpackedSize > 256U * 1024U * 1024U)
        { if (error) *error = QString::fromUtf8("ARC 条目越界或过大：%1").arg(entry.name); return false; }
        QByteArray framed(4, '\0');
        qToBigEndian<quint32>(unpackedSize, reinterpret_cast<uchar *>(framed.data()));
        framed.append(bytes.constData() + offset, int(compressedSize));
        entry.data = qUncompress(framed);
        if (entry.data.size() != int(unpackedSize))
        { if (error) *error = QString::fromUtf8("ARC zlib 解压失败：%1").arg(entry.name); return false; }
        entries->append(entry);
    }
    return true;
}

bool Mh3gArchiveLoader::validateWeaponArchive(const QString &path, QString *error)
{
    QVector<Mh3gArchiveEntry> entries;
    if (!read(path, &entries, error)) return false;
    int mods = 0, mrls = 0, textures = 0;
    for (int index = 0; index < entries.size(); ++index)
    {
        const Mh3gArchiveEntry &entry = entries[index];
        const QByteArray &data = entry.data;
        quint16 version = 0;
        if (entry.typeHash == HashMod)
        {
            ++mods;
            if (data.size() < 64 || std::memcmp(data.constData(), "MOD\0", 4) != 0 || !readLe(data, 4, &version) || version != 0xE6)
            { if (error) *error = QString::fromUtf8("MOD v0xE6 校验失败：%1").arg(entry.name); return false; }
        }
        else if (entry.typeHash == HashTex)
        {
            ++textures;
            if (data.size() < 20 || std::memcmp(data.constData(), "TEX\0", 4) != 0 || !readLe(data, 4, &version) || version != 0xA5)
            { if (error) *error = QString::fromUtf8("TEX v0xA5 校验失败：%1").arg(entry.name); return false; }
        }
        else if (entry.typeHash == HashMrl)
        {
            ++mrls;
            if (data.size() < 16 || std::memcmp(data.constData(), "MRL\0", 4) != 0 || !readLe(data, 4, &version) || version != 0x20)
            { if (error) *error = QString::fromUtf8("MRL v0x20 校验失败：%1").arg(entry.name); return false; }
        }
    }
    if (mods == 0 || mrls == 0 || textures == 0)
    { if (error) *error = QString::fromUtf8("武器 ARC 缺少 MOD、MRL 或 TEX 条目"); return false; }
    return true;
}

bool Mh3gModelLoader::parseMod(const QByteArray &data, Mh3gCpuModel *model, QString *error)
{
    if (!model) return false;
    quint16 version = 0, primitiveCount = 0;
    quint32 vertexCount = 0, primitiveOffset = 0, vertexOffset = 0, indexOffset = 0;
    if (data.size() < 64 || std::memcmp(data.constData(), "MOD\0", 4) != 0
        || !readLe(data, 4, &version) || version != 0xE6 || !readLe(data, 8, &primitiveCount)
        || !readLe(data, 12, &vertexCount) || !readLe(data, 0x34, &primitiveOffset)
        || !readLe(data, 0x38, &vertexOffset) || !readLe(data, 0x3c, &indexOffset))
    { if (error) *error = QString::fromUtf8("MOD 头部无效或版本不是 v0xE6"); return false; }
    if (primitiveCount == 0 || primitiveCount > 4096 || vertexCount == 0 || vertexCount > 4000000
        || !rangeOk(primitiveOffset, qint64(primitiveCount) * 48 + 4, data.size()) || vertexOffset >= quint32(data.size())
        || indexOffset >= quint32(data.size()))
    { if (error) *error = QString::fromUtf8("MOD 数量或数据偏移越界"); return false; }

    model->vertices.clear(); model->indices.clear();
    model->vertices.resize(int(vertexCount));
    QVector<bool> decoded(int(vertexCount), false);
    bool hasBounds = false;
    for (int primitive = 0; primitive < primitiveCount; ++primitive)
    {
        const int base = int(primitiveOffset) + primitive * 48;
        quint16 count = 0; quint8 stride = 0; quint32 vertexStart = 0, relative = 0;
        quint32 stripOffset = 0, stripCount = 0;
        if (!readLe(data, base + 2, &count) || !rangeOk(base + 10, 2, data.size())) return false;
        stride = quint8(data.at(base + 10));
        if (!readLe(data, base + 12, &vertexStart) || !readLe(data, base + 16, &relative)
            || !readLe(data, base + 24, &stripOffset) || !readLe(data, base + 28, &stripCount)) return false;
        if ((stride != 28 && stride != 32 && stride != 36) || vertexStart > vertexCount || count > vertexCount - vertexStart
            || !rangeOk(qint64(vertexOffset) + relative + qint64(vertexStart) * stride, qint64(count) * stride, data.size())
            || !rangeOk(qint64(indexOffset) + qint64(stripOffset) * 2, qint64(stripCount) * 2, data.size()))
        { if (error) *error = QString::fromUtf8("MOD Primitive %1 越界或顶点步长未知").arg(primitive); return false; }
        for (quint32 local = 0; local < count; ++local)
        {
            const quint32 global = vertexStart + local;
            const int address = int(vertexOffset + relative + global * stride);
            bool ok = true;
            Mh3gVertex vertex;
            vertex.position = QVector3D(readFloat(data, address, &ok), readFloat(data, address + 4, &ok), readFloat(data, address + 8, &ok));
            const qint8 nx = qint8(data.at(address + 12)), ny = qint8(data.at(address + 13)), nz = qint8(data.at(address + 14));
            vertex.normal = QVector3D(nx / 127.0f, ny / 127.0f, nz / 127.0f).normalized();
            vertex.uv = QVector2D(readFloat(data, address + 16, &ok), readFloat(data, address + 20, &ok));
            if (!ok) { if (error) *error = QString::fromUtf8("MOD 顶点包含无效浮点数"); return false; }
            model->vertices[int(global)] = vertex; decoded[int(global)] = true;
            if (!hasBounds) { model->boundsMinimum = model->boundsMaximum = vertex.position; hasBounds = true; }
            else
            {
                model->boundsMinimum.setX(qMin(model->boundsMinimum.x(), vertex.position.x()));
                model->boundsMinimum.setY(qMin(model->boundsMinimum.y(), vertex.position.y()));
                model->boundsMinimum.setZ(qMin(model->boundsMinimum.z(), vertex.position.z()));
                model->boundsMaximum.setX(qMax(model->boundsMaximum.x(), vertex.position.x()));
                model->boundsMaximum.setY(qMax(model->boundsMaximum.y(), vertex.position.y()));
                model->boundsMaximum.setZ(qMax(model->boundsMaximum.z(), vertex.position.z()));
            }
        }
        QVector<quint16> strip;
        strip.reserve(int(stripCount));
        for (quint32 item = 0; item < stripCount; ++item)
        {
            quint16 value = 0; readLe(data, int(indexOffset + (stripOffset + item) * 2), &value);
            if (value >= vertexCount || !decoded[int(value)])
            { if (error) *error = QString::fromUtf8("MOD Primitive %1 的索引越界").arg(primitive); return false; }
            strip.append(value);
        }
        for (int item = 2; item < strip.size(); ++item)
        {
            quint32 a = strip[item - 2], b = strip[item - 1], c = strip[item];
            if (item & 1) qSwap(a, b);
            if (a == b || b == c || a == c) continue;
            model->indices << a << b << c;
        }
    }
    if (!hasBounds || model->indices.isEmpty())
    { if (error) *error = QString::fromUtf8("MOD 没有可显示的网格"); return false; }
    return true;
}

bool Mh3gModelLoader::decodeTex(const QByteArray &data, QImage *image, QString *error)
{
    quint16 version = 0; quint32 packed = 0;
    if (!image || data.size() < 20 || std::memcmp(data.constData(), "TEX\0", 4) != 0
        || !readLe(data, 4, &version) || version != 0xA5 || !readLe(data, 8, &packed))
    { if (error) *error = QString::fromUtf8("TEX 头部无效或版本不是 v0xA5"); return false; }
    const int mipCount = int(packed & 0x3fU), width = int((packed >> 6) & 0x1fffU), height = int((packed >> 19) & 0x1fffU);
    const int format = quint8(data.at(0x0d));
    if (mipCount < 1 || mipCount > 16 || width < 1 || height < 1 || width > 8192 || height > 8192
        || (format != 0x0b && format != 0x0c) || !rangeOk(16, qint64(mipCount) * 4, data.size()))
    { if (error) *error = QString::fromUtf8("TEX 尺寸、MIP 或 ETC 格式无效"); return false; }
    quint32 firstRelativeOffset = 0; readLe(data, 16, &firstRelativeOffset);
    const quint32 firstOffset = quint32(16 + mipCount * 4) + firstRelativeOffset;
    if (firstOffset >= quint32(data.size())) { if (error) *error = QString::fromUtf8("TEX 主 MIP 偏移越界"); return false; }
    const int bytesPerBlock = format == 0x0c ? 16 : 8;
    const int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
    if (!rangeOk(firstOffset, qint64(blocksX) * blocksY * bytesPerBlock, data.size()))
    { if (error) *error = QString::fromUtf8("TEX 主 MIP 数据越界"); return false; }
    QImage result(width, height, QImage::Format_RGBA8888);
    result.fill(Qt::transparent);
    const uchar *source = reinterpret_cast<const uchar *>(data.constData() + firstOffset);
    int blockIndex = 0;
    for (int tileY = 0; tileY < blocksY; tileY += 2)
    {
        for (int tileX = 0; tileX < blocksX; tileX += 2)
        {
            for (int sub = 0; sub < 4; ++sub)
            {
                const int bx = tileX + (sub & 1), by = tileY + (sub >> 1);
                const uchar *block = source + qint64(blockIndex++) * bytesPerBlock;
                if (bx >= blocksX || by >= blocksY) continue;
                const uchar *colorData = block + (format == 0x0c ? 8 : 0);
                const quint64 color = qFromLittleEndian<quint64>(colorData);
                const quint64 alphaBits = format == 0x0c ? qFromLittleEndian<quint64>(block) : std::numeric_limits<quint64>::max();
                for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
                {
                    const int px = bx * 4 + x, py = by * 4 + y;
                    if (px >= width || py >= height) continue;
                    const int alpha = format == 0x0c ? int((alphaBits >> ((x * 4 + y) * 4)) & 15U) * 17 : 255;
                    result.setPixel(px, py, etcPixel(color, x, y, alpha));
                }
            }
        }
    }
    *image = result.mirrored(false, true);
    return true;
}

QSharedPointer<Mh3gCpuModel> Mh3gModelLoader::load(const QString &modelKey, const QString &arcPath)
{
    QSharedPointer<Mh3gCpuModel> model(new Mh3gCpuModel);
    model->modelKey = modelKey;
    QVector<Mh3gArchiveEntry> entries;
    if (!Mh3gArchiveLoader::read(arcPath, &entries, &model->error)) return model;
    QVector<int> modIndexes;
    int textureIndex = -1, environmentIndex = -1;
    const QString prefix = modelKey.left(3);
    for (int index = 0; index < entries.size(); ++index)
    {
        if (entries[index].typeHash == HashMod) modIndexes.append(index);
        if (entries[index].typeHash == HashTex && textureIndex < 0
            && entries[index].name.contains("_BM", Qt::CaseInsensitive)
            && !entries[index].name.contains("common", Qt::CaseInsensitive)) textureIndex = index;
        if (entries[index].typeHash == HashTex && environmentIndex < 0
            && entries[index].name.contains("common", Qt::CaseInsensitive)
            && entries[index].name.contains("env_", Qt::CaseInsensitive)) environmentIndex = index;
    }
    if (modIndexes.isEmpty()) { model->error = QString::fromUtf8("ARC 中没有 MOD 条目"); return model; }
    bool haveBounds = false;
    for (int index : modIndexes)
    {
        Mh3gCpuModel part;
        if (!parseMod(entries[index].data, &part, &model->error))
        { model->error = QString::fromUtf8("%1：%2").arg(entries[index].name, model->error); return model; }
        const quint32 baseVertex = quint32(model->vertices.size());
        model->vertices += part.vertices;
        for (quint32 value : part.indices) model->indices.append(baseVertex + value);
        if (!haveBounds)
        { model->boundsMinimum = part.boundsMinimum; model->boundsMaximum = part.boundsMaximum; haveBounds = true; }
        else
        {
            model->boundsMinimum.setX(qMin(model->boundsMinimum.x(), part.boundsMinimum.x()));
            model->boundsMinimum.setY(qMin(model->boundsMinimum.y(), part.boundsMinimum.y()));
            model->boundsMinimum.setZ(qMin(model->boundsMinimum.z(), part.boundsMinimum.z()));
            model->boundsMaximum.setX(qMax(model->boundsMaximum.x(), part.boundsMaximum.x()));
            model->boundsMaximum.setY(qMax(model->boundsMaximum.y(), part.boundsMaximum.y()));
            model->boundsMaximum.setZ(qMax(model->boundsMaximum.z(), part.boundsMaximum.z()));
        }
    }
    if (textureIndex >= 0)
    {
        QString textureError;
        if (!decodeTex(entries[textureIndex].data, &model->diffuse, &textureError))
            model->diffuse = QImage();
    }
    if (environmentIndex >= 0)
    {
        QString textureError;
        if (!decodeTex(entries[environmentIndex].data, &model->environment, &textureError))
            model->environment = QImage();
    }
    Q_UNUSED(prefix);
    return model;
}

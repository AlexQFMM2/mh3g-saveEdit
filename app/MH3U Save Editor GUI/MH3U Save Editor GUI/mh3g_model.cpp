#include "mh3g_model.hpp"

#include <QFile>
#include <QHash>
#include <QtEndian>
#include <QtMath>

#include <cstring>
#include <functional>
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

QString fixedString(const QByteArray &data, int offset, int maximum)
{
    if (!rangeOk(offset, maximum, data.size())) return QString();
    int length = 0;
    while (length < maximum && data.at(offset + length) != '\0') ++length;
    return QString::fromLatin1(data.constData() + offset, length).replace('\\', '/');
}

bool parseMrl(const QByteArray &data, const QHash<QString, QImage> &textures,
              QVector<Mh3gMaterial> *materials, QString *error)
{
    quint32 version = 0, materialCount = 0, textureCount = 0, textureOffset = 0, materialOffset = 0;
    if (!materials || data.size() < 0x1c || std::memcmp(data.constData(), "MRL\0", 4) != 0
        || !readLe(data, 4, &version) || version != 0x20 || !readLe(data, 8, &materialCount)
        || !readLe(data, 0x0c, &textureCount) || !readLe(data, 0x14, &textureOffset)
        || !readLe(data, 0x18, &materialOffset))
    { if (error) *error = QString::fromUtf8("MRL v0x20 头部无效"); return false; }
    if (materialCount == 0 || materialCount > 4096 || textureCount > 4096
        || !rangeOk(textureOffset, qint64(textureCount) * 0x4c, data.size())
        || !rangeOk(materialOffset, qint64(materialCount) * 0x3c, data.size()))
    { if (error) *error = QString::fromUtf8("MRL 材质或贴图引用表越界"); return false; }

    struct TextureReference { quint32 type = 0; QString name; };
    QVector<TextureReference> references;
    references.resize(int(textureCount));
    for (quint32 index = 0; index < textureCount; ++index)
    {
        const int base = int(textureOffset + index * 0x4c);
        readLe(data, base, &references[int(index)].type);
        references[int(index)].name = fixedString(data, base + 12, 64);
    }

    materials->clear();
    materials->reserve(int(materialCount));
    for (quint32 materialIndex = 0; materialIndex < materialCount; ++materialIndex)
    {
        const int base = int(materialOffset + materialIndex * 0x3c);
        quint32 nameHash = 0, blendState = 0, commandInfo = 0, commandOffset = 0;
        readLe(data, base + 4, &nameHash);
        readLe(data, base + 0x0c, &blendState);
        readLe(data, base + 0x18, &commandInfo);
        readLe(data, base + 0x34, &commandOffset);
        const int encodedCount = int(commandInfo & 0xfffU);
        const int commandCount = encodedCount / 2; // 3DS encodes command count in eight-byte units.
        if ((encodedCount & 1) || commandCount > 2048
            || !rangeOk(commandOffset, qint64(commandCount) * 0x18, data.size()))
        { if (error) *error = QString::fromUtf8("MRL 材质 %1 的命令表越界").arg(materialIndex); return false; }

        Mh3gMaterial material;
        material.name = QString("0x%1").arg(nameHash, 8, 16, QLatin1Char('0'));
        const quint32 blendHash = blendState >> 12;
        material.transparent = blendHash != 0x4d2c8U && blendHash != 0x62b2dU && blendHash != 0x67927U;
        for (int command = 0; command < commandCount; ++command)
        {
            const int commandBase = int(commandOffset) + command * 0x18;
            quint32 info = 0, referenceIndex = 0;
            readLe(data, commandBase, &info);
            readLe(data, commandBase + 4, &referenceIndex);
            if ((info & 0x1fU) != 3U || referenceIndex == 0 || referenceIndex > textureCount) continue;
            const TextureReference &reference = references[int(referenceIndex - 1)];
            if (reference.type != HashTex || reference.name.isEmpty()) continue;
            const QImage image = textures.value(reference.name.toLower());
            if (image.isNull()) continue;
            const QString lower = reference.name.toLower();
            if (lower.contains("env_") || lower.contains("environment")) material.environment = image;
            else if (lower.contains("_nm") || lower.contains("normal")) material.normal = image;
            else if (lower.contains("_mm") || lower.contains("_sm") || lower.contains("spec") || lower.contains("mask"))
                material.specular = image;
            else if (material.albedo.isNull()) material.albedo = image;
        }
        for (int command = 0; command < commandCount; ++command)
        {
            const int commandBase = int(commandOffset) + command * 0x18;
            quint32 info = 0, relativeOffset = 0;
            readLe(data, commandBase, &info);
            readLe(data, commandBase + 4, &relativeOffset);
            const int constants = int(commandOffset + relativeOffset);
            if ((info & 0x1fU) != 1U || !rangeOk(constants, 8 * 16, data.size())) continue;
            bool ok = true;
            material.albedoFactor = QVector4D(readFloat(data, constants + 16, &ok),
                readFloat(data, constants + 20, &ok), readFloat(data, constants + 24, &ok),
                readFloat(data, constants + 28, &ok));
            const float materialSpecular = readFloat(data, constants + 44, &ok);
            const float materialRoughness = readFloat(data, constants + 96, &ok);
            if (ok)
            {
                material.specularStrength = qBound(0.04f, materialSpecular, 1.0f);
                material.roughness = qBound(0.08f, materialRoughness, 0.95f);
                material.environmentStrength = qBound(0.02f, material.specularStrength * 0.24f, 0.22f);
            }
            break;
        }
        materials->append(material);
    }
    return true;
}

struct BindMatrix
{
    float values[16];

    static BindMatrix identity()
    {
        BindMatrix result = {};
        result.values[0] = result.values[5] = result.values[10] = result.values[15] = 1.0f;
        return result;
    }

    static BindMatrix multiply(const BindMatrix &left, const BindMatrix &right)
    {
        BindMatrix result = {};
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                for (int inner = 0; inner < 4; ++inner)
                    result.values[column * 4 + row] += left.values[inner * 4 + row]
                        * right.values[column * 4 + inner];
        return result;
    }

    QVector3D position(const QVector3D &value) const
    {
        return QVector3D(
            value.x() * values[0] + value.y() * values[4] + value.z() * values[8] + values[12],
            value.x() * values[1] + value.y() * values[5] + value.z() * values[9] + values[13],
            value.x() * values[2] + value.y() * values[6] + value.z() * values[10] + values[14]);
    }

    QVector3D direction(const QVector3D &value) const
    {
        return QVector3D(
            value.x() * values[0] + value.y() * values[4] + value.z() * values[8],
            value.x() * values[1] + value.y() * values[5] + value.z() * values[9],
            value.x() * values[2] + value.y() * values[6] + value.z() * values[10]);
    }
};

struct PoseRotation
{
    int boneId;
    float x, y, z, w;
};

// mot_plcom_0000, motion 1, sampled at the middle of its 201-frame idle loop.
// The common player motion uses global bone IDs, while MOD vertex indices remain
// local to each component. Keeping this small fixed pose avoids shipping or
// playing the complete LMT animation set in the fitting room.
const PoseRotation FittingRotations[] = {
    {1, -0.045747315f, -0.325327837f, -0.008270531f, 0.944457823f},
    {2,  0.081412968f,  0.016342610f, -0.004038605f, 0.996538277f},
    {3,  0.028916137f,  0.150653877f,  0.016933873f, 0.988018477f},
    {4, -0.007232913f,  0.144047805f, -0.048394873f, 0.988360183f},
    {5,  0.058701695f,  0.028961747f, -0.108843029f, 0.991901468f},
    {6,  0.005600116f,  0.043687044f, -0.491238816f, 0.869910632f},
    {7,  0.047852868f, -0.277992174f, -0.014236839f, 0.959285029f},
    {8, -0.185661004f,  0.037339729f, -0.057787306f, 0.980202205f},
    {9,  0.078020535f, -0.011169766f,  0.104098596f, 0.991439113f},
    {10, 0.072924245f,  0.139027588f,  0.466034601f, 0.870726786f},
    {11,-0.002243121f,  0.210593440f, -0.000442520f, 0.977571059f},
    {12,-0.173207392f, -0.082964368f,  0.119952728f, 0.974026415f},
    {13,-0.070939651f, -0.316886177f,  0.001220728f, 0.945806125f},
    {14,-0.010895107f,  0.217978339f,  0.123859308f, 0.968000833f},
    {15, 0.0f,          0.0f,          0.0f,         1.0f},
    {16,-0.047425688f, -0.053132632f, -0.106219495f, 0.991788862f},
    {17, 0.041429238f, -0.148489116f, -0.182410736f, 0.971061751f},
    {18, 0.0f,          0.0f,          0.0f,         1.0f},
    {19,-0.079738433f,  0.017484207f,  0.143573535f, 0.986267066f}
};

const QVector3D FittingRootTranslation(0.109665794f, 101.707554f, -0.309764757f);

bool fittingRotation(int boneId, PoseRotation *rotation)
{
    for (const PoseRotation &candidate : FittingRotations)
        if (candidate.boneId == boneId)
        {
            if (rotation) *rotation = candidate;
            return true;
        }
    return false;
}

BindMatrix replaceRotation(const BindMatrix &source, const PoseRotation &input)
{
    BindMatrix result = source;
    const float length = qSqrt(input.x * input.x + input.y * input.y + input.z * input.z + input.w * input.w);
    if (length <= 1.0e-8f) return result;
    const float x = input.x / length, y = input.y / length, z = input.z / length, w = input.w / length;
    const float scaleX = qSqrt(source.values[0] * source.values[0]
        + source.values[1] * source.values[1] + source.values[2] * source.values[2]);
    const float scaleY = qSqrt(source.values[4] * source.values[4]
        + source.values[5] * source.values[5] + source.values[6] * source.values[6]);
    const float scaleZ = qSqrt(source.values[8] * source.values[8]
        + source.values[9] * source.values[9] + source.values[10] * source.values[10]);
    const float sx = scaleX > 1.0e-8f ? scaleX : 1.0f;
    const float sy = scaleY > 1.0e-8f ? scaleY : 1.0f;
    const float sz = scaleZ > 1.0e-8f ? scaleZ : 1.0f;
    result.values[0] = (1.0f - 2.0f * (y * y + z * z)) * sx;
    result.values[1] = (2.0f * (x * y + z * w)) * sx;
    result.values[2] = (2.0f * (x * z - y * w)) * sx;
    result.values[4] = (2.0f * (x * y - z * w)) * sy;
    result.values[5] = (1.0f - 2.0f * (x * x + z * z)) * sy;
    result.values[6] = (2.0f * (y * z + x * w)) * sy;
    result.values[8] = (2.0f * (x * z + y * w)) * sz;
    result.values[9] = (2.0f * (y * z - x * w)) * sz;
    result.values[10] = (1.0f - 2.0f * (x * x + y * y)) * sz;
    return result;
}

typedef QHash<int, BindMatrix> SkeletonPose;

bool readMatrix(const QByteArray &data, int offset, BindMatrix *matrix);

BindMatrix fromQtMatrix(const QMatrix4x4 &source)
{
    BindMatrix result;
    std::memcpy(result.values, source.constData(), sizeof(result.values));
    return result;
}

QMatrix4x4 toQtMatrix(const BindMatrix &source)
{
    QMatrix4x4 result;
    std::memcpy(result.data(), source.values, sizeof(source.values));
    return result;
}

Mh3gSkeletonPose toPublicPose(const SkeletonPose &source)
{
    Mh3gSkeletonPose result;
    for (auto it = source.constBegin(); it != source.constEnd(); ++it)
        result.globalByBoneId.insert(it.key(), toQtMatrix(it.value()));
    return result;
}

bool buildSkeletonPose(const QByteArray &data, quint16 boneCount, quint32 boneOffset,
                       bool fitting, SkeletonPose *pose, QVector<BindMatrix> *worldByLocal,
                       QString *error)
{
    if (!pose || !worldByLocal || boneCount == 0 || boneCount > 256
        || !rangeOk(boneOffset, qint64(boneCount) * (24 + 64 + 64) + 256, data.size()))
    { if (error) *error = QString::fromUtf8("MOD 人物骨架缺失或越界"); return false; }
    const int referenceOffset = int(boneOffset) + int(boneCount) * 24;
    QVector<BindMatrix> local;
    QVector<int> boneIds, parents;
    local.resize(int(boneCount));
    boneIds.resize(int(boneCount));
    parents.resize(int(boneCount));
    bool hasPlayerRoot = false;
    for (int bone = 0; bone < boneCount; ++bone)
    {
        const int record = int(boneOffset) + bone * 24;
        boneIds[bone] = quint8(data.at(record));
        parents[bone] = quint8(data.at(record + 1));
        hasPlayerRoot = hasPlayerRoot || boneIds[bone] == 0;
        if (!readMatrix(data, referenceOffset + bone * 64, &local[bone]))
        { if (error) *error = QString::fromUtf8("MOD 骨骼参考矩阵无效"); return false; }
        if (fitting)
        {
            PoseRotation rotation;
            if (fittingRotation(boneIds[bone], &rotation)) local[bone] = replaceRotation(local[bone], rotation);
            if (boneIds[bone] == 0)
            {
                local[bone].values[12] = FittingRootTranslation.x();
                local[bone].values[13] = FittingRootTranslation.y();
                local[bone].values[14] = FittingRootTranslation.z();
            }
        }
    }

    BindMatrix externalRoot = BindMatrix::identity();
    if (fitting && !hasPlayerRoot)
    {
        externalRoot.values[12] = FittingRootTranslation.x();
        externalRoot.values[13] = FittingRootTranslation.y();
        externalRoot.values[14] = FittingRootTranslation.z();
    }
    worldByLocal->resize(int(boneCount));
    QVector<quint8> state(int(boneCount), 0);
    std::function<bool(int)> resolve = [&](int bone) -> bool {
        if (state[bone] == 2) return true;
        if (state[bone] == 1)
        { if (error) *error = QString::fromUtf8("MOD 骨骼父子关系存在循环"); return false; }
        state[bone] = 1;
        const int parent = parents[bone];
        if (parent == 0xff) (*worldByLocal)[bone] = BindMatrix::multiply(externalRoot, local[bone]);
        else
        {
            if (parent < 0 || parent >= boneCount || !resolve(parent))
            { if (error && error->isEmpty()) *error = QString::fromUtf8("MOD 骨骼父索引越界"); return false; }
            (*worldByLocal)[bone] = BindMatrix::multiply((*worldByLocal)[parent], local[bone]);
        }
        state[bone] = 2;
        return true;
    };
    pose->clear();
    for (int bone = 0; bone < boneCount; ++bone)
    {
        if (!resolve(bone)) return false;
        // A small number of event/cloth models repeat a global animation ID
        // for an auxiliary local bone. Both local bones remain skin-able; the
        // first occurrence is the canonical player-skeleton transform.
        if (!pose->contains(boneIds[bone])) pose->insert(boneIds[bone], (*worldByLocal)[bone]);
    }
    return true;
}

bool readMatrix(const QByteArray &data, int offset, BindMatrix *matrix)
{
    if (!matrix || !rangeOk(offset, 64, data.size())) return false;
    bool ok = true;
    for (int index = 0; index < 16; ++index)
        matrix->values[index] = readFloat(data, offset + index * 4, &ok);
    return ok;
}

void generateTangents(Mh3gCpuModel *model)
{
    if (!model || model->vertices.isEmpty()) return;
    QVector<QVector3D> tangents(model->vertices.size()), bitangents(model->vertices.size());
    for (int index = 0; index + 2 < model->indices.size(); index += 3)
    {
        const int ia = int(model->indices[index]), ib = int(model->indices[index + 1]), ic = int(model->indices[index + 2]);
        if (ia < 0 || ib < 0 || ic < 0 || ia >= model->vertices.size() || ib >= model->vertices.size() || ic >= model->vertices.size()) continue;
        const Mh3gVertex &a = model->vertices[ia], &b = model->vertices[ib], &c = model->vertices[ic];
        const QVector3D edge1 = b.position - a.position, edge2 = c.position - a.position;
        const QVector2D uv1 = b.uv - a.uv, uv2 = c.uv - a.uv;
        const float determinant = uv1.x() * uv2.y() - uv1.y() * uv2.x();
        if (qAbs(determinant) < 1.0e-8f) continue;
        const float reciprocal = 1.0f / determinant;
        const QVector3D tangent = (edge1 * uv2.y() - edge2 * uv1.y()) * reciprocal;
        const QVector3D bitangent = (edge2 * uv1.x() - edge1 * uv2.x()) * reciprocal;
        tangents[ia] += tangent; tangents[ib] += tangent; tangents[ic] += tangent;
        bitangents[ia] += bitangent; bitangents[ib] += bitangent; bitangents[ic] += bitangent;
    }
    for (int index = 0; index < model->vertices.size(); ++index)
    {
        const QVector3D normal = model->vertices[index].normal.normalized();
        QVector3D tangent = tangents[index] - normal * QVector3D::dotProduct(normal, tangents[index]);
        if (tangent.lengthSquared() < 1.0e-8f)
            tangent = QVector3D::crossProduct(normal, qAbs(normal.y()) < 0.9f ? QVector3D(0, 1, 0) : QVector3D(1, 0, 0));
        tangent.normalize();
        const float handedness = QVector3D::dotProduct(QVector3D::crossProduct(normal, tangent), bitangents[index]) < 0.0f ? -1.0f : 1.0f;
        model->vertices[index].tangent = QVector4D(tangent, handedness);
    }
}

}

qint64 Mh3gCpuModel::memoryBytes() const
{
    qint64 total = qint64(vertices.size()) * qint64(sizeof(Mh3gVertex))
        + qint64(indices.size()) * qint64(sizeof(quint32));
    for (const Mh3gMaterial &material : materials)
        total += material.albedo.sizeInBytes() + material.normal.sizeInBytes()
            + material.specular.sizeInBytes() + material.environment.sizeInBytes();
    return total;
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

bool Mh3gModelLoader::parseModWithPose(const QByteArray &data, Mh3gCpuModel *model, QString *error,
                                       Mh3gModelLoadMode mode, const Mh3gSkeletonPose *externalPose)
{
    if (!model) return false;
    quint16 version = 0, boneCount = 0, primitiveCount = 0, materialCount = 0;
    quint32 vertexCount = 0, boneOffset = 0, primitiveOffset = 0, vertexOffset = 0, indexOffset = 0;
    if (data.size() < 64 || std::memcmp(data.constData(), "MOD\0", 4) != 0
        || !readLe(data, 4, &version) || version != 0xE6 || !readLe(data, 6, &boneCount)
        || !readLe(data, 8, &primitiveCount)
        || !readLe(data, 0x0a, &materialCount)
        || !readLe(data, 12, &vertexCount) || !readLe(data, 0x28, &boneOffset)
        || !readLe(data, 0x34, &primitiveOffset)
        || !readLe(data, 0x38, &vertexOffset) || !readLe(data, 0x3c, &indexOffset))
    { if (error) *error = QString::fromUtf8("MOD 头部无效或版本不是 v0xE6"); return false; }
    if (primitiveCount == 0 || primitiveCount > 4096 || vertexCount == 0 || vertexCount > 4000000
        || !rangeOk(primitiveOffset, qint64(primitiveCount) * 48 + 4, data.size()) || vertexOffset >= quint32(data.size())
        || indexOffset >= quint32(data.size()))
    { if (error) *error = QString::fromUtf8("MOD 数量或数据偏移越界"); return false; }

    QVector<BindMatrix> bindMatrices;
    if (mode != Mh3gModelLoadMode::Raw)
    {
        SkeletonPose localPose;
        QVector<BindMatrix> worldByLocal;
        const bool fitting = mode == Mh3gModelLoadMode::FittingPose;
        if (!buildSkeletonPose(data, boneCount, boneOffset, fitting, &localPose, &worldByLocal, error)) return false;
        const int inverseBindOffset = int(boneOffset) + int(boneCount) * 24 + int(boneCount) * 64;
        bindMatrices.resize(int(boneCount));
        for (int bone = 0; bone < boneCount; ++bone)
        {
            BindMatrix inverseBind;
            if (!readMatrix(data, inverseBindOffset + bone * 64, &inverseBind))
            { if (error) *error = QString::fromUtf8("MOD 骨骼绑定矩阵无效"); return false; }
            BindMatrix desiredPose = worldByLocal[bone];
            const int globalId = quint8(data.at(int(boneOffset) + bone * 24));
            if (fitting && externalPose && externalPose->globalByBoneId.contains(globalId))
                desiredPose = fromQtMatrix(externalPose->globalByBoneId.value(globalId));
            bindMatrices[bone] = BindMatrix::multiply(desiredPose, inverseBind);
        }
    }

    model->vertices.clear(); model->indices.clear(); model->drawCalls.clear();
    model->vertices.reserve(int(vertexCount));
    QHash<quint32, quint32> vertexByAddress;
    bool hasBounds = false;
    for (int primitive = 0; primitive < primitiveCount; ++primitive)
    {
        const int base = int(primitiveOffset) + primitive * 48;
        quint16 count = 0; quint8 stride = 0; quint32 packedPrimitive = 0, vertexStart = 0, relative = 0;
        quint32 stripOffset = 0, stripCount = 0;
        if (!readLe(data, base + 2, &count) || !readLe(data, base + 4, &packedPrimitive)
            || !rangeOk(base + 10, 2, data.size())) return false;
        stride = quint8(data.at(base + 10));
        const int materialIndex = int((packedPrimitive >> 12) & 0xfffU);
        if (!readLe(data, base + 12, &vertexStart) || !readLe(data, base + 16, &relative)
            || !readLe(data, base + 24, &stripOffset) || !readLe(data, base + 28, &stripCount)) return false;
        if ((stride != 28 && stride != 32 && stride != 36 && stride != 44) || materialIndex >= qMax(1, int(materialCount))
            || vertexStart > vertexCount || count > vertexCount - vertexStart
            || !rangeOk(qint64(vertexOffset) + relative + qint64(vertexStart) * stride, qint64(count) * stride, data.size())
            || !rangeOk(qint64(indexOffset) + qint64(stripOffset) * 2, qint64(stripCount) * 2, data.size()))
        { if (error) *error = QString::fromUtf8("MOD Primitive %1 越界或顶点步长未知").arg(primitive); return false; }
        for (quint32 local = 0; local < count; ++local)
        {
            const quint32 global = vertexStart + local;
            const quint32 addressValue = vertexOffset + relative + global * stride;
            const int address = int(addressValue);
            if (vertexByAddress.contains(addressValue)) continue;
            bool ok = true;
            Mh3gVertex vertex;
            vertex.position = QVector3D(readFloat(data, address, &ok), readFloat(data, address + 4, &ok), readFloat(data, address + 8, &ok));
            const qint8 nx = qint8(data.at(address + 12)), ny = qint8(data.at(address + 13)), nz = qint8(data.at(address + 14));
            vertex.normal = QVector3D(nx / 127.0f, ny / 127.0f, nz / 127.0f).normalized();
            vertex.uv = QVector2D(readFloat(data, address + 16, &ok), readFloat(data, address + 20, &ok));
            if (!ok) { if (error) *error = QString::fromUtf8("MOD 顶点包含无效浮点数"); return false; }
            if (mode != Mh3gModelLoadMode::Raw)
            {
                int boneIndexes[4] = {quint8(data.at(address + 24)), quint8(data.at(address + 25)), 0, 0};
                int boneWeights[4] = {quint8(data.at(address + 26)), quint8(data.at(address + 27)), 0, 0};
                int influences = 2;
                if (stride >= 36)
                {
                    boneIndexes[2] = quint8(data.at(address + 32)); boneIndexes[3] = quint8(data.at(address + 33));
                    boneWeights[2] = quint8(data.at(address + 34)); boneWeights[3] = quint8(data.at(address + 35));
                    influences = 4;
                }
                int totalWeight = 0;
                QVector3D position, normal;
                for (int influence = 0; influence < influences; ++influence)
                {
                    const int weight = boneWeights[influence];
                    if (!weight) continue;
                    if (boneIndexes[influence] < 0 || boneIndexes[influence] >= bindMatrices.size())
                    { if (error) *error = QString::fromUtf8("MOD 顶点骨骼索引越界"); return false; }
                    totalWeight += weight;
                    position += bindMatrices[boneIndexes[influence]].position(vertex.position) * float(weight);
                    normal += bindMatrices[boneIndexes[influence]].direction(vertex.normal) * float(weight);
                }
                if (totalWeight <= 0)
                { if (error) *error = QString::fromUtf8("MOD 顶点骨骼权重为空"); return false; }
                vertex.position = position / float(totalWeight);
                vertex.normal = normal.lengthSquared() > 1.0e-10f ? normal.normalized() : QVector3D(0, 1, 0);
            }
            vertexByAddress[addressValue] = quint32(model->vertices.size());
            model->vertices.append(vertex);
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
            const quint32 address = vertexOffset + relative + quint32(value) * stride;
            if (value >= vertexCount || !vertexByAddress.contains(address))
            { if (error) *error = QString::fromUtf8("MOD Primitive %1 的索引越界").arg(primitive); return false; }
            strip.append(quint16(vertexByAddress.value(address)));
        }
        Mh3gDrawCall draw;
        draw.firstIndex = model->indices.size();
        draw.materialIndex = materialIndex;
        for (int item = 2; item < strip.size(); ++item)
        {
            quint32 a = strip[item - 2], b = strip[item - 1], c = strip[item];
            if (item & 1) qSwap(a, b);
            if (a == b || b == c || a == c) continue;
            model->indices << a << b << c;
        }
        draw.indexCount = model->indices.size() - draw.firstIndex;
        if (draw.indexCount > 0) model->drawCalls.append(draw);
    }
    if (!hasBounds || model->indices.isEmpty())
    { if (error) *error = QString::fromUtf8("MOD 没有可显示的网格"); return false; }
    return true;
}

bool Mh3gModelLoader::parseMod(const QByteArray &data, Mh3gCpuModel *model, QString *error,
                               Mh3gModelLoadMode mode)
{
    return parseModWithPose(data, model, error, mode, 0);
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

QSharedPointer<Mh3gCpuModel> Mh3gModelLoader::loadWithPose(const QString &modelKey,
    const QString &arcPath, Mh3gModelLoadMode mode, const Mh3gSkeletonPose *pose)
{
    QSharedPointer<Mh3gCpuModel> model(new Mh3gCpuModel);
    model->modelKey = modelKey;
    QVector<Mh3gArchiveEntry> entries;
    if (!Mh3gArchiveLoader::read(arcPath, &entries, &model->error)) return model;
    QVector<int> modIndexes;
    QHash<QString, QImage> textures;
    QImage fallbackAlbedo, fallbackEnvironment;
    for (int index = 0; index < entries.size(); ++index)
    {
        if (entries[index].typeHash == HashMod) modIndexes.append(index);
        if (entries[index].typeHash != HashTex) continue;
        QImage image; QString textureError;
        if (!decodeTex(entries[index].data, &image, &textureError)) continue;
        const QString name = entries[index].name.toLower();
        textures.insert(name, image);
        if (fallbackAlbedo.isNull() && name.contains("_bm") && !name.contains("common")) fallbackAlbedo = image;
        if (fallbackEnvironment.isNull() && name.contains("env_")) fallbackEnvironment = image;
    }
    if (modIndexes.isEmpty()) { model->error = QString::fromUtf8("ARC 中没有 MOD 条目"); return model; }

    QHash<QString, QVector<Mh3gMaterial> > materialSets;
    for (int index = 0; index < entries.size(); ++index)
    {
        if (entries[index].typeHash != HashMrl) continue;
        QVector<Mh3gMaterial> materials;
        QString materialError;
        if (!parseMrl(entries[index].data, textures, &materials, &materialError))
        { model->error = QString::fromUtf8("%1：%2").arg(entries[index].name, materialError); return model; }
        for (Mh3gMaterial &material : materials)
        {
            if (material.albedo.isNull()) material.albedo = fallbackAlbedo;
        }
        materialSets.insert(entries[index].name.toLower(), materials);
    }

    bool haveBounds = false;
    for (int index : modIndexes)
    {
        Mh3gCpuModel part;
        if (!parseModWithPose(entries[index].data, &part, &model->error, mode, pose))
        { model->error = QString::fromUtf8("%1：%2").arg(entries[index].name, model->error); return model; }
        const quint32 baseVertex = quint32(model->vertices.size());
        const int baseIndex = model->indices.size();
        const int baseMaterial = model->materials.size();
        QVector<Mh3gMaterial> partMaterials = materialSets.value(entries[index].name.toLower());
        if (partMaterials.isEmpty())
        {
            Mh3gMaterial fallback;
            fallback.name = QString::fromUtf8("默认材质");
            fallback.albedo = fallbackAlbedo;
            fallback.environment = fallbackEnvironment;
            partMaterials.append(fallback);
        }
        model->materials += partMaterials;
        model->vertices += part.vertices;
        for (quint32 value : part.indices) model->indices.append(baseVertex + value);
        for (Mh3gDrawCall draw : part.drawCalls)
        {
            draw.firstIndex += baseIndex;
            draw.materialIndex = baseMaterial + qBound(0, draw.materialIndex, partMaterials.size() - 1);
            model->drawCalls.append(draw);
        }
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
    generateTangents(model.data());
    return model;
}

QSharedPointer<Mh3gCpuModel> Mh3gModelLoader::load(const QString &modelKey, const QString &arcPath,
                                                   Mh3gModelLoadMode mode)
{
    return loadWithPose(modelKey, arcPath, mode, 0);
}

QSharedPointer<Mh3gCpuModel> Mh3gModelLoader::loadCharacter(
    const QString &modelKey, const QVector<QPair<QString, QString> > &components,
    int bodyComponentIndex)
{
    QSharedPointer<Mh3gCpuModel> result(new Mh3gCpuModel);
    result->modelKey = modelKey;
    if (bodyComponentIndex < 0 || bodyComponentIndex >= components.size())
    { result->error = QString::fromUtf8("人物主体组件索引无效"); return result; }

    QVector<Mh3gArchiveEntry> bodyEntries;
    if (!Mh3gArchiveLoader::read(components[bodyComponentIndex].second, &bodyEntries, &result->error)) return result;
    Mh3gSkeletonPose fittingPose;
    QString poseError;
    for (const Mh3gArchiveEntry &entry : bodyEntries)
    {
        if (entry.typeHash != HashMod || entry.data.size() < 64) continue;
        quint16 boneCount = 0;
        quint32 boneOffset = 0;
        if (!readLe(entry.data, 6, &boneCount) || !readLe(entry.data, 0x28, &boneOffset)) continue;
        SkeletonPose internalPose;
        QVector<BindMatrix> worldByLocal;
        if (!buildSkeletonPose(entry.data, boneCount, boneOffset, true,
                &internalPose, &worldByLocal, &poseError)) continue;
        if (internalPose.contains(0) && internalPose.size() >= 20)
        {
            fittingPose = toPublicPose(internalPose);
            break;
        }
    }
    if (fittingPose.globalByBoneId.isEmpty())
    {
        result->error = QString::fromUtf8("人物主体没有完整玩家骨架：%1")
            .arg(poseError.isEmpty() ? QString::fromUtf8("未找到骨骼 0..19") : poseError);
        return result;
    }

    QVector<QSharedPointer<Mh3gCpuModel> > parts;
    parts.reserve(components.size());
    for (const QPair<QString, QString> &component : components)
    {
        QSharedPointer<Mh3gCpuModel> part = loadWithPose(component.first, component.second,
            Mh3gModelLoadMode::FittingPose, &fittingPose);
        if (!part->valid()) return part;
        parts.append(part);
    }
    return combine(modelKey, parts);
}

QSharedPointer<Mh3gCpuModel> Mh3gModelLoader::combine(
    const QString &modelKey, const QVector<QSharedPointer<Mh3gCpuModel> > &parts)
{
    QSharedPointer<Mh3gCpuModel> result(new Mh3gCpuModel);
    result->modelKey = modelKey;
    bool haveBounds = false;
    for (const QSharedPointer<Mh3gCpuModel> &part : parts)
    {
        if (!part || !part->valid())
        {
            result->error = part ? part->error : QString::fromUtf8("人物组件加载失败");
            return result;
        }
        const quint32 baseVertex = quint32(result->vertices.size());
        const int baseIndex = result->indices.size();
        const int baseMaterial = result->materials.size();
        result->vertices += part->vertices;
        result->materials += part->materials;
        for (quint32 index : part->indices) result->indices.append(baseVertex + index);
        for (Mh3gDrawCall draw : part->drawCalls)
        {
            draw.firstIndex += baseIndex;
            draw.materialIndex += baseMaterial;
            result->drawCalls.append(draw);
        }
        if (!haveBounds)
        {
            result->boundsMinimum = part->boundsMinimum; result->boundsMaximum = part->boundsMaximum;
            haveBounds = true;
        }
        else
        {
            result->boundsMinimum.setX(qMin(result->boundsMinimum.x(), part->boundsMinimum.x()));
            result->boundsMinimum.setY(qMin(result->boundsMinimum.y(), part->boundsMinimum.y()));
            result->boundsMinimum.setZ(qMin(result->boundsMinimum.z(), part->boundsMinimum.z()));
            result->boundsMaximum.setX(qMax(result->boundsMaximum.x(), part->boundsMaximum.x()));
            result->boundsMaximum.setY(qMax(result->boundsMaximum.y(), part->boundsMaximum.y()));
            result->boundsMaximum.setZ(qMax(result->boundsMaximum.z(), part->boundsMaximum.z()));
        }
    }
    if (!haveBounds || result->vertices.isEmpty() || result->indices.isEmpty())
        result->error = QString::fromUtf8("人物模型没有可显示的组件");
    return result;
}

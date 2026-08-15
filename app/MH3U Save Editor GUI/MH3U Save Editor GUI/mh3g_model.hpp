#ifndef MH3G_MODEL_HPP
#define MH3G_MODEL_HPP

#include <QByteArray>
#include <QImage>
#include <QSharedPointer>
#include <QString>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

struct Mh3gArchiveEntry
{
    QString name;
    quint32 typeHash = 0;
    QByteArray data;
};

class Mh3gArchiveLoader
{
public:
    static bool read(const QString &path, QVector<Mh3gArchiveEntry> *entries, QString *error);
    static bool validateWeaponArchive(const QString &path, QString *error);
};

struct Mh3gVertex
{
    QVector3D position;
    QVector3D normal;
    QVector4D tangent;
    QVector2D uv;
};

struct Mh3gMaterial
{
    QString name;
    QImage albedo;
    QImage normal;
    QImage specular;
    QImage environment;
    QVector4D albedoFactor = QVector4D(1, 1, 1, 1);
    float specularStrength = 0.28f;
    float roughness = 0.55f;
    float environmentStrength = 0.08f;
    bool transparent = false;
};

struct Mh3gDrawCall
{
    int firstIndex = 0;
    int indexCount = 0;
    int materialIndex = 0;
};

struct Mh3gCpuModel
{
    QString modelKey;
    QVector<Mh3gVertex> vertices;
    QVector<quint32> indices;
    QVector<Mh3gMaterial> materials;
    QVector<Mh3gDrawCall> drawCalls;
    QVector3D boundsMinimum;
    QVector3D boundsMaximum;
    QString error;

    bool valid() const { return error.isEmpty() && !vertices.isEmpty() && !indices.isEmpty(); }
    qint64 memoryBytes() const;
};

class Mh3gModelLoader
{
public:
    static QSharedPointer<Mh3gCpuModel> load(const QString &modelKey, const QString &arcPath);
    static bool parseMod(const QByteArray &data, Mh3gCpuModel *model, QString *error);
    static bool decodeTex(const QByteArray &data, QImage *image, QString *error);
};

#endif

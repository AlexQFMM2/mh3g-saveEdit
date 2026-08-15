#include "game_resource_manager.hpp"
#include "mh3g_model.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace
{
const char *RequiredFolders[] = {"w00","w01","w02","w03","w04","w06","w07","w08","w09","w10","w11","w12"};

bool looksLikeWeaponRoot(const QString &path)
{
    QDir root(path);
    if (!root.exists()) return false;
    for (const char *folder : RequiredFolders)
        if (!QFileInfo(root.filePath(QString::fromLatin1(folder))).isDir()) return false;
    return true;
}

QString hashFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return QString(); }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd())
    {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFile::NoError) { if (error) *error = file.errorString(); return QString(); }
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}

bool removeOwnedTree(const QString &path, QString *error)
{
    QFileInfo info(path);
    if (!info.exists()) return true;
    QDir directory(path);
    if (!directory.removeRecursively())
    { if (error) *error = QString::fromUtf8("无法清理应用资源目录：%1").arg(path); return false; }
    return true;
}
}

GameResourceManager::GameResourceManager()
{
}

QString GameResourceManager::resourceRoot() const
{
#ifdef Q_OS_WIN
    QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath("MH3USaveEditor/resources/mh3g/weapon-mod");
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath("resources/mh3g/weapon-mod");
#endif
}

QString GameResourceManager::activeRoot() const { return QDir(resourceRoot()).filePath("v1"); }

bool GameResourceManager::validateInstalled(QString *error) const
{
    QFile manifest(QDir(activeRoot()).filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) { if (error) *error = QString::fromUtf8("尚未导入 MH3G 武器模型资源"); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value("format").toString() != "mh3g-weapon-resources-v1"
        || root.value("files").toArray().size() != 558)
    { if (error) *error = QString::fromUtf8("本地武器资源 manifest 损坏，请重新导入"); return false; }
    return true;
}

bool GameResourceManager::available() const { return validateInstalled(0); }
QString GameResourceManager::statusText() const
{
    QString error;
    return validateInstalled(&error) ? QString::fromUtf8("已导入 558 个 MH3G 武器模型资源") : error;
}

QString GameResourceManager::archivePath(const QString &relativePath) const
{
    if (!available() || relativePath.contains("..") || QDir::isAbsolutePath(relativePath)) return QString();
    const QString path = QDir(activeRoot()).filePath(relativePath);
    return QFileInfo(path).isFile() ? path : QString();
}

QString GameResourceManager::locateWeaponModDirectory(const QString &selectedDirectory)
{
    const QString selected = QDir(selectedDirectory).absolutePath();
    const QStringList direct = QStringList() << selected
        << QDir(selected).filePath("arc/weapon/mod") << QDir(selected).filePath("weapon/mod")
        << QDir(selected).filePath("romfs/arc/weapon/mod") << QDir(selected).filePath("cci_unpacked/romfs/arc/weapon/mod");
    for (const QString &candidate : direct) if (looksLikeWeaponRoot(candidate)) return QDir(candidate).canonicalPath();

    QDirIterator iterator(selected, QStringList() << "mod", QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString candidate = iterator.next();
        QString relative = QDir(selected).relativeFilePath(candidate);
        if (relative.count('/') > 5) continue;
        if (looksLikeWeaponRoot(candidate)) return QDir(candidate).canonicalPath();
    }
    return QString();
}

bool GameResourceManager::importWeaponResources(const QString &selectedDirectory, QString *error)
{
    const QString sourceRoot = locateWeaponModDirectory(selectedDirectory);
    if (sourceRoot.isEmpty())
    { if (error) *error = QString::fromUtf8("未找到 romfs/arc/weapon/mod（应包含 12 个 wXX 目录）"); return false; }
    if (QDir::cleanPath(sourceRoot) == QDir::cleanPath(activeRoot()))
    { if (error) *error = QString::fromUtf8("请选择原始解包目录，而不是应用的本地缓存目录"); return false; }

    QStringList relativeFiles;
    for (const char *folderName : RequiredFolders)
    {
        const QString folder = QString::fromLatin1(folderName);
        QDir directory(QDir(sourceRoot).filePath(folder));
        const QStringList files = directory.entryList(QStringList() << "*.arc", QDir::Files, QDir::Name);
        for (const QString &file : files) relativeFiles.append(folder + "/" + file);
    }
    if (relativeFiles.size() != 558)
    { if (error) *error = QString::fromUtf8("武器 ARC 数量应为 558，当前找到 %1 个").arg(relativeFiles.size()); return false; }

    QDir().mkpath(resourceRoot());
    const QString staging = QDir(resourceRoot()).filePath("v1.importing");
    const QString previous = QDir(resourceRoot()).filePath("v1.old");
    if (!removeOwnedTree(staging, error) || !removeOwnedTree(previous, error) || !QDir().mkpath(staging))
    { if (error && error->isEmpty()) *error = QString::fromUtf8("无法创建临时导入目录"); return false; }

    QJsonArray filesJson;
    for (int index = 0; index < relativeFiles.size(); ++index)
    {
        const QString relative = relativeFiles[index];
        const QString source = QDir(sourceRoot).filePath(relative);
        QString validationError;
        if (!Mh3gArchiveLoader::validateWeaponArchive(source, &validationError))
        { removeOwnedTree(staging, 0); if (error) *error = QString::fromUtf8("%1\n%2").arg(relative, validationError); return false; }
        const QString destination = QDir(staging).filePath(relative);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(source, destination))
        { removeOwnedTree(staging, 0); if (error) *error = QString::fromUtf8("复制资源失败（可能空间不足）：%1").arg(relative); return false; }
        const QString digest = hashFile(destination, &validationError);
        if (digest.isEmpty()) { removeOwnedTree(staging, 0); if (error) *error = validationError; return false; }
        QJsonObject item;
        item.insert("path", relative); item.insert("bytes", double(QFileInfo(destination).size())); item.insert("sha256", digest);
        filesJson.append(item);
    }
    QJsonObject manifest;
    manifest.insert("format", "mh3g-weapon-resources-v1");
    manifest.insert("game", "mh3g");
    manifest.insert("arc_count", 558);
    manifest.insert("files", filesJson);
    QSaveFile manifestFile(QDir(staging).filePath("manifest.json"));
    if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented)) < 0
        || !manifestFile.commit())
    { removeOwnedTree(staging, 0); if (error) *error = QString::fromUtf8("无法写入本地资源 manifest"); return false; }

    QDir parent(resourceRoot());
    const bool hadActive = QFileInfo(activeRoot()).isDir();
    if (hadActive && !parent.rename("v1", "v1.old"))
    { removeOwnedTree(staging, 0); if (error) *error = QString::fromUtf8("无法替换旧的本地资源"); return false; }
    if (!parent.rename("v1.importing", "v1"))
    {
        if (hadActive) parent.rename("v1.old", "v1");
        if (error) *error = QString::fromUtf8("无法激活新导入的本地资源，旧资源已保留");
        return false;
    }
    removeOwnedTree(previous, 0);
    return true;
}

bool GameResourceManager::clearWeaponResources(QString *error)
{
    return removeOwnedTree(activeRoot(), error) && removeOwnedTree(QDir(resourceRoot()).filePath("v1.old"), error)
        && removeOwnedTree(QDir(resourceRoot()).filePath("v1.importing"), error);
}

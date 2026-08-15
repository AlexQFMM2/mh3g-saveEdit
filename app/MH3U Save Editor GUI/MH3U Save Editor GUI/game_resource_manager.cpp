#include "game_resource_manager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

GameResourceManager::GameResourceManager()
{
}

QString GameResourceManager::bundledRoot() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("resources/mh3g/weapon-mod/v1");
}

QString GameResourceManager::activeRoot() const { return bundledRoot(); }

bool GameResourceManager::validateRoot(const QString &resourcePath, QString *error) const
{
    QFile manifest(QDir(resourcePath).filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) { if (error) *error = QString::fromUtf8("当前整合包未包含 MH3G 武器模型资源"); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value("format").toString() != "mh3g-weapon-resources-v1"
        || root.value("files").toArray().size() != 558)
    { if (error) *error = QString::fromUtf8("武器资源 manifest 损坏或版本不匹配"); return false; }
    return true;
}

bool GameResourceManager::validateInstalled(QString *error) const { return validateRoot(activeRoot(), error); }

bool GameResourceManager::available() const { return validateInstalled(0); }
QString GameResourceManager::statusText() const
{
    QString error;
    if (!validateInstalled(&error)) return error;
    return QDir::cleanPath(activeRoot()) == QDir::cleanPath(bundledRoot())
        ? QString::fromUtf8("已直接加载整合包内的 558 个 MH3G 武器模型")
        : QString::fromUtf8("已加载 558 个 MH3G 武器模型资源");
}

QString GameResourceManager::archivePath(const QString &relativePath) const
{
    if (!available() || relativePath.contains("..") || QDir::isAbsolutePath(relativePath)) return QString();
    const QString path = QDir(activeRoot()).filePath(relativePath);
    return QFileInfo(path).isFile() ? path : QString();
}

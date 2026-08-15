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
    QString error;
    if (validateRoot(bundledRoot(), "mh3g-resources-v3", 2612, 0))
    {
        m_activeRoot = bundledRoot(); m_available = true; m_v2 = true; m_v3 = true;
        m_status = QString::fromUtf8("已加载 Resources v3：558 个武器、2,004 个防具和 50 个人物模型");
    }
    else if (validateRoot(legacyUnifiedRoot(), "mh3g-resources-v2", 2562, 0))
    {
        m_activeRoot = legacyUnifiedRoot(); m_available = true; m_v2 = true;
        m_status = QString::fromUtf8("已加载 Resources v2：完整人物试衣间需要 Resources v3");
    }
    else if (validateRoot(legacyWeaponRoot(), "mh3g-weapon-resources-v1", 558, &error))
    {
        m_activeRoot = legacyWeaponRoot(); m_available = true;
        m_status = QString::fromUtf8("已加载旧版整合包的 558 个武器模型；防具模型需要 Resources v2");
    }
    else m_status = error;
}

QString GameResourceManager::bundledRoot() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("resources/mh3g/v3");
}

QString GameResourceManager::legacyUnifiedRoot() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("resources/mh3g/v2");
}

QString GameResourceManager::legacyWeaponRoot() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath("resources/mh3g/weapon-mod/v1");
}

QString GameResourceManager::activeRoot() const
{
    return m_activeRoot;
}

bool GameResourceManager::validateRoot(const QString &resourcePath, const QString &format, int count, QString *error) const
{
    QFile manifest(QDir(resourcePath).filePath("manifest.json"));
    if (!manifest.open(QIODevice::ReadOnly)) { if (error) *error = QString::fromUtf8("当前整合包未包含 MH3G 模型资源"); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parseError);
    const QJsonObject root = document.object();
    if (parseError.error != QJsonParseError::NoError || root.value("format").toString() != format
        || root.value("files").toArray().size() != count)
    { if (error) *error = QString::fromUtf8("模型资源 manifest 损坏或版本不匹配"); return false; }
    return true;
}

bool GameResourceManager::validateInstalled(QString *error) const
{
    if (!m_available && error) *error = m_status;
    return m_available;
}

bool GameResourceManager::available() const { return m_available; }
bool GameResourceManager::armorAvailable() const
{
    return m_v2;
}
bool GameResourceManager::characterAvailable() const { return m_v3; }

QString GameResourceManager::statusText(const QString &relativePath) const
{
    const bool armor = relativePath.startsWith("armor-mod/");
    const bool character = relativePath.startsWith("character-mod/");
    if (armor && !armorAvailable()) return QString::fromUtf8("防具 3D 模型需要 Resources v2 整合包");
    if (character && !characterAvailable()) return QString::fromUtf8("完整人物试衣间需要 Resources v3 整合包");
    return m_status;
}

QString GameResourceManager::archivePath(const QString &relativePath) const
{
    if (!available() || relativePath.contains("..") || QDir::isAbsolutePath(relativePath)) return QString();
    QString resolved = relativePath;
    QString root = activeRoot();
    if (m_v2)
    {
        if (!resolved.startsWith("weapon-mod/") && !resolved.startsWith("armor-mod/")
            && !resolved.startsWith("character-mod/"))
            resolved.prepend("weapon-mod/");
        if (resolved.startsWith("character-mod/") && !m_v3) return QString();
    }
    else if (resolved.startsWith("armor-mod/")) return QString();
    const QString path = QDir(root).filePath(resolved);
    return QFileInfo(path).isFile() ? path : QString();
}

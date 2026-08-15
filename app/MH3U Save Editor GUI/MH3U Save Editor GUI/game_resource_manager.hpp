#ifndef GAME_RESOURCE_MANAGER_HPP
#define GAME_RESOURCE_MANAGER_HPP

#include <QString>

class GameResourceManager
{
public:
    GameResourceManager();

    QString bundledRoot() const;
    QString legacyUnifiedRoot() const;
    QString legacyWeaponRoot() const;
    QString activeRoot() const;
    bool available() const;
    bool armorAvailable() const;
    bool characterAvailable() const;
    QString statusText(const QString &relativePath = QString()) const;
    QString archivePath(const QString &relativePath) const;

private:
    bool validateRoot(const QString &root, const QString &format, int count, QString *error) const;
    bool validateInstalled(QString *error) const;
    QString m_activeRoot;
    QString m_status;
    bool m_available = false;
    bool m_v2 = false;
    bool m_v3 = false;
};

#endif

#ifndef GAME_RESOURCE_MANAGER_HPP
#define GAME_RESOURCE_MANAGER_HPP

#include <QString>

class GameResourceManager
{
public:
    GameResourceManager();

    QString resourceRoot() const;
    QString activeRoot() const;
    bool available() const;
    QString statusText() const;
    QString archivePath(const QString &relativePath) const;

    bool importWeaponResources(const QString &selectedDirectory, QString *error);
    bool clearWeaponResources(QString *error);
    static QString locateWeaponModDirectory(const QString &selectedDirectory);

private:
    bool validateInstalled(QString *error) const;
};

#endif

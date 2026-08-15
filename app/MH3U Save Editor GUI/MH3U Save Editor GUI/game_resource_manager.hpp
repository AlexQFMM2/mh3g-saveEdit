#ifndef GAME_RESOURCE_MANAGER_HPP
#define GAME_RESOURCE_MANAGER_HPP

#include <QString>

class GameResourceManager
{
public:
    GameResourceManager();

    QString bundledRoot() const;
    QString activeRoot() const;
    bool available() const;
    QString statusText() const;
    QString archivePath(const QString &relativePath) const;

private:
    bool validateRoot(const QString &root, QString *error) const;
    bool validateInstalled(QString *error) const;
};

#endif

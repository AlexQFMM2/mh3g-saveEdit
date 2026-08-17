#ifndef GAME_DATA_REPOSITORY_HPP
#define GAME_DATA_REPOSITORY_HPP

#include "mh3u_ds.hpp"

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariant>

struct equipment_data_t
{
    bool found;
    bool placeholder;
    bool confirmed;
    bool mh3gOnly;
    int slotCount;
    int maxUpgradeLevel;
    int combat;
    int gender;
    QString name;
    QString mappingSource;

    equipment_data_t()
        : found(false), placeholder(false), confirmed(false), mh3gOnly(false), slotCount(-1), maxUpgradeLevel(-1), combat(-1), gender(-1)
    {
    }
};

struct decoration_data_t
{
    bool found;
    bool confirmed;
    int slotCount;
    QString name;

    decoration_data_t() : found(false), confirmed(false), slotCount(-1) {}
};

struct skill_point_data_t
{
    int skillTreeId;
    int points;
    QString name;
};

struct active_skill_data_t
{
    int id;
    int skillTreeId;
    int points;
    QString name;
};

class GameDataRepository
{
public:
    static GameDataRepository &instance();

    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QString errorString() const;
    QString databasePath() const;

    dataset_t *characterOptions(const QString &kind) const;
    dataset_t *items() const;
    dataset_t *equipmentTypes() const;
    dataset_t *equipmentNames(int saveType) const;
    dataset_t *skills() const;
    dataset_t *decorations() const;
    dataset_t *charmClasses() const;
    dataset_t *searchItems(const QString &text) const;
    dataset_t *searchEquipment(const QString &text, int saveType = -1) const;

    equipment_data_t equipment(int saveType, int saveId) const;
    decoration_data_t decoration(int saveId) const;
    QList<skill_point_data_t> armorSkillPoints(int saveType, int saveId) const;
    QList<skill_point_data_t> decorationSkillPoints(int saveId) const;
    QList<active_skill_data_t> activeSkills(int skillTreeId) const;
    bool skillExists(int skillId) const;
    bool charmClassExists(int classId) const;
    bool charmCombinationExists(int classId, int slotCount, int skill1Id, int skill1Points,
                                int skill2Id, int skill2Points) const;

private:
    GameDataRepository();
    GameDataRepository(const GameDataRepository &);
    GameDataRepository &operator=(const GameDataRepository &);

    dataset_t *loadDataset(const QString &sql, const QList<QVariant> &arguments = QList<QVariant>()) const;
    QString m_connectionName;
    QString m_path;
    QString m_error;
};

#endif

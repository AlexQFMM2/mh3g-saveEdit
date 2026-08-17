#ifndef GAME_DATA_REPOSITORY_HPP
#define GAME_DATA_REPOSITORY_HPP

#include "mh3u_ds.hpp"

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSet>
#include <QVariant>

struct equipment_data_t
{
    bool found;
    bool placeholder;
    bool confirmed;
    bool mh3gOnly;
    int slotCount;
    int combat;
    int gender;
    QString name;
    QString mappingSource;

    equipment_data_t()
        : found(false), placeholder(false), confirmed(false), mh3gOnly(false), slotCount(-1), combat(-1), gender(-1)
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

enum skill_comparison_e
{
    SkillGreater,
    SkillGreaterEqual,
    SkillEqual,
    SkillLessEqual,
    SkillLess
};

struct skill_filter_t
{
    int skillTreeId;
    skill_comparison_e comparison;
    int points;
};

struct equipment_query_t
{
    QString text;
    int weaponType;
    int combat;
    int gender;
    int rarityMin;
    int rarityMax;
    int slotsMin;
    bool confirmedOnly;
    QList<skill_filter_t> skills;
    int offset;
    int limit;

    equipment_query_t()
        : weaponType(-1), combat(-1), gender(-1), rarityMin(-1), rarityMax(-1),
          slotsMin(-1), confirmedOnly(true), offset(0), limit(200) {}
};

struct loadout_candidate_t
{
    bool found;
    bool placeholder;
    bool confirmed;
    bool mh3gOnly;
    int saveType;
    int saveId;
    int rarity;
    int slotCount;
    int combat;
    int gender;
    int attack;
    int affinity;
    int defense;
    int baseDefense;
    int maxDefense;
    int fireRes;
    int waterRes;
    int iceRes;
    int thunderRes;
    int dragonRes;
    int classId;
    int skill1Id;
    int skill1Points;
    int skill2Id;
    int skill2Points;
    QString name;
    QString english;
    QString mappingStatus;
    QMap<int, int> skillPoints;

    loadout_candidate_t()
        : found(false), placeholder(false), confirmed(false), mh3gOnly(false), saveType(0), saveId(0),
          rarity(-1), slotCount(-1), combat(-1), gender(-1), attack(-1), affinity(0), defense(0),
          baseDefense(-1), maxDefense(-1), fireRes(0), waterRes(0), iceRes(0), thunderRes(0),
          dragonRes(0), classId(0), skill1Id(0), skill1Points(0), skill2Id(0), skill2Points(0) {}
};

struct skill_tree_data_t
{
    int id;
    QString name;
    QString english;
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
    QList<skill_tree_data_t> skillTreesDetailed() const;
    QList<loadout_candidate_t> queryCandidates(int expectedSaveType, const equipment_query_t &query,
                                               int *total = NULL) const;
    loadout_candidate_t candidate(int saveType, int saveId) const;
    loadout_candidate_t charmCandidate(int classId, int slotCount, int skill1Id, int skill1Points,
                                       int skill2Id, int skill2Points) const;
    QList<loadout_candidate_t> decorationCandidates() const;
    QString dataVersion() const;
    bool skillExists(int skillId) const;
    bool charmClassExists(int classId) const;
    bool charmCombinationExists(int classId, int slotCount, int skill1Id, int skill1Points,
                                int skill2Id, int skill2Points) const;
    QString charmClassName(int classId) const;
    QString skillName(int skillId) const;
    QList<int> charmSlots(int classId) const;
    QList<int> charmSkillPoints(int classId, int skillId, int position) const;
    bool charmSkillPairExists(int classId, int skill1Id, int skill2Id) const;

private:
    GameDataRepository();
    GameDataRepository(const GameDataRepository &);
    GameDataRepository &operator=(const GameDataRepository &);

    dataset_t *loadDataset(const QString &sql, const QList<QVariant> &arguments = QList<QVariant>()) const;
    bool loadCharmRules();
    static QString charmSkillRuleKey(int classId, int position, int skillId);
    static QString charmSkillPairKey(int classId, int skill1Id, int skill2Id);
    QString m_connectionName;
    QString m_path;
    QString m_error;
    QMap<int, QString> m_charmClassNames;
    QMap<int, QString> m_skillNames;
    QMap<int, QSet<int> > m_charmSlotRules;
    QMap<QString, QSet<int> > m_charmSkillPointRules;
    QSet<QString> m_charmSkillPairRules;
};

#endif

#ifndef ENCYCLOPEDIA_DATA_HPP
#define ENCYCLOPEDIA_DATA_HPP

#include <QMap>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

struct EncyclopediaWeaponType
{
    int dexType = -1;
    int saveType = -1;
    QString slug;
    QString name;
    QString english;
};

struct EncyclopediaWeapon
{
    int dexId = -1;
    int dexType = -1;
    int saveType = -1;
    int saveId = -1;
    int displayOrder = 0;
    QString name;
    QString english;
    QString japanese;
    int rarity = 0;
    int attack = 0;
    int attribute1Id = -1;
    int attribute1Value = 0;
    int attribute2Id = -1;
    int attribute2Value = 0;
    double affinity = 0.0;
    int defense = 0;
    int slotCount = 0;
    int productionPrice = 0;
    int upgradePrice = 0;
    QVector<int> sharpness;
    int sharpPlus = 0;
    int gunlanceType = 0;
    int switchAxePhial = 0;
    int huntingNotes[3] = {0, 0, 0};
    int gunReload = 0;
    int gunSteadiness = 0;
    int gunRecoil = 0;
    int bowShot = 0;
    int bowCharges[4] = {0, 0, 0, 0};
    QString imageKey;
    QString modelKey;
    QString modelArcPath;
    QString modelMappingStatus;
    bool writable = false;
    QString mappingSource;
};

struct EncyclopediaItem
{
    int dexId = -1;
    int saveId = -1;
    QString name;
    QString english;
    QString japanese;
    int rarity = 0;
    int maxCount = 99;
    int sellPrice = 0;
    int buyPrice = 0;
    bool writable = false;
};

struct EncyclopediaMaterial
{
    EncyclopediaItem item;
    int quantity = 0;
    QString kind;
    QString region;
};

struct EncyclopediaActiveSkill
{
    int id = -1;
    int points = 0;
    QString name;
};

struct EncyclopediaArmorSkill
{
    int treeId = -1;
    QString treeName;
    int points = 0;
    QVector<EncyclopediaActiveSkill> thresholds;
};

struct EncyclopediaArmor
{
    int dexId = -1;
    int saveType = -1;
    int saveId = -1;
    QString setId;
    QString part;
    QString combat;
    QString gender;
    QString name;
    QString english;
    QString japanese;
    int rarity = 0;
    int slotCount = 0;
    int defense = 0;
    int maxDefense = 0;
    int price = 0;
    int resistances[5] = {0, 0, 0, 0, 0};
    bool writable = false;
    QString mappingSource;
    int maleModelId = -1;
    int femaleModelId = -1;
    QString modelMappingStatus;
    QString modelMappingSource;
};

struct EncyclopediaArmorSet
{
    QString setId;
    QString rank;
    QString combat;
    QString name;
    QString english;
    int displayOrder = 0;
    QString reviewStatus;
    QVector<int> members;
};

struct EncyclopediaArmorModel
{
    int modelId = -1;
    QString modelKey;
    QString arcRelativePath;
    QString mappingStatus;
    QString mappingSource;
    bool available() const { return modelId >= 0 && !modelKey.isEmpty() && !arcRelativePath.isEmpty(); }
};

struct EncyclopediaCharacterModel
{
    QString modelKey;
    QString arcRelativePath;
    QString gender;
    QString kind;
    int variant = -1;
};

class EncyclopediaRepository
{
public:
    EncyclopediaRepository();
    ~EncyclopediaRepository();

    bool open();
    bool available() const;
    QString error() const;
    QString databasePath() const;

    QVector<EncyclopediaWeaponType> weaponTypes() const;
    QVector<int> attributeIds() const;
    QVector<int> weaponIdsForType(int dexType) const;
    QVector<int> rootIdsForType(int dexType) const;
    EncyclopediaWeapon weapon(int dexId) const;
    EncyclopediaWeapon weaponBySaveId(int saveType, int saveId) const;
    EncyclopediaItem item(int dexId) const;
    EncyclopediaItem itemBySaveId(int saveId) const;
    QVector<int> parentIds(int dexId) const;
    QVector<int> childIds(int dexId) const;
    QVector<EncyclopediaMaterial> materials(int weaponDexId) const;
    QVector<int> weaponUses(int itemDexId) const;
    QVector<int> armorUses(int itemDexId) const;
    QString attributeName(int id) const;
    QVector<EncyclopediaArmorSet> armorSets() const;
    EncyclopediaArmorSet armorSet(const QString &setId) const;
    EncyclopediaArmor armor(int dexId) const;
    QVector<EncyclopediaMaterial> armorMaterials(int armorDexId) const;
    QVector<EncyclopediaArmorSkill> armorSkills(int armorDexId) const;
    EncyclopediaArmorModel armorModel(int armorDexId, const QString &gender) const;
    EncyclopediaArmorModel baseArmorModel(const QString &gender, const QString &part) const;
    EncyclopediaCharacterModel characterModel(const QString &gender, const QString &kind, int variant) const;

private:
    QString locateDatabase() const;
    bool loadAll();

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_databasePath;
    QString m_error;
    QVector<EncyclopediaWeaponType> m_types;
    QMap<int, EncyclopediaWeapon> m_weapons;
    QMap<quint32, int> m_weaponBySave;
    QMap<int, QVector<int> > m_weaponsByType;
    QMap<int, QVector<int> > m_rootsByType;
    QMap<int, QVector<int> > m_parents;
    QMap<int, QVector<int> > m_children;
    QMap<int, EncyclopediaItem> m_items;
    QMap<int, int> m_itemBySave;
    QMap<int, QVector<EncyclopediaMaterial> > m_materials;
    QMap<int, QVector<int> > m_itemUses;
    QMap<int, QString> m_attributes;
    QVector<EncyclopediaArmorSet> m_armorSets;
    QMap<QString, EncyclopediaArmorSet> m_armorSetsById;
    QMap<int, EncyclopediaArmor> m_armors;
    QMap<int, QVector<EncyclopediaMaterial> > m_armorMaterials;
    QMap<int, QVector<EncyclopediaArmorSkill> > m_armorSkills;
    QMap<int, QVector<int> > m_armorItemUses;
    QMap<QString, EncyclopediaArmorModel> m_armorModels;
    QMap<QString, EncyclopediaCharacterModel> m_characterModels;
};

#endif

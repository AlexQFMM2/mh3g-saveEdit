#include "encyclopedia_data.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVariant>

EncyclopediaRepository::EncyclopediaRepository()
{
}

EncyclopediaRepository::~EncyclopediaRepository()
{
    if (m_database.isValid())
    {
        m_database.close();
        m_database = QSqlDatabase();
    }
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QString EncyclopediaRepository::locateDatabase() const
{
    const QString app = QCoreApplication::applicationDirPath();
    const QString current = QDir::currentPath();
    const QStringList candidates = QStringList()
        << QDir(app).filePath("data/encyclopedia.sqlite")
        << QDir(app).filePath("../data/encyclopedia.sqlite")
        << QDir(app).filePath("../../data/encyclopedia.sqlite")
        << QDir(current).filePath("data/encyclopedia.sqlite")
        << QDir(current).filePath("../data/encyclopedia.sqlite");
    for (int index = 0; index < candidates.size(); ++index)
    {
        QFileInfo file(candidates[index]);
        if (file.isFile())
        {
            return file.canonicalFilePath();
        }
    }
    return QString();
}

bool EncyclopediaRepository::open()
{
    m_databasePath = locateDatabase();
    if (m_databasePath.isEmpty())
    {
        m_error = QString::fromUtf8("找不到 data/encyclopedia.sqlite。");
        return false;
    }
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
    {
        m_error = QString::fromUtf8("Qt SQLite 驱动不可用（QSQLITE）。");
        return false;
    }
    m_connectionName = QString("mh3g-encyclopedia-%1").arg(QUuid::createUuid().toString());
    m_database = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_database.setConnectOptions("QSQLITE_OPEN_READONLY");
    m_database.setDatabaseName(m_databasePath);
    if (!m_database.open())
    {
        m_error = m_database.lastError().text();
        return false;
    }
    return loadAll();
}

bool EncyclopediaRepository::available() const { return m_database.isOpen() && m_error.isEmpty(); }
QString EncyclopediaRepository::error() const { return m_error; }
QString EncyclopediaRepository::databasePath() const { return m_databasePath; }

bool EncyclopediaRepository::loadAll()
{
    QSqlQuery query(m_database);
    if (!query.exec("SELECT value FROM meta WHERE key='format'") || !query.next()
        || query.value(0).toString() != "mh3g-encyclopedia-v2")
    {
        m_error = QString::fromUtf8("图鉴数据库版本不匹配，需要 v2。");
        return false;
    }
    if (!query.exec("SELECT dex_type,save_type,slug,name_cn,name_en FROM weapon_types ORDER BY display_order"))
    {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next())
    {
        EncyclopediaWeaponType type;
        type.dexType = query.value(0).toInt();
        type.saveType = query.value(1).toInt();
        type.slug = query.value(2).toString();
        type.name = query.value(3).toString();
        type.english = query.value(4).toString();
        m_types.append(type);
    }

    if (!query.exec("SELECT id,name_cn FROM special_attributes ORDER BY id"))
    {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next()) m_attributes[query.value(0).toInt()] = query.value(1).toString();

    if (!query.exec("SELECT dex_id,save_id,name_cn,name_en,name_jp,rarity,max_count,sell_price,buy_price,writable FROM items ORDER BY dex_id"))
    {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next())
    {
        EncyclopediaItem item;
        item.dexId = query.value(0).toInt();
        item.saveId = query.value(1).isNull() ? -1 : query.value(1).toInt();
        item.name = query.value(2).toString();
        item.english = query.value(3).toString();
        item.japanese = query.value(4).toString();
        item.rarity = query.value(5).isNull() ? 0 : query.value(5).toInt();
        item.maxCount = query.value(6).isNull() ? 99 : query.value(6).toInt();
        item.sellPrice = query.value(7).isNull() ? 0 : query.value(7).toInt();
        item.buyPrice = query.value(8).isNull() ? 0 : query.value(8).toInt();
        item.writable = query.value(9).toBool();
        m_items[item.dexId] = item;
        if (item.saveId >= 0) m_itemBySave[item.saveId] = item.dexId;
    }

    const char *weaponSql =
        "SELECT dex_id,dex_type,save_type,save_id,display_order,name_cn,name_en,name_jp,rarity,attack,"
        "attribute1_id,attribute1_value,attribute2_id,attribute2_value,affinity,defense,slots,"
        "production_price,upgrade_price,sharp_red,sharp_orange,sharp_yellow,sharp_green,sharp_blue,"
        "sharp_white,sharp_purple,sharp_plus,gunlance_type,switch_axe_phial,hunting_note1,hunting_note2,"
        "hunting_note3,gun_reload,gun_steadiness,gun_recoil,bow_shot,bow_charge1,bow_charge2,bow_charge3,"
        "bow_charge4,image_key,writable,w.mapping_source,wm.model_key,mr.arc_relative_path,wm.mapping_status "
        "FROM weapons w LEFT JOIN weapon_models wm ON wm.weapon_dex_id=w.dex_id "
        "LEFT JOIN model_resources mr ON mr.model_key=wm.model_key ORDER BY dex_type,display_order";
    if (!query.exec(weaponSql))
    {
        m_error = query.lastError().text();
        return false;
    }
    while (query.next())
    {
        EncyclopediaWeapon weapon;
        int column = 0;
        weapon.dexId = query.value(column++).toInt();
        weapon.dexType = query.value(column++).toInt();
        weapon.saveType = query.value(column).isNull() ? -1 : query.value(column).toInt(); column++;
        weapon.saveId = query.value(column).isNull() ? -1 : query.value(column).toInt(); column++;
        weapon.displayOrder = query.value(column++).toInt();
        weapon.name = query.value(column++).toString();
        weapon.english = query.value(column++).toString();
        weapon.japanese = query.value(column++).toString();
        weapon.rarity = query.value(column++).toInt();
        weapon.attack = query.value(column++).toInt();
        weapon.attribute1Id = query.value(column++).toInt();
        weapon.attribute1Value = query.value(column++).toInt();
        weapon.attribute2Id = query.value(column++).toInt();
        weapon.attribute2Value = query.value(column++).toInt();
        weapon.affinity = query.value(column++).toDouble();
        weapon.defense = query.value(column++).toInt();
        weapon.slotCount = query.value(column++).toInt();
        weapon.productionPrice = query.value(column++).toInt();
        weapon.upgradePrice = query.value(column++).toInt();
        weapon.sharpness.clear();
        for (int index = 0; index < 7; ++index) weapon.sharpness.append(query.value(column++).toInt());
        weapon.sharpPlus = query.value(column++).toInt();
        weapon.gunlanceType = query.value(column++).toInt();
        weapon.switchAxePhial = query.value(column++).toInt();
        for (int index = 0; index < 3; ++index) weapon.huntingNotes[index] = query.value(column++).toInt();
        weapon.gunReload = query.value(column++).toInt();
        weapon.gunSteadiness = query.value(column++).toInt();
        weapon.gunRecoil = query.value(column++).toInt();
        weapon.bowShot = query.value(column++).toInt();
        for (int index = 0; index < 4; ++index) weapon.bowCharges[index] = query.value(column++).toInt();
        weapon.imageKey = query.value(column++).toString();
        weapon.writable = query.value(column++).toBool();
        weapon.mappingSource = query.value(column++).toString();
        weapon.modelKey = query.value(column++).toString();
        weapon.modelArcPath = query.value(column++).toString();
        weapon.modelMappingStatus = query.value(column++).toString();
        m_weapons[weapon.dexId] = weapon;
        m_weaponsByType[weapon.dexType].append(weapon.dexId);
        if (weapon.saveType >= 0 && weapon.saveId >= 0)
        {
            const quint32 key = (quint32(weapon.saveType) << 16) | quint32(weapon.saveId);
            m_weaponBySave[key] = weapon.dexId;
        }
    }

    if (!query.exec("SELECT dex_type,weapon_dex_id FROM weapon_roots ORDER BY dex_type,weapon_dex_id"))
    {
        m_error = query.lastError().text(); return false;
    }
    while (query.next()) m_rootsByType[query.value(0).toInt()].append(query.value(1).toInt());
    if (!query.exec("SELECT parent_dex_id,child_dex_id FROM weapon_edges ORDER BY parent_dex_id,child_dex_id"))
    {
        m_error = query.lastError().text(); return false;
    }
    while (query.next())
    {
        const int parent = query.value(0).toInt();
        const int child = query.value(1).toInt();
        m_children[parent].append(child);
        m_parents[child].append(parent);
    }
    if (!query.exec("SELECT weapon_dex_id,item_dex_id,quantity,kind,region FROM weapon_materials ORDER BY weapon_dex_id,kind,item_dex_id"))
    {
        m_error = query.lastError().text(); return false;
    }
    while (query.next())
    {
        EncyclopediaMaterial material;
        const int weaponId = query.value(0).toInt();
        const int itemId = query.value(1).toInt();
        material.item = m_items.value(itemId);
        material.quantity = query.value(2).toInt();
        material.kind = query.value(3).toString();
        material.region = query.value(4).toString();
        m_materials[weaponId].append(material);
        m_itemUses[itemId].append(weaponId);
    }

    if (!query.exec("SELECT set_id,rank,combat,model_id,name_cn,name_en,display_order,review_status FROM armor_sets ORDER BY display_order"))
    { m_error = query.lastError().text(); return false; }
    QStringList orderedSetIds;
    while (query.next())
    {
        EncyclopediaArmorSet set;
        set.setId = query.value(0).toString();
        set.rank = query.value(1).toString();
        set.combat = query.value(2).toString();
        set.modelId = query.value(3).toInt();
        set.name = query.value(4).toString();
        set.english = query.value(5).toString();
        set.displayOrder = query.value(6).toInt();
        set.reviewStatus = query.value(7).toString();
        m_armorSetsById[set.setId] = set;
        orderedSetIds.append(set.setId);
    }
    const char *armorSql =
        "SELECT a.dex_id,a.save_type,a.save_id,m.set_id,a.part,a.combat,a.gender,a.name_cn,a.name_en,a.name_jp,"
        "a.rarity,a.slots,a.defense,a.max_defense,a.price,a.fire_res,a.water_res,a.ice_res,a.thunder_res,a.dragon_res,"
        "a.writable,a.mapping_source FROM armors a JOIN armor_set_members m ON m.armor_dex_id=a.dex_id "
        "ORDER BY m.set_id,m.slot_order";
    if (!query.exec(armorSql)) { m_error = query.lastError().text(); return false; }
    while (query.next())
    {
        EncyclopediaArmor armor;
        int column = 0;
        armor.dexId = query.value(column++).toInt();
        armor.saveType = query.value(column).isNull() ? -1 : query.value(column).toInt(); column++;
        armor.saveId = query.value(column).isNull() ? -1 : query.value(column).toInt(); column++;
        armor.setId = query.value(column++).toString();
        armor.part = query.value(column++).toString();
        armor.combat = query.value(column++).toString();
        armor.gender = query.value(column++).toString();
        armor.name = query.value(column++).toString();
        armor.english = query.value(column++).toString();
        armor.japanese = query.value(column++).toString();
        armor.rarity = query.value(column++).toInt();
        armor.slotCount = query.value(column++).toInt();
        armor.defense = query.value(column++).toInt();
        armor.maxDefense = query.value(column++).toInt();
        armor.price = query.value(column++).toInt();
        for (int index = 0; index < 5; ++index) armor.resistances[index] = query.value(column++).toInt();
        armor.writable = query.value(column++).toBool();
        armor.mappingSource = query.value(column++).toString();
        m_armors[armor.dexId] = armor;
        m_armorSetsById[armor.setId].members.append(armor.dexId);
    }
    for (const QString &setId : orderedSetIds) m_armorSets.append(m_armorSetsById.value(setId));

    if (!query.exec("SELECT armor_dex_id,item_dex_id,quantity FROM armor_materials ORDER BY armor_dex_id,item_dex_id"))
    { m_error = query.lastError().text(); return false; }
    while (query.next())
    {
        EncyclopediaMaterial material;
        const int armorId = query.value(0).toInt();
        const int itemId = query.value(1).toInt();
        material.item = m_items.value(itemId);
        material.quantity = query.value(2).toInt();
        material.kind = "production";
        m_armorMaterials[armorId].append(material);
        m_armorItemUses[itemId].append(armorId);
    }

    QMap<int, QVector<EncyclopediaActiveSkill> > thresholds;
    if (!query.exec("SELECT skill_tree_id,id,points,name_cn FROM active_skills ORDER BY skill_tree_id,points,id"))
    { m_error = query.lastError().text(); return false; }
    while (query.next())
    {
        EncyclopediaActiveSkill skill;
        const int treeId = query.value(0).toInt();
        skill.id = query.value(1).toInt(); skill.points = query.value(2).toInt(); skill.name = query.value(3).toString();
        thresholds[treeId].append(skill);
    }
    if (!query.exec("SELECT p.armor_dex_id,p.skill_tree_id,t.name_cn,p.points FROM armor_skill_points p "
                    "JOIN skill_trees t ON t.id=p.skill_tree_id ORDER BY p.armor_dex_id,p.skill_tree_id"))
    { m_error = query.lastError().text(); return false; }
    while (query.next())
    {
        EncyclopediaArmorSkill skill;
        const int armorId = query.value(0).toInt();
        skill.treeId = query.value(1).toInt(); skill.treeName = query.value(2).toString(); skill.points = query.value(3).toInt();
        skill.thresholds = thresholds.value(skill.treeId);
        m_armorSkills[armorId].append(skill);
    }
    if (!query.exec("SELECT model_id,gender,part,model_key,arc_relative_path FROM armor_model_resources ORDER BY model_id,gender,part"))
    { m_error = query.lastError().text(); return false; }
    while (query.next())
    {
        const QString key = QString("%1|%2|%3").arg(query.value(0).toInt()).arg(query.value(1).toString(), query.value(2).toString());
        EncyclopediaArmorModel model;
        model.modelKey = query.value(3).toString(); model.arcRelativePath = query.value(4).toString();
        m_armorModels[key] = model;
    }
    return true;
}

QVector<EncyclopediaWeaponType> EncyclopediaRepository::weaponTypes() const { return m_types; }
QVector<int> EncyclopediaRepository::attributeIds() const { return m_attributes.keys().toVector(); }
QVector<int> EncyclopediaRepository::weaponIdsForType(int dexType) const { return m_weaponsByType.value(dexType); }
QVector<int> EncyclopediaRepository::rootIdsForType(int dexType) const { return m_rootsByType.value(dexType); }
EncyclopediaWeapon EncyclopediaRepository::weapon(int dexId) const { return m_weapons.value(dexId); }
EncyclopediaItem EncyclopediaRepository::item(int dexId) const { return m_items.value(dexId); }
QVector<int> EncyclopediaRepository::parentIds(int dexId) const { return m_parents.value(dexId); }
QVector<int> EncyclopediaRepository::childIds(int dexId) const { return m_children.value(dexId); }
QVector<EncyclopediaMaterial> EncyclopediaRepository::materials(int weaponDexId) const { return m_materials.value(weaponDexId); }
QVector<int> EncyclopediaRepository::weaponUses(int itemDexId) const { return m_itemUses.value(itemDexId); }
QVector<int> EncyclopediaRepository::armorUses(int itemDexId) const { return m_armorItemUses.value(itemDexId); }
QString EncyclopediaRepository::attributeName(int id) const { return m_attributes.value(id, QString("#%1").arg(id)); }
QVector<EncyclopediaArmorSet> EncyclopediaRepository::armorSets() const { return m_armorSets; }
EncyclopediaArmorSet EncyclopediaRepository::armorSet(const QString &setId) const { return m_armorSetsById.value(setId); }
EncyclopediaArmor EncyclopediaRepository::armor(int dexId) const { return m_armors.value(dexId); }
QVector<EncyclopediaMaterial> EncyclopediaRepository::armorMaterials(int armorDexId) const { return m_armorMaterials.value(armorDexId); }
QVector<EncyclopediaArmorSkill> EncyclopediaRepository::armorSkills(int armorDexId) const { return m_armorSkills.value(armorDexId); }
EncyclopediaArmorModel EncyclopediaRepository::armorModel(int modelId, const QString &gender, const QString &part) const
{
    return m_armorModels.value(QString("%1|%2|%3").arg(modelId).arg(gender, part));
}

EncyclopediaWeapon EncyclopediaRepository::weaponBySaveId(int saveType, int saveId) const
{
    return weapon(m_weaponBySave.value((quint32(saveType) << 16) | quint32(saveId), -1));
}

EncyclopediaItem EncyclopediaRepository::itemBySaveId(int saveId) const
{
    return item(m_itemBySave.value(saveId, -1));
}

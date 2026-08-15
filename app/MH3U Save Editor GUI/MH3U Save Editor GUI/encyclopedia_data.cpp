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
        "bow_charge4,image_key,writable,mapping_source FROM weapons ORDER BY dex_type,display_order";
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
QString EncyclopediaRepository::attributeName(int id) const { return m_attributes.value(id, QString("#%1").arg(id)); }

EncyclopediaWeapon EncyclopediaRepository::weaponBySaveId(int saveType, int saveId) const
{
    return weapon(m_weaponBySave.value((quint32(saveType) << 16) | quint32(saveId), -1));
}

EncyclopediaItem EncyclopediaRepository::itemBySaveId(int saveId) const
{
    return item(m_itemBySave.value(saveId, -1));
}

#include "game_data_repository.hpp"
#include "mh3u_se.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace
{
QString displayIdentifier(const QString &name, const QString &english)
{
    if (name.isEmpty()) return english;
    if (!english.isEmpty() && name != english) return name + " (" + english + ")";
    return name;
}

QString comparisonSql(skill_comparison_e comparison)
{
    switch (comparison)
    {
        case SkillGreater: return ">";
        case SkillGreaterEqual: return ">=";
        case SkillEqual: return "=";
        case SkillLessEqual: return "<=";
        case SkillLess: return "<";
    }
    return "=";
}

bool comparisonMatches(int value, skill_comparison_e comparison, int expected)
{
    switch (comparison)
    {
        case SkillGreater: return value > expected;
        case SkillGreaterEqual: return value >= expected;
        case SkillEqual: return value == expected;
        case SkillLessEqual: return value <= expected;
        case SkillLess: return value < expected;
    }
    return value == expected;
}

void bindArguments(QSqlQuery &query, const QList<QVariant> &arguments)
{
    for (int index = 0; index < arguments.size(); ++index) query.addBindValue(arguments.at(index));
}
}

GameDataRepository &GameDataRepository::instance()
{
    static GameDataRepository repository;
    return repository;
}

GameDataRepository::GameDataRepository() : m_connectionName("mh3g_game_data") {}

bool GameDataRepository::open(const QString &path)
{
    close();
    m_error.clear();
    QFileInfo info(path);
    if (!info.isFile())
    {
        m_error = QString::fromUtf8("找不到游戏数据库：%1").arg(path);
        return false;
    }
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
    {
        m_error = QString::fromUtf8("Qt QSQLITE 驱动不可用；请确认 sqldrivers/qsqlite.dll 已随程序发布。");
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setConnectOptions("QSQLITE_OPEN_READONLY");
    db.setDatabaseName(info.absoluteFilePath());
    if (!db.open())
    {
        m_error = QString::fromUtf8("无法只读打开游戏数据库：%1").arg(db.lastError().text());
        close();
        return false;
    }
    QSqlQuery query(db);
    if (!query.exec("PRAGMA foreign_keys=ON") || !query.exec("PRAGMA user_version") || !query.next() || query.value(0).toInt() != 1)
    {
        m_error = QString::fromUtf8("游戏数据库版本不匹配，需要 user_version=1。");
        close();
        return false;
    }
    if (!query.exec("SELECT value FROM meta WHERE key='format'") || !query.next() ||
        query.value(0).toString() != "mh3g-save-editor-data-v1")
    {
        m_error = QString::fromUtf8("游戏数据库格式标识无效。");
        close();
        return false;
    }
    if (!query.exec("PRAGMA integrity_check") || !query.next() || query.value(0).toString() != "ok")
    {
        m_error = QString::fromUtf8("游戏数据库完整性检查失败。");
        close();
        return false;
    }
    if (!loadCharmRules())
    {
        close();
        return false;
    }
    m_path = info.absoluteFilePath();
    return true;
}

void GameDataRepository::close()
{
    m_charmClassNames.clear();
    m_skillNames.clear();
    m_charmSlotRules.clear();
    m_charmSkillPointRules.clear();
    m_charmSkillPairRules.clear();
    if (QSqlDatabase::contains(m_connectionName))
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            if (db.isValid()) db.close();
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_path.clear();
}

QString GameDataRepository::charmSkillRuleKey(int classId, int position, int skillId)
{
    return QString("%1:%2:%3").arg(classId).arg(position).arg(skillId);
}

QString GameDataRepository::charmSkillPairKey(int classId, int skill1Id, int skill2Id)
{
    return QString("%1:%2:%3").arg(classId).arg(skill1Id).arg(skill2Id);
}

bool GameDataRepository::loadCharmRules()
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("SELECT save_id,name_cn FROM charm_classes"))
    {
        m_error = QString::fromUtf8("读取护石品级规则失败：%1").arg(query.lastError().text());
        return false;
    }
    while (query.next()) m_charmClassNames.insert(query.value(0).toInt(), query.value(1).toString());

    if (!query.exec("SELECT id,name_cn FROM skill_trees"))
    {
        m_error = QString::fromUtf8("读取护石技能名称失败：%1").arg(query.lastError().text());
        return false;
    }
    m_skillNames.insert(0, QString::fromUtf8("无"));
    while (query.next()) m_skillNames.insert(query.value(0).toInt(), query.value(1).toString());

    if (!query.exec("SELECT class_id,slots,skill1_id,skill1_points,skill2_id,skill2_points FROM charm_combinations"))
    {
        m_error = QString::fromUtf8("读取原生护石组合规则失败：%1").arg(query.lastError().text());
        return false;
    }
    while (query.next())
    {
        const int classId = query.value(0).toInt();
        const int skill1Id = query.value(2).toInt();
        const int skill2Id = query.value(4).toInt();
        m_charmSlotRules[classId].insert(query.value(1).toInt());
        m_charmSkillPointRules[charmSkillRuleKey(classId, 1, skill1Id)].insert(query.value(3).toInt());
        m_charmSkillPointRules[charmSkillRuleKey(classId, 2, skill2Id)].insert(query.value(5).toInt());
        m_charmSkillPairRules.insert(charmSkillPairKey(classId, skill1Id, skill2Id));
    }
    return true;
}

bool GameDataRepository::isOpen() const
{
    return QSqlDatabase::contains(m_connectionName) && QSqlDatabase::database(m_connectionName, false).isOpen();
}

QString GameDataRepository::errorString() const { return m_error; }
QString GameDataRepository::databasePath() const { return m_path; }

dataset_t *GameDataRepository::loadDataset(const QString &sql, const QList<QVariant> &arguments) const
{
    if (!isOpen()) return NULL;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql);
    for (int i = 0; i < arguments.size(); ++i) query.bindValue(i, arguments.at(i));
    if (!query.exec()) return NULL;
    dataset_t *result = new dataset_t();
    while (query.next())
    {
        dataitem_t item;
        item.count = query.value(0).toUInt();
        item.name = query.value(1).toString().toStdString();
        item.english = query.value(2).toString().toStdString();
        item.source = query.value(3).toString().toStdString();
        item.identifier = displayIdentifier(query.value(1).toString(), query.value(2).toString()).toStdString();
        result->push_back(item);
    }
    return result;
}

dataset_t *GameDataRepository::characterOptions(const QString &kind) const
{
    return loadDataset("SELECT id,name_cn,name_en,source FROM character_options WHERE kind=? ORDER BY id", QList<QVariant>() << kind);
}
dataset_t *GameDataRepository::items() const
{
    return loadDataset("SELECT save_id,name_cn,name_en,source FROM items ORDER BY save_id");
}
dataset_t *GameDataRepository::equipmentTypes() const
{
    return loadDataset("SELECT save_type,name_cn,name_en,source FROM equipment_types ORDER BY save_type");
}
dataset_t *GameDataRepository::equipmentNames(int saveType) const
{
    if (saveType >= 1 && saveType <= 5)
        return loadDataset("SELECT save_id,name_cn,name_en,mapping_source FROM armors WHERE save_type=? ORDER BY save_id", QList<QVariant>() << saveType);
    if (saveType >= 7 && saveType <= 19 && saveType != 12)
        return loadDataset("SELECT save_id,name_cn,name_en,mapping_source FROM weapons WHERE save_type=? ORDER BY save_id", QList<QVariant>() << saveType);
    return NULL;
}
dataset_t *GameDataRepository::skills() const
{
    return loadDataset("SELECT id,name_cn,name_en,mapping_status FROM skill_trees ORDER BY id");
}
dataset_t *GameDataRepository::decorations() const
{
    return loadDataset("SELECT save_id,name_cn,name_en,source FROM save_decorations ORDER BY save_id");
}
dataset_t *GameDataRepository::charmClasses() const
{
    return loadDataset("SELECT save_id,name_cn,name_en,source FROM charm_classes ORDER BY save_id");
}

dataset_t *GameDataRepository::searchItems(const QString &text) const
{
    const QString pattern = "%" + text.trimmed() + "%";
    return loadDataset("SELECT save_id,name_cn,name_en,source FROM items "
                       "WHERE name_cn LIKE ? OR name_en LIKE ? ORDER BY save_id",
                       QList<QVariant>() << pattern << pattern);
}

dataset_t *GameDataRepository::searchEquipment(const QString &text, int saveType) const
{
    const QString pattern = "%" + text.trimmed() + "%";
    QString sql = "SELECT save_id,name_cn,name_en,mapping_source FROM ("
                  "SELECT save_type,save_id,name_cn,name_en,mapping_source FROM armors WHERE save_id IS NOT NULL "
                  "UNION ALL SELECT save_type,save_id,name_cn,name_en,mapping_source FROM weapons WHERE save_id IS NOT NULL) "
                  "WHERE (name_cn LIKE ? OR name_en LIKE ?)";
    QList<QVariant> arguments;
    arguments << pattern << pattern;
    if (saveType >= 0)
    {
        sql += " AND save_type=?";
        arguments << saveType;
    }
    sql += " ORDER BY save_type,save_id";
    return loadDataset(sql, arguments);
}

equipment_data_t GameDataRepository::equipment(int saveType, int saveId) const
{
    equipment_data_t result;
    if (!isOpen()) return result;
    const bool armor = saveType >= 1 && saveType <= 5;
    const QString sql = armor
        ? "SELECT name_cn,is_placeholder,mapping_status,mapping_source,slots,combat,gender FROM armors WHERE save_type=? AND save_id=?"
        : "SELECT name_cn,is_placeholder,mapping_status,mapping_source,slots,NULL,NULL FROM weapons WHERE save_type=? AND save_id=?";
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(sql); query.addBindValue(saveType); query.addBindValue(saveId);
    if (!query.exec() || !query.next()) return result;
    result.found = true;
    result.name = query.value(0).toString();
    result.placeholder = query.value(1).toBool();
    const QString mappingStatus = query.value(2).toString();
    result.confirmed = mappingStatus == "confirmed" || mappingStatus == "confirmed_mh3g";
    result.mh3gOnly = mappingStatus == "confirmed_mh3g";
    result.mappingSource = query.value(3).toString();
    result.slotCount = query.value(4).isNull() ? -1 : query.value(4).toInt();
    result.combat = query.value(5).isNull() ? -1 : query.value(5).toInt();
    result.gender = query.value(6).isNull() ? -1 : query.value(6).toInt();
    return result;
}

decoration_data_t GameDataRepository::decoration(int saveId) const
{
    decoration_data_t result;
    if (!isOpen()) return result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT s.name_cn,s.mapping_status,MIN(d.slots),MAX(d.slots) FROM save_decorations s "
                  "LEFT JOIN decorations d ON d.save_id=s.save_id WHERE s.save_id=? GROUP BY s.save_id");
    query.addBindValue(saveId);
    if (!query.exec() || !query.next()) return result;
    result.found = true;
    result.name = query.value(0).toString();
    result.confirmed = query.value(1).toString() == "confirmed" && !query.value(2).isNull() && query.value(2) == query.value(3);
    result.slotCount = result.confirmed ? query.value(2).toInt() : -1;
    return result;
}

QList<skill_point_data_t> GameDataRepository::armorSkillPoints(int saveType, int saveId) const
{
    QList<skill_point_data_t> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT p.skill_tree_id,p.points,s.name_cn FROM armors a "
                  "JOIN armor_skill_points p ON p.armor_dex_id=a.dex_id "
                  "JOIN skill_trees s ON s.id=p.skill_tree_id WHERE a.save_type=? AND a.save_id=? "
                  "ORDER BY p.skill_tree_id");
    query.addBindValue(saveType); query.addBindValue(saveId);
    if (!query.exec()) return result;
    while (query.next())
    {
        skill_point_data_t row = {query.value(0).toInt(), query.value(1).toInt(), query.value(2).toString()};
        result.append(row);
    }
    return result;
}

QList<skill_point_data_t> GameDataRepository::decorationSkillPoints(int saveId) const
{
    QList<skill_point_data_t> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT DISTINCT p.skill_tree_id,p.points,s.name_cn FROM decorations d "
                  "JOIN decoration_skill_points p ON p.decoration_dex_id=d.dex_id "
                  "JOIN skill_trees s ON s.id=p.skill_tree_id WHERE d.save_id=? "
                  "ORDER BY p.skill_tree_id,p.points");
    query.addBindValue(saveId);
    if (!query.exec()) return result;
    while (query.next())
    {
        skill_point_data_t row = {query.value(0).toInt(), query.value(1).toInt(), query.value(2).toString()};
        result.append(row);
    }
    return result;
}

QList<active_skill_data_t> GameDataRepository::activeSkills(int skillTreeId) const
{
    QList<active_skill_data_t> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT id,skill_tree_id,points,name_cn FROM active_skills WHERE skill_tree_id=? ORDER BY points,id");
    query.addBindValue(skillTreeId);
    if (!query.exec()) return result;
    while (query.next())
    {
        active_skill_data_t row = {query.value(0).toInt(), query.value(1).toInt(),
                                   query.value(2).toInt(), query.value(3).toString()};
        result.append(row);
    }
    return result;
}

QList<skill_tree_data_t> GameDataRepository::skillTreesDetailed() const
{
    QList<skill_tree_data_t> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("SELECT id,name_cn,name_en FROM skill_trees ORDER BY id")) return result;
    while (query.next())
    {
        skill_tree_data_t row = {query.value(0).toInt(), query.value(1).toString(), query.value(2).toString()};
        result.append(row);
    }
    return result;
}

loadout_candidate_t GameDataRepository::candidate(int saveType, int saveId) const
{
    loadout_candidate_t result;
    if (!isOpen()) return result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (saveType >= 1 && saveType <= 5)
    {
        query.prepare("SELECT save_type,save_id,name_cn,name_en,is_placeholder,mapping_status,rarity,slots,combat,gender,"
                      "base_defense,max_defense,fire_res,water_res,ice_res,thunder_res,dragon_res "
                      "FROM armors WHERE save_type=? AND save_id=?");
        query.addBindValue(saveType); query.addBindValue(saveId);
        if (!query.exec() || !query.next()) return result;
        result.found = true;
        result.saveType = query.value(0).toInt(); result.saveId = query.value(1).toInt();
        result.name = query.value(2).toString(); result.english = query.value(3).toString();
        result.placeholder = query.value(4).toBool(); result.mappingStatus = query.value(5).toString();
        result.confirmed = result.mappingStatus == "confirmed" || result.mappingStatus == "confirmed_mh3g";
        result.mh3gOnly = result.mappingStatus == "confirmed_mh3g";
        result.rarity = query.value(6).isNull() ? -1 : query.value(6).toInt();
        result.slotCount = query.value(7).isNull() ? -1 : query.value(7).toInt();
        result.combat = query.value(8).isNull() ? -1 : query.value(8).toInt();
        result.gender = query.value(9).isNull() ? -1 : query.value(9).toInt();
        result.baseDefense = query.value(10).isNull() ? -1 : query.value(10).toInt();
        result.maxDefense = query.value(11).isNull() ? -1 : query.value(11).toInt();
        result.fireRes = query.value(12).toInt(); result.waterRes = query.value(13).toInt();
        result.iceRes = query.value(14).toInt(); result.thunderRes = query.value(15).toInt();
        result.dragonRes = query.value(16).toInt();
        const QList<skill_point_data_t> points = armorSkillPoints(saveType, saveId);
        for (int i = 0; i < points.size(); ++i) result.skillPoints[points.at(i).skillTreeId] += points.at(i).points;
        return result;
    }
    if (saveType >= 7 && saveType <= 19 && saveType != 12)
    {
        query.prepare("SELECT save_type,save_id,name_cn,name_en,is_placeholder,mapping_status,rarity,slots,attack,affinity,defense "
                      "FROM weapons WHERE save_type=? AND save_id=?");
        query.addBindValue(saveType); query.addBindValue(saveId);
        if (!query.exec() || !query.next()) return result;
        result.found = true;
        result.saveType = query.value(0).toInt(); result.saveId = query.value(1).toInt();
        result.name = query.value(2).toString(); result.english = query.value(3).toString();
        result.placeholder = query.value(4).toBool(); result.mappingStatus = query.value(5).toString();
        result.confirmed = result.mappingStatus == "confirmed";
        result.rarity = query.value(6).isNull() ? -1 : query.value(6).toInt();
        result.slotCount = query.value(7).isNull() ? -1 : query.value(7).toInt();
        result.attack = query.value(8).isNull() ? -1 : query.value(8).toInt();
        result.affinity = query.value(9).toInt(); result.defense = query.value(10).toInt();
    }
    return result;
}

loadout_candidate_t GameDataRepository::charmCandidate(int classId, int slotCount, int skill1Id, int skill1Points,
                                                        int skill2Id, int skill2Points) const
{
    loadout_candidate_t result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT c.name_cn,c.name_en FROM charm_classes c WHERE c.save_id=?");
    query.addBindValue(classId);
    if (!query.exec() || !query.next() || slotCount < 0 || slotCount > 3 ||
        skill1Id < 0 || skill1Id > 255 || skill2Id < 0 || skill2Id > 255 ||
        skill1Points < -128 || skill1Points > 127 || skill2Points < -128 || skill2Points > 127 ||
        !skillExists(skill1Id) || !skillExists(skill2Id)) return result;
    const bool natural = charmCombinationExists(classId, slotCount, skill1Id, skill1Points,
                                                 skill2Id, skill2Points);
    result.found = true; result.confirmed = natural; result.placeholder = !natural;
    result.saveType = 6; result.saveId = classId;
    result.classId = classId; result.slotCount = slotCount; result.name = query.value(0).toString();
    result.english = query.value(1).toString();
    result.mappingStatus = natural ? "confirmed" : "invalid_combination";
    result.skill1Id = skill1Id; result.skill1Points = skill1Points;
    result.skill2Id = skill2Id; result.skill2Points = skill2Points;
    if (skill1Id > 0) result.skillPoints[skill1Id] += skill1Points;
    if (skill2Id > 0) result.skillPoints[skill2Id] += skill2Points;
    return result;
}

QList<loadout_candidate_t> GameDataRepository::queryCandidates(int expectedSaveType,
                                                               const equipment_query_t &options,
                                                               int *total) const
{
    QList<loadout_candidate_t> result;
    if (total) *total = 0;
    if (!isOpen()) return result;
    QString fromWhere;
    QList<QVariant> arguments;
    const QString pattern = "%" + options.text.trimmed() + "%";

    if (expectedSaveType >= 1 && expectedSaveType <= 5)
    {
        fromWhere = " FROM armors a WHERE a.save_type=? AND a.save_id IS NOT NULL";
        arguments << expectedSaveType;
        if (!options.text.trimmed().isEmpty())
        {
            fromWhere += " AND (a.name_cn LIKE ? OR a.name_en LIKE ?)";
            arguments << pattern << pattern;
        }
        if (options.confirmedOnly) fromWhere += " AND a.is_placeholder=0 AND a.mapping_status IN('confirmed','confirmed_mh3g')";
        if (options.combat >= 0) { fromWhere += " AND a.combat IN(0,?)"; arguments << options.combat; }
        if (options.gender >= 0) { fromWhere += " AND a.gender IN(0,?)"; arguments << (options.gender + 1); }
        if (options.rarityMin >= 0) { fromWhere += " AND COALESCE(a.rarity,-1)>=?"; arguments << options.rarityMin; }
        if (options.rarityMax >= 0) { fromWhere += " AND COALESCE(a.rarity,999)<=?"; arguments << options.rarityMax; }
        if (options.slotsMin >= 0) { fromWhere += " AND COALESCE(a.slots,-1)>=?"; arguments << options.slotsMin; }
        for (int i = 0; i < options.skills.size(); ++i)
        {
            const skill_filter_t filter = options.skills.at(i);
            fromWhere += " AND COALESCE((SELECT p.points FROM armor_skill_points p WHERE p.armor_dex_id=a.dex_id "
                         "AND p.skill_tree_id=?),0) " + comparisonSql(filter.comparison) + " ?";
            arguments << filter.skillTreeId << filter.points;
        }
        QSqlQuery countQuery(QSqlDatabase::database(m_connectionName));
        countQuery.prepare("SELECT COUNT(*)" + fromWhere); bindArguments(countQuery, arguments);
        if (total && countQuery.exec() && countQuery.next()) *total = countQuery.value(0).toInt();
        QSqlQuery query(QSqlDatabase::database(m_connectionName));
        query.prepare("SELECT a.save_type,a.save_id" + fromWhere + " ORDER BY a.save_id LIMIT ? OFFSET ?");
        QList<QVariant> paged = arguments; paged << qMax(1, options.limit) << qMax(0, options.offset);
        bindArguments(query, paged);
        if (!query.exec()) return result;
        while (query.next()) result.append(candidate(query.value(0).toInt(), query.value(1).toInt()));
        return result;
    }

    if (expectedSaveType == MH3U_Type::CharmType)
    {
        fromWhere = " FROM charm_combinations x JOIN charm_classes c ON c.save_id=x.class_id WHERE 1=1";
        if (!options.text.trimmed().isEmpty())
        {
            fromWhere += " AND (c.name_cn LIKE ? OR c.name_en LIKE ?)"; arguments << pattern << pattern;
        }
        if (options.slotsMin >= 0) { fromWhere += " AND x.slots>=?"; arguments << options.slotsMin; }
        for (int i = 0; i < options.skills.size(); ++i)
        {
            const skill_filter_t filter = options.skills.at(i);
            const QString op = comparisonSql(filter.comparison);
            fromWhere += " AND ((x.skill1_id=? AND x.skill1_points " + op + " ?)"
                         " OR (x.skill2_id=? AND x.skill2_points " + op + " ?)";
            arguments << filter.skillTreeId << filter.points << filter.skillTreeId << filter.points;
            if (comparisonMatches(0, filter.comparison, filter.points))
            {
                fromWhere += " OR (x.skill1_id<>? AND x.skill2_id<>?)";
                arguments << filter.skillTreeId << filter.skillTreeId;
            }
            fromWhere += ")";
        }
        QSqlQuery countQuery(QSqlDatabase::database(m_connectionName));
        countQuery.prepare("SELECT COUNT(*)" + fromWhere); bindArguments(countQuery, arguments);
        if (total && countQuery.exec() && countQuery.next()) *total = countQuery.value(0).toInt();
        QSqlQuery query(QSqlDatabase::database(m_connectionName));
        query.prepare("SELECT x.class_id,x.slots,x.skill1_id,x.skill1_points,x.skill2_id,x.skill2_points" + fromWhere +
                      " ORDER BY x.class_id,x.slots,x.skill1_id,x.skill1_points,x.skill2_id,x.skill2_points LIMIT ? OFFSET ?");
        QList<QVariant> paged = arguments; paged << qMax(1, options.limit) << qMax(0, options.offset);
        bindArguments(query, paged);
        if (!query.exec()) return result;
        while (query.next()) result.append(charmCandidate(query.value(0).toInt(), query.value(1).toInt(),
            query.value(2).toInt(), query.value(3).toInt(), query.value(4).toInt(), query.value(5).toInt()));
        return result;
    }

    fromWhere = " FROM weapons w WHERE w.save_id IS NOT NULL";
    int weaponType = options.weaponType;
    if (expectedSaveType >= 7 && expectedSaveType <= 19 && expectedSaveType != 12) weaponType = expectedSaveType;
    if (weaponType >= 0) { fromWhere += " AND w.save_type=?"; arguments << weaponType; }
    if (!options.text.trimmed().isEmpty())
    {
        fromWhere += " AND (w.name_cn LIKE ? OR w.name_en LIKE ?)"; arguments << pattern << pattern;
    }
    if (options.confirmedOnly) fromWhere += " AND w.is_placeholder=0 AND w.mapping_status='confirmed'";
    if (options.rarityMin >= 0) { fromWhere += " AND COALESCE(w.rarity,-1)>=?"; arguments << options.rarityMin; }
    if (options.rarityMax >= 0) { fromWhere += " AND COALESCE(w.rarity,999)<=?"; arguments << options.rarityMax; }
    if (options.slotsMin >= 0) { fromWhere += " AND COALESCE(w.slots,-1)>=?"; arguments << options.slotsMin; }
    QSqlQuery countQuery(QSqlDatabase::database(m_connectionName));
    countQuery.prepare("SELECT COUNT(*)" + fromWhere); bindArguments(countQuery, arguments);
    if (total && countQuery.exec() && countQuery.next()) *total = countQuery.value(0).toInt();
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT w.save_type,w.save_id" + fromWhere + " ORDER BY w.save_type,w.save_id LIMIT ? OFFSET ?");
    QList<QVariant> paged = arguments; paged << qMax(1, options.limit) << qMax(0, options.offset);
    bindArguments(query, paged);
    if (!query.exec()) return result;
    while (query.next()) result.append(candidate(query.value(0).toInt(), query.value(1).toInt()));
    return result;
}

QList<loadout_candidate_t> GameDataRepository::decorationCandidates() const
{
    QList<loadout_candidate_t> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("SELECT save_id,name_cn,name_en,mapping_status FROM save_decorations ORDER BY save_id")) return result;
    while (query.next())
    {
        loadout_candidate_t row;
        row.found = true; row.saveId = query.value(0).toInt(); row.name = query.value(1).toString();
        row.english = query.value(2).toString(); row.mappingStatus = query.value(3).toString();
        decoration_data_t detail = decoration(row.saveId);
        row.confirmed = detail.confirmed; row.slotCount = detail.slotCount;
        const QList<skill_point_data_t> points = decorationSkillPoints(row.saveId);
        for (int i = 0; i < points.size(); ++i) row.skillPoints[points.at(i).skillTreeId] += points.at(i).points;
        result.append(row);
    }
    return result;
}

QList<loadout_candidate_t> GameDataRepository::naturalCharmCandidates(const QList<int> &targetSkillIds) const
{
    QList<loadout_candidate_t> result;
    if (!isOpen()) return result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("SELECT class_id,slots,skill1_id,skill1_points,skill2_id,skill2_points "
                    "FROM charm_combinations ORDER BY class_id,slots,skill1_id,skill1_points,skill2_id,skill2_points"))
        return result;
    QMap<QString, loadout_candidate_t> unique;
    while (query.next())
    {
        loadout_candidate_t row;
        row.found = true; row.confirmed = true; row.saveType = MH3U_Type::CharmType;
        row.classId = query.value(0).toInt(); row.saveId = row.classId;
        row.slotCount = query.value(1).toInt(); row.skill1Id = query.value(2).toInt();
        row.skill1Points = query.value(3).toInt(); row.skill2Id = query.value(4).toInt();
        row.skill2Points = query.value(5).toInt(); row.name = charmClassName(row.classId);
        if (row.skill1Id > 0) row.skillPoints[row.skill1Id] += row.skill1Points;
        if (row.skill2Id > 0) row.skillPoints[row.skill2Id] += row.skill2Points;
        if (targetSkillIds.isEmpty())
        {
            result.append(row);
            continue;
        }
        QString key = QString::number(row.slotCount);
        for (int i = 0; i < targetSkillIds.size(); ++i)
            key += QString("/%1").arg(row.skillPoints.value(targetSkillIds.at(i), 0));
        // The query order is stable; retaining its first natural representative makes
        // equivalent target vectors deterministic without materialising 120k rich rows.
        if (!unique.contains(key)) unique.insert(key, row);
    }
    if (!targetSkillIds.isEmpty()) result = unique.values();
    return result;
}

QString GameDataRepository::dataVersion() const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) hash.addData(file.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

bool GameDataRepository::skillExists(int skillId) const
{
    return m_skillNames.contains(skillId);
}
bool GameDataRepository::charmClassExists(int classId) const
{
    return m_charmClassNames.contains(classId);
}
bool GameDataRepository::charmCombinationExists(int classId, int slotCount, int skill1Id, int skill1Points,
                                                int skill2Id, int skill2Points) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT 1 FROM charm_combinations WHERE class_id=? AND slots=? AND skill1_id=? AND skill1_points=? "
                  "AND skill2_id=? AND skill2_points=?");
    query.addBindValue(classId); query.addBindValue(slotCount); query.addBindValue(skill1Id);
    query.addBindValue(skill1Points); query.addBindValue(skill2Id); query.addBindValue(skill2Points);
    return query.exec() && query.next();
}

QString GameDataRepository::charmClassName(int classId) const
{
    return m_charmClassNames.value(classId);
}

QString GameDataRepository::skillName(int skillId) const
{
    return m_skillNames.value(skillId);
}

QList<int> GameDataRepository::charmSlots(int classId) const
{
    QList<int> result = m_charmSlotRules.value(classId).values();
    std::sort(result.begin(), result.end());
    return result;
}

QList<int> GameDataRepository::charmSkillPoints(int classId, int skillId, int position) const
{
    QList<int> result = m_charmSkillPointRules.value(charmSkillRuleKey(classId, position, skillId)).values();
    std::sort(result.begin(), result.end());
    return result;
}

bool GameDataRepository::charmSkillPairExists(int classId, int skill1Id, int skill2Id) const
{
    return m_charmSkillPairRules.contains(charmSkillPairKey(classId, skill1Id, skill2Id));
}

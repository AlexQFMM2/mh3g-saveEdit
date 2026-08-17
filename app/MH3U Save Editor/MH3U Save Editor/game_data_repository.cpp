#include "game_data_repository.hpp"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace
{
QString displayIdentifier(const QString &name, const QString &english)
{
    if (name.isEmpty()) return english;
    if (!english.isEmpty() && name != english) return name + " (" + english + ")";
    return name;
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
    m_path = info.absoluteFilePath();
    return true;
}

void GameDataRepository::close()
{
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
        ? "SELECT name_cn,is_placeholder,mapping_status,mapping_source,slots,max_upgrade_level,combat,gender FROM armors WHERE save_type=? AND save_id=?"
        : "SELECT name_cn,is_placeholder,mapping_status,mapping_source,slots,NULL,NULL,NULL FROM weapons WHERE save_type=? AND save_id=?";
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
    result.maxUpgradeLevel = query.value(5).isNull() ? -1 : query.value(5).toInt();
    result.combat = query.value(6).isNull() ? -1 : query.value(6).toInt();
    result.gender = query.value(7).isNull() ? -1 : query.value(7).toInt();
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

bool GameDataRepository::skillExists(int skillId) const
{
    if (skillId == 0) return true;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT 1 FROM skill_trees WHERE id=?"); query.addBindValue(skillId);
    return query.exec() && query.next();
}
bool GameDataRepository::charmClassExists(int classId) const
{
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare("SELECT 1 FROM charm_classes WHERE save_id=?"); query.addBindValue(classId);
    return query.exec() && query.next();
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

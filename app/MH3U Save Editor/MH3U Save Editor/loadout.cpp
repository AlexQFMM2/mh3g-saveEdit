#include "loadout.hpp"

#include "equipment_validator.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cstring>
#include <cmath>

namespace
{
const int kArmorTypes[5] = {
    MH3U_Type::HeadType, MH3U_Type::ChestType, MH3U_Type::ArmsType,
    MH3U_Type::WaistType, MH3U_Type::LegsType
};

void setU16(equipment_t &equipment, int offset, int value)
{
    equipment[offset] = (uint8_t)(value & 0xff);
    equipment[offset + 1] = (uint8_t)((value >> 8) & 0xff);
}

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

bool jsonInteger(const QJsonObject &object, const char *key, int minimum, int maximum,
                 int *result, QString *error)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        std::floor(value.toDouble()) != value.toDouble() ||
        value.toDouble() < minimum || value.toDouble() > maximum)
    {
        setError(error, QString::fromUtf8("%1 必须是 %2..%3 内的整数。")
            .arg(QString::fromLatin1(key)).arg(minimum).arg(maximum));
        return false;
    }
    *result = (int)value.toDouble();
    return true;
}

QString slotName(loadout_slot_e slot)
{
    static const char *names[] = {"武器", "头", "胸", "腕", "腰", "腿", "护石"};
    return QString::fromUtf8(names[(int)slot]);
}

void addDecorationContributions(const QList<int> &decorations, int column,
                                QMap<int, QVector<int> > &values, int &usedSlots,
                                bool &slotsUnknown, QStringList &diagnostics)
{
    GameDataRepository &repository = GameDataRepository::instance();
    for (int index = 0; index < decorations.size(); ++index)
    {
        const int id = decorations.at(index);
        if (id == 0) continue;
        decoration_data_t decoration = repository.decoration(id);
        if (!decoration.found || decoration.slotCount < 0)
        {
            slotsUnknown = true;
            diagnostics << QString::fromUtf8("装饰珠 ID %1 的孔位或技能效果未确认。").arg(id);
        }
        else usedSlots += decoration.slotCount;
        const QList<skill_point_data_t> points = repository.decorationSkillPoints(id);
        for (int p = 0; p < points.size(); ++p)
        {
            if (!values.contains(points.at(p).skillTreeId)) values[points.at(p).skillTreeId] = QVector<int>(LoadoutSlotCount, 0);
            values[points.at(p).skillTreeId][column] += points.at(p).points;
        }
    }
}

QJsonArray decorationsJson(const QList<int> &decorations)
{
    QJsonArray array;
    for (int index = 0; index < decorations.size(); ++index) array.append(decorations.at(index));
    return array;
}

QJsonValue pieceJson(const loadout_piece_t &piece)
{
    if (!piece.selected) return QJsonValue(QJsonValue::Null);
    QJsonObject object;
    object.insert("save_type", piece.saveType);
    object.insert("save_id", piece.saveId);
    object.insert("decorations", decorationsJson(piece.decorations));
    return object;
}

bool readDecorations(const QJsonValue &value, QList<int> *decorations, QString *error)
{
    if (!value.isArray()) { setError(error, QString::fromUtf8("decorations 必须是数组。")); return false; }
    QJsonArray array = value.toArray();
    if (array.size() > 3) { setError(error, QString::fromUtf8("每件装备最多记录三个装饰珠。")); return false; }
    QList<int> parsed;
    for (int i = 0; i < array.size(); ++i)
    {
        if (!array.at(i).isDouble() || !std::isfinite(array.at(i).toDouble()) ||
            std::floor(array.at(i).toDouble()) != array.at(i).toDouble())
        { setError(error, QString::fromUtf8("装饰珠 ID 必须是整数。")); return false; }
        int id = array.at(i).toInt(-1);
        if (id < 0 || id > 65535 || !GameDataRepository::instance().decoration(id).found)
        { setError(error, QString::fromUtf8("装饰珠 ID %1 无法解析。").arg(id)); return false; }
        parsed.append(id);
    }
    *decorations = parsed;
    return true;
}

bool readPiece(const QJsonValue &value, loadout_slot_e slot, loadout_piece_t *piece, QString *error)
{
    *piece = loadout_piece_t();
    if (value.isNull() || value.isUndefined()) return true;
    if (!value.isObject()) { setError(error, slotName(slot) + QString::fromUtf8("记录必须是对象或 null。")); return false; }
    QJsonObject object = value.toObject();
    int saveType = -1;
    int saveId = -1;
    if (!jsonInteger(object, "save_type", 0, 255, &saveType, error) ||
        !jsonInteger(object, "save_id", 0, 65535, &saveId, error)) return false;
    const int expected = LoadoutCalculator::expectedSaveType(slot);
    if (slot != LoadoutWeapon && saveType != expected)
    { setError(error, slotName(slot) + QString::fromUtf8("装备类型与部位不一致。")); return false; }
    if (slot == LoadoutWeapon && (saveType < 7 || saveType > 19 || saveType == 12))
    { setError(error, QString::fromUtf8("武器类型无效。")); return false; }
    if (!GameDataRepository::instance().candidate(saveType, saveId).found)
    { setError(error, slotName(slot) + QString::fromUtf8("的存档 ID 无法解析。")); return false; }
    QList<int> decorations;
    if (!readDecorations(object.value("decorations"), &decorations, error)) return false;
    piece->selected = true; piece->saveType = saveType; piece->saveId = saveId; piece->decorations = decorations;
    return true;
}
}

loadout_piece_t *loadout_model_t::piece(loadout_slot_e slot)
{
    switch (slot)
    {
        case LoadoutWeapon: return &weapon;
        case LoadoutHead: return &head;
        case LoadoutChest: return &chest;
        case LoadoutArms: return &arms;
        case LoadoutWaist: return &waist;
        case LoadoutLegs: return &legs;
        default: return NULL;
    }
}

const loadout_piece_t *loadout_model_t::piece(loadout_slot_e slot) const
{
    return const_cast<loadout_model_t *>(this)->piece(slot);
}

bool loadout_model_t::complete() const
{
    return weapon.selected && head.selected && chest.selected && arms.selected &&
           waist.selected && legs.selected && charm.selected;
}

void loadout_model_t::clear()
{
    const int currentGender = gender;
    *this = loadout_model_t();
    gender = currentGender;
}

loadout_summary_t::loadout_summary_t()
    : baseDefense(0), maxDefense(0), weaponDefense(0), fireRes(0), waterRes(0), iceRes(0),
      thunderRes(0), dragonRes(0), totalSlots(0), usedSlots(0), invalidCount(0), unknownCount(0),
      defenseUnknown(false), resistanceUnknown(false), slotsUnknown(false) {}

int LoadoutCalculator::expectedSaveType(loadout_slot_e slot)
{
    if (slot >= LoadoutHead && slot <= LoadoutLegs) return kArmorTypes[(int)slot - 1];
    if (slot == LoadoutCharm) return MH3U_Type::CharmType;
    return -1;
}

bool LoadoutCalculator::isRangedWeapon(int saveType)
{
    return saveType == MH3U_Type::LBGType || saveType == MH3U_Type::HBGType || saveType == MH3U_Type::BowType;
}

bool LoadoutCalculator::buildEquipment(const loadout_model_t &model, loadout_slot_e slot,
                                       equipment_t &equipment, QString *error)
{
    std::memset(equipment, 0, sizeof(equipment_t));
    if (slot == LoadoutCharm)
    {
        const loadout_charm_t &charm = model.charm;
        if (!charm.selected) { setError(error, QString::fromUtf8("护石尚未选择。")); return false; }
        if (!GameDataRepository::instance().charmCandidate(charm.classId, charm.slotCount, charm.skill1Id,
            charm.skill1Points, charm.skill2Id, charm.skill2Points).found)
        { setError(error, QString::fromUtf8("护石组合无法解析。")); return false; }
        if (charm.decorations.size() > 3 || charm.classId < 0 || charm.classId > 65535 ||
            charm.slotCount < 0 || charm.slotCount > 255 || charm.skill1Id < 0 || charm.skill1Id > 255 ||
            charm.skill2Id < 0 || charm.skill2Id > 255 || charm.skill1Points < -128 || charm.skill1Points > 127 ||
            charm.skill2Points < -128 || charm.skill2Points > 127)
        { setError(error, QString::fromUtf8("护石字段超出存档范围。")); return false; }
        equipment[0] = MH3U_Type::CharmType; equipment[1] = (uint8_t)charm.slotCount;
        setU16(equipment, 2, charm.classId); equipment[4] = (uint8_t)charm.skill1Id;
        equipment[5] = (uint8_t)(int8_t)charm.skill1Points; equipment[6] = (uint8_t)charm.skill2Id;
        equipment[7] = (uint8_t)(int8_t)charm.skill2Points;
        for (int i = 0; i < charm.decorations.size(); ++i) setU16(equipment, 8 + i * 2, charm.decorations.at(i));
        return true;
    }
    const loadout_piece_t *piece = model.piece(slot);
    if (!piece || !piece->selected) { setError(error, slotName(slot) + QString::fromUtf8("尚未选择。")); return false; }
    if (slot != LoadoutWeapon && piece->saveType != expectedSaveType(slot))
    { setError(error, slotName(slot) + QString::fromUtf8("装备类型与部位不一致。")); return false; }
    if (slot == LoadoutWeapon && (piece->saveType < 7 || piece->saveType > 19 || piece->saveType == 12))
    { setError(error, QString::fromUtf8("武器类型无效。")); return false; }
    if (!GameDataRepository::instance().candidate(piece->saveType, piece->saveId).found ||
        piece->saveId < 0 || piece->saveId > 65535 || piece->decorations.size() > 3)
    { setError(error, slotName(slot) + QString::fromUtf8("装备 ID 或珠子记录无法安全编码。")); return false; }
    equipment[0] = (uint8_t)piece->saveType; setU16(equipment, 2, piece->saveId);
    for (int i = 0; i < piece->decorations.size(); ++i)
    {
        if (!GameDataRepository::instance().decoration(piece->decorations.at(i)).found)
        { setError(error, QString::fromUtf8("装饰珠 ID %1 无法解析。").arg(piece->decorations.at(i))); return false; }
        setU16(equipment, 8 + i * 2, piece->decorations.at(i));
    }
    return true;
}

loadout_summary_t LoadoutCalculator::calculate(const loadout_model_t &model, save_format_e platform)
{
    loadout_summary_t summary;
    GameDataRepository &repository = GameDataRepository::instance();
    QMap<int, QVector<int> > values;
    for (int slotIndex = 0; slotIndex < LoadoutSlotCount; ++slotIndex)
    {
        loadout_slot_e slot = (loadout_slot_e)slotIndex;
        if (slot == LoadoutCharm)
        {
            if (!model.charm.selected) continue;
            summary.totalSlots += model.charm.slotCount;
            if (model.charm.skill1Id > 0)
            { if (!values.contains(model.charm.skill1Id)) values[model.charm.skill1Id] = QVector<int>(LoadoutSlotCount, 0);
              values[model.charm.skill1Id][slotIndex] += model.charm.skill1Points; }
            if (model.charm.skill2Id > 0)
            { if (!values.contains(model.charm.skill2Id)) values[model.charm.skill2Id] = QVector<int>(LoadoutSlotCount, 0);
              values[model.charm.skill2Id][slotIndex] += model.charm.skill2Points; }
            addDecorationContributions(model.charm.decorations, slotIndex, values, summary.usedSlots,
                                       summary.slotsUnknown, summary.diagnostics);
        }
        else
        {
            const loadout_piece_t *piece = model.piece(slot);
            if (!piece || !piece->selected) continue;
            loadout_candidate_t detail = repository.candidate(piece->saveType, piece->saveId);
            if (!detail.found) { summary.unknownCount++; summary.diagnostics << slotName(slot) + QString::fromUtf8("数据无法解析。"); continue; }
            if (detail.slotCount < 0) summary.slotsUnknown = true; else summary.totalSlots += detail.slotCount;
            QMap<int, int>::const_iterator point = detail.skillPoints.constBegin();
            for (; point != detail.skillPoints.constEnd(); ++point)
            { if (!values.contains(point.key())) values[point.key()] = QVector<int>(LoadoutSlotCount, 0);
              values[point.key()][slotIndex] += point.value(); }
            addDecorationContributions(piece->decorations, slotIndex, values, summary.usedSlots,
                                       summary.slotsUnknown, summary.diagnostics);
            if (slot == LoadoutWeapon) { summary.weaponDefense = detail.defense; summary.baseDefense += detail.defense; summary.maxDefense += detail.defense; }
            else
            {
                if (detail.baseDefense < 0 || detail.maxDefense < 0) summary.defenseUnknown = true;
                else { summary.baseDefense += detail.baseDefense; summary.maxDefense += detail.maxDefense; }
                if (!detail.confirmed) summary.resistanceUnknown = true;
                summary.fireRes += detail.fireRes; summary.waterRes += detail.waterRes; summary.iceRes += detail.iceRes;
                summary.thunderRes += detail.thunderRes; summary.dragonRes += detail.dragonRes;
            }
        }
        equipment_t raw;
        QString buildError;
        if (buildEquipment(model, slot, raw, &buildError))
        {
            equipment_validation_t validation = EquipmentValidator::validate(raw, platform, model.gender);
            if (validation.status == EquipmentInvalid) summary.invalidCount++;
            else if (validation.status == EquipmentUnknown) summary.unknownCount++;
            if (validation.status != EquipmentValid) summary.diagnostics << slotName(slot) + QString::fromUtf8("：") + validation.details();
        }
    }
    if (model.weapon.selected)
    {
        const int requiredCombat = isRangedWeapon(model.weapon.saveType) ? 2 : 1;
        for (int index = LoadoutHead; index <= LoadoutLegs; ++index)
        {
            const loadout_piece_t *piece = model.piece((loadout_slot_e)index);
            if (!piece || !piece->selected) continue;
            loadout_candidate_t armor = repository.candidate(piece->saveType, piece->saveId);
            if (armor.combat > 0 && armor.combat != requiredCombat)
            { summary.invalidCount++; summary.diagnostics << slotName((loadout_slot_e)index) + QString::fromUtf8("与当前武器的近战/远程类型不适用。"); }
            if (armor.gender > 0 && armor.gender != model.gender + 1)
            { summary.invalidCount++; summary.diagnostics << slotName((loadout_slot_e)index) + QString::fromUtf8("与当前配装性别不适用。"); }
        }
    }
    if (values.contains(1))
    {
        const QVector<int> torsoMarkers = values.value(1);
        const QVector<int> chestValuesTemplate = QVector<int>();
        Q_UNUSED(chestValuesTemplate);
        for (int column = LoadoutHead; column <= LoadoutLegs; ++column)
        {
            if (column == LoadoutChest || torsoMarkers.value(column) <= 0) continue;
            QList<int> keys = values.keys();
            for (int k = 0; k < keys.size(); ++k)
            {
                if (keys.at(k) == 1) continue;
                values[keys.at(k)][column] += values[keys.at(k)].value(LoadoutChest);
            }
        }
    }
    const QList<skill_tree_data_t> treeRows = repository.skillTreesDetailed();
    QMap<int, skill_tree_data_t> trees;
    for (int i = 0; i < treeRows.size(); ++i) trees.insert(treeRows.at(i).id, treeRows.at(i));
    QList<int> skillIds = values.keys();
    for (int i = 0; i < skillIds.size(); ++i)
    {
        const int id = skillIds.at(i);
        loadout_skill_row_t row;
        row.skillTreeId = id; row.name = trees.value(id).name; row.columns = values.value(id);
        row.total = 0; for (int c = 0; c < row.columns.size(); ++c) row.total += row.columns.at(c);
        row.distanceToNext = 0; row.positiveActive = false; row.negativeActive = false;
        const QList<active_skill_data_t> active = repository.activeSkills(id);
        int chosenPositive = -2147483647;
        int chosenNegative = 2147483647;
        int nextPositive = 2147483647;
        int nextNegative = -2147483647;
        QString nextPositiveName;
        QString nextNegativeName;
        for (int a = 0; a < active.size(); ++a)
        {
            const int threshold = active.at(a).points;
            if (threshold > 0 && row.total >= threshold && threshold > chosenPositive)
            { chosenPositive = threshold; row.activeSkill = active.at(a).name; row.positiveActive = true; }
            if (threshold < 0 && row.total <= threshold && threshold < chosenNegative)
            { chosenNegative = threshold; row.activeSkill = active.at(a).name; row.negativeActive = true; row.positiveActive = false; }
            if (threshold > row.total && threshold > 0 && threshold < nextPositive)
            { nextPositive = threshold; nextPositiveName = active.at(a).name; }
            if (threshold < row.total && threshold < 0 && threshold > nextNegative)
            { nextNegative = threshold; nextNegativeName = active.at(a).name; }
        }
        const int positiveDistance = nextPositive < 2147483647 ? nextPositive - row.total : 2147483647;
        const int negativeDistance = nextNegative > -2147483647 ? row.total - nextNegative : 2147483647;
        if (positiveDistance <= negativeDistance)
        {
            row.distanceToNext = positiveDistance;
            row.nextSkill = nextPositiveName;
        }
        else
        {
            row.distanceToNext = negativeDistance;
            row.nextSkill = nextNegativeName;
        }
        if (row.distanceToNext == 2147483647) row.distanceToNext = 0;
        summary.skills.append(row);
    }
    return summary;
}

bool LoadoutFile::save(const QString &path, const loadout_model_t &model, QString *error)
{
    QJsonObject root;
    root.insert("schema", "MH_LOADOUT"); root.insert("schema_version", 1); root.insert("game", "mh3g");
    root.insert("data_version", GameDataRepository::instance().dataVersion()); root.insert("name", model.name);
    root.insert("gender", model.gender == 1 ? "female" : "male"); root.insert("weapon", pieceJson(model.weapon));
    QJsonObject armor; armor.insert("head", pieceJson(model.head)); armor.insert("chest", pieceJson(model.chest));
    armor.insert("arms", pieceJson(model.arms)); armor.insert("waist", pieceJson(model.waist)); armor.insert("legs", pieceJson(model.legs));
    root.insert("armor", armor);
    if (model.charm.selected)
    {
        QJsonObject charm; charm.insert("class_id", model.charm.classId); charm.insert("slots", model.charm.slotCount);
        charm.insert("skill1_id", model.charm.skill1Id); charm.insert("skill1_points", model.charm.skill1Points);
        charm.insert("skill2_id", model.charm.skill2Id); charm.insert("skill2_points", model.charm.skill2Points);
        charm.insert("decorations", decorationsJson(model.charm.decorations)); root.insert("charm", charm);
    }
    else root.insert("charm", QJsonValue(QJsonValue::Null));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { setError(error, file.errorString()); return false; }
    QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) { setError(error, file.errorString()); return false; }
    return true;
}

bool LoadoutFile::load(const QString &path, loadout_model_t *model, bool *versionWarning, QString *error)
{
    if (!model) { setError(error, QString::fromUtf8("配装输出为空。")); return false; }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { setError(error, file.errorString()); return false; }
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull() || !document.isObject()) { setError(error, parseError.errorString()); return false; }
    QJsonObject root = document.object();
    if (root.value("schema").toString() != "MH_LOADOUT" || root.value("schema_version").toInt() != 1 ||
        root.value("game").toString() != "mh3g")
    { setError(error, QString::fromUtf8("不是受支持的 MH3G 配装文件。")); return false; }
    loadout_model_t parsed; parsed.name = root.value("name").toString();
    const QString gender = root.value("gender").toString();
    if (gender != "male" && gender != "female") { setError(error, QString::fromUtf8("配装性别无效。")); return false; }
    parsed.gender = gender == "female" ? 1 : 0;
    if (!readPiece(root.value("weapon"), LoadoutWeapon, &parsed.weapon, error)) return false;
    if (!root.value("armor").isObject()) { setError(error, QString::fromUtf8("armor 必须是对象。")); return false; }
    QJsonObject armor = root.value("armor").toObject();
    if (!readPiece(armor.value("head"), LoadoutHead, &parsed.head, error) ||
        !readPiece(armor.value("chest"), LoadoutChest, &parsed.chest, error) ||
        !readPiece(armor.value("arms"), LoadoutArms, &parsed.arms, error) ||
        !readPiece(armor.value("waist"), LoadoutWaist, &parsed.waist, error) ||
        !readPiece(armor.value("legs"), LoadoutLegs, &parsed.legs, error)) return false;
    QJsonValue charmValue = root.value("charm");
    if (!charmValue.isNull() && !charmValue.isUndefined())
    {
        if (!charmValue.isObject()) { setError(error, QString::fromUtf8("charm 必须是对象或 null。")); return false; }
        QJsonObject charm = charmValue.toObject();
        if (!jsonInteger(charm, "class_id", 0, 65535, &parsed.charm.classId, error) ||
            !jsonInteger(charm, "slots", 0, 3, &parsed.charm.slotCount, error) ||
            !jsonInteger(charm, "skill1_id", 0, 255, &parsed.charm.skill1Id, error) ||
            !jsonInteger(charm, "skill1_points", -128, 127, &parsed.charm.skill1Points, error) ||
            !jsonInteger(charm, "skill2_id", 0, 255, &parsed.charm.skill2Id, error) ||
            !jsonInteger(charm, "skill2_points", -128, 127, &parsed.charm.skill2Points, error)) return false;
        if (!GameDataRepository::instance().charmCandidate(parsed.charm.classId, parsed.charm.slotCount,
            parsed.charm.skill1Id, parsed.charm.skill1Points, parsed.charm.skill2Id, parsed.charm.skill2Points).found ||
            !readDecorations(charm.value("decorations"), &parsed.charm.decorations, error))
        { if (error && error->isEmpty()) *error = QString::fromUtf8("护石组合无法解析。"); return false; }
        parsed.charm.selected = true;
    }
    if (versionWarning) *versionWarning = root.value("data_version").toString() != GameDataRepository::instance().dataVersion();
    *model = parsed;
    return true;
}

bool LoadoutSaveBridge::appendCompleteLoadout(const loadout_model_t &model, save_t *save,
                                              QList<int> *writtenIndexes, QString *error)
{
    if (!save) { setError(error, QString::fromUtf8("尚未读取存档。")); return false; }
    if (!model.complete())
    {
        QStringList missing;
        for (int slot = LoadoutWeapon; slot <= LoadoutLegs; ++slot)
        {
            const loadout_piece_t *piece = model.piece((loadout_slot_e)slot);
            if (!piece || !piece->selected) missing << slotName((loadout_slot_e)slot);
        }
        if (!model.charm.selected) missing << slotName(LoadoutCharm);
        setError(error, QString::fromUtf8("请先选择：%1。").arg(missing.join(QString::fromUtf8("、"))));
        return false;
    }
    equipment_t records[LoadoutSlotCount];
    for (int slot = 0; slot < LoadoutSlotCount; ++slot)
        if (!LoadoutCalculator::buildEquipment(model, (loadout_slot_e)slot, records[slot], error)) return false;
    QList<int> empty;
    for (int index = 0; index < 1000 && empty.size() < LoadoutSlotCount; ++index)
    {
        equipment_t &equipment = save->box[index / 100][index % 100];
        const int id = equipment[2] | (equipment[3] << 8);
        if (equipment[0] == 0 && id == 0) empty.append(index);
    }
    if (empty.size() != LoadoutSlotCount) { setError(error, QString::fromUtf8("装备箱不足七个空格。")); return false; }
    save_t staged = *save;
    for (int slot = 0; slot < LoadoutSlotCount; ++slot)
        std::memcpy(staged.box[empty.at(slot) / 100][empty.at(slot) % 100], records[slot], EQUIPMENT_SIZE);
    *save = staged;
    if (writtenIndexes) *writtenIndexes = empty;
    return true;
}

#include "equipment_validator.hpp"

#include "game_data_repository.hpp"

#include <QStringList>

namespace
{
void add(equipment_validation_t &result, equipment_validity_e severity,
         const char *field, const char *code, const QString &message)
{
    equipment_diagnostic_t diagnostic;
    diagnostic.severity = severity;
    diagnostic.field = QString::fromLatin1(field);
    diagnostic.code = QString::fromLatin1(code);
    diagnostic.message = message;
    result.diagnostics.append(diagnostic);
    if (severity > result.status) result.status = severity;
}

int u16(const equipment_t &equipment, int offset)
{
    return equipment[offset] | (equipment[offset + 1] << 8);
}

QString compactIntegerSet(const QList<int> &values)
{
    QStringList ranges;
    int index = 0;
    while (index < values.size())
    {
        const int first = values.at(index);
        int last = first;
        while (index + 1 < values.size() && values.at(index + 1) == last + 1)
        {
            ++index;
            last = values.at(index);
        }
        ranges << (first == last ? QString::number(first)
                                 : QString::fromUtf8("%1～%2").arg(first).arg(last));
        ++index;
    }
    return ranges.join(QString::fromUtf8("、"));
}

QString namedSkill(const GameDataRepository &repository, int skillId)
{
    const QString name = repository.skillName(skillId);
    return name.isEmpty() ? QString::fromUtf8("技能 ID %1").arg(skillId) : name;
}

void validateDecorations(equipment_validation_t &result, const equipment_t &equipment, int naturalSlots)
{
    GameDataRepository &repository = GameDataRepository::instance();
    int usedSlots = 0;
    bool costKnown = true;
    for (int index = 0; index < 3; ++index)
    {
        const int id = u16(equipment, 8 + index * 2);
        if (id == 0) continue;
        decoration_data_t decoration = repository.decoration(id);
        if (!decoration.found)
        {
            add(result, EquipmentInvalid, "decoration", "DECORATION_ID_UNKNOWN",
                QString::fromUtf8("装饰珠槽 %1 的 ID %2 不存在于存档本地珠子表。").arg(index + 1).arg(id));
        }
        else if (!decoration.confirmed)
        {
            costKnown = false;
            add(result, EquipmentUnknown, "decoration", "DECORATION_MAPPING_UNKNOWN",
                QString::fromUtf8("%1（ID %2）的原生参数映射尚未确认。").arg(decoration.name).arg(id));
        }
        else
        {
            usedSlots += decoration.slotCount;
        }
    }
    if (naturalSlots < 0 && usedSlots > 0)
        add(result, EquipmentUnknown, "slots", "EQUIPMENT_SLOTS_UNKNOWN", QString::fromUtf8("装备天然孔位缺少游戏参数证据，无法核对装饰珠占孔。"));
    else if (naturalSlots >= 0 && costKnown && usedSlots > naturalSlots)
        add(result, EquipmentInvalid, "slots", "SLOT_CAPACITY_EXCEEDED",
            QString::fromUtf8("装饰珠共占 %1 孔，但该装备只有 %2 孔。").arg(usedSlots).arg(naturalSlots));
}
}

QString equipment_validation_t::statusText() const
{
    if (status == EquipmentInvalid) return QString::fromUtf8("不合法");
    if (status == EquipmentUnknown) return QString::fromUtf8("未确认");
    return QString::fromUtf8("合法");
}

QString equipment_validation_t::details() const
{
    if (diagnostics.isEmpty()) return QString::fromUtf8("已通过当前可用的游戏数据规则。");
    QStringList lines;
    for (int i = 0; i < diagnostics.size(); ++i)
        lines << QString::fromUtf8("• %1 [%2]").arg(diagnostics.at(i).message, diagnostics.at(i).code);
    return lines.join("\n");
}

equipment_validation_t EquipmentValidator::validate(const equipment_t &equipment, save_format_e platform, int characterSex)
{
    equipment_validation_t result;
    GameDataRepository &repository = GameDataRepository::instance();
    const int type = equipment[0];
    const int id = u16(equipment, 2);
    if (type == 0 && id == 0) return result;
    if (type == 0 || type == 12 || type > 19)
    {
        add(result, EquipmentInvalid, "type", "EQUIPMENT_TYPE_INVALID",
            QString::fromUtf8("装备类型 %1 不是可识别的自然装备类型。").arg(type));
        return result;
    }
    if (id == 0)
    {
        add(result, EquipmentInvalid, "id", "EQUIPMENT_ID_ZERO", QString::fromUtf8("非空装备类型使用了空 ID。"));
        return result;
    }

    if (type == MH3U_Type::CharmType)
    {
        const int slotCount = equipment[1];
        const int skill1 = equipment[4];
        const int points1 = (int)(int8_t)equipment[5];
        const int skill2 = equipment[6];
        const int points2 = (int)(int8_t)equipment[7];
        if (!repository.charmClassExists(id))
            add(result, EquipmentInvalid, "charm_class", "CHARM_CLASS_INVALID", QString::fromUtf8("护石品级 ID %1 不存在。").arg(id));
        if (slotCount < 0 || slotCount > 3)
            add(result, EquipmentInvalid, "slots", "CHARM_SLOTS_INVALID", QString::fromUtf8("护石孔数 %1 超出 0..3。").arg(slotCount));
        if (!repository.skillExists(skill1) || !repository.skillExists(skill2))
            add(result, EquipmentInvalid, "skill", "CHARM_SKILL_ID_INVALID", QString::fromUtf8("护石包含不存在的技能系 ID。"));
        if (skill1 != 0 && skill1 == skill2)
            add(result, EquipmentInvalid, "skill", "CHARM_DUPLICATE_SKILL", QString::fromUtf8("护石的两项技能系相同。"));
        if (skill1 == 0 && points1 != 0)
            add(result, EquipmentInvalid, "skill1_points", "CHARM_SKILL_POINTS_WITHOUT_SKILL",
                QString::fromUtf8("第1技能为“无”时点数必须为 0，当前为 %1。").arg(points1));
        if (skill2 == 0 && points2 != 0)
            add(result, EquipmentInvalid, "skill2_points", "CHARM_SKILL_POINTS_WITHOUT_SKILL",
                QString::fromUtf8("第2技能为“无”时点数必须为 0，当前为 %1。").arg(points2));

        const bool classKnown = repository.charmClassExists(id);
        const bool skill1Known = repository.skillExists(skill1);
        const bool skill2Known = repository.skillExists(skill2);
        if (classKnown)
        {
            const QString className = repository.charmClassName(id);
            const QList<int> allowedSlots = repository.charmSlots(id);
            if (!allowedSlots.contains(slotCount))
                add(result, EquipmentInvalid, "slots", "CHARM_SLOT_NOT_GENERATED",
                    QString::fromUtf8("%1不支持 %2 孔；原生允许孔数：%3。")
                        .arg(className).arg(slotCount).arg(compactIntegerSet(allowedSlots)));

            const QList<int> allowedPoints1 = skill1Known ? repository.charmSkillPoints(id, skill1, 1) : QList<int>();
            const QList<int> allowedPoints2 = skill2Known ? repository.charmSkillPoints(id, skill2, 2) : QList<int>();
            const bool skill1PositionValid = !allowedPoints1.isEmpty();
            const bool skill2PositionValid = !allowedPoints2.isEmpty();
            if (skill1Known && !skill1PositionValid)
                add(result, EquipmentInvalid, "skill1", "CHARM_SKILL_POSITION_INVALID",
                    QString::fromUtf8("“%1”不能作为%2的第1技能。")
                        .arg(namedSkill(repository, skill1), className));
            else if (skill1Known && !(skill1 == 0 && points1 != 0) && !allowedPoints1.contains(points1))
                add(result, EquipmentInvalid, "skill1_points", "CHARM_SKILL_POINTS_INVALID",
                    QString::fromUtf8("%1的第1技能“%2”不支持 %3 点；原生允许：%4。")
                        .arg(className, namedSkill(repository, skill1)).arg(points1)
                        .arg(compactIntegerSet(allowedPoints1)));
            if (skill2Known && !skill2PositionValid)
                add(result, EquipmentInvalid, "skill2", "CHARM_SKILL_POSITION_INVALID",
                    QString::fromUtf8("“%1”不能作为%2的第2技能。")
                        .arg(namedSkill(repository, skill2), className));
            else if (skill2Known && !(skill2 == 0 && points2 != 0) && !allowedPoints2.contains(points2))
                add(result, EquipmentInvalid, "skill2_points", "CHARM_SKILL_POINTS_INVALID",
                    QString::fromUtf8("%1的第2技能“%2”不支持 %3 点；原生允许：%4。")
                        .arg(className, namedSkill(repository, skill2)).arg(points2)
                        .arg(compactIntegerSet(allowedPoints2)));

            if (skill1Known && skill2Known && skill1 != 0 && skill2 != 0 && skill1PositionValid && skill2PositionValid &&
                !repository.charmSkillPairExists(id, skill1, skill2))
                add(result, EquipmentInvalid, "skill", "CHARM_SKILL_PAIR_NOT_GENERATED",
                    QString::fromUtf8("%1不会自然生成第1技能“%2”与第2技能“%3”的组合。")
                        .arg(className, namedSkill(repository, skill1), namedSkill(repository, skill2)));

            if (slotCount >= 0 && slotCount <= 3 && skill1Known && skill2Known &&
                !repository.charmCombinationExists(id, slotCount, skill1, points1, skill2, points2))
                add(result, EquipmentInvalid, "charm", "CHARM_COMBINATION_NOT_GENERATED",
                    QString::fromUtf8("该品级、孔数、技能与点数组合不在 261,448 条原生生成记录中。"));
        }
        validateDecorations(result, equipment, slotCount);
        return result;
    }

    equipment_data_t data = repository.equipment(type, id);
    if (!data.found)
    {
        add(result, EquipmentInvalid, "id", "EQUIPMENT_ID_OUT_OF_RANGE",
            QString::fromUtf8("类型 %1 中不存在 ID %2。").arg(type).arg(id));
        return result;
    }
    if (data.placeholder)
        add(result, EquipmentInvalid, "id", "EQUIPMENT_PLACEHOLDER", QString::fromUtf8("该 ID 是 DUMMY / 占位记录，不是自然装备。"));
    else if (!data.confirmed)
        add(result, EquipmentUnknown, "id", "EQUIPMENT_PARAMETERS_UNKNOWN",
            QString::fromUtf8("%1 有存档 ID，但缺少完整原生参数证据。").arg(data.name));

    if (platform == SAVE_FORMAT_WIIU && data.mh3gOnly)
        add(result, EquipmentUnknown, "platform", "WIIU_RULE_UNCONFIRMED",
            QString::fromUtf8("该条目只有 MH3G 原生参数证据，Wii U / MH3U 平台规则尚未确认。"));
    if (type >= 1 && type <= 5 && characterSex >= 0 && data.gender > 0)
    {
        const int requiredSex = data.gender == 1 ? 0 : 1;
        if (requiredSex != characterSex)
            add(result, EquipmentValid, "gender", "ARMOR_GENDER_MISMATCH",
                QString::fromUtf8("该防具与当前角色性别不适用，但可正常保存在装备箱中。"));
    }
    validateDecorations(result, equipment, data.slotCount);
    return result;
}

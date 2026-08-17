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
        if (repository.charmClassExists(id) && slotCount <= 3 && repository.skillExists(skill1) && repository.skillExists(skill2) &&
            !repository.charmCombinationExists(id, slotCount, skill1, points1, skill2, points2))
            add(result, EquipmentInvalid, "charm", "CHARM_COMBINATION_NOT_GENERATED",
                QString::fromUtf8("该品级、孔数、技能与点数组合不在 261,448 条原生生成记录中。"));
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

    if (type >= 1 && type <= 5 && data.maxUpgradeLevel >= 0 && equipment[1] > data.maxUpgradeLevel)
        add(result, EquipmentInvalid, "upgrade", "ARMOR_UPGRADE_EXCEEDED",
            QString::fromUtf8("强化等级 %1 超过上限 %2。").arg(equipment[1]).arg(data.maxUpgradeLevel));
    else if (type >= 1 && type <= 5 && data.maxUpgradeLevel < 0 && equipment[1] > 0)
        add(result, EquipmentUnknown, "upgrade", "ARMOR_UPGRADE_LIMIT_UNKNOWN",
            QString::fromUtf8("已记录强化等级 %1，但当前原生数据未能确认该防具的强化次数上限。").arg(equipment[1]));
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

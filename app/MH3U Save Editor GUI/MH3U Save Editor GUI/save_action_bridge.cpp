#include "save_action_bridge.hpp"

#include <cstring>

QString SaveActionResult::slotLabel() const
{
    return success ? QString::fromUtf8("第 %1 页，第 %2 格").arg(panel + 1).arg(slot + 1) : QString();
}

SaveActionBridge::SaveActionBridge(MH3U_SE *save) : m_save(save)
{
}

bool SaveActionBridge::hasOpenSave() const
{
    return m_save != 0 && m_save->loaded() && m_save->savedata != 0;
}

SaveActionResult SaveActionBridge::previewAddItem(quint16 saveId, quint16 count) const
{
    SaveActionResult result;
    if (!hasOpenSave())
    {
        result.error = QString::fromUtf8("请先读取 3DS 或 Wii U 角色存档。");
        return result;
    }
    if (saveId <= 1 || count == 0)
    {
        result.error = QString::fromUtf8("该道具的存档 ID 或数量无效。");
        return result;
    }
    for (int panel = 0; panel < 10; ++panel)
    {
        for (int slot = 0; slot < 100; ++slot)
        {
            if (m_save->savedata->chest[panel][slot].id == 0)
            {
                result.success = true;
                result.panel = panel;
                result.slot = slot;
                return result;
            }
        }
    }
    result.error = QString::fromUtf8("道具箱没有空格。");
    return result;
}

SaveActionResult SaveActionBridge::addItem(quint16 saveId, quint16 count)
{
    const SaveActionResult result = previewAddItem(saveId, count);
    if (!result.success) return result;
    item_t &target = m_save->savedata->chest[result.panel][result.slot];
    target.id = saveId;
    target.count = count;
    return result;
}

SaveActionResult SaveActionBridge::previewAddWeapon(quint8 saveType, quint16 saveId) const
{
    SaveActionResult result;
    if (!hasOpenSave())
    {
        result.error = QString::fromUtf8("请先读取 3DS 或 Wii U 角色存档。");
        return result;
    }
    if (saveId == 0 || MH3U_Armory::convertSubtype(saveType) != MH3U_Type::WeaponSubtype)
    {
        result.error = QString::fromUtf8("该武器缺少已验证的存档类型或 ID。");
        return result;
    }
    for (int panel = 0; panel < 10; ++panel)
    {
        for (int slot = 0; slot < 100; ++slot)
        {
            const equipment_t &equipment = m_save->savedata->box[panel][slot];
            const quint16 identifier = equipment[2] | (quint16(equipment[3]) << 8);
            if (equipment[0] == MH3U_Type::NoneType && identifier == 0)
            {
                result.success = true;
                result.panel = panel;
                result.slot = slot;
                return result;
            }
        }
    }
    result.error = QString::fromUtf8("装备箱没有空格。");
    return result;
}

SaveActionResult SaveActionBridge::addWeapon(quint8 saveType, quint16 saveId)
{
    const SaveActionResult result = previewAddWeapon(saveType, saveId);
    if (!result.success) return result;
    equipment_t &target = m_save->savedata->box[result.panel][result.slot];
    std::memset(target, 0, EQUIPMENT_SIZE);
    target[0] = saveType;
    target[2] = saveId & 0xff;
    target[3] = (saveId >> 8) & 0xff;
    return result;
}


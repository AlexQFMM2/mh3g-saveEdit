#include "mh3u_se.hpp"
#include "mh3u_transfer.hpp"
#include "save_action_bridge.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
    if (!stream)
    {
        throw std::runtime_error("cannot open " + path);
    }
    std::streamoff size = stream.tellg();
    std::vector<uint8_t> bytes((size_t) size);
    stream.seekg(0, stream.beg);
    stream.read((char*) bytes.data(), size);
    if (!stream)
    {
        throw std::runtime_error("cannot read " + path);
    }
    return bytes;
}

static void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

static void testSave(const std::string &path, save_format_e expectedFormat)
{
    const std::string output = path + ".roundtrip-test.tmp";
    std::remove(output.c_str());

    try
    {
        const std::vector<uint8_t> original = readFile(path);
        MH3U_SE editor;
        require(editor.load(path), editor.lastError());
        require(editor.format() == expectedFormat, "wrong format detected for " + path);
        require(editor.save(output), editor.lastError());
        require(readFile(output) == original, "unchanged round trip differs for " + path);

        editor.savedata->money = 0x00020304;
        editor.savedata->mogapoint = 0x00050607;
        editor.savedata->inventory[0][0].id = 0x1234;
        editor.savedata->inventory[0][0].count = 0x0042;
        editor.savedata->box[0][0][0] = MH3U_Type::GSType;
        editor.savedata->box[0][0][2] = 0x34;
        editor.savedata->box[0][0][3] = 0x12;
        editor.savedata->box[0][0][8] = 0x78;
        editor.savedata->box[0][0][9] = 0x56;
        require(editor.save(output), editor.lastError());

        const std::vector<uint8_t> edited = readFile(output);
        const size_t base = expectedFormat == SAVE_FORMAT_WIIU ? WIIU_HEADER_SIZE : 0;
        if (expectedFormat == SAVE_FORMAT_WIIU)
        {
            require(std::equal(original.begin(), original.begin() + WIIU_HEADER_SIZE, edited.begin()),
                "Wii U header changed");
            require(edited[base + MONEY_OFFSET] == 0x00 && edited[base + MONEY_OFFSET + 1] == 0x02 &&
                edited[base + MONEY_OFFSET + 2] == 0x03 && edited[base + MONEY_OFFSET + 3] == 0x04,
                "Wii U money was not written big-endian");
            require(edited[base + INVENTORY_OFFSET] == 0x12 && edited[base + INVENTORY_OFFSET + 1] == 0x34,
                "Wii U item ID was not written big-endian");
            require(edited[base + BOX_OFFSET + 2] == 0x12 && edited[base + BOX_OFFSET + 3] == 0x34,
                "Wii U equipment ID was not written big-endian");
            require(edited[base + BOX_OFFSET + 8] == 0x56 && edited[base + BOX_OFFSET + 9] == 0x78,
                "Wii U jewel ID was not written big-endian");
        }
        else
        {
            require(edited[MONEY_OFFSET] == 0x04 && edited[MONEY_OFFSET + 1] == 0x03 &&
                edited[MONEY_OFFSET + 2] == 0x02 && edited[MONEY_OFFSET + 3] == 0x00,
                "3DS money was not written little-endian");
            require(edited[INVENTORY_OFFSET] == 0x34 && edited[INVENTORY_OFFSET + 1] == 0x12,
                "3DS item ID was not written little-endian");
            require(edited[BOX_OFFSET + 2] == 0x34 && edited[BOX_OFFSET + 3] == 0x12,
                "3DS equipment ID was not written little-endian");
            require(edited[BOX_OFFSET + 8] == 0x78 && edited[BOX_OFFSET + 9] == 0x56,
                "3DS jewel ID was not written little-endian");
        }

        MH3U_SE reloaded;
        require(reloaded.load(output), reloaded.lastError());
        require(reloaded.format() == expectedFormat, "format changed after save");
        require(reloaded.savedata->money == 0x00020304, "money byte order is wrong");
        require(reloaded.savedata->mogapoint == 0x00050607, "Moga Point byte order is wrong");
        require(reloaded.savedata->inventory[0][0].id == 0x1234, "item ID byte order is wrong");
        require(reloaded.savedata->inventory[0][0].count == 0x0042, "item count byte order is wrong");
        require(reloaded.savedata->box[0][0][2] == 0x34 && reloaded.savedata->box[0][0][3] == 0x12,
            "equipment ID byte order is wrong");
        require(reloaded.savedata->box[0][0][8] == 0x78 && reloaded.savedata->box[0][0][9] == 0x56,
            "jewel ID byte order is wrong");
    }
    catch (...)
    {
        std::remove(output.c_str());
        throw;
    }

    std::remove(output.c_str());
}

static void requireTransferredDataMatches(const save_t &source, const save_t &target, const std::string &direction)
{
    require(std::memcmp(source.chest, target.chest, sizeof(source.chest)) == 0,
        "item chest differs after " + direction + " transfer");
    require(std::memcmp(source.box, target.box, sizeof(source.box)) == 0,
        "equipment box differs after " + direction + " transfer");
}

static void testTransfer(const std::string &sourcePath, const std::string &targetPath,
    save_format_e expectedTargetFormat, const std::string &direction)
{
    const std::string output = targetPath + ".transfer-test.tmp";
    std::remove(output.c_str());

    try
    {
        MH3U_SE source;
        MH3U_SE target;
        require(source.load(sourcePath), source.lastError());
        require(target.load(targetPath), target.lastError());

        std::vector<MH3U_Transfer::chest_entry_t> chestEntries;
        std::vector<MH3U_Transfer::equipment_entry_t> equipmentEntries;
        std::string error;
        require(MH3U_Transfer::parseChest(MH3U_Transfer::exportChest(*source.savedata), chestEntries, error), error);
        require(chestEntries.size() == 1000, "item form did not contain all 1000 slots");
        require(MH3U_Transfer::parseEquipmentBox(MH3U_Transfer::exportEquipmentBox(*source.savedata), equipmentEntries, error), error);
        require(equipmentEntries.size() == 1000, "equipment form did not contain all 1000 slots");

        std::memset(target.savedata->chest, 0, sizeof(target.savedata->chest));
        std::memset(target.savedata->box, 0, sizeof(target.savedata->box));
        MH3U_Transfer::applyChest(chestEntries, *target.savedata);
        MH3U_Transfer::applyEquipmentBox(equipmentEntries, *target.savedata);
        requireTransferredDataMatches(*source.savedata, *target.savedata, direction);

        require(target.save(output), target.lastError());
        MH3U_SE reloaded;
        require(reloaded.load(output), reloaded.lastError());
        require(reloaded.format() == expectedTargetFormat, "target format changed after " + direction + " transfer");
        requireTransferredDataMatches(*source.savedata, *reloaded.savedata, direction + " saved");
    }
    catch (...)
    {
        std::remove(output.c_str());
        throw;
    }

    std::remove(output.c_str());
}

static void testMalformedTransferForms()
{
    std::vector<MH3U_Transfer::chest_entry_t> chestEntries;
    std::vector<MH3U_Transfer::equipment_entry_t> equipmentEntries;
    std::string error;

    const std::string duplicateChest =
        "MH3U_TRANSFER,1,ITEM_CHEST\n"
        "page,slot,item_id,count\n"
        "1,1,10,20\n"
        "1,1,11,21\n";
    require(!MH3U_Transfer::parseChest(duplicateChest, chestEntries, error), "duplicate item slot was accepted");
    require(chestEntries.empty(), "failed item parse returned partial entries");

    const std::string wrongEquipmentType =
        "MH3U_TRANSFER,1,EQUIPMENT_BOX\n"
        "page,slot,equipment_type,byte_0,byte_1,byte_2,byte_3,byte_4,byte_5,byte_6,byte_7,byte_8,byte_9,byte_10,byte_11,byte_12,byte_13,byte_14,byte_15\n"
        "1,1,7,8,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
    require(!MH3U_Transfer::parseEquipmentBox(wrongEquipmentType, equipmentEntries, error),
        "equipment type mismatch was accepted");
    require(equipmentEntries.empty(), "failed equipment parse returned partial entries");
}

static void testSaveActions(const std::string &path, save_format_e expectedFormat)
{
    const std::vector<uint8_t> diskBefore = readFile(path);
    MH3U_SE editor;
    require(editor.load(path), editor.lastError());
    require(editor.format() == expectedFormat, "wrong format in save-action test");
    SaveActionBridge bridge(&editor);
    require(bridge.hasOpenSave(), "save-action bridge did not detect the loaded save");

    save_t original = *editor.savedata;
    SaveActionResult preview = bridge.previewAddItem(2, 7);
    require(preview.success, "sample item chest has no empty slot");
    require(std::memcmp(&original, editor.savedata, sizeof(save_t)) == 0,
        "item preview modified the in-memory save");
    SaveActionResult result = bridge.addItem(2, 7);
    require(result.success && result.panel == preview.panel && result.slot == preview.slot,
        "item add did not use the previewed slot");
    save_t expected = original;
    expected.chest[result.panel][result.slot].id = 2;
    expected.chest[result.panel][result.slot].count = 7;
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "item add changed data outside the target item slot");

    *editor.savedata = original;
    preview = bridge.previewAddWeapon(MH3U_Type::GSType, 1);
    require(preview.success, "sample equipment box has no empty slot");
    require(std::memcmp(&original, editor.savedata, sizeof(save_t)) == 0,
        "weapon preview modified the in-memory save");
    result = bridge.addWeapon(MH3U_Type::GSType, 1);
    require(result.success && result.panel == preview.panel && result.slot == preview.slot,
        "weapon add did not use the previewed slot");
    expected = original;
    std::memset(expected.box[result.panel][result.slot], 0, EQUIPMENT_SIZE);
    expected.box[result.panel][result.slot][0] = MH3U_Type::GSType;
    expected.box[result.panel][result.slot][2] = 1;
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "weapon add changed data outside the target equipment slot");

    *editor.savedata = original;
    preview = bridge.previewAddArmor(MH3U_Type::HeadType, 1);
    require(preview.success, "sample equipment box has no empty slot for armor");
    require(std::memcmp(&original, editor.savedata, sizeof(save_t)) == 0,
        "armor preview modified the in-memory save");
    result = bridge.addArmor(MH3U_Type::HeadType, 1);
    require(result.success && result.panel == preview.panel && result.slot == preview.slot,
        "armor add did not use the previewed slot");
    expected = original;
    std::memset(expected.box[result.panel][result.slot], 0, EQUIPMENT_SIZE);
    expected.box[result.panel][result.slot][0] = MH3U_Type::HeadType;
    expected.box[result.panel][result.slot][2] = 1;
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "armor add changed data outside the target equipment slot");

    *editor.savedata = original;
    QVector<ArmorSaveRef> armorSet;
    const quint8 armorTypes[] = {MH3U_Type::HeadType, MH3U_Type::ChestType, MH3U_Type::ArmsType,
        MH3U_Type::WaistType, MH3U_Type::LegsType};
    for (quint8 type : armorTypes) { ArmorSaveRef ref; ref.saveType = type; ref.saveId = 1; armorSet.append(ref); }
    const SaveActionBatchResult batchPreview = bridge.previewAddArmorSet(armorSet);
    require(batchPreview.success && batchPreview.placements.size() == 5, "armor-set preview failed");
    require(std::memcmp(&original, editor.savedata, sizeof(save_t)) == 0,
        "armor-set preview modified the in-memory save");
    const SaveActionBatchResult batchResult = bridge.addArmorSet(armorSet);
    require(batchResult.success && batchResult.placements.size() == 5, "armor-set add failed");
    expected = original;
    for (int index = 0; index < armorSet.size(); ++index)
    {
        const SaveActionResult &placement = batchResult.placements[index];
        std::memset(expected.box[placement.panel][placement.slot], 0, EQUIPMENT_SIZE);
        expected.box[placement.panel][placement.slot][0] = armorSet[index].saveType;
        expected.box[placement.panel][placement.slot][2] = 1;
    }
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "armor-set add changed data outside the five target slots");

    *editor.savedata = original;
    require(!bridge.addItem(0, 1).success, "invalid item ID was accepted");
    require(!bridge.addItem(2, 0).success, "zero item count was accepted");
    require(!bridge.addWeapon(MH3U_Type::ChestType, 1).success, "armor type was accepted as a weapon");
    require(!bridge.addWeapon(MH3U_Type::GSType, 0).success, "zero weapon ID was accepted");
    require(!bridge.addArmor(MH3U_Type::GSType, 1).success, "weapon type was accepted as armor");
    QVector<ArmorSaveRef> invalidSet = armorSet;
    invalidSet[2].saveId = 0;
    require(!bridge.addArmorSet(invalidSet).success, "armor set with an unmapped member was accepted");
    require(std::memcmp(&original, editor.savedata, sizeof(save_t)) == 0,
        "rejected save action modified the in-memory save");

    for (int panel = 0; panel < 10; ++panel)
    {
        for (int slot = 0; slot < 100; ++slot)
        {
            editor.savedata->chest[panel][slot].id = 2;
            editor.savedata->chest[panel][slot].count = 1;
        }
    }
    expected = *editor.savedata;
    require(!bridge.addItem(2, 1).success, "full item chest accepted an add");
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "failed full-chest add modified the in-memory save");

    *editor.savedata = original;
    for (int panel = 0; panel < 10; ++panel)
    {
        for (int slot = 0; slot < 100; ++slot)
        {
            std::memset(editor.savedata->box[panel][slot], 0, EQUIPMENT_SIZE);
            editor.savedata->box[panel][slot][0] = MH3U_Type::GSType;
            editor.savedata->box[panel][slot][2] = 1;
        }
    }
    expected = *editor.savedata;
    require(!bridge.addWeapon(MH3U_Type::GSType, 1).success, "full equipment box accepted an add");
    require(!bridge.addArmor(MH3U_Type::HeadType, 1).success, "full equipment box accepted an armor add");
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "failed full-box add modified the in-memory save");

    for (int slot = 0; slot < 4; ++slot) std::memset(editor.savedata->box[0][slot], 0, EQUIPMENT_SIZE);
    expected = *editor.savedata;
    require(!bridge.addArmorSet(armorSet).success, "armor set was added with only four empty slots");
    require(std::memcmp(&expected, editor.savedata, sizeof(save_t)) == 0,
        "failed armor-set add partially modified the in-memory save");

    require(readFile(path) == diskBefore, "save action wrote to disk before the main save command");
}

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: test_save_formats <3ds-user-file> <wiiu-user-file>" << std::endl;
        return 2;
    }

    try
    {
        testSave(argv[1], SAVE_FORMAT_N3DS);
        testSave(argv[2], SAVE_FORMAT_WIIU);
        testTransfer(argv[1], argv[2], SAVE_FORMAT_WIIU, "3DS to Wii U");
        testTransfer(argv[2], argv[1], SAVE_FORMAT_N3DS, "Wii U to 3DS");
        testSaveActions(argv[1], SAVE_FORMAT_N3DS);
        testSaveActions(argv[2], SAVE_FORMAT_WIIU);
        testMalformedTransferForms();
        std::cout << "3DS/Wii U save format and transfer-form tests passed" << std::endl;
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "test failed: " << error.what() << std::endl;
        return 1;
    }
}

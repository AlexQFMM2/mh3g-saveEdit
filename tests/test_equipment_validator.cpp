#include "equipment_validator.hpp"
#include "game_data_repository.hpp"

#include <QCoreApplication>

#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static void setId(equipment_t &equipment, int id)
{
    equipment[2] = (uint8_t)(id & 0xff);
    equipment[3] = (uint8_t)((id >> 8) & 0xff);
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2)
    {
        std::cerr << "usage: test_equipment_validator <mh3g.sqlite>\n";
        return 2;
    }
    try
    {
        require(GameDataRepository::instance().open(QString::fromLocal8Bit(argv[1])), "database open failed");
        dataset_t *itemSearch = GameDataRepository::instance().searchItems("Potion");
        require(itemSearch && !itemSearch->empty(), "English item search failed");
        delete itemSearch;
        dataset_t *equipmentSearch = GameDataRepository::instance().searchEquipment("Iron Sword", MH3U_Type::GSType);
        require(equipmentSearch && !equipmentSearch->empty(), "English equipment search failed");
        delete equipmentSearch;
        require(!GameDataRepository::instance().armorSkillPoints(MH3U_Type::ChestType, 1).isEmpty(),
                "armor skill lookup failed");
        require(!GameDataRepository::instance().decorationSkillPoints(1).isEmpty(),
                "decoration effect lookup failed");
        require(!GameDataRepository::instance().activeSkills(1).isEmpty(),
                "active skill threshold lookup failed");
        equipment_t equipment;
        std::memset(equipment, 0, sizeof(equipment));
        require(EquipmentValidator::validate(equipment).status == EquipmentValid, "empty slot is not valid");

        equipment[0] = MH3U_Type::GSType;
        setId(equipment, 1);
        require(EquipmentValidator::validate(equipment).status == EquipmentValid, "known great sword is not valid");

        setId(equipment, 65535);
        require(EquipmentValidator::validate(equipment).status == EquipmentInvalid, "out-of-range weapon was accepted");

        std::memset(equipment, 0, sizeof(equipment));
        equipment[0] = MH3U_Type::ChestType;
        setId(equipment, 356);
        equipment_data_t mh3gArmor = GameDataRepository::instance().equipment(MH3U_Type::ChestType, 356);
        require(mh3gArmor.confirmed && mh3gArmor.mh3gOnly && mh3gArmor.slotCount == 0,
                "native MH3G-only armor parameters are missing");
        require(EquipmentValidator::validate(equipment, SAVE_FORMAT_N3DS).status == EquipmentValid,
                "native MH3G armor is not valid for 3DS");
        equipment_validation_t platformUnknown = EquipmentValidator::validate(equipment, SAVE_FORMAT_WIIU);
        require(platformUnknown.status == EquipmentUnknown,
                "MH3G-only armor is not marked unknown for Wii U");
        setId(equipment, 358);
        equipment_validation_t emptyNative = EquipmentValidator::validate(equipment, SAVE_FORMAT_N3DS);
        require(emptyNative.status == EquipmentUnknown,
                "empty native armor record is not unknown");

        std::memset(equipment, 0, sizeof(equipment));
        equipment[0] = MH3U_Type::CharmType;
        equipment[1] = 0;
        setId(equipment, 1);
        equipment[4] = 2;
        equipment[5] = 1;
        require(EquipmentValidator::validate(equipment).status == EquipmentValid, "native charm combination is not valid");
        equipment[5] = (uint8_t)(int8_t)-128;
        require(EquipmentValidator::validate(equipment).status == EquipmentInvalid, "impossible signed charm value was accepted");

        std::memset(equipment, 0, sizeof(equipment));
        equipment[0] = MH3U_Type::ChestType;
        setId(equipment, 1);
        equipment[8] = 1;
        require(EquipmentValidator::validate(equipment).status == EquipmentInvalid, "one-slot jewel in zero-slot armor was accepted");
        std::cout << "equipment validator tests passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}

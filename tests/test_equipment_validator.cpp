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

static void setCharm(equipment_t &equipment, int classId, int slotCount,
                     int skill1Id, int skill1Points, int skill2Id, int skill2Points)
{
    std::memset(equipment, 0, sizeof(equipment));
    equipment[0] = MH3U_Type::CharmType;
    equipment[1] = (uint8_t)slotCount;
    setId(equipment, classId);
    equipment[4] = (uint8_t)skill1Id;
    equipment[5] = (uint8_t)(int8_t)skill1Points;
    equipment[6] = (uint8_t)skill2Id;
    equipment[7] = (uint8_t)(int8_t)skill2Points;
}

static bool hasDiagnostic(const equipment_validation_t &validation, const char *code)
{
    for (int index = 0; index < validation.diagnostics.size(); ++index)
        if (validation.diagnostics.at(index).code == QString::fromLatin1(code)) return true;
    return false;
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
        require(GameDataRepository::instance().charmClassName(1) == QString::fromUtf8("士兵护石"),
                "cached charm class name lookup failed");
        require(GameDataRepository::instance().skillName(11) == QString::fromUtf8("攻击"),
                "cached charm skill name lookup failed");
        require(GameDataRepository::instance().charmSlots(1) == (QList<int>() << 0 << 1),
                "cached charm slot rule lookup failed");
        require(GameDataRepository::instance().charmSkillPoints(10, 11, 2) ==
                    (QList<int>() << 4 << 6 << 7),
                "cached exact charm point rule lookup failed");
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
        equipment[0] = MH3U_Type::ChestType;
        setId(equipment, 1);
        equipment[1] = 0xff;
        require(EquipmentValidator::validate(equipment, SAVE_FORMAT_N3DS).status == EquipmentValid,
                "armor upgrade byte must not affect validity");

        setCharm(equipment, 1, 0, 2, 1, 0, 0);
        require(EquipmentValidator::validate(equipment).status == EquipmentValid, "native charm combination is not valid");
        equipment[5] = (uint8_t)(int8_t)-128;
        require(EquipmentValidator::validate(equipment).status == EquipmentInvalid, "impossible signed charm value was accepted");

        setCharm(equipment, 1, 0, 11, 3, 0, 0);
        require(EquipmentValidator::validate(equipment).status == EquipmentValid,
                "native pawn attack skill-1 points were rejected");

        setCharm(equipment, 1, 0, 2, 1, 11, 1);
        equipment_validation_t invalidPosition = EquipmentValidator::validate(equipment);
        require(hasDiagnostic(invalidPosition, "CHARM_SKILL_POSITION_INVALID") &&
                    invalidPosition.details().contains(QString::fromUtf8("攻击")) &&
                    invalidPosition.details().contains(QString::fromUtf8("第2技能")),
                "invalid pawn skill-2 position was not explained");

        setCharm(equipment, 10, 0, 10, 3, 11, 8);
        equipment_validation_t invalidPoints = EquipmentValidator::validate(equipment);
        require(hasDiagnostic(invalidPoints, "CHARM_SKILL_POINTS_INVALID") &&
                    invalidPoints.details().contains(QString::fromUtf8("4、6～7")),
                "creator attack skill-2 exact allowed points were not explained");

        setCharm(equipment, 1, 2, 2, 1, 0, 0);
        equipment_validation_t invalidSlots = EquipmentValidator::validate(equipment);
        require(hasDiagnostic(invalidSlots, "CHARM_SLOT_NOT_GENERATED") &&
                    invalidSlots.details().contains(QString::fromUtf8("0～1")),
                "invalid pawn slot count was not explained");

        setCharm(equipment, 1, 0, 2, 1, 0, 1);
        equipment_validation_t pointsWithoutSkill = EquipmentValidator::validate(equipment);
        require(hasDiagnostic(pointsWithoutSkill, "CHARM_SKILL_POINTS_WITHOUT_SKILL"),
                "points assigned to no skill were not explained");

        setCharm(equipment, 3, 0, 2, 1, 3, 2);
        equipment_validation_t invalidPair = EquipmentValidator::validate(equipment);
        require(hasDiagnostic(invalidPair, "CHARM_SKILL_PAIR_NOT_GENERATED") &&
                    hasDiagnostic(invalidPair, "CHARM_COMBINATION_NOT_GENERATED"),
                "individually valid but impossible skill pair was not explained");

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

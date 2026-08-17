#include "game_data_repository.hpp"
#include "loadout.hpp"
#include "equipment_validator.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

static loadout_piece_t piece(int type, int id)
{
    loadout_piece_t result;
    result.selected = true;
    result.saveType = type;
    result.saveId = id;
    return result;
}

static const loadout_skill_row_t *skill(const loadout_summary_t &summary, int id)
{
    for (int index = 0; index < summary.skills.size(); ++index)
        if (summary.skills.at(index).skillTreeId == id) return &summary.skills.at(index);
    return NULL;
}

static std::vector<unsigned char> readBytes(const std::string &path)
{
    std::ifstream stream(path.c_str(), std::ios::binary | std::ios::ate);
    require((bool)stream, "cannot open save sample");
    const std::streamoff size = stream.tellg();
    std::vector<unsigned char> bytes((size_t)size);
    stream.seekg(0, stream.beg);
    stream.read((char *)bytes.data(), size);
    require((bool)stream, "cannot read save sample");
    return bytes;
}

static bool inWrittenRecord(size_t position, size_t base, const QList<int> &indexes)
{
    for (int i = 0; i < indexes.size(); ++i)
    {
        const size_t start = base + BOX_OFFSET + (size_t)indexes.at(i) * EQUIPMENT_SIZE;
        if (position >= start && position < start + EQUIPMENT_SIZE) return true;
    }
    return false;
}

static void testRealSave(const loadout_model_t &model, const std::string &path,
                         save_format_e expectedFormat)
{
    MH3U_SE editor;
    require(editor.load(path), "real sample could not be loaded");
    require(editor.format() == expectedFormat, "real sample format mismatch");
    const save_t before = *editor.savedata;
    const std::vector<unsigned char> original = readBytes(path);
    QList<int> written;
    QString error;
    require(LoadoutSaveBridge::appendCompleteLoadout(model, editor.savedata, &written, &error),
            "real sample rejected complete loadout");
    require(written.size() == LoadoutSlotCount, "real sample did not reserve seven slots");

    for (int index = 0; index < 1000; ++index)
    {
        const bool changed = written.contains(index);
        if (!changed)
            require(std::memcmp(before.box[index / 100][index % 100],
                                editor.savedata->box[index / 100][index % 100], EQUIPMENT_SIZE) == 0,
                    "transaction changed an equipment slot that was not reserved");
    }

    QTemporaryDir outputDirectory;
    require(outputDirectory.isValid(), "real-save temporary directory failed");
    const std::string output = (outputDirectory.path() + "/user1").toStdString();
    require(editor.save(output), "real sample could not be saved");
    const std::vector<unsigned char> edited = readBytes(output);
    require(edited.size() == original.size(), "real sample size changed");
    const size_t base = expectedFormat == SAVE_FORMAT_WIIU ? WIIU_HEADER_SIZE : 0;
    for (size_t position = 0; position < original.size(); ++position)
        if (!inWrittenRecord(position, base, written))
            require(original[position] == edited[position], "bytes outside seven target records changed");

    MH3U_SE reloaded;
    require(reloaded.load(output), "written real sample could not be reloaded");
    require(reloaded.format() == expectedFormat, "written real sample changed format");
    for (int slot = 0; slot < LoadoutSlotCount; ++slot)
    {
        equipment_t expected;
        require(LoadoutCalculator::buildEquipment(model, (loadout_slot_e)slot, expected, &error),
                "could not rebuild expected equipment record");
        const int index = written.at(slot);
        require(std::memcmp(expected, reloaded.savedata->box[index / 100][index % 100], EQUIPMENT_SIZE) == 0,
                "real sample equipment record changed after platform encoding");
    }
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    if (argc != 2 && argc != 4)
    {
        std::cerr << "usage: test_loadout <mh3g.sqlite> [3ds-user1 wiiu-user1]\n";
        return 2;
    }
    try
    {
        GameDataRepository &repository = GameDataRepository::instance();
        require(repository.open(QString::fromLocal8Bit(argv[1])), "database open failed");

        equipment_query_t armorQuery;
        skill_filter_t sharp = {5, SkillGreater, 10};
        skill_filter_t missingAttack = {11, SkillEqual, 0};
        armorQuery.skills << sharp << missingAttack;
        int armorTotal = 0;
        QList<loadout_candidate_t> heads = repository.queryCandidates(MH3U_Type::HeadType, armorQuery, &armorTotal);
        require(armorTotal == 1 && heads.size() == 1, "multi-skill armor AND filter failed");
        require(heads.first().saveType == MH3U_Type::HeadType && heads.first().saveId == 347,
                "armor picker did not remain head-locked");

        equipment_query_t negativeQuery;
        skill_filter_t negativeSharpness = {5, SkillLess, -2};
        negativeQuery.skills << negativeSharpness;
        int negativeTotal = 0;
        QList<loadout_candidate_t> negativeHeads = repository.queryCandidates(MH3U_Type::HeadType, negativeQuery, &negativeTotal);
        require(negativeTotal > 0 && !negativeHeads.isEmpty(), "strict negative skill comparison returned no rows");
        for (int index = 0; index < negativeHeads.size(); ++index)
            require(negativeHeads.at(index).skillPoints.value(5) < -2, "strict less-than skill comparison included an endpoint");
        negativeQuery.skills[0].comparison = SkillLessEqual;
        negativeQuery.skills[0].points = -3;
        negativeHeads = repository.queryCandidates(MH3U_Type::HeadType, negativeQuery, &negativeTotal);
        require(negativeTotal > 0, "less-than-or-equal negative skill comparison returned no rows");
        for (int index = 0; index < negativeHeads.size(); ++index)
            require(negativeHeads.at(index).skillPoints.value(5) <= -3, "less-than-or-equal skill comparison was incorrect");

        equipment_query_t charmQuery;
        skill_filter_t skill21 = {21, SkillGreater, 4};
        skill_filter_t skill18 = {18, SkillGreaterEqual, 1};
        charmQuery.skills << skill21 << skill18;
        int charmTotal = 0;
        QList<loadout_candidate_t> charms = repository.queryCandidates(MH3U_Type::CharmType, charmQuery, &charmTotal);
        require(charmTotal > 0 && !charms.isEmpty(), "two-skill charm filter failed");
        bool foundReversedSlots = false;
        for (int index = 0; index < charms.size(); ++index)
            if (charms.at(index).skillPoints.value(21) > 4 && charms.at(index).skillPoints.value(18) >= 1)
                foundReversedSlots = true;
        require(foundReversedSlots, "charm filter depended on skill field position");

        equipment_query_t eitherCharmPosition;
        skill_filter_t anySkill21 = {21, SkillGreaterEqual, 1};
        eitherCharmPosition.skills << anySkill21;
        bool foundFirstPosition = false;
        bool foundSecondPosition = false;
        int eitherTotal = 0;
        for (int offset = 0; offset < 4000 && !(foundFirstPosition && foundSecondPosition); offset += 200)
        {
            eitherCharmPosition.offset = offset;
            const QList<loadout_candidate_t> page = repository.queryCandidates(MH3U_Type::CharmType, eitherCharmPosition, &eitherTotal);
            for (int index = 0; index < page.size(); ++index)
            {
                foundFirstPosition |= page.at(index).skill1Id == 21;
                foundSecondPosition |= page.at(index).skill2Id == 21;
            }
            if (page.isEmpty() || offset + 200 >= eitherTotal) break;
        }
        require(foundFirstPosition && foundSecondPosition, "charm skill query did not search both skill positions");

        loadout_model_t model;
        model.name = QString::fromUtf8("测试配装");
        model.gender = 0;
        model.weapon = piece(MH3U_Type::GSType, 1);
        model.head = piece(MH3U_Type::HeadType, 1);
        model.chest = piece(MH3U_Type::ChestType, 1);
        model.arms = piece(MH3U_Type::ArmsType, 1);
        model.waist = piece(MH3U_Type::WaistType, 168);
        model.legs = piece(MH3U_Type::LegsType, 9);
        model.charm.selected = true;
        model.charm.classId = 1;
        model.charm.slotCount = 0;
        model.charm.skill1Id = 2;
        model.charm.skill1Points = 1;
        model.weapon.decorations << 1;
        model.chest.decorations << 1;
        require(model.complete(), "complete loadout was not complete");

        loadout_summary_t summary = LoadoutCalculator::calculate(model, SAVE_FORMAT_N3DS);
        const loadout_skill_row_t *gathering = skill(summary, 90);
        require(gathering && gathering->columns.value(LoadoutChest) == 2 &&
                gathering->columns.value(LoadoutWaist) == 2 &&
                gathering->columns.value(LoadoutLegs) == 2,
                "one or more Torso Up pieces did not copy chest skill points");
        const loadout_skill_row_t *poison = skill(summary, 2);
        require(poison && poison->columns.value(LoadoutWeapon) == 1 &&
                poison->columns.value(LoadoutChest) == 1 &&
                poison->columns.value(LoadoutWaist) == 1 &&
                poison->columns.value(LoadoutLegs) == 1,
                "chest decoration was not copied by every Torso Up piece");

        loadout_model_t thresholdModel;
        thresholdModel.charm.selected = true;
        thresholdModel.charm.classId = 6;
        thresholdModel.charm.slotCount = 0;
        thresholdModel.charm.skill1Id = 15;
        thresholdModel.charm.skill1Points = 2;
        thresholdModel.charm.skill2Id = 11;
        thresholdModel.charm.skill2Points = 10;
        loadout_summary_t positiveThreshold = LoadoutCalculator::calculate(thresholdModel, SAVE_FORMAT_N3DS);
        const loadout_skill_row_t *attack = skill(positiveThreshold, 11);
        require(attack && attack->positiveActive && attack->distanceToNext == 5 && !attack->nextSkill.isEmpty(),
                "positive active skill did not retain distance to its next tier");
        thresholdModel.charm.classId = 5;
        thresholdModel.charm.skill1Id = 15;
        thresholdModel.charm.skill1Points = 3;
        thresholdModel.charm.skill2Points = -10;
        loadout_summary_t negativeThreshold = LoadoutCalculator::calculate(thresholdModel, SAVE_FORMAT_N3DS);
        attack = skill(negativeThreshold, 11);
        require(attack && attack->negativeActive && attack->distanceToNext == 5 && !attack->nextSkill.isEmpty(),
                "negative active skill did not retain distance to its next tier");

        loadout_model_t genderConflict = model;
        genderConflict.head = piece(MH3U_Type::HeadType, 30);
        loadout_summary_t conflictSummary = LoadoutCalculator::calculate(genderConflict, SAVE_FORMAT_N3DS);
        require(conflictSummary.invalidCount > summary.invalidCount &&
                conflictSummary.diagnostics.join("\n").contains(QString::fromUtf8("性别不适用")),
                "selected armor gender conflict was not diagnosed");

        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory failed");
        QString loadoutPath = temporary.path() + "/test.mhloadout.json";
        QString error;
        require(LoadoutFile::save(loadoutPath, model, &error), "loadout JSON save failed");
        loadout_model_t roundTrip;
        bool versionWarning = false;
        require(LoadoutFile::load(loadoutPath, &roundTrip, &versionWarning, &error), "loadout JSON load failed");
        require(!versionWarning && roundTrip.complete() && roundTrip.waist.saveId == 168,
                "loadout JSON round trip changed IDs");

        QFile jsonFile(loadoutPath);
        require(jsonFile.open(QIODevice::ReadOnly), "loadout JSON reopen failed");
        QJsonDocument json = QJsonDocument::fromJson(jsonFile.readAll());
        jsonFile.close();
        QJsonObject object = json.object();
        object.insert("data_version", "old-database-version");
        require(jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "loadout JSON version rewrite failed");
        jsonFile.write(QJsonDocument(object).toJson());
        jsonFile.close();
        require(LoadoutFile::load(loadoutPath, &roundTrip, &versionWarning, &error) && versionWarning,
                "resolvable old data version was rejected");

        loadout_model_t unnaturalCharm = model;
        unnaturalCharm.charm.skill1Points = 127;
        equipment_t unnaturalCharmBytes;
        require(LoadoutCalculator::buildEquipment(unnaturalCharm, LoadoutCharm, unnaturalCharmBytes, &error),
                "safely encodable non-natural charm was blocked");
        equipment_validation_t unnaturalValidation = EquipmentValidator::validate(unnaturalCharmBytes, SAVE_FORMAT_N3DS);
        require(unnaturalValidation.status == EquipmentInvalid &&
                unnaturalValidation.details().contains("CHARM_COMBINATION_NOT_GENERATED"),
                "non-natural charm was not retained as a red diagnostic");
        QString unnaturalPath = temporary.path() + "/unnatural.mhloadout.json";
        require(LoadoutFile::save(unnaturalPath, unnaturalCharm, &error),
                "non-natural charm JSON save failed");
        loadout_model_t unnaturalRoundTrip;
        require(LoadoutFile::load(unnaturalPath, &unnaturalRoundTrip, &versionWarning, &error) &&
                unnaturalRoundTrip.charm.skill1Points == 127,
                "non-natural but encodable charm JSON was rejected");
        save_t unnaturalTarget;
        std::memset(&unnaturalTarget, 0, sizeof(unnaturalTarget));
        QList<int> unnaturalWritten;
        require(LoadoutSaveBridge::appendCompleteLoadout(unnaturalCharm, &unnaturalTarget,
                                                         &unnaturalWritten, &error) &&
                    unnaturalWritten.size() == 7 &&
                    (int)(int8_t)unnaturalTarget.box[0][6][5] == 127,
                "non-natural but encodable charm was blocked from equipment-box insertion");

        QFile malformedFile(unnaturalPath);
        require(malformedFile.open(QIODevice::ReadOnly), "malformed JSON fixture reopen failed");
        QJsonObject malformedRoot = QJsonDocument::fromJson(malformedFile.readAll()).object();
        malformedFile.close();
        QJsonObject malformedWeapon = malformedRoot.value("weapon").toObject();
        malformedWeapon.insert("save_id", 1.5);
        malformedRoot.insert("weapon", malformedWeapon);
        require(malformedFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "malformed JSON fixture rewrite failed");
        malformedFile.write(QJsonDocument(malformedRoot).toJson());
        malformedFile.close();
        loadout_model_t unchangedTarget;
        unchangedTarget.name = "keep";
        require(!LoadoutFile::load(unnaturalPath, &unchangedTarget, &versionWarning, &error) && unchangedTarget.name == "keep",
                "fractional save ID was accepted or changed the target model after parse failure");

        save_t save;
        std::memset(&save, 0, sizeof(save));
        QList<int> written;
        require(LoadoutSaveBridge::appendCompleteLoadout(model, &save, &written, &error),
                "complete loadout append failed");
        const int expectedTypes[7] = {7, 5, 1, 2, 3, 4, 6};
        for (int index = 0; index < 7; ++index)
        {
            require(written.at(index) == index, "loadout did not use first seven empty slots");
            require(save.box[0][index][0] == expectedTypes[index], "loadout equipment order is wrong");
        }
        require(save.box[0][1][1] == 0 && save.box[0][5][1] == 0,
                "new armor did not use initial upgrade value");

        loadout_model_t incomplete = model;
        incomplete.charm = loadout_charm_t();
        save_t incompleteTarget;
        std::memset(&incompleteTarget, 0, sizeof(incompleteTarget));
        save_t incompleteBefore = incompleteTarget;
        require(!LoadoutSaveBridge::appendCompleteLoadout(incomplete, &incompleteTarget, NULL, &error) &&
                error.contains(QString::fromUtf8("护石")),
                "incomplete loadout did not report its exact missing slot");
        require(std::memcmp(&incompleteBefore, &incompleteTarget, sizeof(incompleteTarget)) == 0,
                "incomplete loadout changed save memory");

        save_t full;
        std::memset(&full, 0, sizeof(full));
        for (int panel = 0; panel < 10; ++panel)
            for (int slotIndex = 0; slotIndex < 100; ++slotIndex)
            {
                full.box[panel][slotIndex][0] = MH3U_Type::GSType;
                full.box[panel][slotIndex][2] = 1;
            }
        save_t before = full;
        require(!LoadoutSaveBridge::appendCompleteLoadout(model, &full, NULL, &error),
                "full equipment box accepted loadout");
        require(std::memcmp(&before, &full, sizeof(full)) == 0,
                "failed loadout append changed save memory");

        if (argc == 4)
        {
            testRealSave(model, argv[2], SAVE_FORMAT_N3DS);
            testRealSave(model, argv[3], SAVE_FORMAT_WIIU);
        }

        std::cout << "loadout repository, calculator, JSON and transaction tests passed\n";
    }
    catch (const std::exception &exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}

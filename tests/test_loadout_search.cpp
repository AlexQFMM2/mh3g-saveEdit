#include "game_data_repository.hpp"
#include "loadout_search.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTimer>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message)
{ if (!condition) throw std::runtime_error(message); }

static loadout_candidate_t candidate(int type, int id, int slotCount = 0, int gender = 0)
{
    loadout_candidate_t value;
    value.found = true; value.confirmed = true; value.saveType = type;
    value.saveId = id; value.slotCount = slotCount; value.gender = gender;
    value.maxDefense = 1;
    return value;
}

static QVector<loadout_search_result_t> runSynthetic(bool torsoDecoration)
{
    loadout_search_snapshot_t snapshot;
    snapshot.request.weaponSaveType = MH3U_Type::GSType;
    snapshot.request.weaponSaveId = 1; snapshot.request.gender = -1;
    snapshot.request.maxSeconds = 1;
    snapshot.request.skills.append(loadout_search_skill_t{16, 11,
        torsoDecoration ? 4 : 6, QString::fromUtf8("测试发动技能")});
    snapshot.targetTrees.append(11); snapshot.targetThresholds.append(torsoDecoration ? 4 : 6);
    snapshot.weapon = candidate(MH3U_Type::GSType, 1, torsoDecoration ? 0 : 3);
    snapshot.armor[0].append(candidate(MH3U_Type::HeadType, 101, 0, 1));
    snapshot.armor[0].append(candidate(MH3U_Type::HeadType, 102, 0, 2));
    snapshot.armor[1].append(candidate(MH3U_Type::ChestType, 201, torsoDecoration ? 1 : 0));
    snapshot.armor[2].append(candidate(MH3U_Type::ArmsType, 301));
    snapshot.armor[3].append(candidate(MH3U_Type::WaistType, 401));
    snapshot.armor[4].append(candidate(MH3U_Type::LegsType, 501));
    if (torsoDecoration)
        for (int i = 0; i < snapshot.armor[0].size(); ++i)
            snapshot.armor[0][i].skillPoints[1] = 1;
    loadout_candidate_t charm = candidate(MH3U_Type::CharmType, 1);
    charm.classId = 1; snapshot.charms.append(charm);
    loadout_candidate_t decoration = candidate(0, 900, 1);
    decoration.skillPoints[11] = 2; snapshot.decorations.append(decoration);

    LoadoutSearchWorker worker(snapshot);
    QVector<loadout_search_result_t> results;
    QObject::connect(&worker, &LoadoutSearchWorker::result,
                     [&](const loadout_search_result_t &result) { results.append(result); });
    worker.run();
    return results;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) { std::cerr << "usage: test_loadout_search <mh3g.sqlite>\n"; return 2; }
    try
    {
        GameDataRepository &repository = GameDataRepository::instance();
        require(repository.open(QString::fromLocal8Bit(argv[1])), "database open failed");
        loadout_search_request_t request;
        request.weaponSaveType = MH3U_Type::GSType; request.weaponSaveId = 1;
        request.gender = -1; request.maxSeconds = 5;
        request.skills.append(loadout_search_skill_t{18, 11, 10, QString::fromUtf8("攻击力UP【小】")});
        loadout_search_snapshot_t snapshot; QString error;
        require(buildLoadoutSearchSnapshot(request, &snapshot, &error), qPrintable(error));
        require(snapshot.weapon.saveType == request.weaponSaveType && snapshot.weapon.saveId == request.weaponSaveId,
                "weapon was not fixed in snapshot");
        for (int i = 0; i < 5; ++i) require(!snapshot.armor[i].isEmpty(), "armor snapshot is incomplete");
        require(!snapshot.charms.isEmpty(), "natural charm snapshot is empty");
        for (int i = 0; i < snapshot.decorations.size(); ++i)
            require(snapshot.decorations.at(i).confirmed, "unconfirmed decoration entered snapshot");
        {
            loadout_search_request_t fixedRequest = request;
            loadout_candidate_t illegalArmor;
            for (int row = 0; row < snapshot.armor[0].size(); ++row)
                if (snapshot.armor[0].at(row).slotCount == 0) { illegalArmor = snapshot.armor[0].at(row); break; }
            require(illegalArmor.found, "test database has no zero-slot fixed armor candidate");
            loadout_candidate_t manualJewel;
            const QList<loadout_candidate_t> allJewels = repository.decorationCandidates();
            for (int row = 0; row < allJewels.size(); ++row)
                if (allJewels.at(row).confirmed && allJewels.at(row).slotCount > manualJewel.slotCount)
                    manualJewel = allJewels.at(row);
            require(manualJewel.found && manualJewel.slotCount > 0, "test database has no confirmed jewel");
            illegalArmor.decorations << manualJewel.saveId << manualJewel.saveId << manualJewel.saveId;
            fixedRequest.fixedWeaponDecorations << manualJewel.saveId << manualJewel.saveId << manualJewel.saveId;
            fixedRequest.fixedArmor[0] = illegalArmor;
            loadout_candidate_t illegalCharm = snapshot.charms.first();
            illegalCharm.skill1Id = 11; illegalCharm.skill1Points = 127;
            illegalCharm.skillPoints[11] = 127; illegalCharm.decorations << manualJewel.saveId;
            fixedRequest.fixedCharm = illegalCharm; fixedRequest.fixedCharmSelected = true;
            loadout_search_snapshot_t fixedSnapshot;
            require(buildLoadoutSearchSnapshot(fixedRequest, &fixedSnapshot, &error),
                    "explicit illegal fixed equipment was rejected");
            require(fixedSnapshot.armor[0].size() == 1 &&
                    fixedSnapshot.armor[0].first().saveId == illegalArmor.saveId,
                    "fixed armor did not become the only candidate");
            require(fixedSnapshot.armor[0].first().decorations.size() == 3,
                    "fixed armor decorations were discarded");
            require(fixedSnapshot.weapon.decorations.size() == 3,
                    "manual weapon decorations were discarded");
            require(fixedSnapshot.charms.size() == 1 && !fixedSnapshot.charms.first().confirmed &&
                    fixedSnapshot.charms.first().skill1Points == 127 &&
                    fixedSnapshot.charms.first().decorations.size() == 1,
                    "fixed illegal charm or decorations did not enter the snapshot unchanged");
            for (int part = 1; part < 5; ++part)
                for (int row = 0; row < fixedSnapshot.armor[part].size(); ++row)
                    require(fixedSnapshot.armor[part].at(row).confirmed,
                            "an unfixed armor part admitted an illegal automatic candidate");
            LoadoutSearchWorker fixedWorker(fixedSnapshot);
            QVector<loadout_search_result_t> fixedResults;
            QObject::connect(&fixedWorker, &LoadoutSearchWorker::result,
                             [&](const loadout_search_result_t &result) { fixedResults.append(result); });
            fixedWorker.run();
            require(!fixedResults.isEmpty(), "fixed equipment search returned no result");
            for (int row = 0; row < fixedResults.size(); ++row)
            {
                require(fixedResults.at(row).model.head.saveId == illegalArmor.saveId,
                        "worker changed the fixed armor");
                require(fixedResults.at(row).model.charm.skill1Points == 127,
                        "worker changed the fixed illegal charm");
                require(fixedResults.at(row).model.head.decorations.size() == 3 &&
                        fixedResults.at(row).model.charm.decorations.size() == 1 &&
                        fixedResults.at(row).model.weapon.decorations.size() == 3,
                        "worker dropped manually selected decorations");
            }
            require(LoadoutCalculator::calculate(fixedResults.first().model, SAVE_FORMAT_N3DS).invalidCount > 0,
                    "fixed illegal input was incorrectly relabelled as legal after search");
        }
        {
            loadout_search_request_t manySkills = request;
            manySkills.maxSeconds = 1;
            manySkills.skills.append(loadout_search_skill_t{24, 12, 10, QString::fromUtf8("防御力UP【小】")});
            manySkills.skills.append(loadout_search_skill_t{29, 13, 10, QString::fromUtf8("体力+20")});
            manySkills.skills.append(loadout_search_skill_t{36, 15, 10, QString::fromUtf8("精灵加护")});
            loadout_search_snapshot_t manySkillsSnapshot;
            require(buildLoadoutSearchSnapshot(manySkills, &manySkillsSnapshot, &error),
                    "four-skill request was rejected");
            require(manySkillsSnapshot.targetTrees.size() == 4,
                    "four-skill request was truncated");
            LoadoutSearchWorker manySkillsWorker(manySkillsSnapshot);
            int progressSignals = 0, resultSignals = 0;
            QObject::connect(&manySkillsWorker, &LoadoutSearchWorker::progress,
                             [&](const loadout_search_progress_t &) { ++progressSignals; });
            QObject::connect(&manySkillsWorker, &LoadoutSearchWorker::result,
                             [&](const loadout_search_result_t &) { ++resultSignals; });
            manySkillsWorker.run();
            require(progressSignals < 40, "four-skill search flooded progress signals");
            require(resultSignals <= 100, "four-skill search flooded result signals");
        }
        std::cout << "snapshot candidates: armor=" << snapshot.armor[0].size() << "/"
                  << snapshot.armor[1].size() << "/" << snapshot.armor[2].size() << "/"
                  << snapshot.armor[3].size() << "/" << snapshot.armor[4].size()
                  << ", charms=" << snapshot.charms.size()
                  << ", decorations=" << snapshot.decorations.size() << "\n";

        LoadoutSearchWorker completeWorker(snapshot);
        QVector<loadout_search_result_t> realResults;
        QString lastStage; qint64 lastChecked = 0;
        QObject::connect(&completeWorker, &LoadoutSearchWorker::result,
                         [&](const loadout_search_result_t &result) { realResults.append(result); });
        QObject::connect(&completeWorker, &LoadoutSearchWorker::progress,
                         [&](const loadout_search_progress_t &progress) {
            lastStage = progress.stage; lastChecked = progress.checked;
        });
        completeWorker.run();
        if (realResults.isEmpty())
            std::cerr << "last stage: " << lastStage.toStdString() << ", checked: " << lastChecked << "\n";
        require(!realResults.isEmpty(), "real five-second search found no easy legal loadout");
        const loadout_search_result_t realResult = realResults.first();
        require(realResult.equipmentNames.size() == LoadoutSlotCount &&
                realResult.naturalSlots.size() == LoadoutSlotCount &&
                realResult.usedSlots.size() == LoadoutSlotCount,
                "worker result omitted GUI display snapshot fields");
        require(realResult.model.weapon.saveType == request.weaponSaveType &&
                realResult.model.weapon.saveId == request.weaponSaveId,
                "real search changed the selected weapon");
        const loadout_summary_t realSummary = LoadoutCalculator::calculate(realResult.model, SAVE_FORMAT_N3DS);
        bool attackReached = false;
        for (int i = 0; i < realSummary.skills.size(); ++i)
            if (realSummary.skills.at(i).skillTreeId == 11 && realSummary.skills.at(i).total >= 10)
                attackReached = true;
        require(attackReached && realSummary.invalidCount == 0,
                "real search returned an invalid loadout or missed its active-skill threshold");

        const QVector<loadout_search_result_t> threeJewelResults = runSynthetic(false);
        require(!threeJewelResults.isEmpty(), "three-decoration solution was not found");
        bool sawMale = false, sawFemale = false;
        for (int i = 0; i < threeJewelResults.size(); ++i)
        {
            const loadout_model_t &model = threeJewelResults.at(i).model;
            require(model.weapon.saveType == MH3U_Type::GSType && model.weapon.saveId == 1,
                    "search changed the fixed weapon");
            require(model.weapon.decorations.size() == 3, "search did not install all three required decorations");
            sawMale |= model.gender == 0 && model.head.saveId == 101;
            sawFemale |= model.gender == 1 && model.head.saveId == 102;
        }
        require(sawMale && sawFemale, "gender branches mixed or omitted gender-locked armor");
        const QVector<loadout_search_result_t> repeatedResults = runSynthetic(false);
        require(repeatedResults.size() == threeJewelResults.size(), "stable search changed result count");
        for (int i = 0; i < repeatedResults.size(); ++i)
            require(repeatedResults.at(i).fingerprint == threeJewelResults.at(i).fingerprint,
                    "stable search changed result order");

        const QVector<loadout_search_result_t> torsoResults = runSynthetic(true);
        bool copiedChestDecoration = false;
        for (int i = 0; i < torsoResults.size(); ++i)
            copiedChestDecoration |= torsoResults.at(i).model.chest.decorations.size() == 1 &&
                torsoResults.at(i).summary.skills.first().total >= 4;
        require(copiedChestDecoration, "Torso Up did not copy the chest decoration contribution");

        QThread thread; LoadoutSearchWorker worker(snapshot); worker.moveToThread(&thread);
        bool finished = false, cancelled = false;
        QObject::connect(&thread, &QThread::started, &worker, &LoadoutSearchWorker::run);
        QObject::connect(&worker, &LoadoutSearchWorker::finished, [&](bool wasCancelled, bool) {
            finished = true; cancelled = wasCancelled; thread.quit();
        });
        QObject::connect(&thread, &QThread::finished, &app, &QCoreApplication::quit);
        QElapsedTimer lifecycle; lifecycle.start();
        QTimer::singleShot(20, [&worker]() { worker.pause(); });
        QTimer::singleShot(100, [&worker]() { worker.resume(); });
        QTimer::singleShot(160, [&worker]() { worker.cancel(); });
        thread.start(); app.exec();
        require(finished && cancelled, "worker did not cancel cleanly");
        require(lifecycle.elapsed() >= 140, "worker ignored pause/resume lifecycle");
        thread.wait();
        std::cout << "loadout search snapshot and worker tests passed\n";
    }
    catch (const std::exception &exception) { std::cerr << exception.what() << "\n"; return 1; }
    return 0;
}

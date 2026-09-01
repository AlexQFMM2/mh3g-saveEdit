#include "loadout_search.hpp"

#include <QMutexLocker>
#include <QThread>
#include <QtAlgorithms>

#include <algorithm>
#include <functional>

namespace
{
struct armor_state_t
{
    loadout_model_t model;
    QVector<int> points;
    QVector<int> chestPoints;
    QVector<int> capacities;
    int maxDefense;
    int resistance;
    int torsoCount;
    int score;

    armor_state_t() : maxDefense(0), resistance(0), torsoCount(0), score(0) {}
};

struct decoration_state_t
{
    loadout_model_t model;
    QVector<int> points;
    int used[LoadoutSlotCount];
    int counts[LoadoutSlotCount];
    int totalCapacity;
    int totalUsed;
    int totalCount;
    int score;

    decoration_state_t() : totalCapacity(0), totalUsed(0), totalCount(0), score(0)
    {
        for (int i = 0; i < LoadoutSlotCount; ++i) { used[i] = 0; counts[i] = 0; }
    }
};

struct decoration_pattern_t
{
    QList<int> ids;
    QVector<int> points;
    int used;

    decoration_pattern_t() : used(0) {}
};

int clampPoints(const loadout_search_snapshot_t &snapshot, int index, int value)
{
    const int threshold = snapshot.targetThresholds.value(index, 127);
    return qBound(-128, value, threshold);
}

void addPoints(const loadout_search_snapshot_t &snapshot, QVector<int> &values,
               const QMap<int, int> &points, bool torsoCopy, const QVector<int> &chestPoints)
{
    for (int i = 0; i < snapshot.targetTrees.size(); ++i)
    {
        const int tree = snapshot.targetTrees.at(i);
        int value = points.value(tree, 0);
        if (torsoCopy) value += chestPoints.value(i, 0);
        values[i] = clampPoints(snapshot, i, values.value(i) + value);
    }
}

void addProjectedPoints(const loadout_search_snapshot_t &snapshot, QVector<int> &values,
                        const QVector<int> &points, int multiplier = 1)
{
    for (int i = 0; i < snapshot.targetTrees.size(); ++i)
        values[i] = clampPoints(snapshot, i, values.value(i) + points.value(i) * multiplier);
}

QString stateKey(const armor_state_t &state)
{
    QString key;
    for (int i = 0; i < state.points.size(); ++i) key += QString::number(state.points.at(i)) + ",";
    key += ":";
    for (int i = 0; i < state.capacities.size(); ++i) key += QString::number(state.capacities.at(i)) + ",";
    key += ":";
    for (int i = 0; i < state.chestPoints.size(); ++i) key += QString::number(state.chestPoints.at(i)) + ",";
    key += QString(":%1").arg(state.torsoCount);
    return key;
}

QString decorationKey(const decoration_state_t &state, const loadout_search_snapshot_t &snapshot)
{
    QString key;
    for (int i = 0; i < state.points.size(); ++i) key += QString::number(state.points.at(i)) + ",";
    Q_UNUSED(snapshot);
    return key;
}

int stateScore(const QVector<int> &points, const loadout_search_snapshot_t &snapshot,
               int availableSlots, int defense = 0, int decorationCount = 0)
{
    int score = 0;
    for (int i = 0; i < points.size(); ++i) score += qMin(points.at(i), snapshot.targetThresholds.value(i));
    return score * 10000 + availableSlots * 100 + defense - decorationCount;
}

bool resultLess(const loadout_search_result_t &left, const loadout_search_result_t &right)
{
    if (left.score != right.score) return left.score > right.score;
    return left.fingerprint < right.fingerprint;
}

QString fingerprint(const loadout_model_t &model)
{
    const auto joinDecorations = [](const QList<int> &decorations) {
        QStringList values;
        for (int i = 0; i < decorations.size(); ++i) values << QString::number(decorations.at(i));
        return values.join(".");
    };
    QStringList values;
    values << QString::number(model.gender);
    for (int slot = LoadoutWeapon; slot <= LoadoutLegs; ++slot)
    {
        const loadout_piece_t *piece = model.piece((loadout_slot_e)slot);
        values << QString("%1:%2:%3").arg(piece->saveType).arg(piece->saveId).arg(joinDecorations(piece->decorations));
    }
    values << QString("%1:%2:%3:%4:%5:%6:%7").arg(model.charm.classId).arg(model.charm.slotCount)
        .arg(model.charm.skill1Id).arg(model.charm.skill1Points).arg(model.charm.skill2Id)
        .arg(model.charm.skill2Points).arg(joinDecorations(model.charm.decorations));
    return values.join("|");
}

QString equipmentFingerprint(const loadout_model_t &model)
{
    QStringList values;
    values << QString::number(model.gender);
    for (int slot = LoadoutWeapon; slot <= LoadoutLegs; ++slot)
    {
        const loadout_piece_t *piece = model.piece((loadout_slot_e)slot);
        values << QString("%1:%2").arg(piece->saveType).arg(piece->saveId);
    }
    values << QString("%1:%2:%3:%4:%5:%6:%7").arg(model.charm.classId).arg(model.charm.slotCount)
        .arg(model.charm.skill1Id).arg(model.charm.skill1Points).arg(model.charm.skill2Id)
        .arg(model.charm.skill2Points).arg(model.charm.selected ? 1 : 0);
    return values.join("|");
}

bool meetsTargets(const QVector<int> &points, const loadout_search_snapshot_t &snapshot)
{
    for (int i = 0; i < snapshot.targetThresholds.size(); ++i)
        if (points.value(i) < snapshot.targetThresholds.at(i)) return false;
    return true;
}

const loadout_candidate_t *armorDetail(const loadout_search_snapshot_t &snapshot,
                                       loadout_slot_e slot, int saveId)
{
    if (slot < LoadoutHead || slot > LoadoutLegs) return 0;
    const QVector<loadout_candidate_t> &rows = snapshot.armor[(int)slot - 1];
    for (int i = 0; i < rows.size(); ++i) if (rows.at(i).saveId == saveId) return &rows.at(i);
    return 0;
}

int chestDecorationMultiplier(const loadout_search_snapshot_t &snapshot, const loadout_model_t &model)
{
    int multiplier = 1;
    const loadout_slot_e armorSlots[] = {LoadoutHead, LoadoutArms, LoadoutWaist, LoadoutLegs};
    for (int i = 0; i < 4; ++i)
    {
        const loadout_piece_t *piece = model.piece(armorSlots[i]);
        const loadout_candidate_t *detail = piece ? armorDetail(snapshot, armorSlots[i], piece->saveId) : 0;
        if (detail && detail->skillPoints.value(1, 0) > 0) ++multiplier;
    }
    return multiplier;
}

loadout_summary_t makeSummary(const decoration_state_t &state,
                              const loadout_search_snapshot_t &snapshot)
{
    loadout_summary_t summary;
    summary.weaponDefense = snapshot.weapon.defense;
    summary.baseDefense = snapshot.weapon.defense;
    summary.maxDefense = snapshot.weapon.defense;
    summary.totalSlots = qMax(0, snapshot.weapon.slotCount) + qMax(0, state.model.charm.slotCount);
    for (int slot = LoadoutHead; slot <= LoadoutLegs; ++slot)
    {
        const loadout_piece_t *piece = state.model.piece((loadout_slot_e)slot);
        const loadout_candidate_t *detail = armorDetail(snapshot, (loadout_slot_e)slot, piece->saveId);
        if (!detail) continue;
        summary.totalSlots += qMax(0, detail->slotCount);
        summary.baseDefense += qMax(0, detail->baseDefense);
        summary.maxDefense += qMax(0, detail->maxDefense);
        summary.fireRes += detail->fireRes; summary.waterRes += detail->waterRes;
        summary.iceRes += detail->iceRes; summary.thunderRes += detail->thunderRes;
        summary.dragonRes += detail->dragonRes;
    }
    for (int slot = 0; slot < LoadoutSlotCount; ++slot) summary.usedSlots += state.used[slot];
    for (int i = 0; i < snapshot.targetTrees.size(); ++i)
    {
        loadout_skill_row_t row;
        row.skillTreeId = snapshot.targetTrees.at(i);
        row.name = snapshot.request.skills.value(i).name;
        row.columns = QVector<int>(LoadoutSlotCount, 0);
        row.total = state.points.value(i);
        row.activeSkill = snapshot.request.skills.value(i).name;
        row.distanceToNext = 0; row.positiveActive = true; row.negativeActive = false;
        summary.skills.append(row);
    }
    return summary;
}

QString projectedKey(const loadout_candidate_t &candidate,
                     const loadout_search_snapshot_t &snapshot, bool includeCompatibility)
{
    QString key = QString::number(qMax(0, candidate.slotCount));
    if (includeCompatibility)
        key += QString("/%1/%2/%3").arg(candidate.gender).arg(candidate.combat)
            .arg(candidate.skillPoints.value(1, 0) > 0 ? 1 : 0);
    for (int i = 0; i < snapshot.targetTrees.size(); ++i)
        key += QString("/%1").arg(clampPoints(snapshot, i,
            candidate.skillPoints.value(snapshot.targetTrees.at(i), 0)));
    return key;
}

bool betterArmorRepresentative(const loadout_candidate_t &candidate,
                               const loadout_candidate_t &current)
{
    if (candidate.maxDefense != current.maxDefense) return candidate.maxDefense > current.maxDefense;
    const int candidateResistance = candidate.fireRes + candidate.waterRes + candidate.iceRes +
        candidate.thunderRes + candidate.dragonRes;
    const int currentResistance = current.fireRes + current.waterRes + current.iceRes +
        current.thunderRes + current.dragonRes;
    if (candidateResistance != currentResistance) return candidateResistance > currentResistance;
    return candidate.saveId < current.saveId;
}

QVector<decoration_pattern_t> decorationPatterns(const loadout_search_snapshot_t &snapshot,
                                                 int capacity)
{
    QMap<QString, decoration_pattern_t> unique;
    decoration_pattern_t empty;
    empty.points = QVector<int>(snapshot.targetTrees.size(), 0);
    unique.insert(QStringLiteral("0"), empty);

    std::function<void(int, int, decoration_pattern_t)> visit =
        [&](int first, int depth, decoration_pattern_t pattern) {
            if (depth >= 3) return;
            for (int i = first; i < snapshot.decorations.size(); ++i)
            {
                const loadout_candidate_t &decoration = snapshot.decorations.at(i);
                if (pattern.used + decoration.slotCount > capacity) continue;
                decoration_pattern_t next = pattern;
                next.ids.append(decoration.saveId);
                next.used += decoration.slotCount;
                for (int skill = 0; skill < snapshot.targetTrees.size(); ++skill)
                    next.points[skill] += decoration.skillPoints.value(snapshot.targetTrees.at(skill), 0);
                QString key = QString::number(next.used);
                for (int skill = 0; skill < next.points.size(); ++skill)
                    key += QString("/%1").arg(next.points.at(skill));
                const decoration_pattern_t current = unique.value(key);
                if (!unique.contains(key) || next.ids.size() < current.ids.size() ||
                    (next.ids.size() == current.ids.size() && next.ids < current.ids))
                    unique.insert(key, next);
                visit(i, depth + 1, next);
            }
        };
    visit(0, 0, empty);
    QVector<decoration_pattern_t> result = QVector<decoration_pattern_t>::fromList(unique.values());
    std::sort(result.begin(), result.end(), [](const decoration_pattern_t &a,
                                               const decoration_pattern_t &b) {
        int aPoints = 0, bPoints = 0;
        for (int i = 0; i < a.points.size(); ++i) { aPoints += a.points.at(i); bPoints += b.points.at(i); }
        if (aPoints != bPoints) return aPoints > bPoints;
        if (a.used != b.used) return a.used < b.used;
        if (a.ids.size() != b.ids.size()) return a.ids.size() < b.ids.size();
        return a.ids < b.ids;
    });
    return result;
}

decoration_pattern_t fixedDecorationPattern(const QList<int> &ids,
                                            const loadout_search_snapshot_t &snapshot)
{
    decoration_pattern_t pattern;
    pattern.ids = ids;
    pattern.points = QVector<int>(snapshot.targetTrees.size(), 0);
    for (int i = 0; i < ids.size(); ++i)
    {
        const loadout_candidate_t detail = snapshot.decorationDetails.value(ids.at(i));
        pattern.used += qMax(0, detail.slotCount);
        for (int skill = 0; skill < snapshot.targetTrees.size(); ++skill)
            pattern.points[skill] += detail.skillPoints.value(snapshot.targetTrees.at(skill), 0);
    }
    return pattern;
}

void trimArmorStates(QVector<armor_state_t> &states, const loadout_search_snapshot_t &snapshot, int limit)
{
    QMap<QString, int> positions;
    QVector<armor_state_t> unique;
    for (int i = 0; i < states.size(); ++i)
    {
        const QString key = stateKey(states.at(i));
        if (!positions.contains(key))
        {
            positions.insert(key, unique.size());
            unique.append(states.at(i));
        }
        else if (states.at(i).score > unique.at(positions.value(key)).score)
            unique[positions.value(key)] = states.at(i);
    }
    std::sort(unique.begin(), unique.end(), [&snapshot](const armor_state_t &a, const armor_state_t &b) {
        if (a.score != b.score) return a.score > b.score;
        return stateKey(a) < stateKey(b);
    });
    if (unique.size() > limit) unique.resize(limit);
    states = unique;
}

void trimDecorationStates(QVector<decoration_state_t> &states,
                          const loadout_search_snapshot_t &snapshot, int limit)
{
    QMap<QString, int> positions;
    QVector<decoration_state_t> unique;
    for (int i = 0; i < states.size(); ++i)
    {
        const QString key = decorationKey(states.at(i), snapshot);
        if (!positions.contains(key))
        {
            positions.insert(key, unique.size());
            unique.append(states.at(i));
        }
        else if (states.at(i).score > unique.at(positions.value(key)).score)
            unique[positions.value(key)] = states.at(i);
    }
    std::sort(unique.begin(), unique.end(), [&snapshot](const decoration_state_t &a, const decoration_state_t &b) {
        if (a.score != b.score) return a.score > b.score;
        return decorationKey(a, snapshot) < decorationKey(b, snapshot);
    });
    if (unique.size() > limit) unique.resize(limit);
    states = unique;
}
}

bool buildLoadoutSearchSnapshot(const loadout_search_request_t &request,
                                loadout_search_snapshot_t *snapshot, QString *error)
{
    if (!snapshot) return false;
    if (request.weaponSaveType < 7 || request.weaponSaveType > 19 || request.weaponSaveType == 12 || request.weaponSaveId <= 0)
    { if (error) *error = QString::fromUtf8("请选择有效的具体武器。"); return false; }
    if (request.skills.isEmpty())
    { if (error) *error = QString::fromUtf8("请至少选择一个需要发动的技能。"); return false; }
    if (request.maxSeconds < 1 || request.maxSeconds > 3600)
    { if (error) *error = QString::fromUtf8("计算时间必须在 1～60 分钟之间。"); return false; }

    GameDataRepository &repository = GameDataRepository::instance();
    if (!repository.isOpen()) { if (error) *error = QString::fromUtf8("游戏数据库尚未打开。"); return false; }
    loadout_search_snapshot_t value;
    value.request = request;
    value.request.dataVersion = repository.dataVersion();
    value.weapon = repository.candidate(request.weaponSaveType, request.weaponSaveId);
    value.weapon.decorations = request.fixedWeaponDecorations;
    if (!value.weapon.found || value.weapon.placeholder || !value.weapon.confirmed ||
        (request.platform == SAVE_FORMAT_WIIU && value.weapon.mh3gOnly))
    { if (error) *error = QString::fromUtf8("所选武器不是已确认的自然装备。"); return false; }
    QSet<int> seenTrees;
    for (int i = 0; i < request.skills.size(); ++i)
    {
        const loadout_search_skill_t &skill = request.skills.at(i);
        if (skill.activeSkillId <= 0 || skill.skillTreeId <= 0 || skill.threshold <= 0)
        { if (error) *error = QString::fromUtf8("存在无效的发动技能目标。"); return false; }
        if (seenTrees.contains(skill.skillTreeId))
        { if (error) *error = QString::fromUtf8("同一技能系只能选择一个发动等级。"); return false; }
        seenTrees.insert(skill.skillTreeId);
        value.targetTrees.append(skill.skillTreeId);
        value.targetThresholds.append(skill.threshold);
    }
    if (value.targetTrees.isEmpty())
    { if (error) *error = QString::fromUtf8("技能目标无效或重复。"); return false; }

    const int combat = LoadoutCalculator::isRangedWeapon(request.weaponSaveType) ? 2 : 1;
    for (int part = 0; part < 5; ++part)
    {
        const int expectedType = LoadoutCalculator::expectedSaveType((loadout_slot_e)(part + 1));
        if (request.fixedArmor.value(part).found)
        {
            const loadout_candidate_t supplied = request.fixedArmor.at(part);
            loadout_candidate_t fixed = repository.candidate(supplied.saveType, supplied.saveId);
            fixed.decorations = supplied.decorations;
            if (!fixed.found || fixed.saveType != expectedType || fixed.saveId <= 0 || fixed.placeholder ||
                !fixed.confirmed || (request.platform == SAVE_FORMAT_WIIU && fixed.mh3gOnly) ||
                (fixed.combat > 0 && fixed.combat != combat) ||
                (request.gender >= 0 && fixed.gender > 0 && fixed.gender != request.gender + 1))
            {
                if (error) *error = QString::fromUtf8("固定的%1防具不是适用的自然装备。").arg(
                    part == 0 ? QString::fromUtf8("头部") : part == 1 ? QString::fromUtf8("胸部") :
                    part == 2 ? QString::fromUtf8("腕部") : part == 3 ? QString::fromUtf8("腰部") :
                    QString::fromUtf8("腿部"));
                return false;
            }
            value.request.fixedArmor[part] = fixed;
            value.armor[part].append(fixed);
            continue;
        }
        equipment_query_t query;
        query.combat = combat;
        query.gender = -1;
        query.confirmedOnly = true;
        query.limit = 10000;
        const QList<loadout_candidate_t> rows = repository.queryCandidates(
            expectedType, query);
        QMap<QString, loadout_candidate_t> representatives;
        for (int row = 0; row < rows.size(); ++row)
        {
            const loadout_candidate_t &candidate = rows.at(row);
            if (request.platform == SAVE_FORMAT_WIIU && candidate.mh3gOnly) continue;
            const QString key = projectedKey(candidate, value, true);
            if (!representatives.contains(key) ||
                betterArmorRepresentative(candidate, representatives.value(key)))
                representatives.insert(key, candidate);
        }
        value.armor[part] = QVector<loadout_candidate_t>::fromList(representatives.values());
        std::sort(value.armor[part].begin(), value.armor[part].end(), [&value](const loadout_candidate_t &a,
                                                                            const loadout_candidate_t &b) {
            int as = qMax(0, a.slotCount), bs = qMax(0, b.slotCount);
            for (int i = 0; i < value.targetTrees.size(); ++i)
            { as += a.skillPoints.value(value.targetTrees.at(i)); bs += b.skillPoints.value(value.targetTrees.at(i)); }
            if (as != bs) return as > bs;
            if (a.maxDefense != b.maxDefense) return a.maxDefense > b.maxDefense;
            return a.saveId < b.saveId;
        });
        if (value.armor[part].isEmpty())
        { if (error) *error = QString::fromUtf8("没有找到可用的%1防具候选。").arg(part == 0 ? QString::fromUtf8("头部") : part == 1 ? QString::fromUtf8("胸部") : part == 2 ? QString::fromUtf8("腕部") : part == 3 ? QString::fromUtf8("腰部") : QString::fromUtf8("腿部")); return false; }
    }
    if (request.fixedCharmSelected)
    {
        loadout_candidate_t fixed = repository.charmCandidate(request.fixedCharm.classId,
            request.fixedCharm.slotCount, request.fixedCharm.skill1Id, request.fixedCharm.skill1Points,
            request.fixedCharm.skill2Id, request.fixedCharm.skill2Points);
        fixed.decorations = request.fixedCharm.decorations;
        if (!fixed.found || fixed.saveType != MH3U_Type::CharmType || fixed.classId <= 0 ||
            fixed.slotCount < 0 || fixed.slotCount > 3)
        {
            if (error) *error = QString::fromUtf8("固定的护石数据无效。");
            return false;
        }
        value.request.fixedCharm = fixed;
        value.charms.append(fixed);
    }
    else
        value.charms = QVector<loadout_candidate_t>::fromList(
            repository.naturalCharmCandidates(value.targetTrees.toList()));
    const QList<loadout_candidate_t> decorations = repository.decorationCandidates();
    QMap<QString, loadout_candidate_t> decorationRepresentatives;
    for (int i = 0; i < decorations.size(); ++i)
    {
        const loadout_candidate_t &decoration = decorations.at(i);
        value.decorationDetails.insert(decoration.saveId, decoration);
        if (!decoration.confirmed || decoration.slotCount <= 0 || decoration.slotCount > 3) continue;
        bool useful = false;
        for (int target = 0; target < value.targetTrees.size(); ++target)
            useful |= decoration.skillPoints.value(value.targetTrees.at(target), 0) > 0;
        if (!useful) continue;
        const QString key = projectedKey(decoration, value, false);
        if (!decorationRepresentatives.contains(key) ||
            decoration.saveId < decorationRepresentatives.value(key).saveId)
            decorationRepresentatives.insert(key, decoration);
    }
    const auto validFixedDecorations = [&value](const QList<int> &ids) {
        if (ids.size() > 3) return false;
        for (int i = 0; i < ids.size(); ++i)
            if (!value.decorationDetails.contains(ids.at(i)) ||
                !value.decorationDetails.value(ids.at(i)).confirmed)
                return false;
        return true;
    };
    if (!validFixedDecorations(value.weapon.decorations))
    { if (error) *error = QString::fromUtf8("武器包含无效的手动装饰珠。"); return false; }
    for (int part = 0; part < 5; ++part)
        if (!value.armor[part].isEmpty() && !validFixedDecorations(value.armor[part].first().decorations))
        { if (error) *error = QString::fromUtf8("固定防具包含无效的手动装饰珠。"); return false; }
    if (request.fixedCharmSelected && !validFixedDecorations(value.charms.first().decorations))
    { if (error) *error = QString::fromUtf8("固定护石包含无效的手动装饰珠。"); return false; }
    value.decorations = QVector<loadout_candidate_t>::fromList(decorationRepresentatives.values());
    std::sort(value.decorations.begin(), value.decorations.end(), [&value](const loadout_candidate_t &a,
                                                                          const loadout_candidate_t &b) {
        int as = 0, bs = 0;
        for (int i = 0; i < value.targetTrees.size(); ++i)
        { as += a.skillPoints.value(value.targetTrees.at(i)); bs += b.skillPoints.value(value.targetTrees.at(i)); }
        if (as != bs) return as > bs;
        if (a.slotCount != b.slotCount) return a.slotCount < b.slotCount;
        return a.saveId < b.saveId;
    });
    std::sort(value.charms.begin(), value.charms.end(), [&value](const loadout_candidate_t &a, const loadout_candidate_t &b) {
        int as = 0, bs = 0;
        for (int i = 0; i < value.targetTrees.size(); ++i) { as += a.skillPoints.value(value.targetTrees.at(i)); bs += b.skillPoints.value(value.targetTrees.at(i)); }
        if (as != bs) return as > bs;
        if (a.slotCount != b.slotCount) return a.slotCount > b.slotCount;
        if (a.classId != b.classId) return a.classId < b.classId;
        if (a.skill1Id != b.skill1Id) return a.skill1Id < b.skill1Id;
        if (a.skill1Points != b.skill1Points) return a.skill1Points < b.skill1Points;
        if (a.skill2Id != b.skill2Id) return a.skill2Id < b.skill2Id;
        return a.skill2Points < b.skill2Points;
    });
    *snapshot = value;
    return true;
}

LoadoutSearchWorker::LoadoutSearchWorker(const loadout_search_snapshot_t &snapshot, QObject *parent)
    : QObject(parent), m_snapshot(snapshot), m_paused(false), m_cancelled(false), m_pausedMs(0),
      m_pauseStartedMs(0), m_lastProgressMs(-1000)
{
}

bool LoadoutSearchWorker::shouldStop()
{
    QMutexLocker locker(&m_controlMutex);
    return m_cancelled;
}

bool LoadoutSearchWorker::wasCancelled()
{
    QMutexLocker locker(&m_controlMutex);
    return m_cancelled;
}

bool LoadoutSearchWorker::timeExpired()
{
    QMutexLocker locker(&m_controlMutex);
    if (m_paused) return false;
    return m_clock.elapsed() - m_pausedMs >= (qint64)m_snapshot.request.maxSeconds * 1000;
}

bool LoadoutSearchWorker::waitIfPaused()
{
    QMutexLocker locker(&m_controlMutex);
    while (m_paused && !m_cancelled) m_pauseCondition.wait(&m_controlMutex);
    return !m_cancelled;
}

void LoadoutSearchWorker::pause()
{
    QMutexLocker locker(&m_controlMutex);
    if (!m_paused) { m_paused = true; m_pauseStartedMs = m_clock.elapsed(); }
}

void LoadoutSearchWorker::resume()
{
    QMutexLocker locker(&m_controlMutex);
    if (m_paused) { m_pausedMs += m_clock.elapsed() - m_pauseStartedMs; m_paused = false; m_pauseCondition.wakeAll(); }
}

void LoadoutSearchWorker::cancel()
{
    QMutexLocker locker(&m_controlMutex);
    m_cancelled = true;
    m_pauseCondition.wakeAll();
}

void LoadoutSearchWorker::emitProgress(const QString &stage, qint64 checked, qint64 elapsed, qint64 remaining)
{
    if (stage == m_lastProgressStage && elapsed - m_lastProgressMs < 150) return;
    m_lastProgressStage = stage; m_lastProgressMs = elapsed;
    loadout_search_progress_t value;
    value.stage = stage; value.checked = checked; value.elapsedMs = elapsed; value.remainingMs = remaining;
    QMutexLocker locker(&m_controlMutex); value.paused = m_paused;
    emit progress(value);
}

void LoadoutSearchWorker::run()
{
    m_clock.start();
    m_seen.clear();
    qint64 checked = 0;
    bool found = false;
    QVector<int> genders;
    if (m_snapshot.request.gender < 0) genders << 0 << 1; else genders << m_snapshot.request.gender;
    QVector<loadout_search_result_t> bestResults;
    QSet<QString> emittedResults;
    qint64 lastResultEmitMs = -1000;
    QVector<decoration_pattern_t> patternsByCapacity[4];
    for (int capacity = 0; capacity <= 3; ++capacity)
        patternsByCapacity[capacity] = decorationPatterns(m_snapshot, capacity);

    QVector<armor_state_t> armorStates;
    armor_state_t initial;
    initial.model.weapon.selected = true;
    initial.model.weapon.saveType = m_snapshot.weapon.saveType;
    initial.model.weapon.saveId = m_snapshot.weapon.saveId;
    initial.model.weapon.decorations = m_snapshot.weapon.decorations;
    initial.points = QVector<int>(m_snapshot.targetTrees.size(), 0);
    initial.chestPoints = initial.points;
    initial.capacities = QVector<int>(LoadoutSlotCount, 0);
    initial.capacities[LoadoutWeapon] = qMax(0, m_snapshot.weapon.slotCount);
    initial.score = 0;
    armorStates.append(initial);
    const loadout_slot_e order[] = {LoadoutChest, LoadoutHead, LoadoutArms, LoadoutWaist, LoadoutLegs};

    for (int genderIndex = 0; genderIndex < genders.size() && !shouldStop() && !timeExpired(); ++genderIndex)
    {
        const int gender = genders.at(genderIndex);
        armorStates = QVector<armor_state_t>(1, initial);
        for (int partIndex = 0; partIndex < 5 && !shouldStop() && !timeExpired(); ++partIndex)
        {
            const loadout_slot_e slot = order[partIndex];
            QVector<armor_state_t> next;
            const QVector<loadout_candidate_t> &candidates = m_snapshot.armor[(int)slot - 1];
            for (int s = 0; s < armorStates.size() && !shouldStop(); ++s)
            {
                if (!waitIfPaused()) break;
                const armor_state_t &base = armorStates.at(s);
                for (int c = 0; c < candidates.size() && !timeExpired(); ++c)
                {
                    if ((checked & 0x3ff) == 0 && !waitIfPaused()) break;
                    if (shouldStop()) break;
                    const loadout_candidate_t &candidate = candidates.at(c);
                    if (candidate.gender > 0 && candidate.gender != gender + 1) continue;
                    armor_state_t state = base;
                    loadout_piece_t *piece = state.model.piece(slot);
                    piece->selected = true; piece->saveType = candidate.saveType; piece->saveId = candidate.saveId;
                    piece->decorations = candidate.decorations;
                    state.capacities[(int)slot] = qMax(0, candidate.slotCount);
                    const bool torso = slot != LoadoutChest && candidate.skillPoints.value(1, 0) > 0;
                    if (torso) ++state.torsoCount;
                    if (slot == LoadoutChest) state.chestPoints = state.points;
                    addPoints(m_snapshot, state.points, candidate.skillPoints, torso, state.chestPoints);
                    if (slot == LoadoutChest) state.chestPoints = state.points;
                    int available = 0; for (int i = 0; i < state.capacities.size(); ++i) available += state.capacities.at(i);
                    state.maxDefense += qMax(0, candidate.maxDefense);
                    state.resistance += candidate.fireRes + candidate.waterRes + candidate.iceRes +
                        candidate.thunderRes + candidate.dragonRes;
                    state.score = stateScore(state.points, m_snapshot, available, state.maxDefense) + state.resistance;
                    next.append(state); ++checked;
                }
            }
            trimArmorStates(next, m_snapshot, 18000);
            armorStates = next;
            emitProgress(QString::fromUtf8("组合%1防具").arg(slot == LoadoutChest ? QString::fromUtf8("胸部") : slot == LoadoutHead ? QString::fromUtf8("头部") : slot == LoadoutArms ? QString::fromUtf8("腕部") : slot == LoadoutWaist ? QString::fromUtf8("腰部") : QString::fromUtf8("腿部")), checked, m_clock.elapsed() - m_pausedMs, qMax<qint64>(0, (qint64)m_snapshot.request.maxSeconds * 1000 - (m_clock.elapsed() - m_pausedMs)));
            if (timeExpired()) break;
        }
        if (armorStates.isEmpty()) continue;
        const int armorLimit = qMin(armorStates.size(), 500);
        const int charmLimit = qMin(m_snapshot.charms.size(), 1200);
        for (int a = 0; a < armorLimit && !shouldStop() && !timeExpired(); ++a)
        {
            const armor_state_t &armor = armorStates.at(a);
            for (int ch = 0; ch < charmLimit && !shouldStop() && !timeExpired(); ++ch)
            {
                if (!waitIfPaused()) break;
                const loadout_candidate_t &charm = m_snapshot.charms.at(ch);
                decoration_state_t base;
                base.model = armor.model;
                base.model.gender = gender;
                base.model.charm.selected = true; base.model.charm.classId = charm.classId; base.model.charm.slotCount = charm.slotCount;
                base.model.charm.skill1Id = charm.skill1Id; base.model.charm.skill1Points = charm.skill1Points;
                base.model.charm.skill2Id = charm.skill2Id; base.model.charm.skill2Points = charm.skill2Points;
                base.model.charm.decorations = charm.decorations;
                base.points = armor.points;
                addPoints(m_snapshot, base.points, charm.skillPoints, false, armor.chestPoints);
                base.used[LoadoutWeapon] = 0;
                for (int i = 1; i <= 5; ++i) base.used[i] = 0;
                base.used[LoadoutCharm] = 0;
                int availableSlots = qMax(0, m_snapshot.weapon.slotCount) + qMax(0, charm.slotCount);
                for (int i = LoadoutHead; i <= LoadoutLegs; ++i) availableSlots += armor.capacities.value(i);
                base.totalCapacity = availableSlots;
                for (int manualSlot = 0; manualSlot < LoadoutSlotCount; ++manualSlot)
                {
                    const QList<int> manualIds = manualSlot == LoadoutCharm ? base.model.charm.decorations :
                        base.model.piece((loadout_slot_e)manualSlot)->decorations;
                    if (manualIds.isEmpty()) continue;
                    const decoration_pattern_t manual = fixedDecorationPattern(manualIds, m_snapshot);
                    base.used[manualSlot] = manual.used; base.counts[manualSlot] = manual.ids.size();
                    base.totalUsed += manual.used; base.totalCount += manual.ids.size();
                    const int multiplier = manualSlot == LoadoutChest ? chestDecorationMultiplier(m_snapshot, base.model) : 1;
                    addProjectedPoints(m_snapshot, base.points, manual.points, multiplier);
                }
                base.score = stateScore(base.points, m_snapshot, availableSlots - base.totalUsed,
                    armor.maxDefense + qMax(0, m_snapshot.weapon.maxDefense), base.totalCount) + armor.resistance;
                QVector<decoration_state_t> beam; beam.append(base);
                for (int pieceIndex = 0; pieceIndex < LoadoutSlotCount && !shouldStop() && !timeExpired(); ++pieceIndex)
                {
                    const int naturalCapacity = pieceIndex == LoadoutWeapon ? qMax(0, m_snapshot.weapon.slotCount) :
                        pieceIndex == LoadoutCharm ? charm.slotCount : armor.capacities.value(pieceIndex);
                    const int capacity = qMax(0, naturalCapacity - base.used[pieceIndex]);
                    const int remainingRecords = qMax(0, 3 - base.counts[pieceIndex]);
                    const QVector<decoration_pattern_t> &patterns = patternsByCapacity[qBound(0, capacity, 3)];
                    QVector<decoration_state_t> next;
                    for (int b = 0; b < beam.size(); ++b)
                    {
                        for (int p = 0; p < patterns.size(); ++p)
                        {
                            if ((checked & 0x3ff) == 0)
                            {
                                if (!waitIfPaused() || shouldStop() || timeExpired()) break;
                            }
                            const decoration_pattern_t &pattern = patterns.at(p);
                            if (pattern.ids.size() > remainingRecords) continue;
                            decoration_state_t state = beam.at(b);
                            state.used[pieceIndex] += pattern.used;
                            state.counts[pieceIndex] += pattern.ids.size();
                            state.totalUsed += pattern.used;
                            state.totalCount += pattern.ids.size();
                            if (pieceIndex == LoadoutCharm) state.model.charm.decorations += pattern.ids;
                            else state.model.piece((loadout_slot_e)pieceIndex)->decorations += pattern.ids;
                            const int multiplier = pieceIndex == LoadoutChest ? chestDecorationMultiplier(m_snapshot, state.model) : 1;
                            addProjectedPoints(m_snapshot, state.points, pattern.points, multiplier);
                            state.score = stateScore(state.points, m_snapshot,
                                state.totalCapacity - state.totalUsed,
                                armor.maxDefense + qMax(0, m_snapshot.weapon.maxDefense),
                                state.totalCount) + armor.resistance;
                            next.append(state); ++checked;
                        }
                    }
                    trimDecorationStates(next, m_snapshot, 260);
                    beam = next;
                    emitProgress(QString::fromUtf8("补足装饰珠"), checked, m_clock.elapsed() - m_pausedMs, qMax<qint64>(0, (qint64)m_snapshot.request.maxSeconds * 1000 - (m_clock.elapsed() - m_pausedMs)));
                }
                // Keep one best decoration solution per seven-piece equipment
                // combination. Different jewel placements that reach the same
                // target are not useful as separate results.
                QMap<QString, decoration_state_t> bestForEquipment;
                for (int b = 0; b < beam.size() && !shouldStop(); ++b)
                {
                    if (!meetsTargets(beam.at(b).points, m_snapshot)) continue;
                    const QString baseKey = equipmentFingerprint(beam.at(b).model);
                    if (!bestForEquipment.contains(baseKey) ||
                        beam.at(b).score > bestForEquipment.value(baseKey).score)
                        bestForEquipment.insert(baseKey, beam.at(b));
                }
                const QVector<decoration_state_t> bestBeam = QVector<decoration_state_t>::fromList(bestForEquipment.values());
                for (int b = 0; b < bestBeam.size() && !shouldStop(); ++b)
                {
                    loadout_search_result_t result;
                    result.model = bestBeam.at(b).model;
                    result.summary = makeSummary(bestBeam.at(b), m_snapshot);
                    result.equipmentNames << m_snapshot.weapon.name;
                    result.naturalSlots = QVector<int>(LoadoutSlotCount, 0);
                    result.usedSlots = QVector<int>(LoadoutSlotCount, 0);
                    result.naturalSlots[LoadoutWeapon] = qMax(0, m_snapshot.weapon.slotCount);
                    result.usedSlots[LoadoutWeapon] = bestBeam.at(b).used[LoadoutWeapon];
                    for (int armorSlot = LoadoutHead; armorSlot <= LoadoutLegs; ++armorSlot)
                    {
                        const loadout_piece_t *piece = result.model.piece((loadout_slot_e)armorSlot);
                        const loadout_candidate_t *detail = armorDetail(m_snapshot, (loadout_slot_e)armorSlot,
                                                                        piece ? piece->saveId : 0);
                        result.equipmentNames << (detail ? detail->name : QString());
                        result.naturalSlots[armorSlot] = detail ? qMax(0, detail->slotCount) : 0;
                        result.usedSlots[armorSlot] = bestBeam.at(b).used[armorSlot];
                    }
                    result.equipmentNames << charm.name;
                    result.naturalSlots[LoadoutCharm] = qMax(0, charm.slotCount);
                    result.usedSlots[LoadoutCharm] = bestBeam.at(b).used[LoadoutCharm];
                    result.fingerprint = fingerprint(result.model);
                    result.score = bestBeam.at(b).score + (result.summary.totalSlots - result.summary.usedSlots);
                    if (m_seen.contains(result.fingerprint)) continue;
                    m_seen.insert(result.fingerprint); found = true;
                    bestResults.append(result);
                    std::sort(bestResults.begin(), bestResults.end(), resultLess);
                    if (bestResults.size() > 100) bestResults.resize(100);
                    bool retained = false;
                    for (int retainedIndex = 0; retainedIndex < bestResults.size(); ++retainedIndex)
                        if (bestResults.at(retainedIndex).fingerprint == result.fingerprint)
                        { retained = true; break; }
                    const qint64 activeElapsed = m_clock.elapsed() - m_pausedMs;
                    if (retained && !emittedResults.contains(result.fingerprint) &&
                        (emittedResults.size() < 10 || activeElapsed - lastResultEmitMs >= 250))
                    {
                        emittedResults.insert(result.fingerprint); lastResultEmitMs = activeElapsed;
                        emit this->result(result);
                    }
                }
                if (timeExpired()) break;
            }
        }
    }
    bool cancelled = wasCancelled();
    if (!cancelled)
        for (int resultIndex = 0; resultIndex < bestResults.size(); ++resultIndex)
            if (!emittedResults.contains(bestResults.at(resultIndex).fingerprint))
                emit this->result(bestResults.at(resultIndex));
    emitProgress(cancelled ? QString::fromUtf8("已取消") : (found ? QString::fromUtf8("搜索完成") : QString::fromUtf8("暂无找到合适的配装")), checked, m_clock.elapsed() - m_pausedMs, cancelled ? 0 : qMax<qint64>(0, (qint64)m_snapshot.request.maxSeconds * 1000 - (m_clock.elapsed() - m_pausedMs)));
    emit finished(cancelled, found);
}

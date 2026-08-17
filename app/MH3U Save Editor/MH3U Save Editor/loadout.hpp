#ifndef MH3G_LOADOUT_HPP
#define MH3G_LOADOUT_HPP

#include "game_data_repository.hpp"
#include "mh3u_se.hpp"

#include <QList>
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

enum loadout_slot_e
{
    LoadoutWeapon = 0,
    LoadoutHead = 1,
    LoadoutChest = 2,
    LoadoutArms = 3,
    LoadoutWaist = 4,
    LoadoutLegs = 5,
    LoadoutCharm = 6,
    LoadoutSlotCount = 7
};

struct loadout_piece_t
{
    bool selected;
    int saveType;
    int saveId;
    QList<int> decorations;

    loadout_piece_t() : selected(false), saveType(0), saveId(0) {}
};

struct loadout_charm_t
{
    bool selected;
    int classId;
    int slotCount;
    int skill1Id;
    int skill1Points;
    int skill2Id;
    int skill2Points;
    QList<int> decorations;

    loadout_charm_t()
        : selected(false), classId(0), slotCount(0), skill1Id(0), skill1Points(0),
          skill2Id(0), skill2Points(0) {}
};

struct loadout_model_t
{
    QString name;
    int gender;
    loadout_piece_t weapon;
    loadout_piece_t head;
    loadout_piece_t chest;
    loadout_piece_t arms;
    loadout_piece_t waist;
    loadout_piece_t legs;
    loadout_charm_t charm;

    loadout_model_t() : gender(0) {}
    loadout_piece_t *piece(loadout_slot_e slot);
    const loadout_piece_t *piece(loadout_slot_e slot) const;
    bool complete() const;
    void clear();
};

struct loadout_skill_row_t
{
    int skillTreeId;
    QString name;
    QVector<int> columns;
    int total;
    QString activeSkill;
    QString nextSkill;
    int distanceToNext;
    bool positiveActive;
    bool negativeActive;
};

struct loadout_summary_t
{
    QList<loadout_skill_row_t> skills;
    int baseDefense;
    int maxDefense;
    int weaponDefense;
    int fireRes;
    int waterRes;
    int iceRes;
    int thunderRes;
    int dragonRes;
    int totalSlots;
    int usedSlots;
    int invalidCount;
    int unknownCount;
    bool defenseUnknown;
    bool resistanceUnknown;
    bool slotsUnknown;
    QStringList diagnostics;

    loadout_summary_t();
};

class LoadoutCalculator
{
public:
    static loadout_summary_t calculate(const loadout_model_t &model,
                                       save_format_e platform = SAVE_FORMAT_UNKNOWN);
    static bool buildEquipment(const loadout_model_t &model, loadout_slot_e slot,
                               equipment_t &equipment, QString *error = NULL);
    static int expectedSaveType(loadout_slot_e slot);
    static bool isRangedWeapon(int saveType);
};

class LoadoutFile
{
public:
    static QByteArray serialize(const loadout_model_t &model);
    static bool deserialize(const QByteArray &bytes, loadout_model_t *model,
                            bool *versionWarning = NULL, QString *error = NULL);
    static bool save(const QString &path, const loadout_model_t &model, QString *error = NULL);
    static bool load(const QString &path, loadout_model_t *model, bool *versionWarning = NULL,
                     QString *error = NULL);
};

class LoadoutSaveBridge
{
public:
    static bool appendCompleteLoadout(const loadout_model_t &model, save_t *save,
                                      QList<int> *writtenIndexes = NULL, QString *error = NULL);
};

#endif

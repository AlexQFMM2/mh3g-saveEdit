#ifndef SAVE_ACTION_BRIDGE_HPP
#define SAVE_ACTION_BRIDGE_HPP

#include "mh3u_se.hpp"

#include <QString>
#include <QVector>

struct SaveActionResult
{
    bool success = false;
    int panel = -1;
    int slot = -1;
    QString error;

    QString slotLabel() const;
};

struct ArmorSaveRef
{
    quint8 saveType = 0;
    quint16 saveId = 0;
};

struct SaveActionBatchResult
{
    bool success = false;
    QVector<SaveActionResult> placements;
    QString error;
};

class SaveActionBridge
{
public:
    explicit SaveActionBridge(MH3U_SE *save);

    bool hasOpenSave() const;
    int characterGender() const;
    SaveActionResult previewAddItem(quint16 saveId, quint16 count) const;
    SaveActionResult addItem(quint16 saveId, quint16 count);
    SaveActionResult previewAddWeapon(quint8 saveType, quint16 saveId) const;
    SaveActionResult addWeapon(quint8 saveType, quint16 saveId);
    SaveActionResult previewAddArmor(quint8 saveType, quint16 saveId) const;
    SaveActionResult addArmor(quint8 saveType, quint16 saveId);
    SaveActionBatchResult previewAddArmorSet(const QVector<ArmorSaveRef> &armors) const;
    SaveActionBatchResult addArmorSet(const QVector<ArmorSaveRef> &armors);

private:
    MH3U_SE *m_save;
};

#endif

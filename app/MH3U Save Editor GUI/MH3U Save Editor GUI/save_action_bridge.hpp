#ifndef SAVE_ACTION_BRIDGE_HPP
#define SAVE_ACTION_BRIDGE_HPP

#include "mh3u_se.hpp"

#include <QString>

struct SaveActionResult
{
    bool success = false;
    int panel = -1;
    int slot = -1;
    QString error;

    QString slotLabel() const;
};

class SaveActionBridge
{
public:
    explicit SaveActionBridge(MH3U_SE *save);

    bool hasOpenSave() const;
    SaveActionResult previewAddItem(quint16 saveId, quint16 count) const;
    SaveActionResult addItem(quint16 saveId, quint16 count);
    SaveActionResult previewAddWeapon(quint8 saveType, quint16 saveId) const;
    SaveActionResult addWeapon(quint8 saveType, quint16 saveId);

private:
    MH3U_SE *m_save;
};

#endif

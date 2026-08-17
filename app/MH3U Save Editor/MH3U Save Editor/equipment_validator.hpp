#ifndef EQUIPMENT_VALIDATOR_HPP
#define EQUIPMENT_VALIDATOR_HPP

#include "mh3u_se.hpp"

#include <QString>
#include <QVector>

enum equipment_validity_e
{
    EquipmentValid = 0,
    EquipmentUnknown = 1,
    EquipmentInvalid = 2,
};

struct equipment_diagnostic_t
{
    equipment_validity_e severity;
    QString field;
    QString code;
    QString message;
};

struct equipment_validation_t
{
    equipment_validity_e status;
    QVector<equipment_diagnostic_t> diagnostics;

    equipment_validation_t() : status(EquipmentValid) {}
    QString statusText() const;
    QString details() const;
};

class EquipmentValidator
{
public:
    static equipment_validation_t validate(const equipment_t &equipment, save_format_e platform = SAVE_FORMAT_UNKNOWN,
                                            int characterSex = -1);
};

#endif

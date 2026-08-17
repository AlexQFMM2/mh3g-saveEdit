#ifndef QWEAPON_H
#define QWEAPON_H

#include "main.hpp"

#include "qequipment.hpp"

#include <QWidget>
#include <QDialog>
class QLabel;

class QWeapon : public QEquipment
{
    Q_OBJECT
public:
    explicit QWeapon(weapon_t *weapon, QWidget *parent = 0,
                     save_format_e platform = SAVE_FORMAT_UNKNOWN, int characterSex = -1);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void saveAndAccept();
    void refreshValidity();

private:
    weapon_t *weapon;
    save_format_e m_platform;
    int m_characterSex;
    QComboBox *m_equipmentType;
    QComboBox *m_identifier;
    QComboBox *m_firstJewelIdentifier;
    QComboBox *m_secondJewelIdentifier;
    QComboBox *m_thirdJewelIdentifier;
    QLabel *m_validityLabel;

    void load();
    void save();
    bool validate();
};

#endif // QWEAPON_H

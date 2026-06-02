#ifndef QWEAPON_H
#define QWEAPON_H

#include "main.hpp"

#include "qequipment.hpp"

#include <QWidget>
#include <QDialog>

class QWeapon : public QEquipment
{
    Q_OBJECT
public:
    explicit QWeapon(weapon_t *weapon, QWidget *parent = 0);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void saveAndAccept();

private:
    weapon_t *weapon;
    QComboBox *m_equipmentType;
    QComboBox *m_identifier;
    QComboBox *m_firstJewelIdentifier;
    QComboBox *m_secondJewelIdentifier;
    QComboBox *m_thirdJewelIdentifier;

    void load();
    void save();
    bool validate();
};

#endif // QWEAPON_H

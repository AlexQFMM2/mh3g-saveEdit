#ifndef QARMOR_HPP
#define QARMOR_HPP

#include "main.hpp"

#include "qequipment.hpp"

#include <QWidget>
#include <QDialog>
#include <QSpinBox>
#include <QComboBox>
class QLabel;

class QArmor : public QEquipment
{
    Q_OBJECT
public:
    explicit QArmor(armor_t *armor, QWidget *parent = 0,
                    save_format_e platform = SAVE_FORMAT_UNKNOWN, int characterSex = -1);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void saveAndAccept();
    void refreshValidity();

private:
    armor_t *armor;
    save_format_e m_platform;
    int m_characterSex;
    QComboBox *m_equipmentType;
    QComboBox *m_identifier;
    QSpinBox *m_blueComponent;
    QSpinBox *m_greenComponent;
    QSpinBox *m_redComponent;
    QComboBox *m_firstJewelIdentifier;
    QComboBox *m_secondJewelIdentifier;
    QComboBox *m_thirdJewelIdentifier;
    QLabel *m_validityLabel;

    void load();
    void save();
    bool validate();
};

#endif // QARMOR_HPP

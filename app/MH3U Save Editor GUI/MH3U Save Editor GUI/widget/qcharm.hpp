#ifndef QCHARM_HPP
#define QCHARM_HPP

#include "main.hpp"

#include "qequipment.hpp"

#include <QWidget>
#include <QDialog>
#include <QSpinBox>
#include <QComboBox>
class QLabel;

class QCharm : public QEquipment
{
    Q_OBJECT
public:
    explicit QCharm(charm_t *charm, QWidget *parent = 0,
                    save_format_e platform = SAVE_FORMAT_UNKNOWN, int characterSex = -1);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void saveAndAccept();
    void refreshValidity();

private:
    charm_t *charm;
    save_format_e m_platform;
    int m_characterSex;
    QComboBox *m_equipmentType;
    QSpinBox *m_slotsCount;
    QComboBox *m_identifier;
    QComboBox *m_firstSkillIdentifier;
    QSpinBox *m_firstSkillValue;
    QComboBox *m_secondSkillIdentifier;
    QSpinBox *m_secondSkillValue;
    QComboBox *m_firstJewelIdentifier;
    QComboBox *m_secondJewelIdentifier;
    QComboBox *m_thirdJewelIdentifier;
    QLabel *m_validityLabel;

    void load();
    void save();
    bool validate();
};

#endif // QCHARM_HPP

#ifndef QCHARM_HPP
#define QCHARM_HPP

#include "main.hpp"

#include "qequipment.hpp"

#include <QWidget>
#include <QDialog>
#include <QSpinBox>
#include <QComboBox>

class QCharm : public QEquipment
{
    Q_OBJECT
public:
    explicit QCharm(charm_t *charm, QWidget *parent = 0);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void saveAndAccept();

private:
    charm_t *charm;
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

    void load();
    void save();
    bool validate();
};

#endif // QCHARM_HPP

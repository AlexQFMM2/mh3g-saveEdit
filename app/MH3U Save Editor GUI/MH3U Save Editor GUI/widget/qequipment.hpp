#ifndef QEQUIPMENT_HPP
#define QEQUIPMENT_HPP

#include "main.hpp"

#include <QWidget>
#include <QDialog>
#include <QComboBox>
#include <QLabel>

class QEquipment : public QDialog
{
    Q_OBJECT
public:
    explicit QEquipment(equipment_t *equipment, QWidget *parent = 0);

protected:
    void closeEvent(QCloseEvent *);

private slots:
    void equipmentTypeChanged(int index);

private:
    equipment_t *equipment;
    QComboBox *m_equipmentType;
    QLabel *m_rawBytes;
    bool m_typeEditable;
    bool m_loading;
    bool m_saved;

    void load();
    void save();
};

#endif // QEQUIPMENT_HPP

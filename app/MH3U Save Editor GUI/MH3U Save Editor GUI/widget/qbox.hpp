#ifndef QBOX_HPP
#define QBOX_HPP

#include "main.hpp"

#include "qequipment.hpp"
#include "qarmor.hpp"
#include "qcharm.hpp"
#include "qweapon.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

class QBox : public QWidget
{
    Q_OBJECT
public:
    explicit QBox(MH3U_SE *mh3u, QWidget *parent = 0);
    ~QBox();

private:
    MH3U_SE *mh3u;
    QTableWidget *m_table;
    QLineEdit *m_search;
    QCheckBox *m_nonEmptyOnly;
    QComboBox *m_typeFilter;
    QLabel *m_selectedInfo;
    QPushButton *m_editButton;
    QPushButton *m_addButton;

    QString equipmentTooltip(equipment_t &equipment) const;
    QString equipmentTypeName(uint8_t equipmentType) const;
    QString equipmentIdentifierName(uint8_t equipmentType, uint16_t identifier) const;
    QString equipmentDisplayName(equipment_t &equipment) const;
    QString equipmentSlotTitle(uint32_t panel, uint32_t slot, equipment_t &equipment) const;
    QString jewelSummary(equipment_t &equipment) const;
    const dataset_t* equipmentDataset(uint8_t equipmentType) const;
    void populateTable();
    bool editSlot(uint32_t panel, uint32_t slot);
    equipment_t& equipmentAt(uint32_t panel, uint32_t slot) const;
    bool equipmentMatchesFilters(equipment_t &equipment, uint32_t panel, uint32_t slot) const;
    bool chooseNewEquipmentType(uint8_t *equipmentType);
    void initializeEmptyEquipment(equipment_t &equipment, uint8_t equipmentType);

public slots:
    void buttonClicked(int id);
    void tableCellDoubleClicked(int row, int column);
    void editSelectedEquipment();
    void addEquipmentToFirstEmptySlot();
    void updateSelectedInfo();
    void refreshFilters();
};

#endif // QBOX_HPP

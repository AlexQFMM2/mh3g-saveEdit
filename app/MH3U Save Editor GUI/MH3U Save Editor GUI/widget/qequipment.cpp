#include "qequipment.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QStringList>
#include <QVBoxLayout>

QEquipment::QEquipment(equipment_t *equipment, QWidget *parent) : QDialog(parent)
{
    if (equipment == NULL)
        return;

    this->equipment = equipment;
    uint16_t identifier = (*equipment)[2] + (*equipment)[3] * 0x100;
    m_typeEditable = ((*equipment)[0] == MH3U_Type::NoneType && identifier == 0);
    m_loading = false;
    m_saved = false;

    m_equipmentType = new QComboBox(this);
    m_equipmentType->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::equipmentTypes()->size(); i++)
    {
        m_equipmentType->addItem(QString(MH3U_DS::equipmentTypes()->at(i).identifier.c_str()), MH3U_DS::equipmentTypes()->at(i).count);
    }
    configureSearchableComboBox(m_equipmentType);

    m_equipmentType->setEnabled(m_typeEditable);
    connect(m_equipmentType, SIGNAL(currentIndexChanged(int)), this, SLOT(equipmentTypeChanged(int)));
    m_rawBytes = new QLabel(this);
    m_rawBytes->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_rawBytes->setWordWrap(true);

    QFormLayout *form = new QFormLayout();
    form->addRow(uiText("Equipment type"), m_equipmentType);
    form->addRow(uiText("Raw bytes"), m_rawBytes);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, SIGNAL(rejected()), this, SLOT(close()));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    this->setLayout(layout);
    this->setWindowTitle(uiText("Single equipment editor"));

    this->load();
}

void QEquipment::load()
{
    m_loading = true;
    m_equipmentType->setCurrentIndex(m_equipmentType->findData((*equipment)[ 0]));

    QStringList bytes;
    for (uint8_t i = 0; i < EQUIPMENT_SIZE; i++)
    {
        bytes << QString("%1:%2").arg(i, 2, 10, QChar('0')).arg((*equipment)[i], 2, 16, QChar('0')).toUpper();
    }
    m_rawBytes->setText(bytes.join("  "));
    m_loading = false;
}

void QEquipment::save()
{
    if (!m_typeEditable)
    {
        // Unknown equipment rows are intentionally read-only.
        return;
    }

    for (uint8_t i = 0; i < EQUIPMENT_SIZE; i++)
    {
        (*equipment)[i] = 0;
    }
    (*equipment)[0] = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    m_saved = true;
}

void QEquipment::closeEvent(QCloseEvent *)
{
    equipment = NULL;
}

void QEquipment::equipmentTypeChanged(int)
{
    if (!m_typeEditable || m_loading)
    {
        return;
    }

    save();
    accept();
}

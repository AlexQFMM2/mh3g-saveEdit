#include "qcharm.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

QCharm::QCharm(charm_t *charm, QWidget *parent) : QEquipment(NULL, parent)
{
    this->charm = charm;

    m_equipmentType = new QComboBox(this);
    m_equipmentType->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::equipmentTypes()->size(); i++)
    {
        m_equipmentType->addItem(QString(MH3U_DS::equipmentTypes()->at(i).identifier.c_str()), MH3U_DS::equipmentTypes()->at(i).count);
    }
    configureSearchableComboBox(m_equipmentType);
    m_equipmentType->setEnabled(false);

    m_slotsCount = new QSpinBox(this);
    m_slotsCount->setMinimum(0x00);
    m_slotsCount->setMaximum(0xff);

    m_identifier = new QComboBox(this);
    m_identifier->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::charms()->size(); i++)
    {
        m_identifier->addItem(QString(MH3U_DS::charms()->at(i).identifier.c_str()), MH3U_DS::charms()->at(i).count);
    }
    configureSearchableComboBox(m_identifier);

    m_firstSkillIdentifier = new QComboBox(this);
    m_secondSkillIdentifier = new QComboBox(this);
    m_firstSkillIdentifier->addItem(uiText("(None)"), 0);
    m_secondSkillIdentifier->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::skills()->size(); i++)
    {
        m_firstSkillIdentifier->addItem(QString(MH3U_DS::skills()->at(i).identifier.c_str()), MH3U_DS::skills()->at(i).count);
        m_secondSkillIdentifier->addItem(QString(MH3U_DS::skills()->at(i).identifier.c_str()), MH3U_DS::skills()->at(i).count);
    }
    configureSearchableComboBox(m_firstSkillIdentifier);
    configureSearchableComboBox(m_secondSkillIdentifier);
    m_firstSkillValue = new QSpinBox(this);
    m_firstSkillValue->setMinimum(0x00);
    m_firstSkillValue->setMaximum(0xff);
    m_secondSkillValue = new QSpinBox(this);
    m_secondSkillValue->setMinimum(0x00);
    m_secondSkillValue->setMaximum(0xff);

    m_firstJewelIdentifier = new QComboBox(this);
    m_secondJewelIdentifier = new QComboBox(this);
    m_thirdJewelIdentifier = new QComboBox(this);
    m_firstJewelIdentifier->addItem(uiText("(None)"), 0);
    m_secondJewelIdentifier->addItem(uiText("(None)"), 0);
    m_thirdJewelIdentifier->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::jewels()->size(); i++)
    {
        m_firstJewelIdentifier->addItem(QString(MH3U_DS::jewels()->at(i).identifier.c_str()), MH3U_DS::jewels()->at(i).count);
        m_secondJewelIdentifier->addItem(QString(MH3U_DS::jewels()->at(i).identifier.c_str()), MH3U_DS::jewels()->at(i).count);
        m_thirdJewelIdentifier->addItem(QString(MH3U_DS::jewels()->at(i).identifier.c_str()), MH3U_DS::jewels()->at(i).count);
    }
    configureSearchableComboBox(m_firstJewelIdentifier);
    configureSearchableComboBox(m_secondJewelIdentifier);
    configureSearchableComboBox(m_thirdJewelIdentifier);


    QGridLayout *layout = new QGridLayout(this);
    layout->addWidget(new QLabel(uiText("Equipment type"), this), 0, 0);
    layout->addWidget(new QLabel(uiText("Slots count"), this), 0, 1);
    layout->addWidget(new QLabel(uiText("Identifier"), this), 0, 2);
    layout->addWidget(new QLabel(uiText("First Skill's Identifier"), this), 0, 3);
    layout->addWidget(new QLabel(uiText("First Skill's Value"), this), 0, 4);
    layout->addWidget(new QLabel(uiText("Second Skill's Identifier"), this), 0, 5);
    layout->addWidget(new QLabel(uiText("Second Skill's Value"), this), 0, 6);
    layout->addWidget(new QLabel(uiText("First Jewel's Identifier"), this), 0, 7);
    layout->addWidget(new QLabel(uiText("Second Jewel's Identifier"), this), 0, 8);
    layout->addWidget(new QLabel(uiText("Third Jewel's Identifier"), this), 0, 9);
    layout->addWidget(m_equipmentType, 1, 0);
    layout->addWidget(m_slotsCount, 1, 1);
    layout->addWidget(m_identifier, 1, 2);
    layout->addWidget(m_firstSkillIdentifier, 1, 3);
    layout->addWidget(m_firstSkillValue, 1, 4);
    layout->addWidget(m_secondSkillIdentifier, 1, 5);
    layout->addWidget(m_secondSkillValue, 1, 6);
    layout->addWidget(m_firstJewelIdentifier, 1, 7);
    layout->addWidget(m_secondJewelIdentifier, 1, 8);
    layout->addWidget(m_thirdJewelIdentifier, 1, 9);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    connect(buttons, SIGNAL(accepted()), this, SLOT(saveAndAccept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
    layout->addWidget(buttons, 2, 0, 1, 10);
    this->setLayout(layout);
    this->setWindowTitle(uiText("Single charm editor"));

    this->load();
}


void QCharm::load()
{
    m_equipmentType->setCurrentIndex(m_equipmentType->findData(charm->equipmentType));
    m_slotsCount->setValue(charm->slotsCount);
    m_identifier->setCurrentIndex(m_identifier->findData(charm->identifier));
    m_firstSkillIdentifier->setCurrentIndex(m_firstSkillIdentifier->findData(charm->firstSkillIdentifier));
    m_firstSkillValue->setValue(charm->firstSkillValue);
    m_secondSkillIdentifier->setCurrentIndex(m_secondSkillIdentifier->findData(charm->secondSkillIdentifier));
    m_secondSkillValue->setValue(charm->secondSkillValue);
    m_firstJewelIdentifier->setCurrentIndex(m_firstJewelIdentifier->findData(charm->firstJewelIdentifier));
    m_secondJewelIdentifier->setCurrentIndex(m_secondJewelIdentifier->findData(charm->secondJewelIdentifier));
    m_thirdJewelIdentifier->setCurrentIndex(m_thirdJewelIdentifier->findData(charm->thirdJewelIdentifier));
}

void QCharm::save()
{
    charm->equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    charm->slotsCount = m_slotsCount->value();
    charm->identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();
    charm->firstSkillIdentifier = (uint8_t) searchableComboBoxCurrentData(m_firstSkillIdentifier).toInt();
    charm->firstSkillValue = m_firstSkillValue->value();
    charm->secondSkillIdentifier = (uint8_t) searchableComboBoxCurrentData(m_secondSkillIdentifier).toInt();
    charm->secondSkillValue = m_secondSkillValue->value();
    charm->firstJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_firstJewelIdentifier).toInt();
    charm->secondJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_secondJewelIdentifier).toInt();
    charm->thirdJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_thirdJewelIdentifier).toInt();
}

void QCharm::closeEvent(QCloseEvent *)
{
    charm = NULL;
}

bool QCharm::validate()
{
    uint8_t equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    uint16_t identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();

    if (equipmentType == MH3U_Type::NoneType)
    {
        QMessageBox::warning(this, windowTitle(), "装备类型不能为“无”。");
        return false;
    }

    if (MH3U_Armory::convertSubtype((equipment_type_e) equipmentType) != MH3U_Type::CharmSubtype)
    {
        QMessageBox::warning(this, windowTitle(), "当前窗口只能保存护石类型。");
        return false;
    }

    if (identifier == 0)
    {
        QMessageBox::warning(this, windowTitle(), "编号不能为“无”。请先选择具体护石。");
        return false;
    }

    return true;
}

void QCharm::saveAndAccept()
{
    if (!validate())
    {
        return;
    }

    save();
    accept();
}

#include "qarmor.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

QArmor::QArmor(armor_t *armor, QWidget *parent) : QEquipment(NULL, parent)
{
    this->armor = armor;

    m_equipmentType = new QComboBox(this);
    m_equipmentType->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::equipmentTypes()->size(); i++)
    {
        m_equipmentType->addItem(QString(MH3U_DS::equipmentTypes()->at(i).identifier.c_str()), MH3U_DS::equipmentTypes()->at(i).count);
    }
    configureSearchableComboBox(m_equipmentType);
    m_equipmentType->setEnabled(false);

    m_upgradeLevel = new QSpinBox(this);
    m_upgradeLevel->setMinimum(0x00);
    m_upgradeLevel->setMaximum(0xff);

    const dataset_t *identifier = NULL;
    switch ((equipment_type_e) armor->equipmentType)
    {
        case MH3U_Type::ChestType:
        {
            identifier = MH3U_DS::chestArmors();
            break;
        }
        case MH3U_Type::ArmsType:
        {
            identifier = MH3U_DS::armsArmors();
            break;
        }
        case MH3U_Type::WaistType:
        {
            identifier = MH3U_DS::waistArmors();
            break;
        }
        case MH3U_Type::LegsType:
        {
            identifier = MH3U_DS::legsArmors();
            break;
        }
        case MH3U_Type::HeadType:
        {
            identifier = MH3U_DS::headArmors();
            break;
        }
        default:
        {
            identifier = MH3U_DS::charms();
            break;
        }
    }

    m_identifier = new QComboBox(this);
    m_identifier->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < identifier->size(); i++)
    {
        m_identifier->addItem(QString(identifier->at(i).identifier.c_str()), identifier->at(i).count);
    }
    configureSearchableComboBox(m_identifier);
    identifier = NULL;

    m_blueComponent = new QSpinBox(this);
    m_blueComponent->setMinimum(0x00);
    m_blueComponent->setMaximum(0xff);
    m_greenComponent = new QSpinBox(this);
    m_greenComponent->setMinimum(0x00);
    m_greenComponent->setMaximum(0xff);
    m_redComponent = new QSpinBox(this);
    m_redComponent->setMinimum(0x00);
    m_redComponent->setMaximum(0xff);

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
    layout->addWidget(new QLabel(uiText("Upgrade level"), this), 0, 1);
    layout->addWidget(new QLabel(uiText("Identifier"), this), 0, 2);
    layout->addWidget(new QLabel(uiText("Blue Component"), this), 0, 3);
    layout->addWidget(new QLabel(uiText("Green Component"), this), 0, 4);
    layout->addWidget(new QLabel(uiText("Red Component"), this), 0, 5);
    layout->addWidget(new QLabel(uiText("First Jewel's Identifier"), this), 0, 6);
    layout->addWidget(new QLabel(uiText("Second Jewel's Identifier"), this), 0, 7);
    layout->addWidget(new QLabel(uiText("Third Jewel's Identifier"), this), 0, 8);
    layout->addWidget(m_equipmentType, 1, 0);
    layout->addWidget(m_upgradeLevel, 1, 1);
    layout->addWidget(m_identifier, 1, 2);
    layout->addWidget(m_blueComponent, 1, 3);
    layout->addWidget(m_greenComponent, 1, 4);
    layout->addWidget(m_redComponent, 1, 5);
    layout->addWidget(m_firstJewelIdentifier, 1, 6);
    layout->addWidget(m_secondJewelIdentifier, 1, 7);
    layout->addWidget(m_thirdJewelIdentifier, 1, 8);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    connect(buttons, SIGNAL(accepted()), this, SLOT(saveAndAccept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
    layout->addWidget(buttons, 2, 0, 1, 9);
    this->setLayout(layout);
    this->setWindowTitle(uiText("Single armor editor"));

    this->load();
}


void QArmor::closeEvent(QCloseEvent *)
{
    armor = NULL;
}


void QArmor::load()
{
    m_equipmentType->setCurrentIndex(m_equipmentType->findData(armor->equipmentType));
    m_upgradeLevel->setValue(armor->upgradeLevel);
    m_identifier->setCurrentIndex(m_identifier->findData(armor->identifier));
    m_blueComponent->setValue(armor->blueComponent);
    m_greenComponent->setValue(armor->greenComponent);
    m_redComponent->setValue(armor->redComponent);
    m_firstJewelIdentifier->setCurrentIndex(m_firstJewelIdentifier->findData(armor->firstJewelIdentifier));
    m_secondJewelIdentifier->setCurrentIndex(m_secondJewelIdentifier->findData(armor->secondJewelIdentifier));
    m_thirdJewelIdentifier->setCurrentIndex(m_thirdJewelIdentifier->findData(armor->thirdJewelIdentifier));
}

void QArmor::save()
{
    armor->equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    armor->upgradeLevel = m_upgradeLevel->value();
    armor->identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();
    armor->blueComponent = m_blueComponent->value();
    armor->greenComponent = m_greenComponent->value();
    armor->redComponent = m_redComponent->value();
    armor->firstJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_firstJewelIdentifier).toInt();
    armor->secondJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_secondJewelIdentifier).toInt();
    armor->thirdJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_thirdJewelIdentifier).toInt();
}

bool QArmor::validate()
{
    uint8_t equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    uint16_t identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();

    if (equipmentType == MH3U_Type::NoneType)
    {
        QMessageBox::warning(this, windowTitle(), "装备类型不能为“无”。");
        return false;
    }

    if (MH3U_Armory::convertSubtype((equipment_type_e) equipmentType) != MH3U_Type::ArmorSubtype)
    {
        QMessageBox::warning(this, windowTitle(), "当前窗口只能保存防具类型。");
        return false;
    }

    if (identifier == 0)
    {
        QMessageBox::warning(this, windowTitle(), "编号不能为“无”。请先选择具体防具。");
        return false;
    }

    return true;
}

void QArmor::saveAndAccept()
{
    if (!validate())
    {
        return;
    }

    save();
    accept();
}

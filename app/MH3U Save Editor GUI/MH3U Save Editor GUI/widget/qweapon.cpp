#include "qweapon.hpp"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>

QWeapon::QWeapon(weapon_t *weapon, QWidget *parent) : QEquipment(NULL, parent)
{
    this->weapon = weapon;

    m_equipmentType = new QComboBox(this);
    m_equipmentType->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::equipmentTypes()->size(); i++)
    {
        m_equipmentType->addItem(QString(MH3U_DS::equipmentTypes()->at(i).identifier.c_str()), MH3U_DS::equipmentTypes()->at(i).count);
    }
    configureSearchableComboBox(m_equipmentType);
    m_equipmentType->setEnabled(false);

    const dataset_t *identifier = NULL;
    switch ((equipment_type_e) weapon->equipmentType)
    {
        case MH3U_Type::GSType:
        {
            identifier = MH3U_DS::gsWeapons();
            break;
        }
        case MH3U_Type::SNSType:
        {
            identifier = MH3U_DS::snsWeapons();
            break;
        }
        case MH3U_Type::HType:
        {
            identifier = MH3U_DS::hWeapons();
            break;
        }
        case MH3U_Type::LType:
        {
            identifier = MH3U_DS::lWeapons();
            break;
        }
        case MH3U_Type::HBGType:
        {
            identifier = MH3U_DS::hbgWeapons();
            break;
        }
        case MH3U_Type::LBGType:
        {
            identifier = MH3U_DS::lbgWeapons();
            break;
        }
        case MH3U_Type::LSType:
        {
            identifier = MH3U_DS::lsWeapons();
            break;
        }
        case MH3U_Type::SAType:
        {
            identifier = MH3U_DS::saWeapons();
            break;
        }
        case MH3U_Type::GLType:
        {
            identifier = MH3U_DS::glWeapons();
            break;
        }
        case MH3U_Type::BowType:
        {
            identifier = MH3U_DS::bowWeapons();
            break;
        }
        case MH3U_Type::DBType:
        {
            identifier = MH3U_DS::dbWeapons();
            break;
        }
        case MH3U_Type::HHType:
        {
            identifier = MH3U_DS::hhWeapons();
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
    if (identifier != NULL)
    {
        for (uint32_t i = 0; i < identifier->size(); i++)
        {
            m_identifier->addItem(QString(identifier->at(i).identifier.c_str()), identifier->at(i).count);
        }
    }
    configureSearchableComboBox(m_identifier);
    identifier = NULL;

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
    layout->addWidget(new QLabel(uiText("Identifier"), this), 0, 1);
    layout->addWidget(new QLabel(uiText("First Jewel's Identifier"), this), 0, 2);
    layout->addWidget(new QLabel(uiText("Second Jewel's Identifier"), this), 0, 3);
    layout->addWidget(new QLabel(uiText("Third Jewel's Identifier"), this), 0, 4);
    layout->addWidget(m_equipmentType, 1, 0);
    layout->addWidget(m_identifier, 1, 1);
    layout->addWidget(m_firstJewelIdentifier, 1, 2);
    layout->addWidget(m_secondJewelIdentifier, 1, 3);
    layout->addWidget(m_thirdJewelIdentifier, 1, 4);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    connect(buttons, SIGNAL(accepted()), this, SLOT(saveAndAccept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
    layout->addWidget(buttons, 2, 0, 1, 5);
    this->setLayout(layout);
    this->setWindowTitle(uiText("Single weapon editor"));

    this->load();
}


void QWeapon::closeEvent(QCloseEvent *)
{
    weapon = NULL;
}


void QWeapon::load()
{
    m_equipmentType->setCurrentIndex(m_equipmentType->findData(weapon->equipmentType));
    m_identifier->setCurrentIndex(m_identifier->findData(weapon->identifier));
    m_firstJewelIdentifier->setCurrentIndex(m_firstJewelIdentifier->findData(weapon->firstJewelIdentifier));
    m_secondJewelIdentifier->setCurrentIndex(m_secondJewelIdentifier->findData(weapon->secondJewelIdentifier));
    m_thirdJewelIdentifier->setCurrentIndex(m_thirdJewelIdentifier->findData(weapon->thirdJewelIdentifier));
}

void QWeapon::save()
{
    weapon->equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    weapon->identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();
    weapon->firstJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_firstJewelIdentifier).toInt();
    weapon->secondJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_secondJewelIdentifier).toInt();
    weapon->thirdJewelIdentifier = (uint16_t) searchableComboBoxCurrentData(m_thirdJewelIdentifier).toInt();
}

bool QWeapon::validate()
{
    uint8_t equipmentType = (uint8_t) searchableComboBoxCurrentData(m_equipmentType).toInt();
    uint16_t identifier = (uint16_t) searchableComboBoxCurrentData(m_identifier).toInt();

    if (equipmentType == MH3U_Type::NoneType)
    {
        QMessageBox::warning(this, windowTitle(), "装备类型不能为“无”。");
        return false;
    }

    if (MH3U_Armory::convertSubtype((equipment_type_e) equipmentType) != MH3U_Type::WeaponSubtype)
    {
        QMessageBox::warning(this, windowTitle(), "当前窗口只能保存武器类型。");
        return false;
    }

    if (identifier == 0)
    {
        QMessageBox::warning(this, windowTitle(), "编号不能为“无”。请先选择具体武器。");
        return false;
    }

    return true;
}

void QWeapon::saveAndAccept()
{
    if (!validate())
    {
        return;
    }

    save();
    accept();
}

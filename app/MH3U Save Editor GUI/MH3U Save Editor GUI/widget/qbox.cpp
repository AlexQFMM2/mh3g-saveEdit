#include "qbox.hpp"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QVBoxLayout>

QBox::QBox(MH3U_SE *mh3u, QWidget *parent) : QWidget(parent)
{
    this->mh3u = mh3u;

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(QStringList() << "页" << "格" << "类型" << "名称" << "ID" << "装饰品");
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);

    connect(m_table, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(tableCellDoubleClicked(int,int)));
    connect(m_table, SIGNAL(itemSelectionChanged()), this, SLOT(updateSelectedInfo()));

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("搜索装备 / 类型 / ID / 装饰品");
    connect(m_search, SIGNAL(textChanged(QString)), this, SLOT(refreshFilters()));

    m_nonEmptyOnly = new QCheckBox("只显示非空", this);
    connect(m_nonEmptyOnly, SIGNAL(toggled(bool)), this, SLOT(refreshFilters()));

    m_typeFilter = new QComboBox(this);
    m_typeFilter->addItem("全部类型", -1);
    m_typeFilter->addItem(uiText("(None)"), 0);
    const dataset_t *types = MH3U_DS::equipmentTypes();
    if (types != NULL)
    {
        for (uint32_t i = 0; i < types->size(); i++)
        {
            if (!types->at(i).identifier.empty())
            {
                m_typeFilter->addItem(QString(types->at(i).identifier.c_str()), types->at(i).count);
            }
        }
    }
    connect(m_typeFilter, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshFilters()));

    m_selectedInfo = new QLabel("(无)", this);
    m_selectedInfo->setWordWrap(true);

    m_editButton = new QPushButton("编辑选中", this);
    connect(m_editButton, SIGNAL(clicked(bool)), this, SLOT(editSelectedEquipment()));

    m_addButton = new QPushButton("新增装备", this);
    connect(m_addButton, SIGNAL(clicked(bool)), this, SLOT(addEquipmentToFirstEmptySlot()));

    QVBoxLayout *sideLayout = new QVBoxLayout();
    sideLayout->addWidget(new QLabel("筛选", this));
    sideLayout->addWidget(m_search);
    sideLayout->addWidget(m_typeFilter);
    sideLayout->addWidget(m_nonEmptyOnly);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(new QLabel("选中", this));
    sideLayout->addWidget(m_selectedInfo);
    sideLayout->addWidget(m_addButton);
    sideLayout->addWidget(m_editButton);
    sideLayout->addStretch(1);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_table, 1);
    mainLayout->addLayout(sideLayout);
    this->setLayout(mainLayout);
    this->setWindowTitle(uiText("Box editor"));
    this->resize(1100, 720);

    populateTable();
    updateSelectedInfo();
}

QBox::~QBox()
{
    this->mh3u = NULL;
}

void QBox::buttonClicked(int id)
{
    editSlot(id / 100, id % 100);
}

void QBox::tableCellDoubleClicked(int row, int)
{
    QTableWidgetItem *pageItem = m_table->item(row, 0);
    if (pageItem == NULL)
    {
        return;
    }

    editSlot(pageItem->data(Qt::UserRole).toUInt(), pageItem->data(Qt::UserRole + 1).toUInt());
}

void QBox::editSelectedEquipment()
{
    int row = m_table->currentRow();
    if (row < 0)
    {
        return;
    }

    tableCellDoubleClicked(row, 0);
}

void QBox::addEquipmentToFirstEmptySlot()
{
    for (uint32_t panel = 0; panel < 10; panel++)
    {
        for (uint32_t slot = 0; slot < 100; slot++)
        {
            equipment_t &equipment = equipmentAt(panel, slot);
            uint16_t identifier = equipment[2] + equipment[3] * 0x100;
            if (equipment[0] == MH3U_Type::NoneType && identifier == 0)
            {
                uint8_t equipmentType = MH3U_Type::NoneType;
                if (!chooseNewEquipmentType(&equipmentType))
                {
                    return;
                }

                initializeEmptyEquipment(equipment, equipmentType);
                if (!editSlot(panel, slot))
                {
                    initializeEmptyEquipment(equipment, MH3U_Type::NoneType);
                    populateTable();
                    updateSelectedInfo();
                }
                return;
            }
        }
    }

    QMessageBox::information(this, windowTitle(), "没有空装备格。");
}

void QBox::updateSelectedInfo()
{
    int row = m_table->currentRow();
    if (row < 0)
    {
        m_selectedInfo->setText("(无)");
        return;
    }

    QTableWidgetItem *pageItem = m_table->item(row, 0);
    if (pageItem == NULL)
    {
        m_selectedInfo->setText("(无)");
        return;
    }

    equipment_t &equipment = equipmentAt(pageItem->data(Qt::UserRole).toUInt(), pageItem->data(Qt::UserRole + 1).toUInt());
    m_selectedInfo->setText(equipmentTooltip(equipment));
}

void QBox::refreshFilters()
{
    populateTable();
    updateSelectedInfo();
}

void QBox::populateTable()
{
    m_table->setRowCount(0);

    for (uint32_t panel = 0; panel < 10; panel++)
    {
        for (uint32_t slot = 0; slot < 100; slot++)
        {
            equipment_t &equipment = equipmentAt(panel, slot);
            if (!equipmentMatchesFilters(equipment, panel, slot))
            {
                continue;
            }

            uint8_t equipmentType = equipment[0];
            uint16_t identifier = equipment[2] + equipment[3] * 0x100;
            QString name = equipmentDisplayName(equipment);
            QString typeName = equipmentTypeName(equipmentType);

            int row = m_table->rowCount();
            m_table->insertRow(row);

            QTableWidgetItem *pageItem = new QTableWidgetItem(QString::number(panel + 1));
            pageItem->setData(Qt::UserRole, panel);
            pageItem->setData(Qt::UserRole + 1, slot);
            m_table->setItem(row, 0, pageItem);
            m_table->setItem(row, 1, new QTableWidgetItem(QString::number(slot + 1)));
            m_table->setItem(row, 2, new QTableWidgetItem(typeName));
            m_table->setItem(row, 3, new QTableWidgetItem(name));
            m_table->setItem(row, 4, new QTableWidgetItem(QString::number(identifier)));
            m_table->setItem(row, 5, new QTableWidgetItem(jewelSummary(equipment)));
        }
    }

    if (m_table->rowCount() > 0)
    {
        m_table->selectRow(0);
    }
}

bool QBox::editSlot(uint32_t panel, uint32_t slot)
{
    equipment_type_e newType(MH3U_Type::NoneType), oldType(MH3U_Type::NoneType);
    equipment_subtype_e subtype;
    bool saved = false;
    uint8_t original[EQUIPMENT_SIZE];

    for (uint8_t i = 0; i < EQUIPMENT_SIZE; i++)
    {
        original[i] = equipmentAt(panel, slot)[i];
    }

    auto finishWithoutSaving = [&]() -> bool
    {
        equipment_t &equipment = equipmentAt(panel, slot);
        uint16_t identifier = equipment[2] + equipment[3] * 0x100;
        if (saved && identifier == 0)
        {
            for (uint8_t i = 0; i < EQUIPMENT_SIZE; i++)
            {
                equipment[i] = original[i];
            }
            saved = false;
        }

        populateTable();
        updateSelectedInfo();
        return saved;
    };

    do
    {
        equipment_t &equipment = equipmentAt(panel, slot);
        oldType = (equipment_type_e) equipment[0];
        subtype = MH3U_Armory::convertSubtype(oldType);
        QString title = equipmentSlotTitle(panel, slot, equipment);

        switch (subtype)
        {
            case MH3U_Type::ArmorSubtype:
            {
                armor_t armor = MH3U_Armory::convertEquipmentToArmor(equipment);

                QArmor qarmor(&armor, this);
                qarmor.setModal(true);
                qarmor.setWindowTitle(title);
                if (qarmor.exec() == QDialog::Accepted)
                {
                    MH3U_Armory::convertArmorToEquipment(armor, equipment);
                    saved = true;
                }
                else
                {
                    return finishWithoutSaving();
                }
                break;
            }
            case MH3U_Type::CharmSubtype:
            {
                charm_t charm = MH3U_Armory::convertEquipmentToCharm(equipment);

                QCharm qcharm(&charm, this);
                qcharm.setModal(true);
                qcharm.setWindowTitle(title);
                if (qcharm.exec() == QDialog::Accepted)
                {
                    MH3U_Armory::convertCharmToEquipment(charm, equipment);
                    saved = true;
                }
                else
                {
                    return finishWithoutSaving();
                }
                break;
            }
            case MH3U_Type::WeaponSubtype:
            {
                weapon_t weapon = MH3U_Armory::convertEquipmentToWeapon(equipment);

                QWeapon qweapon(&weapon, this);
                qweapon.setModal(true);
                qweapon.setWindowTitle(title);
                if (qweapon.exec() == QDialog::Accepted)
                {
                    MH3U_Armory::convertWeaponToEquipment(weapon, equipment);
                    saved = true;
                }
                else
                {
                    return finishWithoutSaving();
                }
                break;
            }
            default:
            {
                QEquipment qequipment(&equipment, this);
                qequipment.setModal(true);
                qequipment.setWindowTitle(title);
                if (qequipment.exec() == QDialog::Accepted)
                {
                    saved = true;
                }
                else
                {
                    return finishWithoutSaving();
                }
                break;
            }
        }

        newType = (equipment_type_e) equipmentAt(panel, slot)[0];

    } while (oldType != newType);

    populateTable();
    updateSelectedInfo();
    return saved;
}

equipment_t& QBox::equipmentAt(uint32_t panel, uint32_t slot) const
{
    return this->mh3u->savedata->box[panel][slot];
}

bool QBox::chooseNewEquipmentType(uint8_t *equipmentType)
{
    if (equipmentType == NULL)
    {
        return false;
    }

    int filterType = m_typeFilter->currentData().toInt();
    if (filterType > 0)
    {
        *equipmentType = (uint8_t) filterType;
        return true;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("选择新增装备类型");

    QLabel *label = new QLabel("选择类型后会直接打开详细编辑窗口。", &dialog);
    QComboBox *combo = new QComboBox(&dialog);
    const dataset_t *types = MH3U_DS::equipmentTypes();
    if (types != NULL)
    {
        for (uint32_t i = 0; i < types->size(); i++)
        {
            if (!types->at(i).identifier.empty())
            {
                combo->addItem(QString(types->at(i).identifier.c_str()), types->at(i).count);
            }
        }
    }
    configureSearchableComboBox(combo);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(label);
    layout->addWidget(combo);
    layout->addWidget(buttons);
    dialog.setLayout(layout);
    dialog.resize(420, 120);

    if (combo->count() == 0 || dialog.exec() != QDialog::Accepted)
    {
        return false;
    }

    *equipmentType = (uint8_t) searchableComboBoxCurrentData(combo).toInt();
    return *equipmentType != MH3U_Type::NoneType;
}

void QBox::initializeEmptyEquipment(equipment_t &equipment, uint8_t equipmentType)
{
    for (uint8_t i = 0; i < EQUIPMENT_SIZE; i++)
    {
        equipment[i] = 0;
    }
    equipment[0] = equipmentType;
}

QString QBox::equipmentTooltip(equipment_t &equipment) const
{
    uint8_t equipmentType = equipment[0];
    uint16_t identifier = equipment[2] + equipment[3] * 0x100;
    QString typeName = equipmentTypeName(equipmentType);
    QString name = equipmentDisplayName(equipment);

    return QString("%1\n%2\n装饰品: %3\nType: %4  ID: %5\nRaw: %6 %7 %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20 %21")
        .arg(name)
        .arg(typeName)
        .arg(jewelSummary(equipment))
        .arg(equipmentType)
        .arg(identifier)
        .arg(equipment[0])
        .arg(equipment[1])
        .arg(equipment[2])
        .arg(equipment[3])
        .arg(equipment[4])
        .arg(equipment[5])
        .arg(equipment[6])
        .arg(equipment[7])
        .arg(equipment[8])
        .arg(equipment[9])
        .arg(equipment[10])
        .arg(equipment[11])
        .arg(equipment[12])
        .arg(equipment[13])
        .arg(equipment[14])
        .arg(equipment[15]);
}

QString QBox::equipmentTypeName(uint8_t equipmentType) const
{
    if (equipmentType == MH3U_Type::NoneType)
    {
        return uiText("(None)");
    }

    QString name = datasetIdentifierName(MH3U_DS::equipmentTypes(), equipmentType);
    if (!name.isEmpty())
    {
        return displayNameWithoutSearchSuffix(name);
    }

    return QString("Type %1").arg(equipmentType);
}

QString QBox::equipmentIdentifierName(uint8_t equipmentType, uint16_t identifier) const
{
    return datasetIdentifierName(equipmentDataset(equipmentType), identifier);
}

QString QBox::equipmentDisplayName(equipment_t &equipment) const
{
    uint8_t equipmentType = equipment[0];
    uint16_t identifier = equipment[2] + equipment[3] * 0x100;

    if (identifier == 0)
    {
        return "空";
    }

    QString name = equipmentIdentifierName(equipmentType, identifier);
    if (name.isEmpty())
    {
        return QString("#%1").arg(identifier);
    }

    return displayNameWithoutSearchSuffix(name);
}

QString QBox::equipmentSlotTitle(uint32_t panel, uint32_t slot, equipment_t &equipment) const
{
    uint32_t displaySlot = panel * 100 + slot + 1;
    return QString("当前编辑格子：%1（%2）").arg(displaySlot).arg(equipmentDisplayName(equipment));
}

QString QBox::jewelSummary(equipment_t &equipment) const
{
    QStringList names;
    uint16_t jewels[] =
    {
        (uint16_t)(equipment[8] + equipment[9] * 0x100),
        (uint16_t)(equipment[10] + equipment[11] * 0x100),
        (uint16_t)(equipment[12] + equipment[13] * 0x100),
    };

    for (uint32_t i = 0; i < 3; i++)
    {
        if (jewels[i] == 0)
        {
            continue;
        }

        QString name = datasetIdentifierName(MH3U_DS::jewels(), jewels[i]);
        if (name.isEmpty())
        {
            names << QString("#%1").arg(jewels[i]);
        }
        else
        {
            names << displayNameWithoutSearchSuffix(name);
        }
    }

    return names.isEmpty() ? "-" : names.join(", ");
}

bool QBox::equipmentMatchesFilters(equipment_t &equipment, uint32_t panel, uint32_t slot) const
{
    uint8_t equipmentType = equipment[0];
    uint16_t identifier = equipment[2] + equipment[3] * 0x100;

    if (m_nonEmptyOnly->isChecked() && equipmentType == MH3U_Type::NoneType && identifier == 0)
    {
        return false;
    }

    int filterType = m_typeFilter->currentData().toInt();
    if (filterType >= 0 && equipmentType != filterType)
    {
        return false;
    }

    QString query = m_search->text().trimmed();
    if (query.isEmpty())
    {
        return true;
    }

    QString searchable = QString("%1 %2 %3 %4 %5 %6")
        .arg(panel + 1)
        .arg(slot + 1)
        .arg(equipmentTypeName(equipmentType))
        .arg(equipmentDisplayName(equipment))
        .arg(identifier)
        .arg(jewelSummary(equipment));

    return searchable.contains(query, Qt::CaseInsensitive);
}

const dataset_t* QBox::equipmentDataset(uint8_t equipmentType) const
{
    switch ((equipment_type_e) equipmentType)
    {
        case MH3U_Type::ChestType:
            return MH3U_DS::chestArmors();
        case MH3U_Type::ArmsType:
            return MH3U_DS::armsArmors();
        case MH3U_Type::WaistType:
            return MH3U_DS::waistArmors();
        case MH3U_Type::LegsType:
            return MH3U_DS::legsArmors();
        case MH3U_Type::HeadType:
            return MH3U_DS::headArmors();
        case MH3U_Type::CharmType:
            return MH3U_DS::charms();
        case MH3U_Type::GSType:
            return MH3U_DS::gsWeapons();
        case MH3U_Type::SNSType:
            return MH3U_DS::snsWeapons();
        case MH3U_Type::HType:
            return MH3U_DS::hWeapons();
        case MH3U_Type::LType:
            return MH3U_DS::lWeapons();
        case MH3U_Type::HBGType:
            return MH3U_DS::hbgWeapons();
        case MH3U_Type::LBGType:
            return MH3U_DS::lbgWeapons();
        case MH3U_Type::LSType:
            return MH3U_DS::lsWeapons();
        case MH3U_Type::SAType:
            return MH3U_DS::saWeapons();
        case MH3U_Type::GLType:
            return MH3U_DS::glWeapons();
        case MH3U_Type::BowType:
            return MH3U_DS::bowWeapons();
        case MH3U_Type::DBType:
            return MH3U_DS::dbWeapons();
        case MH3U_Type::HHType:
            return MH3U_DS::hhWeapons();
        default:
            return NULL;
    }
}

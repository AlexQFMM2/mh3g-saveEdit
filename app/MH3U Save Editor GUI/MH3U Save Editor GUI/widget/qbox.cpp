#include "qbox.hpp"

#include "mh3u_transfer.hpp"
#include "equipment_validator.hpp"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QModelIndex>
#include <QScrollBar>
#include <QTableView>
#include <QVBoxLayout>
#include <QBrush>
#include <QColor>

class EquipmentBoxTableModel : public QAbstractTableModel
{
public:
    explicit EquipmentBoxTableModel(QBox *box) : QAbstractTableModel(box), m_box(box) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 7;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
        static const char *headers[] = {"页", "格", "类型", "名称", "ID", "装饰品", "合法性"};
        return section >= 0 && section < 7 ? QString::fromUtf8(headers[section]) : QVariant();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return QVariant();
        const slot_ref_t &ref = m_rows.at(index.row());
        row_cache_t &cache = m_cache[index.row()];
        equipment_t &equipment = m_box->equipmentAt(ref.panel, ref.slot);
        const int identifier = equipment[2] | (equipment[3] << 8);

        if (role == Qt::DisplayRole)
        {
            if (index.column() == 0) return ref.panel + 1;
            if (index.column() == 1) return ref.slot + 1;
            if (index.column() == 4) return identifier;
            ensureBasic(cache, equipment);
            if (index.column() == 2) return cache.typeName;
            if (index.column() == 3) return cache.name;
            if (index.column() == 5) return cache.jewels;
            if (index.column() == 6) { ensureValidation(cache, equipment); return cache.validation.statusText(); }
        }
        if (index.column() == 6)
        {
            ensureValidation(cache, equipment);
            if (role == Qt::ToolTipRole) return cache.validation.details();
            if (cache.validation.status == EquipmentInvalid)
            {
                if (role == Qt::ForegroundRole) return QBrush(QColor("#b42318"));
                if (role == Qt::BackgroundRole) return QBrush(QColor("#fee4e2"));
            }
            else if (cache.validation.status == EquipmentUnknown)
            {
                if (role == Qt::ForegroundRole) return QBrush(QColor("#8a4b08"));
                if (role == Qt::BackgroundRole) return QBrush(QColor("#fff3cd"));
            }
        }
        return QVariant();
    }

    void rebuild()
    {
        beginResetModel();
        m_rows.clear();
        for (uint32_t panel = 0; panel < 10; ++panel)
            for (uint32_t slot = 0; slot < 100; ++slot)
            {
                equipment_t &equipment = m_box->equipmentAt(panel, slot);
                if (m_box->equipmentMatchesFilters(equipment, panel, slot))
                {
                    slot_ref_t ref = {(uint16_t)panel, (uint16_t)slot};
                    m_rows.append(ref);
                }
            }
        m_cache.clear();
        m_cache.resize(m_rows.size());
        endResetModel();
    }

    bool slotAt(int row, uint32_t *panel, uint32_t *slot) const
    {
        if (row < 0 || row >= m_rows.size()) return false;
        if (panel) *panel = m_rows.at(row).panel;
        if (slot) *slot = m_rows.at(row).slot;
        return true;
    }

    int findSlot(uint32_t panel, uint32_t slot) const
    {
        for (int row = 0; row < m_rows.size(); ++row)
            if (m_rows.at(row).panel == panel && m_rows.at(row).slot == slot) return row;
        return -1;
    }

private:
    struct slot_ref_t { uint16_t panel; uint16_t slot; };
    struct row_cache_t
    {
        row_cache_t() : basicReady(false), validationReady(false) {}
        bool basicReady;
        bool validationReady;
        QString typeName;
        QString name;
        QString jewels;
        equipment_validation_t validation;
    };

    QBox *m_box;
    QVector<slot_ref_t> m_rows;
    mutable QVector<row_cache_t> m_cache;

    void ensureBasic(row_cache_t &cache, equipment_t &equipment) const
    {
        if (cache.basicReady) return;
        cache.typeName = m_box->equipmentTypeName(equipment[0]);
        cache.name = m_box->equipmentDisplayName(equipment);
        cache.jewels = m_box->jewelSummary(equipment);
        cache.basicReady = true;
    }

    void ensureValidation(row_cache_t &cache, equipment_t &equipment) const
    {
        if (cache.validationReady) return;
        cache.validation = EquipmentValidator::validate(equipment, m_box->mh3u->format(), m_box->mh3u->savedata->sex);
        cache.validationReady = true;
    }
};

QBox::QBox(MH3U_SE *mh3u, QWidget *parent) : QWidget(parent)
{
    setObjectName("pageSurface");
    this->mh3u = mh3u;

    m_table = new QTableView(this);
    m_tableModel = new EquipmentBoxTableModel(this);
    m_table->setModel(m_tableModel);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 46);
    m_table->setColumnWidth(1, 46);
    m_table->setColumnWidth(2, 96);
    m_table->setColumnWidth(4, 64);
    m_table->setColumnWidth(6, 72);

    connect(m_table, &QTableView::doubleClicked, [this](const QModelIndex &index) {
        tableCellDoubleClicked(index.row(), index.column());
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, [this]() { updateSelectedInfo(); });

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("搜索装备 / 类型 / ID / 装饰品");
    connect(m_search, SIGNAL(textChanged(QString)), this, SLOT(refreshFilters()));

    m_nonEmptyOnly = new QCheckBox("只显示非空", this);
    connect(m_nonEmptyOnly, SIGNAL(toggled(bool)), this, SLOT(refreshFilters()));
    m_validOnly = new QCheckBox("只显示合法", this);
    m_validOnly->setChecked(false);
    connect(m_validOnly, SIGNAL(toggled(bool)), this, SLOT(refreshFilters()));

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

    m_exportButton = new QPushButton("导出装备箱表单", this);
    connect(m_exportButton, SIGNAL(clicked(bool)), this, SLOT(exportEquipmentForm()));

    m_importButton = new QPushButton("导入装备箱表单", this);
    connect(m_importButton, SIGNAL(clicked(bool)), this, SLOT(importEquipmentForm()));

    QVBoxLayout *sideLayout = new QVBoxLayout();
    sideLayout->addWidget(new QLabel("筛选", this));
    sideLayout->addWidget(m_search);
    sideLayout->addWidget(m_typeFilter);
    sideLayout->addWidget(m_nonEmptyOnly);
    sideLayout->addWidget(m_validOnly);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(new QLabel("选中", this));
    sideLayout->addWidget(m_selectedInfo);
    sideLayout->addWidget(m_addButton);
    sideLayout->addWidget(m_editButton);
    sideLayout->addSpacing(12);
    sideLayout->addWidget(new QLabel("跨平台批量迁移", this));
    sideLayout->addWidget(m_exportButton);
    sideLayout->addWidget(m_importButton);
    sideLayout->addStretch(1);

    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(m_table, 1);
    mainLayout->addLayout(sideLayout);
    this->setLayout(mainLayout);
    updateSelectedInfo();
}

QBox::~QBox()
{
    this->mh3u = NULL;
}

void QBox::loadFromModel()
{
    populateTable();
    updateSelectedInfo();
}

bool QBox::commitToModel(QString *)
{
    return mh3u != NULL && mh3u->loaded();
}

void QBox::buttonClicked(int id)
{
    editSlot(id / 100, id % 100);
}

void QBox::tableCellDoubleClicked(int row, int)
{
    uint32_t panel = 0;
    uint32_t slot = 0;
    if (!m_tableModel->slotAt(row, &panel, &slot))
    {
        return;
    }

    editSlot(panel, slot);
}

void QBox::editSelectedEquipment()
{
    const int row = m_table->currentIndex().row();
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
    const int row = m_table->currentIndex().row();
    if (row < 0)
    {
        m_selectedInfo->setText("(无)");
        return;
    }

    uint32_t panel = 0;
    uint32_t slot = 0;
    if (!m_tableModel->slotAt(row, &panel, &slot))
    {
        m_selectedInfo->setText("(无)");
        return;
    }

    equipment_t &equipment = equipmentAt(panel, slot);
    m_selectedInfo->setText(equipmentTooltip(equipment));
}

void QBox::refreshFilters()
{
    populateTable();
    updateSelectedInfo();
}

void QBox::exportEquipmentForm()
{
    QString filename = QFileDialog::getSaveFileName(this, "导出装备箱表单", "mh3u-equipment-box.csv", "CSV 表单 (*.csv);;所有文件 (*)");
    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, windowTitle(), QString("无法写入表单：\n%1").arg(file.errorString()));
        return;
    }

    std::string form = MH3U_Transfer::exportEquipmentBox(*mh3u->savedata);
    qint64 written = file.write(form.data(), (qint64) form.size());
    if (written != (qint64) form.size() || !file.flush())
    {
        QMessageBox::critical(this, windowTitle(), QString("表单没有完整写入：\n%1").arg(file.errorString()));
        return;
    }

    QMessageBox::information(this, windowTitle(), "已导出全部 1000 个装备格。此表单可导入 3DS 或 Wii U 存档。");
}

void QBox::importEquipmentForm()
{
    QString filename = QFileDialog::getOpenFileName(this, "导入装备箱表单", QString(), "CSV 表单 (*.csv);;所有文件 (*)");
    if (filename.isEmpty())
    {
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, windowTitle(), QString("无法读取表单：\n%1").arg(file.errorString()));
        return;
    }
    QByteArray contents = file.readAll();
    if (file.error() != QFile::NoError)
    {
        QMessageBox::critical(this, windowTitle(), QString("表单没有完整读出：\n%1").arg(file.errorString()));
        return;
    }

    std::vector<MH3U_Transfer::equipment_entry_t> entries;
    std::string error;
    if (!MH3U_Transfer::parseEquipmentBox(std::string(contents.constData(), (size_t) contents.size()), entries, error))
    {
        QMessageBox::critical(this, windowTitle(), QString("表单格式错误，未修改存档：\n%1").arg(QString::fromStdString(error)));
        return;
    }

    int invalidCount = 0;
    int unknownCount = 0;
    QStringList examples;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        equipment_t raw;
        for (int byte = 0; byte < EQUIPMENT_SIZE; ++byte) raw[byte] = entries[i].bytes[(size_t)byte];
        equipment_validation_t validation = EquipmentValidator::validate(raw, mh3u->format(), mh3u->savedata->sex);
        if (validation.status == EquipmentInvalid) ++invalidCount;
        else if (validation.status == EquipmentUnknown) ++unknownCount;
        if (validation.status != EquipmentValid && examples.size() < 5)
            examples << QString("第 %1 页第 %2 格：%3").arg(entries[i].panel + 1).arg(entries[i].slot + 1).arg(validation.details().section('\n', 0, 0));
    }
    QString prompt = QString("表单包含 %1 个装备格。\n非法 %2 条，未确认 %3 条。\n\n导入会覆盖表单中列出的格子，未列出的格子保持不变。合法性只作提示，不会阻止导入。")
        .arg(entries.size()).arg(invalidCount).arg(unknownCount);
    if (!examples.isEmpty()) prompt += "\n\n部分原因：\n" + examples.join("\n");
    prompt += "\n\n是否继续？";
    if (QMessageBox::question(this, "确认导入装备箱", prompt, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    MH3U_Transfer::applyEquipmentBox(entries, *mh3u->savedata);
    populateTable();
    updateSelectedInfo();
    emit modified();
    QMessageBox::information(this, windowTitle(), "装备箱已批量导入。请回到主窗口保存存档后再退出。");
}

void QBox::populateTable()
{
    const int previousRow = m_table->currentIndex().row();
    int selectedPanel = -1;
    int selectedSlot = -1;
    if (previousRow >= 0)
    {
        uint32_t panel = 0;
        uint32_t slot = 0;
        if (m_tableModel->slotAt(previousRow, &panel, &slot))
        {
            selectedPanel = (int)panel;
            selectedSlot = (int)slot;
        }
    }
    const int scrollPosition = m_table->verticalScrollBar()->value();
    m_tableModel->rebuild();
    int restoredRow = selectedPanel >= 0 ? m_tableModel->findSlot(selectedPanel, selectedSlot) : -1;

    if (m_tableModel->rowCount() > 0)
    {
        if (restoredRow < 0)
        {
            restoredRow = previousRow >= 0 ? qMin(previousRow, m_tableModel->rowCount() - 1) : 0;
        }
        m_table->selectRow(restoredRow);
        m_table->verticalScrollBar()->setValue(scrollPosition);
    }
}

bool QBox::editSlot(uint32_t panel, uint32_t slot)
{
    equipment_type_e newType(MH3U_Type::NoneType), oldType(MH3U_Type::NoneType);
    equipment_subtype_e subtype;
    bool saved = false;

    auto finishWithoutSaving = [&]() -> bool
    {
        populateTable();
        updateSelectedInfo();
        if (saved) emit modified();
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

                QArmor qarmor(&armor, this, mh3u->format(), mh3u->savedata->sex);
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

                QCharm qcharm(&charm, this, mh3u->format(), mh3u->savedata->sex);
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

                QWeapon qweapon(&weapon, this, mh3u->format(), mh3u->savedata->sex);
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
    if (saved) emit modified();
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

    equipment_validation_t validation = EquipmentValidator::validate(equipment, mh3u->format(), mh3u->savedata->sex);
    return QString("%1\n%2\n合法性: %3\n%4\n装饰品: %5\nType: %6  ID: %7\nRaw: %8 %9 %10 %11 %12 %13 %14 %15 %16 %17 %18 %19 %20 %21 %22 %23")
        .arg(name)
        .arg(typeName)
        .arg(validation.statusText())
        .arg(validation.details())
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

    if (m_validOnly->isChecked() && EquipmentValidator::validate(equipment, mh3u->format(), mh3u->savedata->sex).status != EquipmentValid)
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

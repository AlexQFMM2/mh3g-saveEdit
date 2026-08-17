#include "qchest.hpp"

#include "mh3u_transfer.hpp"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QModelIndex>
#include <QScrollBar>
#include <QTableView>
#include <QVBoxLayout>

class ItemChestTableModel : public QAbstractTableModel
{
public:
    explicit ItemChestTableModel(QChest *chest) : QAbstractTableModel(chest), m_chest(chest) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 5;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return QVariant();
        static const char *headers[] = {"页", "格", "道具", "数量", "ID"};
        return section >= 0 && section < 5 ? QString::fromUtf8(headers[section]) : QVariant();
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role != Qt::DisplayRole || !index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
            return QVariant();
        const slot_ref_t &ref = m_rows.at(index.row());
        item_t &item = m_chest->itemAt(ref.panel, ref.slot);
        if (index.column() == 0) return ref.panel + 1;
        if (index.column() == 1) return ref.slot + 1;
        if (index.column() == 2)
        {
            QString &name = m_names[index.row()];
            if (name.isNull()) name = localizedItemName(item.id);
            return name;
        }
        if (index.column() == 3) return item.count;
        if (index.column() == 4) return item.id;
        return QVariant();
    }

    void rebuild()
    {
        beginResetModel();
        m_rows.clear();
        for (uint32_t panel = 0; panel < 10; ++panel)
            for (uint32_t slot = 0; slot < 100; ++slot)
            {
                item_t &item = m_chest->itemAt(panel, slot);
                if (m_chest->itemMatchesFilters(item))
                {
                    slot_ref_t ref = {(uint16_t)panel, (uint16_t)slot};
                    m_rows.append(ref);
                }
            }
        m_names.clear();
        m_names.resize(m_rows.size());
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
    QChest *m_chest;
    QVector<slot_ref_t> m_rows;
    mutable QVector<QString> m_names;
};

QChest::QChest(MH3U_SE *mh3u, QWidget *parent) : QWidget(parent)
{
    setObjectName("pageSurface");
    this->mh3u = mh3u;

    m_table = new QTableView(this);
    m_tableModel = new ItemChestTableModel(this);
    m_table->setModel(m_tableModel);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 46);
    m_table->setColumnWidth(1, 46);
    m_table->setColumnWidth(3, 64);
    m_table->setColumnWidth(4, 72);

    connect(m_table, &QTableView::doubleClicked, [this](const QModelIndex &index) {
        tableCellDoubleClicked(index.row(), index.column());
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, [this]() { updateSelectedInfo(); });

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("搜索道具 / ID / 数量");
    connect(m_search, SIGNAL(textChanged(QString)), this, SLOT(refreshFilters()));

    m_nonEmptyOnly = new QCheckBox("只显示非空", this);
    connect(m_nonEmptyOnly, SIGNAL(toggled(bool)), this, SLOT(refreshFilters()));

    m_selectedInfo = new QLabel("(无)", this);
    m_selectedInfo->setWordWrap(true);
    m_selectedInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_selectedInfo->setMinimumWidth(230);

    m_editButton = new QPushButton("编辑选中", this);
    connect(m_editButton, SIGNAL(clicked(bool)), this, SLOT(editSelectedItem()));

    m_addButton = new QPushButton("新增到空位", this);
    connect(m_addButton, SIGNAL(clicked(bool)), this, SLOT(addItemToFirstEmptySlot()));

    m_exportButton = new QPushButton("导出道具箱表单", this);
    connect(m_exportButton, SIGNAL(clicked(bool)), this, SLOT(exportChestForm()));

    m_importButton = new QPushButton("导入道具箱表单", this);
    connect(m_importButton, SIGNAL(clicked(bool)), this, SLOT(importChestForm()));

    QVBoxLayout *sideLayout = new QVBoxLayout();
    sideLayout->addWidget(new QLabel("筛选", this));
    sideLayout->addWidget(m_search);
    sideLayout->addWidget(m_nonEmptyOnly);
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

QChest::~QChest()
{
    this->mh3u = NULL;
}

void QChest::loadFromModel()
{
    populateTable();
    updateSelectedInfo();
}

bool QChest::commitToModel(QString *)
{
    return mh3u != NULL && mh3u->loaded();
}

void QChest::buttonClicked(int id)
{
    editSlot(id / 100, id % 100);
}

void QChest::tableCellDoubleClicked(int row, int)
{
    uint32_t panel = 0;
    uint32_t slot = 0;
    if (!m_tableModel->slotAt(row, &panel, &slot))
    {
        return;
    }

    editSlot(panel, slot);
}

void QChest::editSelectedItem()
{
    const int row = m_table->currentIndex().row();
    if (row < 0)
    {
        return;
    }

    tableCellDoubleClicked(row, 0);
}

void QChest::addItemToFirstEmptySlot()
{
    for (uint32_t panel = 0; panel < 10; panel++)
    {
        for (uint32_t slot = 0; slot < 100; slot++)
        {
            item_t &item = itemAt(panel, slot);
            if (item.id == 0)
            {
                item.count = 1;
                editSlot(panel, slot);
                return;
            }
        }
    }

    QMessageBox::information(this, windowTitle(), "没有空道具格。");
}

void QChest::updateSelectedInfo()
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

    item_t &item = itemAt(panel, slot);
    m_selectedInfo->setText(itemTooltipText(item));
}

void QChest::refreshFilters()
{
    populateTable();
    updateSelectedInfo();
}

void QChest::exportChestForm()
{
    QString filename = QFileDialog::getSaveFileName(this, "导出道具箱表单", "mh3u-item-chest.csv", "CSV 表单 (*.csv);;所有文件 (*)");
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

    std::string form = MH3U_Transfer::exportChest(*mh3u->savedata);
    qint64 written = file.write(form.data(), (qint64) form.size());
    if (written != (qint64) form.size() || !file.flush())
    {
        QMessageBox::critical(this, windowTitle(), QString("表单没有完整写入：\n%1").arg(file.errorString()));
        return;
    }

    QMessageBox::information(this, windowTitle(), "已导出全部 1000 个道具格。此表单可导入 3DS 或 Wii U 存档。");
}

void QChest::importChestForm()
{
    QString filename = QFileDialog::getOpenFileName(this, "导入道具箱表单", QString(), "CSV 表单 (*.csv);;所有文件 (*)");
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

    std::vector<MH3U_Transfer::chest_entry_t> entries;
    std::string error;
    if (!MH3U_Transfer::parseChest(std::string(contents.constData(), (size_t) contents.size()), entries, error))
    {
        QMessageBox::critical(this, windowTitle(), QString("表单格式错误，未修改存档：\n%1").arg(QString::fromStdString(error)));
        return;
    }

    QString prompt = QString("表单包含 %1 个道具格。\n\n导入会覆盖表单中列出的格子，未列出的格子保持不变。是否继续？")
        .arg(entries.size());
    if (QMessageBox::question(this, "确认导入道具箱", prompt, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    MH3U_Transfer::applyChest(entries, *mh3u->savedata);
    populateTable();
    updateSelectedInfo();
    emit modified();
    QMessageBox::information(this, windowTitle(), "道具箱已批量导入。请回到主窗口保存存档后再退出。");
}

void QChest::populateTable()
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

void QChest::editSlot(uint32_t panel, uint32_t slot)
{
    item_t editedItem = itemAt(panel, slot);
    QItem qitem(&editedItem, this);
    qitem.setModal(true);

    item_t &item = itemAt(panel, slot);
    if (qitem.exec() == QDialog::Accepted)
    {
        item = editedItem;
        if (item.id == 0)
        {
            item.count = 0;
        }
        emit modified();
    }
    else if (item.id == 0)
    {
        item.count = 0;
    }

    populateTable();
    updateSelectedInfo();
}

item_t& QChest::itemAt(uint32_t panel, uint32_t slot) const
{
    return this->mh3u->savedata->chest[panel][slot];
}

bool QChest::itemMatchesFilters(const item_t &item) const
{
    if (m_nonEmptyOnly->isChecked() && item.id == 0)
    {
        return false;
    }

    QString query = m_search->text().trimmed();
    if (query.isEmpty())
    {
        return true;
    }

    QString name = datasetIdentifierName(MH3U_DS::items(), item.id);
    QString englishName = englishItemName(item.id);
    return name.contains(query, Qt::CaseInsensitive)
        || englishName.contains(query, Qt::CaseInsensitive)
        || QString::number(item.id).contains(query)
        || QString::number(item.count).contains(query);
}

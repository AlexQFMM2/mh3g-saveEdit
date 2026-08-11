#include "qchest.hpp"

#include "mh3u_transfer.hpp"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollBar>
#include <QTableWidgetItem>
#include <QVBoxLayout>

QChest::QChest(MH3U_SE *mh3u, QWidget *parent) : QWidget(parent)
{
    this->mh3u = mh3u;

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(QStringList() << "页" << "格" << "道具" << "数量" << "ID");
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);

    connect(m_table, SIGNAL(cellDoubleClicked(int,int)), this, SLOT(tableCellDoubleClicked(int,int)));
    connect(m_table, SIGNAL(itemSelectionChanged()), this, SLOT(updateSelectedInfo()));

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
    this->setWindowTitle(uiText("Chest editor"));
    this->resize(980, 680);

    populateTable();
    updateSelectedInfo();
}

QChest::~QChest()
{
    this->mh3u = NULL;
}

void QChest::buttonClicked(int id)
{
    editSlot(id / 100, id % 100);
}

void QChest::tableCellDoubleClicked(int row, int)
{
    QTableWidgetItem *pageItem = m_table->item(row, 0);
    if (pageItem == NULL)
    {
        return;
    }

    editSlot(pageItem->data(Qt::UserRole).toUInt(), pageItem->data(Qt::UserRole + 1).toUInt());
}

void QChest::editSelectedItem()
{
    int row = m_table->currentRow();
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

    item_t &item = itemAt(pageItem->data(Qt::UserRole).toUInt(), pageItem->data(Qt::UserRole + 1).toUInt());
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
    QMessageBox::information(this, windowTitle(), "道具箱已批量导入。请回到主窗口保存存档后再退出。");
}

void QChest::populateTable()
{
    const int previousRow = m_table->currentRow();
    int selectedPanel = -1;
    int selectedSlot = -1;
    if (previousRow >= 0)
    {
        QTableWidgetItem *selectedItem = m_table->item(previousRow, 0);
        if (selectedItem != NULL)
        {
            selectedPanel = selectedItem->data(Qt::UserRole).toInt();
            selectedSlot = selectedItem->data(Qt::UserRole + 1).toInt();
        }
    }
    const int scrollPosition = m_table->verticalScrollBar()->value();
    int restoredRow = -1;

    m_table->setRowCount(0);

    for (uint32_t panel = 0; panel < 10; panel++)
    {
        for (uint32_t slot = 0; slot < 100; slot++)
        {
            item_t &item = itemAt(panel, slot);
            if (!itemMatchesFilters(item))
            {
                continue;
            }

            QString name = localizedItemName(item.id);

            int row = m_table->rowCount();
            m_table->insertRow(row);

            QTableWidgetItem *pageItem = new QTableWidgetItem(QString::number(panel + 1));
            pageItem->setData(Qt::UserRole, panel);
            pageItem->setData(Qt::UserRole + 1, slot);
            m_table->setItem(row, 0, pageItem);
            if ((int) panel == selectedPanel && (int) slot == selectedSlot)
            {
                restoredRow = row;
            }
            m_table->setItem(row, 1, new QTableWidgetItem(QString::number(slot + 1)));
            m_table->setItem(row, 2, new QTableWidgetItem(name));
            m_table->setItem(row, 3, new QTableWidgetItem(QString::number(item.count)));
            m_table->setItem(row, 4, new QTableWidgetItem(QString::number(item.id)));
        }
    }

    if (m_table->rowCount() > 0)
    {
        if (restoredRow < 0)
        {
            restoredRow = previousRow >= 0 ? qMin(previousRow, m_table->rowCount() - 1) : 0;
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

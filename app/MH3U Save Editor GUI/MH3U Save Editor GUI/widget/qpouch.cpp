#include "qpouch.hpp"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QTableWidgetItem>
#include <QVBoxLayout>

QPouch::QPouch(MH3U_SE *mh3u, QWidget *parent) : QWidget(parent)
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

    QVBoxLayout *sideLayout = new QVBoxLayout();
    sideLayout->addWidget(new QLabel("筛选", this));
    sideLayout->addWidget(m_search);
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
    this->setWindowTitle(uiText("Pouch editor"));
    this->resize(820, 560);

    populateTable();
    updateSelectedInfo();
}

QPouch::~QPouch()
{
    this->mh3u = NULL;
}

void QPouch::buttonClicked(int id)
{
    editSlot(id / 8, id % 8);
}

void QPouch::tableCellDoubleClicked(int row, int)
{
    QTableWidgetItem *pageItem = m_table->item(row, 0);
    if (pageItem == NULL)
    {
        return;
    }

    editSlot(pageItem->data(Qt::UserRole).toUInt(), pageItem->data(Qt::UserRole + 1).toUInt());
}

void QPouch::editSelectedItem()
{
    int row = m_table->currentRow();
    if (row < 0)
    {
        return;
    }

    tableCellDoubleClicked(row, 0);
}

void QPouch::addItemToFirstEmptySlot()
{
    for (uint32_t panel = 0; panel < 4; panel++)
    {
        for (uint32_t slot = 0; slot < 8; slot++)
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

void QPouch::updateSelectedInfo()
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

void QPouch::refreshFilters()
{
    populateTable();
    updateSelectedInfo();
}

void QPouch::populateTable()
{
    m_table->setRowCount(0);

    for (uint32_t panel = 0; panel < 4; panel++)
    {
        for (uint32_t slot = 0; slot < 8; slot++)
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
            m_table->setItem(row, 1, new QTableWidgetItem(QString::number(slot + 1)));
            m_table->setItem(row, 2, new QTableWidgetItem(name));
            m_table->setItem(row, 3, new QTableWidgetItem(QString::number(item.count)));
            m_table->setItem(row, 4, new QTableWidgetItem(QString::number(item.id)));
        }
    }

    if (m_table->rowCount() > 0)
    {
        m_table->selectRow(0);
    }
}

void QPouch::editSlot(uint32_t panel, uint32_t slot)
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

item_t& QPouch::itemAt(uint32_t panel, uint32_t slot) const
{
    return this->mh3u->savedata->pouch[panel][slot];
}

bool QPouch::itemMatchesFilters(const item_t &item) const
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

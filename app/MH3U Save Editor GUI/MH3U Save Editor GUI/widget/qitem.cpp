#include "qitem.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

QItem::QItem(item_t *item, QWidget *parent) : QDialog(parent)
{
    this->item = item;

    m_id = new QComboBox(this);
    m_id->addItem(uiText("(None)"), 0);
    for (uint32_t i = 0; i < MH3U_DS::items()->size(); i++)
    {
        m_id->addItem(QString(MH3U_DS::items()->at(i).identifier.c_str()), MH3U_DS::items()->at(i).count);
    }
    configureSearchableComboBox(m_id);

    //m_id = new QSpinBox(this);
    //m_id->setMinimum(0x0000);
    //m_id->setMaximum(0xffff);

    m_count = new QSpinBox(this);
    m_count->setMinimum(0x0000);
    m_count->setMaximum(0xffff);

    QHBoxLayout *layout = new QHBoxLayout();
    layout->addWidget(m_id);
    layout->addWidget(m_count);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText("保存");
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    connect(buttons, SIGNAL(accepted()), this, SLOT(saveAndAccept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(layout);
    mainLayout->addWidget(buttons);
    this->setLayout(mainLayout);
    this->setWindowTitle(uiText("Single item editor"));

    this->load();
}

void QItem::load()
{
    m_id->setCurrentIndex(m_id->findData(item->id));
    m_count->setValue(item->count);
}

void QItem::save()
{
    item->id = (uint16_t) searchableComboBoxCurrentData(m_id).toInt();
    item->count = m_count->value();
}

void QItem::closeEvent(QCloseEvent *)
{
    item = NULL;
}

bool QItem::validate()
{
    uint16_t id = (uint16_t) searchableComboBoxCurrentData(m_id).toInt();
    uint16_t count = (uint16_t) m_count->value();

    if (id == 0 && count > 0)
    {
        QMessageBox::warning(this, windowTitle(), "道具为“无”时，数量必须为 0。");
        return false;
    }

    if (id != 0 && count == 0)
    {
        QMessageBox::warning(this, windowTitle(), "请选择数量，不能为 0。");
        return false;
    }

    return true;
}

void QItem::saveAndAccept()
{
    if (!validate())
    {
        return;
    }

    save();
    accept();
}

#include "mh3u_sv.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include <QFileDialog>
#include <QMessageBox>

//#define DEBUG

MH3U_SV::MH3U_SV(QWidget *parent) : QWidget(parent)
{
    this->setObjectName("mainSurface");

    if (!MH3U_DS::readData(LANG_CN))
    {
        MH3U_DS::deleteData();
        return;
    }

    this->mh3u = new MH3U_SE();

#ifdef DEBUG
    this->mh3u->load("H:/Users/Gocario/Documents/Monster Hunter/Monster Hunter 3 Ultimate/save/analyse/user3_eq_2");
#endif

    characterButton = new QPushButton(this);
    connect(characterButton, SIGNAL(clicked(bool)), this, SLOT(openQCharacter()));
    inventoryButton = new QPushButton(this);
    connect(inventoryButton, SIGNAL(clicked(bool)), this, SLOT(openQInventory()));
    pouchButton = new QPushButton(this);
    connect(pouchButton, SIGNAL(clicked(bool)), this, SLOT(openQPouch()));
    chestButton = new QPushButton(this);
    connect(chestButton, SIGNAL(clicked(bool)), this, SLOT(openQChest()));
    boxButton = new QPushButton(this);
    connect(boxButton, SIGNAL(clicked(bool)), this, SLOT(openQBox()));

    optButton = new QPushButton(this);
    connect(optButton, SIGNAL(clicked(bool)), this, SLOT(openQOptions()));
    loadButton = new QPushButton(this);
    connect(loadButton, SIGNAL(clicked(bool)), this, SLOT(loadFile()));
    saveButton = new QPushButton(this);
    connect(saveButton, SIGNAL(clicked(bool)), this, SLOT(saveFile()));

    characterButton->setObjectName("navigationButton");
    inventoryButton->setObjectName("navigationButton");
    pouchButton->setObjectName("navigationButton");
    chestButton->setObjectName("navigationButton");
    boxButton->setObjectName("navigationButton");
    loadButton->setObjectName("primaryButton");
    saveButton->setObjectName("saveButton");

    characterButton->setCursor(Qt::PointingHandCursor);
    inventoryButton->setCursor(Qt::PointingHandCursor);
    pouchButton->setCursor(Qt::PointingHandCursor);
    chestButton->setCursor(Qt::PointingHandCursor);
    boxButton->setCursor(Qt::PointingHandCursor);
    optButton->setCursor(Qt::PointingHandCursor);
    loadButton->setCursor(Qt::PointingHandCursor);
    saveButton->setCursor(Qt::PointingHandCursor);

    QLabel *titleLabel = new QLabel("MH3G / MH3U 存档修改器", this);
    titleLabel->setObjectName("appTitle");
    QLabel *subtitleLabel = new QLabel("Nintendo 3DS / Wii U · 角色存档编辑", this);
    subtitleLabel->setObjectName("appSubtitle");
    statusLabel = new QLabel(this);
    statusLabel->setObjectName("statusLabel");

    QFrame *contentCard = new QFrame(this);
    contentCard->setObjectName("contentCard");
    QVBoxLayout *contentLayout = new QVBoxLayout(contentCard);
    contentLayout->setContentsMargins(18, 16, 18, 18);
    contentLayout->setSpacing(10);
    QLabel *contentTitle = new QLabel("存档内容", contentCard);
    contentTitle->setObjectName("sectionTitle");
    contentLayout->addWidget(contentTitle);

    QGridLayout *contentGrid = new QGridLayout();
    contentGrid->setHorizontalSpacing(10);
    contentGrid->setVerticalSpacing(10);
    contentGrid->addWidget(characterButton, 0, 0, 1, 2);
    contentGrid->addWidget(inventoryButton, 1, 0);
    contentGrid->addWidget(pouchButton, 1, 1);
    contentGrid->addWidget(chestButton, 2, 0);
    contentGrid->addWidget(boxButton, 2, 1);
    contentLayout->addLayout(contentGrid);

    QFrame *actionCard = new QFrame(this);
    actionCard->setObjectName("contentCard");
    QVBoxLayout *actionLayout = new QVBoxLayout(actionCard);
    actionLayout->setContentsMargins(18, 16, 18, 18);
    actionLayout->setSpacing(10);
    QLabel *actionTitle = new QLabel("存档文件", actionCard);
    actionTitle->setObjectName("sectionTitle");
    actionLayout->addWidget(actionTitle);

    QHBoxLayout *saveloadLayout = new QHBoxLayout();
    saveloadLayout->setSpacing(10);
    saveloadLayout->addWidget(loadButton);
    saveloadLayout->addWidget(saveButton);
    actionLayout->addLayout(saveloadLayout);
    actionLayout->addWidget(optButton);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(28, 24, 28, 26);
    mainLayout->setSpacing(15);
    mainLayout->addWidget(titleLabel);
    mainLayout->addWidget(subtitleLabel);
    mainLayout->addWidget(statusLabel);
    mainLayout->addWidget(contentCard);
    mainLayout->addWidget(actionCard);
    this->setLayout(mainLayout);
    this->setMinimumSize(470, 550);
    this->resize(500, 570);
    this->updateText();

    this->refresh();
}

MH3U_SV::~MH3U_SV()
{
    MH3U_DS::deleteData();
    cdelete(mh3u);
}


void MH3U_SV::refresh()
{
    const bool loaded = this->mh3u->loaded();
    if (loaded)
    {
        characterButton->setDisabled(false);
        inventoryButton->setDisabled(false);
        pouchButton->setDisabled(false);
        chestButton->setDisabled(false);
        boxButton->setDisabled(false);
        optButton->setDisabled(false);
        loadButton->setDisabled(false);
        saveButton->setDisabled(false);
    }
    else
    {
        characterButton->setDisabled(true);
        inventoryButton->setDisabled(true);
        pouchButton->setDisabled(true);
        chestButton->setDisabled(true);
        boxButton->setDisabled(true);
        optButton->setDisabled(false);
        loadButton->setDisabled(false);
        saveButton->setDisabled(true);
    }

    statusLabel->setText(loaded
        ? QString("已读取存档 · %1").arg(QString::fromStdString(this->mh3u->formatName()))
        : QString("尚未读取存档，请先打开 user1 / user2 / user3"));
    statusLabel->setProperty("loaded", loaded);
    statusLabel->style()->unpolish(statusLabel);
    statusLabel->style()->polish(statusLabel);
}

void MH3U_SV::updateText()
{
    characterButton->setText(uiText("Character"));
    inventoryButton->setText(uiText("Inventory"));
    pouchButton->setText(uiText("Pouch"));
    chestButton->setText(uiText("Chest"));
    boxButton->setText(uiText("Box"));
    optButton->setText(uiText("Options"));
    loadButton->setText(uiText("Load file"));
    saveButton->setText(uiText("Save file"));
    QString title = uiText("MH3U - Save viewer/editor");
    if (this->mh3u->loaded())
    {
        title += QString(" [%1]").arg(QString::fromStdString(this->mh3u->formatName()));
    }
    this->setWindowTitle(title);
}


void MH3U_SV::openQCharacter()
{
    QCharacter *qView = new QCharacter(this->mh3u);
    qView->setModal(true);
    qView->show();
}

void MH3U_SV::openQInventory()
{
    QInventory *qView = new QInventory(this->mh3u);
    qView->show();
}

void MH3U_SV::openQPouch()
{
    QPouch *qView = new QPouch(this->mh3u);
    qView->show();
}

void MH3U_SV::openQChest()
{
    QChest *qView = new QChest(this->mh3u);
    qView->show();
}

void MH3U_SV::openQBox()
{
    QBox *qView = new QBox(this->mh3u);
    qView->show();
}


void MH3U_SV::openQOptions()
{
    QOption *qOption = new QOption();
    qOption->setModal(true);
    qOption->exec();
    qOption->deleteLater();
    this->updateText();
}

void MH3U_SV::loadFile()
{
    std::cout << "Loading file!" << std::endl;

    QString filename = QFileDialog::getOpenFileName(this, uiText("Open file"), QString(), uiText("User files (user1 user2 user3);;All files (*)"));

    if (!filename.isNull())
    {
        if (!mh3u->load(filename.toStdString()))
        {
            QMessageBox::critical(this, uiText("Load failed"), QString::fromStdString(mh3u->lastError()));
        }
    }

    this->refresh();
    this->updateText();
}

void MH3U_SV::saveFile()
{
    std::cout << "Writing file!" << std::endl;

    QString filename = QFileDialog::getSaveFileName(this, uiText("Save file as"), QString(), uiText("User files (user1 user2 user3);;All files (*)"));

    if (!filename.isNull())
    {
        if (!mh3u->save(filename.toStdString()))
        {
            QMessageBox::critical(this, uiText("Save failed"), QString::fromStdString(mh3u->lastError()));
        }
    }

    this->refresh();
}

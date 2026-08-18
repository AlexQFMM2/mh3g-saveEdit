#include "mh3u_sv.hpp"

#include <QAbstractButton>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

MH3U_SV::MH3U_SV(QWidget *parent)
    : QMainWindow(parent), mh3u(new MH3U_SE()), characterPage(0), chestPage(0), boxPage(0), loadoutPage(0), communityPage(0), accountPage(0), aboutPage(0), dirty(false), dataReady(false)
{
    setObjectName("mainSurface");
    setWindowTitle(QString::fromUtf8("MH3G / MH3U 存档修改器"));
    dataReady = MH3U_DS::readData(LANG_CN);
    if (!dataReady)
        QMessageBox::critical(this, QString::fromUtf8("数据加载失败"),
            QString::fromUtf8("静态游戏数据库不可用，编辑功能已禁用。\n\n%1")
                .arg(QString::fromStdString(MH3U_DS::lastError())));

    QWidget *surface = new QWidget(this);
    setCentralWidget(surface);
    QHBoxLayout *shell = new QHBoxLayout(surface);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    QFrame *sidebar = new QFrame(surface);
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(210);
    QVBoxLayout *navigation = new QVBoxLayout(sidebar);
    navigation->setContentsMargins(18, 24, 18, 20);
    navigation->setSpacing(8);
    QLabel *brand = new QLabel(QString::fromUtf8("MH3G / MH3U"), sidebar);
    brand->setObjectName("sidebarTitle");
    QLabel *brandCaption = new QLabel(QString::fromUtf8("存档修改器"), sidebar);
    brandCaption->setObjectName("sidebarCaption");
    navigation->addWidget(brand);
    navigation->addWidget(brandCaption);
    navigation->addSpacing(22);
    auto makeNavigation = [sidebar](const QString &text) {
        QPushButton *button = new QPushButton(text, sidebar);
        button->setObjectName("navigationButton");
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setCursor(Qt::PointingHandCursor);
        return button;
    };
    characterButton = makeNavigation(QString::fromUtf8("角色"));
    chestButton = makeNavigation(QString::fromUtf8("道具箱"));
    boxButton = makeNavigation(QString::fromUtf8("装备箱"));
    loadoutButton = makeNavigation(QString::fromUtf8("配装器"));
    communityButton = makeNavigation(QString::fromUtf8("配装广场"));
    accountButton = makeNavigation(QString::fromUtf8("个人信息"));
    aboutButton = makeNavigation(QString::fromUtf8("关于"));
    navigation->addWidget(characterButton);
    navigation->addWidget(chestButton);
    navigation->addWidget(boxButton);
    navigation->addWidget(loadoutButton);
    navigation->addWidget(communityButton);
    navigation->addWidget(accountButton);
    navigation->addStretch();
    navigation->addWidget(aboutButton);

    QWidget *workspace = new QWidget(surface);
    QVBoxLayout *workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(24, 18, 24, 18);
    workspaceLayout->setSpacing(12);
    QHBoxLayout *header = new QHBoxLayout;
    QVBoxLayout *heading = new QVBoxLayout;
    pageTitle = new QLabel(QString::fromUtf8("存档管理"), workspace);
    pageTitle->setObjectName("pageTitle");
    QLabel *subtitle = new QLabel(QString::fromUtf8("Nintendo 3DS / Wii U · 中文界面"), workspace);
    subtitle->setObjectName("appSubtitle");
    heading->addWidget(pageTitle);
    heading->addWidget(subtitle);
    header->addLayout(heading);
    header->addStretch();
    workspaceLayout->addLayout(header);

    pageStack = new QStackedWidget(workspace);
    emptyPage = new QWidget(pageStack);
    emptyPage->setObjectName("pageSurface");
    QVBoxLayout *emptyLayout = new QVBoxLayout(emptyPage);
    emptyLayout->setAlignment(Qt::AlignCenter);
    QLabel *emptyTitle = new QLabel(QString::fromUtf8("尚未读取存档"), emptyPage);
    emptyTitle->setObjectName("emptyTitle");
    emptyTitle->setAlignment(Qt::AlignCenter);
    QLabel *emptyHint = new QLabel(QString::fromUtf8("点击右下角“读取存档”，选择 user1、user2 或 user3。"), emptyPage);
    emptyHint->setObjectName("appSubtitle");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyHint);
    pageStack->addWidget(emptyPage);
    loadoutPage = new QLoadout(mh3u, pageStack);
    pageStack->addWidget(loadoutPage);
    communityPage = new QCommunity(loadoutPage, pageStack);
    pageStack->addWidget(communityPage);
    accountPage = communityPage->accountPage();
    pageStack->addWidget(accountPage);

    aboutPage = new QWidget(pageStack);
    aboutPage->setObjectName("pageSurface");
    QVBoxLayout *aboutLayout = new QVBoxLayout(aboutPage);
    aboutLayout->setContentsMargins(18, 18, 18, 18);
    aboutLayout->setSpacing(14);
    QLabel *riskWarning = new QLabel(QString::fromUtf8("⚠ 修改有风险，请自主备份存档后再修改。"), aboutPage);
    riskWarning->setObjectName("riskWarning");
    riskWarning->setWordWrap(true);
    aboutLayout->addWidget(riskWarning);
    QLabel *statusTitle = new QLabel(QString::fromUtf8("当前存档状态"), aboutPage);
    statusTitle->setObjectName("emptyTitle");
    aboutLayout->addWidget(statusTitle);
    statusLabel = new QLabel(aboutPage);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusLabel->setWordWrap(true);
    aboutLayout->addWidget(statusLabel);
    QLabel *aboutText = new QLabel(QString::fromUtf8("MH3G / MH3U 存档修改器\n配装广场中的合法性与风险均为提示，不会限制导入或保存。"), aboutPage);
    aboutText->setObjectName("appSubtitle");
    aboutText->setWordWrap(true);
    aboutLayout->addWidget(aboutText);
    aboutLayout->addStretch();
    pageStack->addWidget(aboutPage);

    QScrollArea *content = new QScrollArea(workspace);
    content->setObjectName("contentArea");
    content->setWidgetResizable(true);
    content->setFrameShape(QFrame::NoFrame);
    content->setWidget(pageStack);
    workspaceLayout->addWidget(content, 1);

    QFrame *footer = new QFrame(workspace);
    footer->setObjectName("footerBar");
    QHBoxLayout *actions = new QHBoxLayout(footer);
    actions->setContentsMargins(14, 10, 14, 10);
    actions->addStretch();
    loadButton = new QPushButton(QString::fromUtf8("读取存档"), footer);
    loadButton->setObjectName("primaryButton");
    saveButton = new QPushButton(QString::fromUtf8("保存修改"), footer);
    saveButton->setObjectName("saveButton");
    actions->addWidget(loadButton);
    actions->addWidget(saveButton);
    workspaceLayout->addWidget(footer);

    shell->addWidget(sidebar);
    shell->addWidget(workspace, 1);
    connect(characterButton, SIGNAL(clicked(bool)), this, SLOT(showCharacter()));
    connect(chestButton, SIGNAL(clicked(bool)), this, SLOT(showChest()));
    connect(boxButton, SIGNAL(clicked(bool)), this, SLOT(showBox()));
    connect(loadoutButton, SIGNAL(clicked(bool)), this, SLOT(showLoadout()));
    connect(communityButton, SIGNAL(clicked(bool)), this, SLOT(showCommunity()));
    connect(accountButton, SIGNAL(clicked(bool)), this, SLOT(showAccount()));
    connect(aboutButton, SIGNAL(clicked(bool)), this, SLOT(showAbout()));
    connect(communityPage, SIGNAL(equipmentBoxModified()), this, SLOT(loadoutApplied()));
    connect(loadoutPage, SIGNAL(publishRequested()), communityPage, SLOT(uploadCurrent()));
    connect(loadoutPage, SIGNAL(saveModified()), this, SLOT(loadoutApplied()));
    connect(loadButton, SIGNAL(clicked(bool)), this, SLOT(loadFile()));
    connect(saveButton, SIGNAL(clicked(bool)), this, SLOT(saveFile()));
    resize(1100, 700);
    setMinimumSize(900, 620);
    updateState();
}

MH3U_SV::~MH3U_SV()
{
    MH3U_DS::deleteData();
    cdelete(mh3u);
}

bool MH3U_SV::smokeTestLoadout(QString *error)
{
    resize(1100, 700);
    showLoadout();
    return saveButton->isVisible() && loadButton->isVisible() && loadoutPage->smokeTestLayout(error);
}

bool MH3U_SV::smokeTestAccount(QString *error)
{
    resize(1100, 700);
    showAccount();
    return accountPage->isVisible() && communityPage->smokeTestAccount(error);
}

void MH3U_SV::createPages()
{
    if (characterPage) return;
    characterPage = new QCharacter(mh3u, pageStack);
    chestPage = new QChest(mh3u, pageStack);
    boxPage = new QBox(mh3u, pageStack);
    pageStack->addWidget(characterPage);
    pageStack->addWidget(chestPage);
    pageStack->addWidget(boxPage);
    connect(characterPage, SIGNAL(modified()), this, SLOT(markModified()));
    connect(chestPage, SIGNAL(modified()), this, SLOT(markModified()));
    connect(boxPage, SIGNAL(modified()), this, SLOT(markModified()));
}

void MH3U_SV::loadPages()
{
    if (!mh3u->loaded()) return;
    createPages();
    characterPage->loadFromModel();
    chestPage->loadFromModel();
    boxPage->loadFromModel();
}

bool MH3U_SV::commitPages(QString *error)
{
    if (!characterPage) return true;
    if (!characterPage->commitToModel(error)) { showCharacter(); return false; }
    if (!chestPage->commitToModel(error)) { showChest(); return false; }
    if (!boxPage->commitToModel(error)) { showBox(); return false; }
    return true;
}

void MH3U_SV::setCurrentPage(QWidget *page, QPushButton *button, const QString &title)
{
    if (!page) return;
    pageStack->setCurrentWidget(page);
    button->setChecked(true);
    pageTitle->setText(title);
}

void MH3U_SV::showCharacter() { setCurrentPage(characterPage, characterButton, QString::fromUtf8("角色")); }
void MH3U_SV::showChest() { setCurrentPage(chestPage, chestButton, QString::fromUtf8("道具箱")); }
void MH3U_SV::showBox() { setCurrentPage(boxPage, boxButton, QString::fromUtf8("装备箱")); }
void MH3U_SV::showLoadout() { setCurrentPage(loadoutPage, loadoutButton, QString::fromUtf8("配装器")); }
void MH3U_SV::showCommunity() { setCurrentPage(communityPage, communityButton, QString::fromUtf8("配装广场")); }
void MH3U_SV::showAccount() { setCurrentPage(accountPage, accountButton, QString::fromUtf8("个人信息")); communityPage->refreshProfile(); }
void MH3U_SV::showAbout() { setCurrentPage(aboutPage, aboutButton, QString::fromUtf8("关于")); }

void MH3U_SV::loadoutApplied()
{
    if (boxPage) boxPage->loadFromModel();
    markModified();
}

void MH3U_SV::markModified()
{
    if (!mh3u->loaded()) return;
    dirty = true;
    updateState();
}

bool MH3U_SV::discardChanges()
{
    const std::string path = mh3u->currentFilename();
    if (path.empty() || !mh3u->load(path)) {
        QMessageBox::critical(this, QString::fromUtf8("重新读取失败"), QString::fromStdString(mh3u->lastError()));
        return false;
    }
    dirty = false;
    loadPages();
    loadoutPage->updateSaveContext();
    updateState();
    return true;
}

bool MH3U_SV::maybeLeaveDirty()
{
    if (!dirty) return true;
    QMessageBox box(QMessageBox::Warning, QString::fromUtf8("尚未保存"),
                    QString::fromUtf8("当前存档有尚未保存的修改。"), QMessageBox::NoButton, this);
    QAbstractButton *save = box.addButton(QString::fromUtf8("保存"), QMessageBox::AcceptRole);
    QAbstractButton *discard = box.addButton(QString::fromUtf8("放弃"), QMessageBox::DestructiveRole);
    box.addButton(QString::fromUtf8("取消"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) return saveFile();
    if (box.clickedButton() == discard) return discardChanges();
    return false;
}

void MH3U_SV::loadFile()
{
    if (!dataReady)
    {
        QMessageBox::critical(this, QString::fromUtf8("无法读取存档"),
            QString::fromUtf8("请先修复 data/mh3g.sqlite 或 QSQLITE 驱动。"));
        return;
    }
    if (!maybeLeaveDirty()) return;
    const QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("读取存档"), QString(),
        QString::fromUtf8("存档文件 (user1 user2 user3);;所有文件 (*)"));
    if (path.isEmpty()) return;
    if (!mh3u->load(path.toStdString())) {
        QMessageBox::critical(this, QString::fromUtf8("读取失败"), QString::fromStdString(mh3u->lastError()));
        updateState();
        return;
    }
    dirty = false;
    loadPages();
    loadoutPage->updateSaveContext();
    showCharacter();
    updateState();
}

bool MH3U_SV::saveFile()
{
    if (!mh3u->loaded()) return false;
    QString error;
    if (!commitPages(&error)) {
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), error.isEmpty() ? QString::fromUtf8("页面数据校验失败。") : error);
        return false;
    }
    if (!mh3u->save()) {
        QMessageBox::critical(this, QString::fromUtf8("保存失败"), QString::fromStdString(mh3u->lastError()));
        dirty = true;
        updateState();
        return false;
    }
    dirty = false;
    updateState();
    QMessageBox::information(this, QString::fromUtf8("保存成功"),
        QString::fromUtf8("已覆盖当前存档：\n%1").arg(QString::fromStdString(mh3u->currentFilename())));
    return true;
}

void MH3U_SV::updateState()
{
    const bool loaded = dataReady && mh3u->loaded();
    characterButton->setEnabled(loaded);
    chestButton->setEnabled(loaded);
    boxButton->setEnabled(loaded);
    loadoutButton->setEnabled(dataReady);
    communityButton->setEnabled(true);
    accountButton->setEnabled(true);
    aboutButton->setEnabled(true);
    saveButton->setEnabled(loaded);
    loadButton->setEnabled(dataReady);
    loadButton->setText(loaded ? QString::fromUtf8("读取其他存档") : QString::fromUtf8("读取存档"));
    if (loaded) {
        const QString name = QFileInfo(QString::fromStdString(mh3u->currentFilename())).fileName();
        statusLabel->setText(QString::fromUtf8("%1 · %2 · %3")
            .arg(name, QString::fromStdString(mh3u->formatName()), dirty ? QString::fromUtf8("未保存") : QString::fromUtf8("已保存")));
    } else {
        statusLabel->setText(QString::fromUtf8("尚未读取存档"));
        pageStack->setCurrentWidget(emptyPage);
        pageTitle->setText(QString::fromUtf8("存档管理"));
    }
    statusLabel->setProperty("loaded", loaded);
    statusLabel->setProperty("dirty", dirty);
    statusLabel->style()->unpolish(statusLabel);
    statusLabel->style()->polish(statusLabel);
}

void MH3U_SV::closeEvent(QCloseEvent *event)
{
    if (loadoutPage->maybeLeaveDirty() && maybeLeaveDirty()) event->accept();
    else event->ignore();
}

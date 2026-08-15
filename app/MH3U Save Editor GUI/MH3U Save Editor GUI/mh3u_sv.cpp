#include "mh3u_sv.hpp"
#include "encyclopedia_page.hpp"
#include "save_action_bridge.hpp"

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
    : QMainWindow(parent), mh3u(new MH3U_SE()), saveBridge(new SaveActionBridge(mh3u)),
      encyclopediaPage(0), characterPage(0), chestPage(0), boxPage(0), dirty(false)
{
    setObjectName("mainSurface");
    setWindowTitle(QString::fromUtf8("MH3G / MH3U 存档修改器"));
    if (!MH3U_DS::readData(LANG_CN))
        QMessageBox::critical(this, QString::fromUtf8("数据加载失败"), QString::fromUtf8("无法读取中文 data 数据。"));

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
    encyclopediaButton = makeNavigation(QString::fromUtf8("资料库"));
    navigation->addWidget(characterButton);
    navigation->addWidget(chestButton);
    navigation->addWidget(boxButton);
    navigation->addSpacing(12);
    navigation->addWidget(encyclopediaButton);
    navigation->addStretch();

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

    QLabel *riskWarning = new QLabel(QString::fromUtf8("⚠ 修改有风险，请自主备份存档后再修改。"), workspace);
    riskWarning->setObjectName("riskWarning");
    workspaceLayout->addWidget(riskWarning);

    statusLabel = new QLabel(workspace);
    statusLabel->setObjectName("statusLabel");
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    workspaceLayout->addWidget(statusLabel);

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

    encyclopediaPage = new EncyclopediaPage(saveBridge, pageStack);
    pageStack->addWidget(encyclopediaPage);
    connect(encyclopediaPage, SIGNAL(modified()), this, SLOT(markModified()));
    connect(encyclopediaPage, SIGNAL(itemAdded()), this, SLOT(refreshChestFromModel()));
    connect(encyclopediaPage, SIGNAL(weaponAdded()), this, SLOT(refreshBoxFromModel()));
    connect(encyclopediaPage, SIGNAL(armorAdded()), this, SLOT(refreshBoxFromModel()));

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
    connect(encyclopediaButton, SIGNAL(clicked(bool)), this, SLOT(showEncyclopedia()));
    connect(loadButton, SIGNAL(clicked(bool)), this, SLOT(loadFile()));
    connect(saveButton, SIGNAL(clicked(bool)), this, SLOT(saveFile()));
    resize(1100, 700);
    setMinimumSize(900, 620);
    updateState();
}

MH3U_SV::~MH3U_SV()
{
    MH3U_DS::deleteData();
    delete saveBridge;
    cdelete(mh3u);
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
    connect(characterPage, SIGNAL(appearanceChanged(int,int,int)),
        encyclopediaPage, SLOT(setCharacterAppearance(int,int,int)));
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
void MH3U_SV::showEncyclopedia() { setCurrentPage(encyclopediaPage, encyclopediaButton, QString::fromUtf8("资料库")); }

void MH3U_SV::markModified()
{
    if (!mh3u->loaded()) return;
    dirty = true;
    updateState();
}

void MH3U_SV::refreshChestFromModel()
{
    if (chestPage) chestPage->loadFromModel();
}

void MH3U_SV::refreshBoxFromModel()
{
    if (boxPage) boxPage->loadFromModel();
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
    const bool loaded = mh3u->loaded();
    characterButton->setEnabled(loaded);
    chestButton->setEnabled(loaded);
    boxButton->setEnabled(loaded);
    encyclopediaButton->setEnabled(true);
    saveButton->setEnabled(loaded);
    encyclopediaPage->updateSaveState();
    loadButton->setText(loaded ? QString::fromUtf8("读取其他存档") : QString::fromUtf8("读取存档"));
    if (loaded) {
        const QString name = QFileInfo(QString::fromStdString(mh3u->currentFilename())).fileName();
        statusLabel->setText(QString::fromUtf8("%1 · %2 · %3")
            .arg(name, QString::fromStdString(mh3u->formatName()), dirty ? QString::fromUtf8("未保存") : QString::fromUtf8("已保存")));
    } else {
        statusLabel->setText(QString::fromUtf8("尚未读取存档"));
        if (pageStack->currentWidget() != encyclopediaPage)
        {
            pageStack->setCurrentWidget(emptyPage);
            pageTitle->setText(QString::fromUtf8("存档管理"));
            encyclopediaButton->setChecked(false);
        }
    }
    statusLabel->setProperty("loaded", loaded);
    statusLabel->setProperty("dirty", dirty);
    statusLabel->style()->unpolish(statusLabel);
    statusLabel->style()->polish(statusLabel);
}

void MH3U_SV::closeEvent(QCloseEvent *event)
{
    if (maybeLeaveDirty()) event->accept();
    else event->ignore();
}

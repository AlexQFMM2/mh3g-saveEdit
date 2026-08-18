#include "qcommunity.hpp"

#include "qloadout.hpp"
#include "game_data_repository.hpp"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QTimer>
#include <QPointer>

#include <functional>

static QString passwordPolicyText()
{
    return QString::fromUtf8("8～16 位，须包含英文字母、数字和特殊符号；支持 ! @ # $ % ^ & * _ - + =");
}

static bool validAccountPassword(const QString &password)
{
    if (password.size() < 8 || password.size() > 16)
        return false;
    const QString specials = QStringLiteral("!@#$%^&*_-+=");
    bool hasLetter = false, hasDigit = false, hasSpecial = false;
    for (int index = 0; index < password.size(); ++index) {
        const ushort character = password.at(index).unicode();
        if ((character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z'))
            hasLetter = true;
        else if (character >= '0' && character <= '9')
            hasDigit = true;
        else if (specials.contains(password.at(index)))
            hasSpecial = true;
        else
            return false;
    }
    return hasLetter && hasDigit && hasSpecial;
}

class CurrentPageStackedWidget : public QStackedWidget
{
public:
    explicit CurrentPageStackedWidget(QWidget *parent = 0) : QStackedWidget(parent)
    {
        connect(this, &QStackedWidget::currentChanged, this, [this](int) { updateGeometry(); });
    }

    QSize sizeHint() const
    {
        return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};

class TagSelectWidget : public QWidget
{
public:
    explicit TagSelectWidget(const QString &placeholder, QWidget *parent = 0)
        : QWidget(parent), m_model(new QStandardItemModel(this)), m_completer(new QCompleter(m_model, this))
    {
        setObjectName("tagSelect");
        setStyleSheet("QWidget#tagSelect{background:#ffffff;border:1px solid #b8c4d2;border-radius:6px;}"
                      "QWidget#tagSelect QLineEdit{border:0;background:transparent;padding:2px;}"
                      "QWidget#tagSelect QPushButton[tagChip=\"true\"]{background:#edf0f4;border:0;border-radius:4px;padding:4px 7px;color:#344054;}"
                      "QWidget#tagSelect QPushButton[tagArrow=\"true\"]{border:0;background:transparent;color:#667085;font-size:15px;}");
        QHBoxLayout *layout = new QHBoxLayout(this);
        layout->setContentsMargins(5, 3, 3, 3);
        layout->setSpacing(4);
        m_tagHost = new QWidget(this);
        m_tagLayout = new QHBoxLayout(m_tagHost);
        m_tagLayout->setContentsMargins(0, 0, 0, 0);
        m_tagLayout->setSpacing(4);
        m_editor = new QLineEdit(this);
        m_editor->setPlaceholderText(placeholder);
        m_arrow = new QPushButton(QString::fromUtf8("⌄"), this);
        m_arrow->setProperty("tagArrow", true);
        m_arrow->setFixedWidth(26);
        layout->addWidget(m_tagHost);
        layout->addWidget(m_editor, 1);
        layout->addWidget(m_arrow);
        setMinimumHeight(38);

        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setMaxVisibleItems(16);
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
        m_completer->setFilterMode(Qt::MatchContains);
#endif
        m_editor->setCompleter(m_completer);
        m_editor->installEventFilter(this);
        connect(m_editor, &QLineEdit::textEdited, this, [this](const QString &text) {
            m_completer->setCompletionPrefix(text);
            if (text.isEmpty()) m_completer->popup()->hide(); else m_completer->complete();
        });
        connect(m_arrow, &QPushButton::clicked, this, [this]() {
            m_editor->setFocus(Qt::MouseFocusReason);
            m_completer->setCompletionPrefix(QString());
            m_completer->complete();
        });
        connect(m_completer, static_cast<void (QCompleter::*)(const QModelIndex &)>(&QCompleter::activated),
                this, [this](const QModelIndex &index) { commitIndex(index); });
    }

    void addCandidate(const QString &text, const QVariant &value)
    {
        QStandardItem *item = new QStandardItem(text);
        item->setData(value, Qt::UserRole);
        m_model->appendRow(item);
    }

    QList<QVariant> values() const
    {
        QList<QVariant> result;
        for (int index = 0; index < m_values.size(); ++index) result << m_values.at(index).first;
        return result;
    }

    void setChangedHandler(const std::function<void()> &handler) { m_changed = handler; }

protected:
    bool eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_editor && event->type() == QEvent::KeyPress)
        {
            QKeyEvent *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            {
                QModelIndex index = m_completer->popup()->currentIndex();
                if (!index.isValid() && m_completer->completionModel()->rowCount() > 0)
                    index = m_completer->completionModel()->index(0, 0);
                if (index.isValid()) commitIndex(index);
                return true;
            }
            if (key->key() == Qt::Key_Escape)
            {
                m_completer->popup()->hide();
                return true;
            }
        }
        if (watched == m_editor && event->type() == QEvent::FocusOut)
        {
            QTimer::singleShot(0, this, [this]() {
                if (!m_editor->hasFocus() && !m_completer->popup()->isVisible()) m_editor->clear();
            });
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    QStandardItemModel *m_model;
    QCompleter *m_completer;
    QWidget *m_tagHost;
    QHBoxLayout *m_tagLayout;
    QLineEdit *m_editor;
    QPushButton *m_arrow;
    QList<QPair<QVariant, QString> > m_values;
    QList<QPushButton *> m_chips;
    std::function<void()> m_changed;

    void commitIndex(const QModelIndex &index)
    {
        if (!index.isValid()) return;
        const QVariant value = index.data(Qt::UserRole);
        if (!value.isValid()) return;
        for (int selected = 0; selected < m_values.size(); ++selected)
            if (m_values.at(selected).first == value) { m_editor->clear(); m_completer->popup()->hide(); return; }
        if (m_values.size() >= 8) return;
        m_values.append(qMakePair(value, index.data(Qt::DisplayRole).toString()));
        m_editor->clear();
        m_completer->popup()->hide();
        rebuildChips();
        if (m_changed) m_changed();
    }

    void rebuildChips()
    {
        for (int index = 0; index < m_chips.size(); ++index)
        {
            m_tagLayout->removeWidget(m_chips.at(index));
            m_chips.at(index)->hide();
            m_chips.at(index)->deleteLater();
        }
        m_chips.clear();
        for (int index = 0; index < m_values.size(); ++index)
        {
            const QVariant value = m_values.at(index).first;
            QPushButton *chip = new QPushButton(m_values.at(index).second + QString::fromUtf8(" ×"), m_tagHost);
            chip->setProperty("tagChip", true);
            chip->setToolTip(m_values.at(index).second + QString::fromUtf8("（点击移除）"));
            chip->setMaximumWidth(180);
            m_tagLayout->addWidget(chip);
            m_chips << chip;
            connect(chip, &QPushButton::clicked, this, [this, value]() {
                for (int selected = 0; selected < m_values.size(); ++selected)
                    if (m_values.at(selected).first == value) { m_values.removeAt(selected); break; }
                rebuildChips();
                m_editor->setFocus(Qt::MouseFocusReason);
                if (m_changed) m_changed();
            });
        }
    }
};

namespace
{
QString apiMessage(const QJsonObject &object, const QString &fallback)
{
    return object.value("error").toObject().value("message").toString(fallback);
}

QTableWidgetItem *centeredItem(const QString &text)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    return item;
}

QString equipmentTypeLabel(int saveType)
{
    switch (saveType)
    {
        case 1: return QString::fromUtf8("胸"); case 2: return QString::fromUtf8("腕");
        case 3: return QString::fromUtf8("腰"); case 4: return QString::fromUtf8("腿");
        case 5: return QString::fromUtf8("头"); case 7: return QString::fromUtf8("大剑");
        case 8: return QString::fromUtf8("片手剑"); case 9: return QString::fromUtf8("大锤");
        case 10: return QString::fromUtf8("长枪"); case 11: return QString::fromUtf8("重弩");
        case 13: return QString::fromUtf8("轻弩"); case 14: return QString::fromUtf8("太刀");
        case 15: return QString::fromUtf8("斩击斧"); case 16: return QString::fromUtf8("铳枪");
        case 17: return QString::fromUtf8("弓"); case 18: return QString::fromUtf8("双剑");
        case 19: return QString::fromUtf8("狩猎笛");
    }
    return QString::fromUtf8("装备");
}
}

QCommunity::QCommunity(QLoadout *loadout, QWidget *parent)
    : QWidget(parent), m_loadout(loadout), m_network(new QNetworkAccessManager(this)),
      m_accountPage(new QWidget(parent))
{
    setObjectName("pageSurface");
    m_accountPage->setObjectName("pageSurface");
    m_baseUrl = QString::fromLocal8Bit(qgetenv("MHED_DESK_API_URL")).trimmed();
    if (m_baseUrl.isEmpty()) m_baseUrl = QStringLiteral("https://mhed.desk.65h26i.top");
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    QGroupBox *filtersBox = new QGroupBox(QString::fromUtf8("筛选公开配装"), this);
    QVBoxLayout *filters = new QVBoxLayout(filtersBox);
    QHBoxLayout *primaryFilters = new QHBoxLayout;
    m_search = new QLineEdit(filtersBox);
    m_search->setPlaceholderText(QString::fromUtf8("搜索配装名或备注"));
    m_legalOnly = new QCheckBox(QString::fromUtf8("只看合法配装"), filtersBox);
    QPushButton *refresh = new QPushButton(QString::fromUtf8("筛选 / 刷新"), filtersBox);
    primaryFilters->addWidget(m_search, 1);
    primaryFilters->addWidget(m_legalOnly);
    primaryFilters->addWidget(refresh);
    filters->addLayout(primaryFilters);

    QHBoxLayout *tagFilters = new QHBoxLayout;
    QVBoxLayout *equipmentColumn = new QVBoxLayout;
    QVBoxLayout *skillColumn = new QVBoxLayout;
    equipmentColumn->addWidget(new QLabel(QString::fromUtf8("装备（多项同时满足）"), filtersBox));
    skillColumn->addWidget(new QLabel(QString::fromUtf8("发动技能（多项同时满足）"), filtersBox));
    m_equipmentFilter = new TagSelectWidget(QString::fromUtf8("输入装备关键词筛选"), filtersBox);
    m_skillFilter = new TagSelectWidget(QString::fromUtf8("输入发动技能关键词筛选"), filtersBox);
    m_equipmentFilter->setToolTip(QString::fromUtf8("输入关键词后点击候选或按回车添加；点击标签 × 删除。"));
    m_skillFilter->setToolTip(QString::fromUtf8("只筛选实际发动技能，不按技能点数筛选。"));
    equipmentColumn->addWidget(m_equipmentFilter);
    skillColumn->addWidget(m_skillFilter);
    tagFilters->addLayout(equipmentColumn, 1);
    tagFilters->addLayout(skillColumn, 1);
    filters->addLayout(tagFilters);
    root->addWidget(filtersBox);

    m_resultState = new QLabel(QString::fromUtf8("正在读取配装大厅…"), this);
    m_resultState->setObjectName("appSubtitle");
    root->addWidget(m_resultState);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(10);
    m_table->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("配装名")
        << QString::fromUtf8("发布者") << QString::fromUtf8("发动技能") << QString::fromUtf8("防御力")
        << QString::fromUtf8("火耐性") << QString::fromUtf8("水耐性") << QString::fromUtf8("雷耐性")
        << QString::fromUtf8("冰耐性") << QString::fromUtf8("龙耐性") << QString::fromUtf8("操作"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    for (int column = 3; column <= 9; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    m_accountPage->setObjectName("accountSurface");
    m_accountPage->setStyleSheet(
        "QWidget#accountSurface{background:#f5f8fc;}"
        "QFrame#accountCard{background:#ffffff;border:1px solid #d5e1ef;border-radius:18px;}"
        "QFrame#accountFormPanel{background:#ffffff;border:0;}"
        "QStackedWidget#accountStack,QStackedWidget#authStack{background:transparent;border:0;}"
        "QWidget#accountGuest,QWidget#accountForm,QWidget#accountProfile{background:transparent;}"
        "QLabel#accountBrand{color:#17355c;font-size:27px;font-weight:900;}"
        "QLabel#accountBadge{color:#2f74c8;background:#eaf2fd;border:1px solid #c9dcf4;border-radius:10px;padding:4px 9px;font-weight:700;}"
        "QLabel#accountHint{color:#6d7f98;font-size:13px;}"
        "QLabel#accountTitle{color:#172033;font-size:19px;font-weight:800;}"
        "QLabel#accountAvatar{background:#2f74c8;color:white;border-radius:28px;font-size:24px;font-weight:800;}"
        "QLabel#accountMetric{color:#2f74c8;font-size:28px;font-weight:800;}"
        "QLabel#accountError{color:#b42318;background:#fef3f2;border:1px solid #fecdca;border-radius:8px;padding:9px;}"
        "QFrame#accountTabs{background:#eef3f9;border:0;border-radius:9px;}"
        "QPushButton#accountTab{background:transparent;border:0;border-radius:7px;padding:9px 24px;color:#66758b;font-weight:700;}"
        "QPushButton#accountTab:checked{color:#1f63b3;background:#ffffff;border:1px solid #d6e2f0;}"
        "QLineEdit[accountField=\"true\"]{min-height:38px;border:1px solid #c9d6e5;border-radius:8px;padding:2px 11px;background:#fbfdff;color:#20324b;}"
        "QLineEdit[accountField=\"true\"]:focus{border:1px solid #2f74c8;background:#ffffff;}"
        "QPushButton#accountPrimary{min-height:38px;background:#2f74c8;color:white;border:0;border-radius:8px;padding:3px 18px;font-weight:800;}"
        "QPushButton#accountPrimary:hover{background:#245fa8;}"
        "QPushButton#accountPrimary:disabled{background:#a9bdd7;color:#edf3fa;}"
        "QPushButton#accountSecondary{min-height:36px;background:#ffffff;color:#315f99;border:1px solid #b9cce3;border-radius:8px;padding:2px 14px;font-weight:700;}"
        "QPushButton#accountLink{color:#2f74c8;background:transparent;border:0;padding:7px;font-weight:700;}"
        "QFrame#profileHero{background:#edf4fd;border:1px solid #d2e2f5;border-radius:12px;}"
        "QFrame#statCard{background:#f8fafc;border:1px solid #e0e8f2;border-radius:10px;}"
        "QGroupBox#accountSection{color:#31445f;font-weight:700;border:1px solid #dce5ef;border-radius:10px;margin-top:10px;padding-top:12px;}"
        "QGroupBox#accountSection::title{subcontrol-origin:margin;left:12px;padding:0 6px;}"
    );
    QVBoxLayout *accountRoot = new QVBoxLayout(m_accountPage);
    accountRoot->setContentsMargins(30, 26, 30, 26);
    accountRoot->setSpacing(0);
    accountRoot->addStretch();
    QHBoxLayout *accountCenter = new QHBoxLayout;
    accountCenter->addStretch();
    QFrame *identity = new QFrame(m_accountPage);
    identity->setObjectName("accountCard");
    identity->setMinimumWidth(500);
    identity->setMaximumWidth(590);
    identity->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QHBoxLayout *identityLayout = new QHBoxLayout(identity);
    identityLayout->setContentsMargins(12, 12, 12, 12);
    identityLayout->setSpacing(0);

    QFrame *formPanel = new QFrame(identity);
    formPanel->setObjectName("accountFormPanel");
    formPanel->setMinimumWidth(450);
    formPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    QVBoxLayout *formPanelLayout = new QVBoxLayout(formPanel);
    formPanelLayout->setContentsMargins(26, 22, 24, 22);
    formPanelLayout->setSpacing(9);
    QHBoxLayout *brandRow = new QHBoxLayout;
    QLabel *brandLabel = new QLabel(QString::fromUtf8("MHED ACCOUNT"), formPanel);
    brandLabel->setObjectName("accountBrand");
    QLabel *accountBadge = new QLabel(QString::fromUtf8("配装云"), formPanel);
    accountBadge->setObjectName("accountBadge");
    brandRow->addWidget(brandLabel);
    brandRow->addStretch();
    brandRow->addWidget(accountBadge);
    QLabel *brandHint = new QLabel(QString::fromUtf8("登录后发布配装、点赞举报，并管理你的公开身份"), formPanel);
    brandHint->setObjectName("accountHint");
    brandHint->setWordWrap(true);
    formPanelLayout->addLayout(brandRow);
    formPanelLayout->addWidget(brandHint);

    m_accountStack = new CurrentPageStackedWidget(formPanel);
    m_accountStack->setObjectName("accountStack");
    m_accountStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QWidget *guestPage = new QWidget(m_accountStack);
    guestPage->setObjectName("accountGuest");
    QVBoxLayout *guestLayout = new QVBoxLayout(guestPage);
    guestLayout->setContentsMargins(0, 13, 0, 0);
    guestLayout->setSpacing(11);
    QFrame *tabsFrame = new QFrame(guestPage);
    tabsFrame->setObjectName("accountTabs");
    QHBoxLayout *tabs = new QHBoxLayout(tabsFrame);
    tabs->setContentsMargins(4, 4, 4, 4);
    tabs->setSpacing(4);
    m_loginTab = new QPushButton(QString::fromUtf8("登录"), tabsFrame);
    m_registerTab = new QPushButton(QString::fromUtf8("注册"), tabsFrame);
    m_loginTab->setObjectName("accountTab"); m_registerTab->setObjectName("accountTab");
    m_loginTab->setCheckable(true); m_registerTab->setCheckable(true); m_loginTab->setChecked(true);
    tabs->addWidget(m_loginTab, 1); tabs->addWidget(m_registerTab, 1);
    guestLayout->addWidget(tabsFrame);
    m_authForms = new CurrentPageStackedWidget(guestPage);
    m_authForms->setObjectName("authStack");
    m_authForms->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    QWidget *loginForm = new QWidget(m_authForms);
    loginForm->setObjectName("accountForm");
    QFormLayout *loginLayout = new QFormLayout(loginForm);
    loginLayout->setContentsMargins(0, 8, 0, 0);
    loginLayout->setVerticalSpacing(10);
    loginLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    loginLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_username = new QLineEdit(loginForm);
    m_username->setProperty("accountField", true);
    m_username->setPlaceholderText(QString::fromUtf8("用户名或已验证邮箱"));
    m_password = new QLineEdit(loginForm);
    m_password->setProperty("accountField", true);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QString::fromUtf8("输入登录密码"));
    m_login = new QPushButton(QString::fromUtf8("登  录"), loginForm);
    m_login->setObjectName("accountPrimary");
    m_forgotPassword = new QPushButton(QString::fromUtf8("忘记密码？"), loginForm);
    m_forgotPassword->setObjectName("accountLink");
    QHBoxLayout *loginActions = new QHBoxLayout;
    loginActions->setContentsMargins(0, 3, 0, 0);
    loginActions->addWidget(m_login, 1); loginActions->addWidget(m_forgotPassword);
    loginLayout->addRow(QString::fromUtf8("账号"), m_username);
    loginLayout->addRow(QString::fromUtf8("密码"), m_password);
    loginLayout->addRow(loginActions);
    m_authForms->addWidget(loginForm);

    QWidget *registerForm = new QWidget(m_authForms);
    registerForm->setObjectName("accountForm");
    QFormLayout *registerLayout = new QFormLayout(registerForm);
    registerLayout->setContentsMargins(0, 8, 0, 0);
    registerLayout->setVerticalSpacing(7);
    registerLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    registerLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_registerUsername = new QLineEdit(registerForm); m_registerUsername->setProperty("accountField", true);
    m_registerUsername->setPlaceholderText(QString::fromUtf8("3～32位字母、数字或下划线"));
    m_registerEmail = new QLineEdit(registerForm); m_registerEmail->setProperty("accountField", true);
    m_registerEmail->setPlaceholderText(QString::fromUtf8("用于验证和找回密码"));
    m_registerPassword = new QLineEdit(registerForm); m_registerPassword->setProperty("accountField", true); m_registerPassword->setEchoMode(QLineEdit::Password);
    m_registerPassword->setPlaceholderText(QString::fromUtf8("8～16 位账号密码")); m_registerPassword->setMaxLength(16);
    m_registerConfirm = new QLineEdit(registerForm); m_registerConfirm->setProperty("accountField", true); m_registerConfirm->setEchoMode(QLineEdit::Password); m_registerConfirm->setMaxLength(16);
    m_registerConfirm->setPlaceholderText(QString::fromUtf8("再次输入密码"));
    QWidget *codeRow = new QWidget(registerForm);
    QHBoxLayout *codeLayout = new QHBoxLayout(codeRow); codeLayout->setContentsMargins(0,0,0,0); codeLayout->setSpacing(8);
    m_registerCode = new QLineEdit(codeRow); m_registerCode->setProperty("accountField", true); m_registerCode->setPlaceholderText(QString::fromUtf8("6位验证码")); m_registerCode->setMaxLength(6);
    m_sendRegisterCode = new QPushButton(QString::fromUtf8("发送验证码"), codeRow); m_sendRegisterCode->setObjectName("accountSecondary");
    codeLayout->addWidget(m_registerCode, 1); codeLayout->addWidget(m_sendRegisterCode);
    m_register = new QPushButton(QString::fromUtf8("验证并创建账号"), registerForm); m_register->setObjectName("accountPrimary");
    registerLayout->addRow(QString::fromUtf8("用户名"), m_registerUsername);
    registerLayout->addRow(QString::fromUtf8("邮箱"), m_registerEmail);
    registerLayout->addRow(QString::fromUtf8("密码"), m_registerPassword);
    QLabel *registerPasswordHint = new QLabel(passwordPolicyText(), registerForm); registerPasswordHint->setObjectName("accountHint"); registerPasswordHint->setWordWrap(true);
    registerLayout->addRow(QString(), registerPasswordHint);
    registerLayout->addRow(QString::fromUtf8("确认密码"), m_registerConfirm);
    registerLayout->addRow(QString::fromUtf8("邮箱验证码"), codeRow);
    registerLayout->addRow(m_register);
    m_authForms->addWidget(registerForm);
    guestLayout->addWidget(m_authForms);
    m_accountError = new QLabel(guestPage); m_accountError->setObjectName("accountError"); m_accountError->setWordWrap(true); m_accountError->hide();
    guestLayout->addWidget(m_accountError);
    m_accountStack->addWidget(guestPage);

    QWidget *profilePage = new QWidget(m_accountStack);
    profilePage->setObjectName("accountProfile");
    QVBoxLayout *profileLayout = new QVBoxLayout(profilePage); profileLayout->setContentsMargins(0,14,0,0); profileLayout->setSpacing(12);
    QFrame *hero = new QFrame(profilePage); hero->setObjectName("profileHero");
    QHBoxLayout *heroLayout = new QHBoxLayout(hero); heroLayout->setContentsMargins(16,14,16,14); heroLayout->setSpacing(13);
    m_profileAvatar = new QLabel(QString::fromUtf8("猎"), hero); m_profileAvatar->setObjectName("accountAvatar"); m_profileAvatar->setFixedSize(56,56); m_profileAvatar->setAlignment(Qt::AlignCenter);
    QVBoxLayout *profileIdentity = new QVBoxLayout;
    m_profileName = new QLabel(hero); m_profileName->setObjectName("accountTitle");
    m_profileMeta = new QLabel(hero); m_profileMeta->setObjectName("accountHint");
    profileIdentity->addWidget(m_profileName); profileIdentity->addWidget(m_profileMeta);
    heroLayout->addWidget(m_profileAvatar); heroLayout->addLayout(profileIdentity,1);
    m_logout = new QPushButton(QString::fromUtf8("退出"), hero); m_logout->setObjectName("accountSecondary"); heroLayout->addWidget(m_logout);
    profileLayout->addWidget(hero);
    QHBoxLayout *summaryRow = new QHBoxLayout;
    QFrame *likeCard = new QFrame(profilePage); likeCard->setObjectName("statCard"); QVBoxLayout *likeLayout = new QVBoxLayout(likeCard);
    likeLayout->addWidget(new QLabel(QString::fromUtf8("公开配装获赞"), likeCard));
    m_receivedLikes = new QLabel(QStringLiteral("0"), likeCard); m_receivedLikes->setObjectName("accountMetric"); likeLayout->addWidget(m_receivedLikes);
    QFrame *emailCard = new QFrame(profilePage); emailCard->setObjectName("statCard"); QVBoxLayout *emailLayout = new QVBoxLayout(emailCard);
    emailLayout->addWidget(new QLabel(QString::fromUtf8("验证邮箱"), emailCard));
    m_profileEmail = new QLabel(QString::fromUtf8("尚未绑定"), emailCard); m_profileEmail->setWordWrap(true); emailLayout->addWidget(m_profileEmail);
    summaryRow->addWidget(likeCard,1); summaryRow->addWidget(emailCard,2); profileLayout->addLayout(summaryRow);
    QGroupBox *publicBox = new QGroupBox(QString::fromUtf8("公开昵称"), profilePage); publicBox->setObjectName("accountSection");
    QHBoxLayout *profileRow = new QHBoxLayout(publicBox);
    m_nickname = new QLineEdit(publicBox); m_nickname->setProperty("accountField", true);
    m_nickname->setPlaceholderText(QString::fromUtf8("公开昵称（2～32字符）"));
    m_saveNickname = new QPushButton(QString::fromUtf8("保存昵称"), publicBox); m_saveNickname->setObjectName("accountPrimary");
    profileRow->addWidget(m_nickname, 1); profileRow->addWidget(m_saveNickname);
    profileLayout->addWidget(publicBox);
    QGroupBox *securityBox = new QGroupBox(QString::fromUtf8("账号安全"), profilePage); securityBox->setObjectName("accountSection");
    QHBoxLayout *securityRow = new QHBoxLayout(securityBox);
    m_bindEmail = new QPushButton(QString::fromUtf8("绑定 / 更换邮箱"), securityBox); m_bindEmail->setObjectName("accountSecondary");
    m_changePassword = new QPushButton(QString::fromUtf8("修改密码"), securityBox); m_changePassword->setObjectName("accountSecondary");
    securityRow->addWidget(m_bindEmail); securityRow->addWidget(m_changePassword); securityRow->addStretch();
    profileLayout->addWidget(securityBox);
    m_accountState = new QLabel(profilePage); m_accountState->setWordWrap(true); m_accountState->setObjectName("accountHint"); profileLayout->addWidget(m_accountState);
    m_accountStack->addWidget(profilePage);
    formPanelLayout->addWidget(m_accountStack);
    formPanelLayout->addStretch();
    identityLayout->addWidget(formPanel);
    accountCenter->addWidget(identity, 1);
    accountCenter->addStretch();
    accountRoot->addLayout(accountCenter);
    accountRoot->addStretch();

    m_resendSeconds = 0;
    m_resendTimer = new QTimer(this);
    m_resendTimer->setInterval(1000);

    connect(refresh, SIGNAL(clicked(bool)), this, SLOT(refreshLoadouts()));
    connect(m_search, SIGNAL(returnPressed()), this, SLOT(refreshLoadouts()));
    connect(m_legalOnly, SIGNAL(toggled(bool)), this, SLOT(refreshLoadouts()));
    connect(m_login, SIGNAL(clicked(bool)), this, SLOT(login()));
    connect(m_password, SIGNAL(returnPressed()), this, SLOT(login()));
    connect(m_loginTab, SIGNAL(clicked(bool)), this, SLOT(showLoginForm()));
    connect(m_registerTab, SIGNAL(clicked(bool)), this, SLOT(showRegisterForm()));
    connect(m_sendRegisterCode, SIGNAL(clicked(bool)), this, SLOT(sendRegisterCode()));
    connect(m_register, SIGNAL(clicked(bool)), this, SLOT(registerAccount()));
    connect(m_forgotPassword, SIGNAL(clicked(bool)), this, SLOT(forgotPassword()));
    connect(m_bindEmail, SIGNAL(clicked(bool)), this, SLOT(bindEmail()));
    connect(m_logout, SIGNAL(clicked(bool)), this, SLOT(logout()));
    connect(m_saveNickname, SIGNAL(clicked(bool)), this, SLOT(saveNickname()));
    connect(m_changePassword, SIGNAL(clicked(bool)), this, SLOT(changePassword()));
    connect(m_resendTimer, &QTimer::timeout, this, [this]() { startRegisterCountdown(m_resendSeconds - 1); });

    m_equipmentFilter->setChangedHandler([this]() { refreshLoadouts(); });
    m_skillFilter->setChangedHandler([this]() { refreshLoadouts(); });
    populateFilters();
    m_token = QSettings().value("platform/accessToken").toString();
    updateAccountUi();
    if (!m_token.isEmpty()) restoreSession();
    refreshLoadouts();
}

QWidget *QCommunity::accountPage() const { return m_accountPage; }

bool QCommunity::smokeTestAccount(QString *error)
{
    if (!m_accountPage || !m_accountStack || !m_authForms || !m_login || !m_register || !m_sendRegisterCode)
    {
        if (error) *error = QString::fromUtf8("账号中心缺少必要控件");
        return false;
    }
    showRegisterForm();
    if (m_authForms->currentIndex() != 1 || !m_registerTab->isChecked())
    {
        if (error) *error = QString::fromUtf8("注册页签无法切换");
        return false;
    }
    showLoginForm();
    if (m_authForms->currentIndex() != 0 || !m_loginTab->isChecked())
    {
        if (error) *error = QString::fromUtf8("登录页签无法恢复");
        return false;
    }
    return true;
}

void QCommunity::refreshProfile()
{
    if (!m_token.isEmpty()) restoreSession();
}

void QCommunity::populateFilters()
{
    equipment_query_t equipmentQuery;
    equipmentQuery.limit = 10000;
    for (int saveType = 1; saveType <= 19; ++saveType)
    {
        if (saveType == 6 || saveType == 12) continue;
        const QList<loadout_candidate_t> equipment = GameDataRepository::instance().queryCandidates(saveType, equipmentQuery);
        for (int index = 0; index < equipment.size(); ++index)
            m_equipmentFilter->addCandidate(QString::fromUtf8("%1 · %2").arg(equipmentTypeLabel(saveType), equipment.at(index).name),
                                            QString("%1:%2").arg(saveType).arg(equipment.at(index).saveId));
    }
    const QList<skill_tree_data_t> trees = GameDataRepository::instance().skillTreesDetailed();
    for (int treeIndex = 0; treeIndex < trees.size(); ++treeIndex)
    {
        const QList<active_skill_data_t> active = GameDataRepository::instance().activeSkills(trees.at(treeIndex).id);
        for (int index = 0; index < active.size(); ++index)
            m_skillFilter->addCandidate(QString::fromUtf8("%1（%2）").arg(active.at(index).name, trees.at(treeIndex).name), active.at(index).id);
    }
}

QNetworkReply *QCommunity::request(const QString &path, const QByteArray &method, const QByteArray &body)
{
    const QUrl url(m_baseUrl + path);
    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    networkRequest.setRawHeader("Accept", "application/json");
    networkRequest.setRawHeader("User-Agent", "MHED-MH3G-Desktop/1");
    if (!m_token.isEmpty()) networkRequest.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    if (method == "GET") return m_network->get(networkRequest);
    if (method == "POST") return m_network->post(networkRequest, body);
    if (method == "PATCH") return m_network->sendCustomRequest(networkRequest, "PATCH", body);
    if (method == "DELETE") return m_network->sendCustomRequest(networkRequest, "DELETE", body);
    return m_network->sendCustomRequest(networkRequest, method, body);
}

bool QCommunity::responseObject(QNetworkReply *reply, QJsonObject *object, bool quiet)
{
    const QJsonObject value = QJsonDocument::fromJson(reply->readAll()).object();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status >= 400)
    {
        if ((status == 401 || status == 403) && value.value("error").toObject().value("code").toString() == "AUTH_REQUIRED")
        {
            m_token.clear(); m_profile = profile_t(); QSettings().remove("platform/accessToken"); updateAccountUi();
        }
        if (!quiet) QMessageBox::warning(this, QString::fromUtf8("请求失败"), apiMessage(value, reply->errorString()));
        return false;
    }
    if (object) *object = value;
    return true;
}

void QCommunity::refreshLoadouts()
{
    QUrlQuery query;
    if (!m_search->text().trimmed().isEmpty()) query.addQueryItem("q", m_search->text().trimmed());
    if (m_legalOnly->isChecked()) query.addQueryItem("legal_only", "true");
    const QList<QVariant> equipment = m_equipmentFilter->values();
    for (int index = 0; index < equipment.size(); ++index)
        query.addQueryItem("equipment", equipment.at(index).toString());
    const QList<QVariant> skills = m_skillFilter->values();
    for (int index = 0; index < skills.size(); ++index)
        query.addQueryItem("active_skill", QString::number(skills.at(index).toInt()));
    query.addQueryItem("limit", "100");
    m_resultState->setText(QString::fromUtf8("正在刷新…"));
    QNetworkReply *reply = request("/v1/desktop/loadouts?" + query.toString(QUrl::FullyEncoded));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (!responseObject(reply, &response))
        {
            m_resultState->setText(QString::fromUtf8("刷新失败，请检查 API 服务。"));
            reply->deleteLater();
            return;
        }
        const QJsonArray items = response.value("items").toArray();
        m_table->setRowCount(items.size());
        const bool accountReady = !m_token.isEmpty() && !m_profile.mustChangePassword;
        for (int row = 0; row < items.size(); ++row)
        {
            const QJsonObject value = items.at(row).toObject();
            const QJsonObject summary = value.value("risk_summary").toObject();
            QTableWidgetItem *name = new QTableWidgetItem(value.value("name").toString());
            name->setData(Qt::UserRole, value.value("id").toString());
            name->setData(Qt::UserRole + 1, value.value("liked_by_me").toBool());
            m_table->setItem(row, 0, name);
            m_table->setItem(row, 1, new QTableWidgetItem(QString("%1 (#%2)").arg(value.value("owner_nickname").toString()).arg(value.value("owner_public_id").toVariant().toLongLong())));
            QStringList activeNames;
            const QJsonArray skills = summary.value("skills").toArray();
            for (int index = 0; index < skills.size(); ++index)
            {
                const QString active = skills.at(index).toObject().value("active_skill").toString();
                if (!active.isEmpty()) activeNames << active;
            }
            m_table->setItem(row, 2, new QTableWidgetItem(activeNames.isEmpty() ? QString::fromUtf8("—") : activeNames.join(" | ")));
            QTableWidgetItem *defense = centeredItem(QString("%1 / %2").arg(summary.value("base_defense").toInt()).arg(summary.value("max_defense").toInt()));
            defense->setToolTip(QString::fromUtf8("初始防御 / 最终防御"));
            m_table->setItem(row, 3, defense);
            m_table->setItem(row, 4, centeredItem(QString::number(summary.value("fire_res").toInt())));
            m_table->setItem(row, 5, centeredItem(QString::number(summary.value("water_res").toInt())));
            m_table->setItem(row, 6, centeredItem(QString::number(summary.value("thunder_res").toInt())));
            m_table->setItem(row, 7, centeredItem(QString::number(summary.value("ice_res").toInt())));
            m_table->setItem(row, 8, centeredItem(QString::number(summary.value("dragon_res").toInt())));

            QWidget *actions = new QWidget(m_table);
            QHBoxLayout *actionLayout = new QHBoxLayout(actions);
            actionLayout->setContentsMargins(2, 1, 2, 1);
            actionLayout->setSpacing(3);
            QPushButton *like = new QPushButton(value.value("liked_by_me").toBool()
                ? QString::fromUtf8("♥ %1").arg(value.value("like_count").toInt())
                : QString::fromUtf8("♡ %1").arg(value.value("like_count").toInt()), actions);
            QPushButton *report = new QPushButton(QString::fromUtf8("⚑"), actions);
            QPushButton *detail = new QPushButton(QString::fromUtf8("详情"), actions);
            like->setToolTip(value.value("liked_by_me").toBool() ? QString::fromUtf8("取消点赞") : QString::fromUtf8("点赞"));
            report->setToolTip(QString::fromUtf8("举报"));
            detail->setToolTip(QString::fromUtf8("使用配装器打开"));
            like->setEnabled(accountReady);
            report->setEnabled(accountReady);
            actionLayout->addWidget(like);
            actionLayout->addWidget(report);
            actionLayout->addWidget(detail);
            connect(like, &QPushButton::clicked, this, [this, row]() { m_table->selectRow(row); toggleLike(); });
            connect(report, &QPushButton::clicked, this, [this, row]() { m_table->selectRow(row); reportSelected(); });
            connect(detail, &QPushButton::clicked, this, [this, row]() { m_table->selectRow(row); importSelected(); });
            m_table->setCellWidget(row, 9, actions);
            m_table->setRowHeight(row, 40);
        }
        if (!items.isEmpty()) m_table->selectRow(0);
        m_resultState->setText(items.isEmpty() ? QString::fromUtf8("没有符合全部条件的公开配装。")
            : QString::fromUtf8("找到 %1 套公开配装").arg(items.size()));
        reply->deleteLater();
    });
}

QString QCommunity::selectedId() const
{
    QTableWidgetItem *item = m_table->currentRow() >= 0 ? m_table->item(m_table->currentRow(), 0) : 0;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

bool QCommunity::selectedLiked() const
{
    QTableWidgetItem *item = m_table->currentRow() >= 0 ? m_table->item(m_table->currentRow(), 0) : 0;
    return item && item->data(Qt::UserRole + 1).toBool();
}

void QCommunity::importSelected()
{
    const QString id = selectedId();
    if (id.isEmpty()) return;
    QNetworkReply *reply = request("/v1/desktop/loadouts/" + id);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (responseObject(reply, &response))
        {
            const QByteArray payload = QJsonDocument(response.value("payload").toObject()).toJson(QJsonDocument::Compact);
            QString error;
            const bool modified = m_loadout->showPayloadDialog(payload, &error);
            if (!error.isEmpty()) QMessageBox::warning(this, QString::fromUtf8("打开失败"), error);
            if (modified) emit equipmentBoxModified();
        }
        reply->deleteLater();
    });
}

void QCommunity::toggleLike()
{
    const QString id = selectedId();
    if (id.isEmpty()) return;
    QNetworkReply *reply = request("/v1/desktop/loadouts/" + id + "/likes", selectedLiked() ? "DELETE" : "POST", "{}");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { if (responseObject(reply, 0)) refreshLoadouts(); reply->deleteLater(); });
}

void QCommunity::reportSelected()
{
    const QString id = selectedId();
    if (id.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QString::fromUtf8("举报配装"));
    QFormLayout layout(&dialog);
    QComboBox reason(&dialog);
    reason.addItem(QString::fromUtf8("不当内容"), "inappropriate");
    reason.addItem(QString::fromUtf8("广告垃圾"), "spam");
    reason.addItem(QString::fromUtf8("无效或恶意数据"), "invalid_data");
    reason.addItem(QString::fromUtf8("侵权冒充"), "infringement");
    reason.addItem(QString::fromUtf8("其他"), "other");
    QTextEdit details(&dialog);
    details.setMaximumHeight(100);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(QString::fromUtf8("原因"), &reason);
    layout.addRow(QString::fromUtf8("说明"), &details);
    layout.addRow(&buttons);
    connect(&buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    if (dialog.exec() != QDialog::Accepted) return;
    QJsonObject body;
    body.insert("reason", reason.currentData().toString());
    body.insert("details", details.toPlainText().left(500));
    QNetworkReply *reply = request("/v1/desktop/loadouts/" + id + "/reports", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (responseObject(reply, 0)) QMessageBox::information(this, QString::fromUtf8("举报已提交"), QString::fromUtf8("管理员会在后台查看，举报不会自动下架配装。"));
        reply->deleteLater();
    });
}

void QCommunity::login()
{
    QJsonObject body;
    body.insert("account", m_username->text().trimmed());
    body.insert("password", m_password->text());
    QNetworkReply *reply = request("/v1/desktop/auth/login", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_login->setEnabled(false);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        m_login->setEnabled(true);
        if (responseObject(reply, &response))
        {
            m_password->clear();
            storeSession(response);
            refreshLoadouts();
            if (m_profile.mustChangePassword)
                QMessageBox::warning(m_accountPage, QString::fromUtf8("需要修改密码"), QString::fromUtf8("这是临时密码，请先修改；修改后需要重新登录。"));
        }
        reply->deleteLater();
    });
}

void QCommunity::storeSession(const QJsonObject &response)
{
    m_token = response.value("access_token").toString();
    if (m_token.isEmpty()) return;
    QSettings().setValue("platform/accessToken", m_token);
    applyProfile(response.value("user").toObject());
}

void QCommunity::showLoginForm()
{
    m_authForms->setCurrentIndex(0);
    m_loginTab->setChecked(true);
    m_registerTab->setChecked(false);
    setAccountError(QString());
}

void QCommunity::showRegisterForm()
{
    m_authForms->setCurrentIndex(1);
    m_loginTab->setChecked(false);
    m_registerTab->setChecked(true);
    setAccountError(QString());
}

void QCommunity::setAccountError(const QString &message)
{
    m_accountError->setStyleSheet(QString());
    m_accountError->setText(message);
    m_accountError->setVisible(!message.isEmpty());
}

void QCommunity::startRegisterCountdown(int seconds)
{
    m_resendSeconds = qMax(0, seconds);
    if (m_resendSeconds > 0)
    {
        m_sendRegisterCode->setEnabled(false);
        m_sendRegisterCode->setText(QString::fromUtf8("%1 秒后重发").arg(m_resendSeconds));
        if (!m_resendTimer->isActive()) m_resendTimer->start();
    }
    else
    {
        m_resendTimer->stop();
        m_sendRegisterCode->setEnabled(true);
        m_sendRegisterCode->setText(QString::fromUtf8("发送验证码"));
    }
}

void QCommunity::sendRegisterCode()
{
    const QString username = m_registerUsername->text().trimmed();
    const QString email = m_registerEmail->text().trimmed();
    if (username.isEmpty() || email.isEmpty())
    {
        setAccountError(QString::fromUtf8("请先填写用户名和邮箱。"));
        return;
    }
    QJsonObject body; body.insert("username", username); body.insert("email", email);
    m_sendRegisterCode->setEnabled(false);
    QNetworkReply *reply = request("/v1/desktop/auth/register/code", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (responseObject(reply, &response, true))
        {
            m_registerChallenge = response.value("challenge_id").toString();
            startRegisterCountdown(response.value("resend_after_seconds").toInt(60));
            m_accountError->setStyleSheet("color:#175cd3;background:#eff8ff;border:1px solid #b2ddff;border-radius:7px;padding:8px;");
            m_accountError->setText(QString::fromUtf8("验证码已进入发送队列，请检查邮箱；10 分钟内有效。"));
            m_accountError->show();
        }
        else
        {
            m_sendRegisterCode->setEnabled(true);
            setAccountError(QString::fromUtf8("验证码发送失败，请检查填写内容或稍后重试。"));
        }
        reply->deleteLater();
    });
}

void QCommunity::registerAccount()
{
    if (m_registerChallenge.isEmpty()) { setAccountError(QString::fromUtf8("请先获取邮箱验证码。")); return; }
    if (!validAccountPassword(m_registerPassword->text())) { setAccountError(passwordPolicyText()); return; }
    if (m_registerPassword->text() != m_registerConfirm->text()) { setAccountError(QString::fromUtf8("两次输入的密码不一致。")); return; }
    QJsonObject body;
    body.insert("challenge_id", m_registerChallenge);
    body.insert("code", m_registerCode->text().trimmed());
    body.insert("password", m_registerPassword->text());
    m_register->setEnabled(false);
    QNetworkReply *reply = request("/v1/desktop/auth/register", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        m_register->setEnabled(true);
        if (responseObject(reply, &response, true))
        {
            storeSession(response);
            m_registerPassword->clear(); m_registerConfirm->clear(); m_registerCode->clear(); m_registerChallenge.clear();
            refreshLoadouts();
        }
        else setAccountError(QString::fromUtf8("注册失败，请确认验证码和密码后重试。"));
        reply->deleteLater();
    });
}

void QCommunity::forgotPassword()
{
    QDialog *dialog = new QDialog(m_accountPage);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setWindowTitle(QString::fromUtf8("找回密码"));
    QFormLayout *layout = new QFormLayout(dialog);
    QLineEdit *account = new QLineEdit(dialog); account->setPlaceholderText(QString::fromUtf8("用户名或已验证邮箱"));
    QLineEdit *code = new QLineEdit(dialog); code->setMaxLength(6); code->setPlaceholderText(QString::fromUtf8("6位验证码"));
    QLineEdit *password = new QLineEdit(dialog); password->setEchoMode(QLineEdit::Password); password->setMaxLength(16); password->setPlaceholderText(QString::fromUtf8("8～16 位账号密码"));
    QLineEdit *confirm = new QLineEdit(dialog); confirm->setEchoMode(QLineEdit::Password); confirm->setMaxLength(16);
    QPushButton *send = new QPushButton(QString::fromUtf8("发送验证码"), dialog);
    QPushButton *reset = new QPushButton(QString::fromUtf8("重置密码"), dialog);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("取消"), dialog);
    QLabel *status = new QLabel(dialog); status->setWordWrap(true);
    QHBoxLayout *codeRow = new QHBoxLayout; codeRow->addWidget(code,1); codeRow->addWidget(send);
    QHBoxLayout *actions = new QHBoxLayout; actions->addStretch(); actions->addWidget(cancel); actions->addWidget(reset);
    layout->addRow(QString::fromUtf8("账号"), account); layout->addRow(QString::fromUtf8("验证码"), codeRow);
    QLabel *passwordHint = new QLabel(passwordPolicyText(), dialog); passwordHint->setWordWrap(true);
    layout->addRow(QString::fromUtf8("新密码"), password); layout->addRow(QString(), passwordHint); layout->addRow(QString::fromUtf8("确认密码"), confirm); layout->addRow(status); layout->addRow(actions);
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    QTimer *timer = new QTimer(dialog); timer->setInterval(1000);
    connect(timer, &QTimer::timeout, dialog, [dialog, send, timer]() { int seconds=dialog->property("resendSeconds").toInt()-1; dialog->setProperty("resendSeconds",seconds); if(seconds<=0){timer->stop();send->setEnabled(true);send->setText(QString::fromUtf8("发送验证码"));}else send->setText(QString::fromUtf8("%1 秒后重发").arg(seconds)); });
    connect(send, &QPushButton::clicked, dialog, [this, dialog, account, send, status, timer]() {
        QJsonObject body; body.insert("account", account->text().trimmed()); send->setEnabled(false);
        QNetworkReply *reply=request("/v1/desktop/auth/password-reset/code","POST",QJsonDocument(body).toJson(QJsonDocument::Compact));
        QPointer<QDialog> guard(dialog);
        connect(reply,&QNetworkReply::finished,this,[this,guard,reply,send,status,timer](){QJsonObject response;if(guard&&responseObject(reply,&response,true)){guard->setProperty("challengeId",response.value("challenge_id").toString());guard->setProperty("resendSeconds",60);timer->start();status->setText(QString::fromUtf8("若账号存在，验证码已发送到已验证邮箱。"));}else if(guard){send->setEnabled(true);status->setText(QString::fromUtf8("发送失败，请稍后重试。"));}reply->deleteLater();});
    });
    connect(reset, &QPushButton::clicked, dialog, [this, dialog, code, password, confirm, reset, status]() {
        if(!validAccountPassword(password->text())){status->setText(passwordPolicyText());return;}
        if(password->text()!=confirm->text()){status->setText(QString::fromUtf8("两次输入的新密码不一致。"));return;}
        QJsonObject body; body.insert("challenge_id",dialog->property("challengeId").toString());body.insert("code",code->text().trimmed());body.insert("new_password",password->text());reset->setEnabled(false);
        QNetworkReply *reply=request("/v1/desktop/auth/password-reset","POST",QJsonDocument(body).toJson(QJsonDocument::Compact));QPointer<QDialog> guard(dialog);
        connect(reply,&QNetworkReply::finished,this,[this,guard,reply,reset,status](){if(guard&&responseObject(reply,0,true)){guard->accept();m_accountError->setStyleSheet("color:#17643a;background:#ecfdf3;border:1px solid #abefc6;border-radius:7px;padding:8px;");m_accountError->setText(QString::fromUtf8("密码已重置，请使用新密码登录。"));m_accountError->show();}else if(guard){reset->setEnabled(true);status->setText(QString::fromUtf8("重置失败，请检查验证码和新密码。"));}reply->deleteLater();});
    });
    dialog->resize(520,330); dialog->show();
}

void QCommunity::restoreSession()
{
    QNetworkReply *reply = request("/v1/desktop/me");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (responseObject(reply, &response, true)) applyProfile(response.value("user").toObject());
        else updateAccountUi();
        reply->deleteLater();
    });
}

void QCommunity::applyProfile(const QJsonObject &user)
{
    m_profile.publicId = user.value("public_id").toVariant().toLongLong();
    m_profile.nickname = user.value("nickname").toString();
    m_profile.email = user.value("email").toString();
    m_profile.emailVerified = user.value("email_verified").toBool();
    m_profile.receivedLikeCount = user.value("received_like_count").toVariant().toLongLong();
    m_profile.mustChangePassword = user.value("must_change_password").toBool();
    m_nickname->setText(m_profile.nickname);
    updateAccountUi();
}

void QCommunity::updateAccountUi()
{
    const bool loggedIn = !m_token.isEmpty() && m_profile.publicId > 0;
    m_accountStack->setCurrentIndex(loggedIn ? 1 : 0);
    if (loggedIn)
    {
        m_profileName->setText(m_profile.nickname);
        m_profileMeta->setText(QString::fromUtf8("公开 ID  #%1%2").arg(m_profile.publicId)
            .arg(m_profile.mustChangePassword ? QString::fromUtf8("  ·  需要修改临时密码") : QString()));
        const QString first = m_profile.nickname.trimmed().left(1);
        m_profileAvatar->setText(first.isEmpty() ? QString::fromUtf8("猎") : first);
        m_profileEmail->setText(m_profile.emailVerified && !m_profile.email.isEmpty()
            ? QString::fromUtf8("✓ %1").arg(m_profile.email) : QString::fromUtf8("尚未绑定验证邮箱"));
        m_receivedLikes->setText(QString::number(m_profile.receivedLikeCount));
        m_accountState->setText(QString::fromUtf8("登录用户名和邮箱仅用于认证，配装广场只展示昵称与公开 ID。"));
    }
    const bool ready = loggedIn && !m_profile.mustChangePassword;
    m_nickname->setEnabled(ready);
    m_saveNickname->setEnabled(ready);
    m_bindEmail->setEnabled(ready);
}

void QCommunity::logout()
{
    QNetworkReply *reply = request("/v1/desktop/auth/logout", "POST", "{}");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        responseObject(reply, 0, true);
        m_token.clear(); m_profile = profile_t(); QSettings().remove("platform/accessToken");
        updateAccountUi(); refreshLoadouts(); reply->deleteLater();
    });
}

void QCommunity::saveNickname()
{
    QJsonObject body;
    body.insert("nickname", m_nickname->text().trimmed());
    QNetworkReply *reply = request("/v1/desktop/me", "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (responseObject(reply, &response))
        {
            applyProfile(response.value("user").toObject());
            refreshLoadouts();
            QMessageBox::information(m_accountPage, QString::fromUtf8("昵称已更新"), QString::fromUtf8("配装广场会显示新昵称和公开ID。"));
        }
        reply->deleteLater();
    });
}

void QCommunity::bindEmail()
{
    QDialog *dialog = new QDialog(m_accountPage);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setWindowTitle(QString::fromUtf8("绑定或更换邮箱"));
    QFormLayout *layout = new QFormLayout(dialog);
    QLineEdit *email = new QLineEdit(dialog); email->setText(m_profile.email);
    QLineEdit *code = new QLineEdit(dialog); code->setMaxLength(6); code->setPlaceholderText(QString::fromUtf8("6位验证码"));
    QLineEdit *current = new QLineEdit(dialog); current->setEchoMode(QLineEdit::Password);
    QPushButton *send = new QPushButton(QString::fromUtf8("发送验证码"), dialog);
    QPushButton *save = new QPushButton(QString::fromUtf8("验证并保存"), dialog);
    QPushButton *cancel = new QPushButton(QString::fromUtf8("取消"), dialog);
    QLabel *status = new QLabel(dialog); status->setWordWrap(true);
    QHBoxLayout *codeRow = new QHBoxLayout; codeRow->addWidget(code,1); codeRow->addWidget(send);
    QHBoxLayout *actions = new QHBoxLayout; actions->addStretch(); actions->addWidget(cancel); actions->addWidget(save);
    layout->addRow(QString::fromUtf8("新邮箱"),email);layout->addRow(QString::fromUtf8("验证码"),codeRow);layout->addRow(QString::fromUtf8("当前密码"),current);layout->addRow(status);layout->addRow(actions);
    connect(cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    QTimer *timer=new QTimer(dialog);timer->setInterval(1000);
    connect(timer,&QTimer::timeout,dialog,[dialog,send,timer](){int seconds=dialog->property("resendSeconds").toInt()-1;dialog->setProperty("resendSeconds",seconds);if(seconds<=0){timer->stop();send->setEnabled(true);send->setText(QString::fromUtf8("发送验证码"));}else send->setText(QString::fromUtf8("%1 秒后重发").arg(seconds));});
    connect(send,&QPushButton::clicked,dialog,[this,dialog,email,send,status,timer](){QJsonObject body;body.insert("email",email->text().trimmed());send->setEnabled(false);QNetworkReply *reply=request("/v1/desktop/me/email/code","POST",QJsonDocument(body).toJson(QJsonDocument::Compact));QPointer<QDialog> guard(dialog);connect(reply,&QNetworkReply::finished,this,[this,guard,reply,send,status,timer](){QJsonObject response;if(guard&&responseObject(reply,&response,true)){guard->setProperty("challengeId",response.value("challenge_id").toString());guard->setProperty("resendSeconds",60);timer->start();status->setText(QString::fromUtf8("验证码已进入发送队列，10 分钟内有效。"));}else if(guard){send->setEnabled(true);status->setText(QString::fromUtf8("发送失败，邮箱可能已被使用。"));}reply->deleteLater();});});
    connect(save,&QPushButton::clicked,dialog,[this,dialog,code,current,save,status](){QJsonObject body;body.insert("challenge_id",dialog->property("challengeId").toString());body.insert("code",code->text().trimmed());body.insert("current_password",current->text());save->setEnabled(false);QNetworkReply *reply=request("/v1/desktop/me/email","PUT",QJsonDocument(body).toJson(QJsonDocument::Compact));QPointer<QDialog> guard(dialog);connect(reply,&QNetworkReply::finished,this,[this,guard,reply,save,status](){if(guard&&responseObject(reply,0,true)){guard->accept();m_token.clear();m_profile=profile_t();QSettings().remove("platform/accessToken");updateAccountUi();refreshLoadouts();m_accountError->setStyleSheet("color:#17643a;background:#ecfdf3;border:1px solid #abefc6;border-radius:7px;padding:8px;");m_accountError->setText(QString::fromUtf8("邮箱已验证，请重新登录。"));m_accountError->show();}else if(guard){save->setEnabled(true);status->setText(QString::fromUtf8("验证失败，请检查验证码和当前密码。"));}reply->deleteLater();});});
    dialog->resize(480,250);dialog->show();
}

void QCommunity::changePassword()
{
    QDialog dialog(m_accountPage);
    dialog.setWindowTitle(QString::fromUtf8("修改密码"));
    QFormLayout layout(&dialog);
    QLineEdit current(&dialog), password(&dialog), confirm(&dialog);
    current.setEchoMode(QLineEdit::Password);
    password.setEchoMode(QLineEdit::Password);
    password.setMaxLength(16);
    password.setPlaceholderText(QString::fromUtf8("8～16 位账号密码"));
    confirm.setEchoMode(QLineEdit::Password);
    confirm.setMaxLength(16);
    QLabel passwordHint(passwordPolicyText(), &dialog);
    passwordHint.setWordWrap(true);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(QString::fromUtf8("当前密码"), &current);
    layout.addRow(QString::fromUtf8("新密码"), &password);
    layout.addRow(QString(), &passwordHint);
    layout.addRow(QString::fromUtf8("确认新密码"), &confirm);
    layout.addRow(&buttons);
    connect(&buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    if (dialog.exec() != QDialog::Accepted) return;
    if (!validAccountPassword(password.text()))
    {
        QMessageBox::warning(m_accountPage, QString::fromUtf8("无法修改"), passwordPolicyText());
        return;
    }
    if (password.text() != confirm.text())
    {
        QMessageBox::warning(m_accountPage, QString::fromUtf8("无法修改"), QString::fromUtf8("两次输入的新密码不一致。"));
        return;
    }
    QJsonObject body;
    body.insert("current_password", current.text());
    body.insert("new_password", password.text());
    QNetworkReply *reply = request("/v1/desktop/auth/change-password", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (responseObject(reply, 0))
        {
            m_token.clear(); m_profile = profile_t(); QSettings().remove("platform/accessToken");
            updateAccountUi();
            QMessageBox::information(m_accountPage, QString::fromUtf8("密码已修改"), QString::fromUtf8("请使用新密码重新登录。"));
        }
        reply->deleteLater();
    });
}

void QCommunity::uploadCurrent()
{
    if (m_token.isEmpty() || m_profile.publicId <= 0)
    {
        QMessageBox::information(m_loadout, QString::fromUtf8("需要登录"), QString::fromUtf8("请先在左侧“个人信息”中登录，再发布当前配装。"));
        return;
    }
    if (m_profile.mustChangePassword)
    {
        QMessageBox::information(m_loadout, QString::fromUtf8("需要修改密码"), QString::fromUtf8("请先在“个人信息”中修改临时密码。"));
        return;
    }
    QString error;
    const QByteArray payload = m_loadout->currentPayload(&error);
    if (payload.isEmpty()) { QMessageBox::warning(m_loadout, QString::fromUtf8("无法发布"), error); return; }
    bool accepted = false;
    const QString remark = QInputDialog::getMultiLineText(m_loadout, QString::fromUtf8("发布到配装广场"),
        QString::fromUtf8("公开备注（可留空，最多 500 字符）"), QString(), &accepted);
    if (!accepted) return;
    if (remark.size() > 500)
    {
        QMessageBox::warning(m_loadout, QString::fromUtf8("备注过长"), QString::fromUtf8("公开备注最多 500 个字符。"));
        return;
    }
    QJsonObject body;
    body.insert("remark", remark.trimmed());
    body.insert("payload", QJsonDocument::fromJson(payload).object());
    QNetworkReply *reply = request("/v1/desktop/loadouts", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        if (responseObject(reply, &response))
        {
            refreshLoadouts();
            QMessageBox::information(m_loadout, QString::fromUtf8("发布成功"), response.value("is_legal").toBool()
                ? QString::fromUtf8("配装已发布到广场，服务器判定为合法。")
                : QString::fromUtf8("配装已发布到广场，但存在风险；不会限制查看和导入。"));
        }
        reply->deleteLater();
    });
}

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
#include <QStandardItemModel>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QTimer>

#include <functional>

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

    QVBoxLayout *accountRoot = new QVBoxLayout(m_accountPage);
    accountRoot->setContentsMargins(14, 14, 14, 14);
    QGroupBox *identity = new QGroupBox(QString::fromUtf8("账号与公开昵称"), m_accountPage);
    QVBoxLayout *identityLayout = new QVBoxLayout(identity);
    m_accountState = new QLabel(identity);
    m_accountState->setWordWrap(true);
    identityLayout->addWidget(m_accountState);
    QHBoxLayout *loginRow = new QHBoxLayout;
    m_username = new QLineEdit(identity);
    m_username->setPlaceholderText(QString::fromUtf8("登录用户名"));
    m_password = new QLineEdit(identity);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QString::fromUtf8("密码"));
    m_login = new QPushButton(QString::fromUtf8("登录"), identity);
    loginRow->addWidget(m_username);
    loginRow->addWidget(m_password);
    loginRow->addWidget(m_login);
    identityLayout->addLayout(loginRow);
    QHBoxLayout *profileRow = new QHBoxLayout;
    m_nickname = new QLineEdit(identity);
    m_nickname->setPlaceholderText(QString::fromUtf8("公开昵称（2～32字符）"));
    m_saveNickname = new QPushButton(QString::fromUtf8("保存昵称"), identity);
    m_changePassword = new QPushButton(QString::fromUtf8("修改密码"), identity);
    m_logout = new QPushButton(QString::fromUtf8("退出登录"), identity);
    profileRow->addWidget(m_nickname, 1);
    profileRow->addWidget(m_saveNickname);
    profileRow->addWidget(m_changePassword);
    profileRow->addWidget(m_logout);
    identityLayout->addLayout(profileRow);
    accountRoot->addWidget(identity);
    accountRoot->addStretch();

    connect(refresh, SIGNAL(clicked(bool)), this, SLOT(refreshLoadouts()));
    connect(m_search, SIGNAL(returnPressed()), this, SLOT(refreshLoadouts()));
    connect(m_legalOnly, SIGNAL(toggled(bool)), this, SLOT(refreshLoadouts()));
    connect(m_login, SIGNAL(clicked(bool)), this, SLOT(login()));
    connect(m_password, SIGNAL(returnPressed()), this, SLOT(login()));
    connect(m_logout, SIGNAL(clicked(bool)), this, SLOT(logout()));
    connect(m_saveNickname, SIGNAL(clicked(bool)), this, SLOT(saveNickname()));
    connect(m_changePassword, SIGNAL(clicked(bool)), this, SLOT(changePassword()));

    m_equipmentFilter->setChangedHandler([this]() { refreshLoadouts(); });
    m_skillFilter->setChangedHandler([this]() { refreshLoadouts(); });
    populateFilters();
    m_token = QSettings().value("platform/accessToken").toString();
    updateAccountUi();
    if (!m_token.isEmpty()) restoreSession();
    refreshLoadouts();
}

QWidget *QCommunity::accountPage() const { return m_accountPage; }

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
    body.insert("username", m_username->text().trimmed());
    body.insert("password", m_password->text());
    QNetworkReply *reply = request("/v1/desktop/auth/login", "POST", QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_login->setEnabled(false);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        QJsonObject response;
        m_login->setEnabled(true);
        if (responseObject(reply, &response))
        {
            m_token = response.value("access_token").toString();
            QSettings().setValue("platform/accessToken", m_token);
            m_password->clear();
            applyProfile(response.value("user").toObject());
            refreshLoadouts();
            if (m_profile.mustChangePassword)
                QMessageBox::warning(m_accountPage, QString::fromUtf8("需要修改密码"), QString::fromUtf8("这是临时密码，请先修改；修改后需要重新登录。"));
        }
        reply->deleteLater();
    });
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
    m_profile.mustChangePassword = user.value("must_change_password").toBool();
    m_nickname->setText(m_profile.nickname);
    updateAccountUi();
}

void QCommunity::updateAccountUi()
{
    const bool loggedIn = !m_token.isEmpty() && m_profile.publicId > 0;
    m_accountState->setText(loggedIn ? QString::fromUtf8("已登录：%1 (#%2)%3").arg(m_profile.nickname).arg(m_profile.publicId)
        .arg(m_profile.mustChangePassword ? QString::fromUtf8(" · 必须先修改临时密码") : QString())
        : QString::fromUtf8("未登录。登录用户名只用于登录，不会在配装广场公开。"));
    m_username->setVisible(!loggedIn);
    m_password->setVisible(!loggedIn);
    m_login->setVisible(!loggedIn);
    m_nickname->setVisible(loggedIn);
    m_saveNickname->setVisible(loggedIn);
    m_changePassword->setVisible(loggedIn);
    m_logout->setVisible(loggedIn);
    const bool ready = loggedIn && !m_profile.mustChangePassword;
    m_nickname->setEnabled(ready);
    m_saveNickname->setEnabled(ready);
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

void QCommunity::changePassword()
{
    QDialog dialog(m_accountPage);
    dialog.setWindowTitle(QString::fromUtf8("修改密码"));
    QFormLayout layout(&dialog);
    QLineEdit current(&dialog), password(&dialog), confirm(&dialog);
    current.setEchoMode(QLineEdit::Password);
    password.setEchoMode(QLineEdit::Password);
    confirm.setEchoMode(QLineEdit::Password);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout.addRow(QString::fromUtf8("当前密码"), &current);
    layout.addRow(QString::fromUtf8("新密码（至少12位）"), &password);
    layout.addRow(QString::fromUtf8("确认新密码"), &confirm);
    layout.addRow(&buttons);
    connect(&buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
    connect(&buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
    if (dialog.exec() != QDialog::Accepted) return;
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

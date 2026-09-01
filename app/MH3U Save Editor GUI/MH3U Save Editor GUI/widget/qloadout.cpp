#include "qloadout.hpp"

#include "equipment_validator.hpp"
#include "game_data_repository.hpp"
#include "mh3u_se.hpp"
#include "../main.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QApplication>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace
{
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel(const QString &text, QWidget *parent = 0) : QLabel(parent), m_fullText(text)
    {
        updateText();
    }

    void setFullText(const QString &text)
    {
        m_fullText = text;
        updateText();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateText();
    }

private:
    QString m_fullText;

    void updateText()
    {
        QLabel::setText(fontMetrics().elidedText(m_fullText, Qt::ElideRight, qMax(1, width())));
    }
};

QString slotLabel(loadout_slot_e slot)
{
    static const char *labels[] = {"武器", "头", "胸", "腕", "腰", "腿", "护石"};
    return QString::fromUtf8(labels[(int)slot]);
}

QString comparisonText(skill_comparison_e comparison)
{
    switch (comparison)
    {
        case SkillGreater: return ">";
        case SkillGreaterEqual: return QString::fromUtf8("≥");
        case SkillEqual: return "=";
        case SkillLessEqual: return QString::fromUtf8("≤");
        case SkillLess: return "<";
    }
    return "=";
}

QString signedText(int points)
{
    return points > 0 ? QString("+%1").arg(points) : QString::number(points);
}

int decorationSlotUsage(const QList<int> &ids)
{
    int used = 0;
    for (int i = 0; i < ids.size(); ++i)
        used += qMax(0, GameDataRepository::instance().decoration(ids.at(i)).slotCount);
    return used;
}

QString slotIndicatorText(int capacity, int used)
{
    if (capacity < 0) return QString::fromUtf8("<span style='color:#98a2b3'>？ ？ ？</span>");
    QStringList glyphs;
    for (int i = 0; i < 3; ++i)
    {
        const QChar glyph = i < used ? QChar(0x25CF) : i < capacity ? QChar(0x25CB) : QChar(0x2298);
        const char *color = i < used ? "#17643a" : i < capacity ? "#1d66c2" : "#98a2b3";
        glyphs << QString("<span style='color:%1;font-size:15px'>%2</span>").arg(color).arg(glyph);
    }
    return glyphs.join(QString::fromUtf8("&nbsp;"));
}

QString slotMetaText(int capacity, int used, int rarity, int jewelCount)
{
    QString value;
    if (rarity >= 0) value = QString::fromUtf8("R%1 · ").arg(rarity);
    value += slotIndicatorText(capacity, used);
    value += QString::fromUtf8(" · 珠%1").arg(jewelCount);
    if (capacity >= 0 && used > capacity) value += QString::fromUtf8(" · 超孔");
    return value;
}

QString skillName(int id)
{
    const QList<skill_tree_data_t> skills = GameDataRepository::instance().skillTreesDetailed();
    for (int i = 0; i < skills.size(); ++i) if (skills.at(i).id == id) return skills.at(i).name;
    return id == 0 ? QString::fromUtf8("（无）") : QString::fromUtf8("技能 %1").arg(id);
}

QString equipmentDecorationSummary(const uint8_t *record, QList<int> *identifiers = 0)
{
    if (identifiers) identifiers->clear();
    if (!record) return QString::fromUtf8("无");
    QStringList values;
    for (int offset = 8; offset <= 12; offset += 2)
    {
        const int identifier = record[offset] | (record[offset + 1] << 8);
        if (!identifier) continue;
        if (identifiers) identifiers->append(identifier);
        const decoration_data_t decoration = GameDataRepository::instance().decoration(identifier);
        values << (decoration.found ? decoration.name : QString::fromUtf8("未知珠 #%1").arg(identifier));
    }
    return values.isEmpty() ? QString::fromUtf8("无") : values.join(QString::fromUtf8(" | "));
}

QString statusText(const loadout_candidate_t &candidate, save_format_e platform = SAVE_FORMAT_UNKNOWN)
{
    if (candidate.placeholder) return QString::fromUtf8("非法");
    if (platform == SAVE_FORMAT_WIIU && candidate.mh3gOnly) return QString::fromUtf8("未确认");
    return candidate.confirmed ? QString::fromUtf8("合法") : QString::fromUtf8("未确认");
}

void colorStatusItem(QTableWidgetItem *item, const loadout_candidate_t &candidate,
                     save_format_e platform = SAVE_FORMAT_UNKNOWN)
{
    if (candidate.placeholder)
    {
        item->setForeground(QColor("#b42318")); item->setBackground(QColor("#fee4e2"));
    }
    else if (!candidate.confirmed || (platform == SAVE_FORMAT_WIIU && candidate.mh3gOnly))
    {
        item->setForeground(QColor("#8a4b08")); item->setBackground(QColor("#fff3cd"));
    }
}

class EquipmentPickerDialog : public QDialog
{
public:
    EquipmentPickerDialog(int expectedSaveType, int combat, int gender, save_format_e platform,
                          MH3U_SE *saveEditor, QWidget *parent = 0, bool boxMode = false,
                          bool naturalOnlyLocked = false)
        : QDialog(parent), m_expectedSaveType(expectedSaveType), m_combat(combat), m_genderValue(gender),
          m_platform(platform), m_saveEditor(saveEditor), m_boxMode(boxMode), m_naturalOnlyLocked(naturalOnlyLocked),
          m_page(0), m_total(0), m_selectedRow(-1)
    {
        const bool armor = expectedSaveType >= 1 && expectedSaveType <= 5;
        const bool charm = expectedSaveType == MH3U_Type::CharmType;
        const QString pickerTitle = armor ? QString::fromUtf8("选择%1部防具").arg(slotLabel(slotForType(expectedSaveType))) :
            charm ? QString::fromUtf8("选择护石") : QString::fromUtf8("选择武器");
        setWindowTitle(m_boxMode ? QString::fromUtf8("从装备箱%1").arg(pickerTitle) : pickerTitle);
        resize(920, 620);
        QVBoxLayout *root = new QVBoxLayout(this);
        QHBoxLayout *baseFilters = new QHBoxLayout;
        m_search = new QLineEdit(this); m_search->setPlaceholderText(QString::fromUtf8("搜索中文或英文名称"));
        baseFilters->addWidget(m_search, 1);
        m_weaponType = new QComboBox(this);
        m_weaponType->addItem(QString::fromUtf8("全部武器类型"), -1);
        const dataset_t *types = MH3U_DS::equipmentTypes();
        if (types)
            for (uint32_t i = 0; i < types->size(); ++i)
                if (types->at(i).count >= 7 && types->at(i).count <= 19 && types->at(i).count != 12)
                    m_weaponType->addItem(QString::fromStdString(types->at(i).identifier), types->at(i).count);
        configureSearchableComboBox(m_weaponType);
        m_weaponType->setVisible(!armor && !charm); baseFilters->addWidget(m_weaponType);
        m_rarityMin = new QSpinBox(this); m_rarityMin->setRange(-1, 10); m_rarityMin->setSpecialValueText(QString::fromUtf8("稀有度不限"));
        m_rarityMin->setValue(-1); m_rarityMin->setVisible(!charm); baseFilters->addWidget(m_rarityMin);
        m_slotsMin = new QSpinBox(this); m_slotsMin->setRange(-1, 3); m_slotsMin->setSpecialValueText(QString::fromUtf8("孔数不限"));
        m_slotsMin->setValue(-1); baseFilters->addWidget(m_slotsMin);
        m_confirmedOnly = new QCheckBox(QString::fromUtf8("只显示已确认自然装备"), this); m_confirmedOnly->setChecked(true);
        m_confirmedOnly->setEnabled(!naturalOnlyLocked);
        m_confirmedOnly->setVisible(!charm); baseFilters->addWidget(m_confirmedOnly);
        m_showIncompatible = new QCheckBox(QString::fromUtf8("显示不适用装备"), this);
        m_showIncompatible->setEnabled(!naturalOnlyLocked);
        m_showIncompatible->setVisible(armor); baseFilters->addWidget(m_showIncompatible);
        root->addLayout(baseFilters);

        m_skillBox = new QGroupBox(QString::fromUtf8("技能点筛选（多个条件同时满足）"), this);
        QVBoxLayout *skillBoxLayout = new QVBoxLayout(m_skillBox);
        QScrollArea *skillScroll = new QScrollArea(m_skillBox);
        skillScroll->setWidgetResizable(true);
        skillScroll->setFrameShape(QFrame::NoFrame);
        skillScroll->setMinimumHeight(42);
        skillScroll->setMaximumHeight(150);
        QWidget *skillRows = new QWidget(skillScroll);
        m_skillLayout = new QGridLayout(skillRows);
        m_skillLayout->setContentsMargins(0, 0, 0, 0);
        m_skillLayout->setHorizontalSpacing(10);
        m_skillLayout->setVerticalSpacing(6);
        m_skillLayout->setColumnStretch(0, 1);
        m_skillLayout->setColumnStretch(1, 1);
        skillScroll->setWidget(skillRows);
        QPushButton *addCondition = new QPushButton(QString::fromUtf8("＋ 添加技能条件"), m_skillBox);
        skillBoxLayout->addWidget(skillScroll);
        skillBoxLayout->addWidget(addCondition);
        root->addWidget(m_skillBox);
        m_skillBox->setVisible(armor || charm);
        connect(addCondition, &QPushButton::clicked, [this]() { addSkillCondition(); scheduleRefresh(); });

        m_table = new QTableWidget(this); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
        m_table->setAlternatingRowColors(true); m_table->verticalHeader()->setVisible(false); root->addWidget(m_table, 1);
        m_detail = new QLabel(QString::fromUtf8("请选择候选装备。"), this); m_detail->setWordWrap(true); root->addWidget(m_detail);
        QHBoxLayout *pager = new QHBoxLayout;
        m_previous = new QPushButton(QString::fromUtf8("上一页"), this); m_next = new QPushButton(QString::fromUtf8("下一页"), this);
        m_pageLabel = new QLabel(this); pager->addWidget(m_previous); pager->addWidget(m_next); pager->addWidget(m_pageLabel); pager->addStretch();
        m_boxButton = new QPushButton(QString::fromUtf8("从装备箱选择…"), this);
        const bool saveLoaded = m_saveEditor && m_saveEditor->loaded() && m_saveEditor->savedata;
        m_boxButton->setVisible(!m_boxMode);
        m_boxButton->setEnabled(saveLoaded);
        m_boxButton->setToolTip(saveLoaded ? QString::fromUtf8("打开当前存档装备箱，按原页/格选择并复制已安装装饰珠。")
            : QString::fromUtf8("请先读取存档，才能从装备箱选择。"));
        pager->addWidget(m_boxButton);
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(m_boxMode
            ? QString::fromUtf8("选择此装备箱实例") : QString::fromUtf8("选择此装备"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消")); pager->addWidget(buttons); root->addLayout(pager);
        m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(200);
        connect(m_timer, &QTimer::timeout, [this]() { m_page = 0; refresh(); });
        connect(m_search, &QLineEdit::textChanged, [this]() { scheduleRefresh(); });
        connect(m_weaponType, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { scheduleRefresh(); });
        connect(m_boxButton, &QPushButton::clicked, [this]() {
            EquipmentPickerDialog boxDialog(m_expectedSaveType, m_combat, m_genderValue, m_platform,
                m_saveEditor, this, true, m_naturalOnlyLocked);
            if (boxDialog.exec() != QDialog::Accepted) return;
            m_selected = boxDialog.selectedCandidate();
            accept();
        });
        connect(m_rarityMin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { scheduleRefresh(); });
        connect(m_slotsMin, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { scheduleRefresh(); });
        connect(m_confirmedOnly, &QCheckBox::toggled, [this]() { scheduleRefresh(); });
        connect(m_showIncompatible, &QCheckBox::toggled, [this]() { scheduleRefresh(); });
        connect(m_previous, &QPushButton::clicked, [this]() { if (m_page > 0) { --m_page; refresh(); } });
        connect(m_next, &QPushButton::clicked, [this]() { if ((m_page + 1) * 200 < m_total) { ++m_page; refresh(); } });
        connect(m_table, &QTableWidget::itemSelectionChanged, [this]() { updateDetail(); });
        connect(m_table, &QTableWidget::cellDoubleClicked, [this](int, int) { acceptSelection(); });
        connect(buttons, &QDialogButtonBox::accepted, [this]() { acceptSelection(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refresh();
    }

    loadout_candidate_t selectedCandidate() const { return m_selected; }

private:
    struct skill_row_t { QWidget *widget; QComboBox *skill; QComboBox *comparison; QSpinBox *points; };
    int m_expectedSaveType, m_combat, m_genderValue;
    save_format_e m_platform;
    MH3U_SE *m_saveEditor;
    bool m_boxMode, m_naturalOnlyLocked;
    int m_page, m_total, m_selectedRow;
    QLineEdit *m_search; QComboBox *m_weaponType; QSpinBox *m_rarityMin; QSpinBox *m_slotsMin;
    QCheckBox *m_confirmedOnly; QCheckBox *m_showIncompatible; QGroupBox *m_skillBox;
    QGridLayout *m_skillLayout; QList<skill_row_t *> m_skillRows; QTableWidget *m_table;
    QLabel *m_detail; QLabel *m_pageLabel; QPushButton *m_previous; QPushButton *m_next; QPushButton *m_boxButton; QTimer *m_timer;
    QList<loadout_candidate_t> m_candidates; loadout_candidate_t m_selected;

    static loadout_slot_e slotForType(int type)
    {
        if (type == MH3U_Type::HeadType) return LoadoutHead;
        if (type == MH3U_Type::ChestType) return LoadoutChest;
        if (type == MH3U_Type::ArmsType) return LoadoutArms;
        if (type == MH3U_Type::WaistType) return LoadoutWaist;
        return LoadoutLegs;
    }
    void scheduleRefresh() { m_timer->start(); }
    QList<skill_filter_t> skillFilters() const
    {
        QList<skill_filter_t> result;
        for (int i = 0; i < m_skillRows.size(); ++i)
        {
            QComboBox *skill = m_skillRows.at(i)->skill;
            const QVariant selectedData = searchableComboBoxCurrentData(skill);
            if (!selectedData.isValid())
                continue;
            skill_filter_t filter = {selectedData.toInt(),
                (skill_comparison_e)m_skillRows.at(i)->comparison->currentData().toInt(), m_skillRows.at(i)->points->value()};
            result.append(filter);
        }
        return result;
    }
    void reflowSkillConditions()
    {
        while (QLayoutItem *item = m_skillLayout->takeAt(0))
            delete item;
        for (int i = 0; i < m_skillRows.size(); ++i)
            m_skillLayout->addWidget(m_skillRows.at(i)->widget, i / 2, i % 2);
    }
    void addSkillCondition()
    {
        skill_row_t *row = new skill_row_t;
        row->widget = new QWidget(m_skillBox); QHBoxLayout *layout = new QHBoxLayout(row->widget);
        layout->setContentsMargins(0, 0, 0, 0); row->skill = new QComboBox(row->widget);
        const QList<skill_tree_data_t> skills = GameDataRepository::instance().skillTreesDetailed();
        for (int i = 0; i < skills.size(); ++i)
            row->skill->addItem(QString("%1 (%2)").arg(skills.at(i).name, skills.at(i).english), skills.at(i).id);
        configureSearchableComboBox(row->skill);
        row->skill->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        row->skill->setMinimumContentsLength(14);
        row->skill->setMinimumWidth(180);
        row->comparison = new QComboBox(row->widget);
        const skill_comparison_e comparisons[] = {SkillGreater, SkillGreaterEqual, SkillEqual, SkillLessEqual, SkillLess};
        for (int i = 0; i < 5; ++i) row->comparison->addItem(comparisonText(comparisons[i]), comparisons[i]);
        row->points = new QSpinBox(row->widget); row->points->setRange(-128, 127); row->points->setValue(1);
        QPushButton *remove = new QPushButton(QString::fromUtf8("删除"), row->widget);
        layout->addWidget(row->skill, 1); layout->addWidget(row->comparison); layout->addWidget(row->points); layout->addWidget(remove);
        m_skillRows.append(row); reflowSkillConditions();
        connect(row->skill, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { scheduleRefresh(); });
        connect(row->comparison, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { scheduleRefresh(); });
        connect(row->points, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { scheduleRefresh(); });
        connect(remove, &QPushButton::clicked, [this, row]() {
            m_skillRows.removeOne(row); row->widget->deleteLater(); delete row; reflowSkillConditions(); scheduleRefresh();
        });
    }
    void refresh()
    {
        equipment_query_t query; query.text = m_search->text(); query.weaponType = searchableComboBoxCurrentData(m_weaponType).toInt();
        query.rarityMin = m_rarityMin->value(); query.slotsMin = m_slotsMin->value();
        query.confirmedOnly = m_confirmedOnly->isChecked(); query.skills = skillFilters();
        query.offset = m_page * 200; query.limit = 200; query.gender = m_showIncompatible->isChecked() ? -1 : m_genderValue;
        query.combat = m_showIncompatible->isChecked() ? -1 : m_combat;
        if (m_boxMode)
        {
            m_candidates.clear();
            m_total = 0;
        }
        else
            m_candidates = GameDataRepository::instance().queryCandidates(m_expectedSaveType, query, &m_total);
        if (m_boxMode && m_saveEditor && m_saveEditor->loaded() && m_saveEditor->savedata)
        {
            for (int boxIndex = 0; boxIndex < 1000; ++boxIndex)
            {
                equipment_t &raw = m_saveEditor->savedata->box[boxIndex / 100][boxIndex % 100];
                const int type = raw[0];
                const int id = raw[2] | (raw[3] << 8);
                if (!type || !id) continue;
                if (m_expectedSaveType >= 0 && type != m_expectedSaveType) continue;
                if (m_expectedSaveType < 0 && (type < 7 || type > 19 || type == 12)) continue;
                if (query.weaponType >= 0 && type != query.weaponType) continue;

                loadout_candidate_t value;
                if (type == MH3U_Type::CharmType)
                {
                    const charm_t charm = MH3U_Armory::convertEquipmentToCharm(raw);
                    value = GameDataRepository::instance().charmCandidate(charm.identifier, charm.slotsCount,
                        charm.firstSkillIdentifier, charm.firstSkillValue,
                        charm.secondSkillIdentifier, charm.secondSkillValue);
                }
                else
                    value = GameDataRepository::instance().candidate(type, id);
                if (!value.found) continue;
                if (query.confirmedOnly && (value.placeholder || !value.confirmed)) continue;
                if (m_platform == SAVE_FORMAT_WIIU && value.mh3gOnly) continue;
                if (query.rarityMin >= 0 && value.rarity < query.rarityMin) continue;
                if (query.slotsMin >= 0 && value.slotCount < query.slotsMin) continue;
                if (type >= 1 && type <= 5)
                {
                    if (query.combat >= 0 && value.combat > 0 && value.combat != query.combat) continue;
                    if (query.gender >= 0 && value.gender > 0 && value.gender != query.gender + 1) continue;
                }
                const QString decorations = equipmentDecorationSummary(raw, &value.decorations);
                const QString position = QString::fromUtf8("第%1页 第%2格 #%3")
                    .arg(boxIndex / 100 + 1).arg(boxIndex % 100 + 1).arg(boxIndex + 1);
                if (!query.text.trimmed().isEmpty() &&
                    !value.name.contains(query.text.trimmed(), Qt::CaseInsensitive) &&
                    !value.english.contains(query.text.trimmed(), Qt::CaseInsensitive) &&
                    !decorations.contains(query.text.trimmed(), Qt::CaseInsensitive) &&
                    !position.contains(query.text.trimmed(), Qt::CaseInsensitive)) continue;
                value.boxIndex = boxIndex;
                m_candidates.append(value);
            }
            m_total = m_candidates.size();
        }
        QStringList headers;
        const bool armor = m_expectedSaveType >= 1 && m_expectedSaveType <= 5;
        const bool charm = m_expectedSaveType == MH3U_Type::CharmType;
        if (m_boxMode) headers << QString::fromUtf8("页") << QString::fromUtf8("格");
        if (armor) headers << QString::fromUtf8("名称") << (m_boxMode ? QString::fromUtf8("装饰珠") : QString())
                           << QString::fromUtf8("稀有度") << QString::fromUtf8("孔")
                           << QString::fromUtf8("初始/最终防御") << QString::fromUtf8("五耐性");
        else if (charm) headers << QString::fromUtf8("护石") << (m_boxMode ? QString::fromUtf8("装饰珠") : QString())
                                << QString::fromUtf8("孔") << QString::fromUtf8("技能1") << QString::fromUtf8("技能2");
        else headers << QString::fromUtf8("名称") << (m_boxMode ? QString::fromUtf8("装饰珠") : QString())
                     << QString::fromUtf8("类型") << QString::fromUtf8("稀有度")
                     << QString::fromUtf8("攻击") << QString::fromUtf8("孔") << QString::fromUtf8("防御");
        headers.removeAll(QString());
        const QList<skill_filter_t> filters = query.skills;
        for (int i = 0; i < filters.size(); ++i) headers << skillName(filters.at(i).skillTreeId);
        headers << QString::fromUtf8("合法性");
        m_table->clear(); m_table->setColumnCount(headers.size()); m_table->setHorizontalHeaderLabels(headers);
        m_table->setRowCount(m_candidates.size());
        for (int row = 0; row < m_candidates.size(); ++row)
        {
            const loadout_candidate_t &candidate = m_candidates.at(row); int column = 0;
            if (m_boxMode)
            {
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.boxIndex / 100 + 1)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.boxIndex % 100 + 1)));
            }
            m_table->setItem(row, column++, new QTableWidgetItem(candidate.name));
            if (m_boxMode)
            {
                QStringList jewelNames;
                for (int i = 0; i < candidate.decorations.size(); ++i)
                {
                    const decoration_data_t detail = GameDataRepository::instance().decoration(candidate.decorations.at(i));
                    jewelNames << (detail.found ? detail.name : QString::fromUtf8("未知珠 #%1").arg(candidate.decorations.at(i)));
                }
                m_table->setItem(row, column++, new QTableWidgetItem(jewelNames.isEmpty()
                    ? QString::fromUtf8("无") : jewelNames.join(QString::fromUtf8(" | "))));
            }
            if (armor)
            {
                m_table->setItem(row, column++, new QTableWidgetItem(candidate.rarity < 0 ? "—" : QString::number(candidate.rarity)));
                m_table->setItem(row, column++, new QTableWidgetItem(candidate.slotCount < 0 ? "—" : QString::number(candidate.slotCount)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString("%1 / %2").arg(candidate.baseDefense).arg(candidate.maxDefense)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString("%1/%2/%3/%4/%5").arg(candidate.fireRes).arg(candidate.waterRes)
                    .arg(candidate.thunderRes).arg(candidate.iceRes).arg(candidate.dragonRes)));
            }
            else if (charm)
            {
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.slotCount)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString("%1 %2").arg(skillName(candidate.skill1Id), signedText(candidate.skill1Points))));
                m_table->setItem(row, column++, new QTableWidgetItem(QString("%1 %2").arg(skillName(candidate.skill2Id), signedText(candidate.skill2Points))));
            }
            else
            {
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.saveType)));
                m_table->setItem(row, column++, new QTableWidgetItem(candidate.rarity < 0 ? "—" : QString::number(candidate.rarity)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.attack)));
                m_table->setItem(row, column++, new QTableWidgetItem(candidate.slotCount < 0 ? "—" : QString::number(candidate.slotCount)));
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.defense)));
            }
            for (int f = 0; f < filters.size(); ++f)
                m_table->setItem(row, column++, new QTableWidgetItem(QString::number(candidate.skillPoints.value(filters.at(f).skillTreeId, 0))));
            QTableWidgetItem *status = new QTableWidgetItem(statusText(candidate, m_platform)); colorStatusItem(status, candidate, m_platform);
            m_table->setItem(row, column, status);
        }
        for (int c = 0; c < m_table->columnCount(); ++c) m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        const int nameColumn = m_boxMode ? 2 : 0;
        m_table->horizontalHeader()->setSectionResizeMode(nameColumn, QHeaderView::Stretch);
        if (m_boxMode) m_table->horizontalHeader()->setSectionResizeMode(nameColumn + 1, QHeaderView::Stretch);
        m_previous->setEnabled(!m_boxMode && m_page > 0); m_next->setEnabled(!m_boxMode && (m_page + 1) * 200 < m_total);
        m_pageLabel->setText(m_boxMode ? QString::fromUtf8("装备箱中共 %1 件匹配实例").arg(m_total)
            : QString::fromUtf8("第 %1 页，共 %2 条").arg(m_page + 1).arg(m_total));
        m_selectedRow = -1; updateDetail();
    }
    void updateDetail()
    {
        m_selectedRow = m_table->currentRow();
        if (m_selectedRow < 0 || m_selectedRow >= m_candidates.size()) { m_detail->setText(QString::fromUtf8("请选择候选装备。")); return; }
        const loadout_candidate_t &candidate = m_candidates.at(m_selectedRow);
        QStringList skills;
        QMap<int, int>::const_iterator it = candidate.skillPoints.constBegin();
        for (; it != candidate.skillPoints.constEnd(); ++it) skills << QString("%1 %2").arg(skillName(it.key()), signedText(it.value()));
        QStringList jewelNames;
        for (int i = 0; i < candidate.decorations.size(); ++i)
        {
            const decoration_data_t detail = GameDataRepository::instance().decoration(candidate.decorations.at(i));
            jewelNames << (detail.found ? detail.name : QString::fromUtf8("未知珠 #%1").arg(candidate.decorations.at(i)));
        }
        const QString boxDetail = candidate.boxIndex >= 0
            ? QString::fromUtf8(" · 装备箱第%1页第%2格 · 装饰珠：%3")
                .arg(candidate.boxIndex / 100 + 1).arg(candidate.boxIndex % 100 + 1)
                .arg(jewelNames.isEmpty() ? QString::fromUtf8("无") : jewelNames.join(QString::fromUtf8(" | ")))
            : QString();
        m_detail->setText(QString("%1 (%2) · Type %3 / ID %4 · %5%6\n%7").arg(candidate.name, candidate.english)
            .arg(candidate.saveType).arg(candidate.saveId).arg(statusText(candidate, m_platform), boxDetail, skills.join("  ")));
    }
    void acceptSelection()
    {
        updateDetail();
        if (m_selectedRow < 0 || m_selectedRow >= m_candidates.size()) return;
        m_selected = m_candidates.at(m_selectedRow);
        if (m_expectedSaveType >= 1 && m_expectedSaveType <= 6 && m_selected.saveType != m_expectedSaveType) return;
        accept();
    }
};

class CharmPickerDialog : public QDialog
{
public:
    CharmPickerDialog(const loadout_charm_t &current, save_format_e platform, MH3U_SE *saveEditor, QWidget *parent = 0)
        : QDialog(parent), m_platform(platform)
    {
        setWindowTitle(QString::fromUtf8("选择护石"));
        resize(720, 480);
        QVBoxLayout *root = new QVBoxLayout(this);
        QLabel *hint = new QLabel(QString::fromUtf8("直接选择护石类型、孔位和两项技能。非自然组合会标红，但仍可使用。"), this);
        hint->setWordWrap(true);
        root->addWidget(hint);

        QGridLayout *form = new QGridLayout;
        m_class = new QComboBox(this);
        const dataset_t *classes = MH3U_DS::charms();
        if (classes)
            for (uint32_t index = 0; index < classes->size(); ++index)
                m_class->addItem(QString::fromStdString(classes->at(index).identifier), (int)classes->at(index).count);
        configureSearchableComboBox(m_class);
        m_slots = new QComboBox(this);
        for (int slotCount = 0; slotCount <= 3; ++slotCount)
            m_slots->addItem(QString::fromUtf8("%1 孔").arg(slotCount), slotCount);
        m_skill1 = createSkillCombo();
        m_skill2 = createSkillCombo();
        m_points1 = new QSpinBox(this); m_points1->setRange(-128, 127);
        m_points2 = new QSpinBox(this); m_points2->setRange(-128, 127);

        form->addWidget(new QLabel(QString::fromUtf8("护石类型"), this), 0, 0);
        form->addWidget(m_class, 0, 1, 1, 3);
        form->addWidget(new QLabel(QString::fromUtf8("孔位数量"), this), 1, 0);
        form->addWidget(m_slots, 1, 1, 1, 3);
        form->addWidget(new QLabel(QString::fromUtf8("技能 1"), this), 2, 0);
        form->addWidget(m_skill1, 2, 1);
        form->addWidget(new QLabel(QString::fromUtf8("点数"), this), 2, 2);
        form->addWidget(m_points1, 2, 3);
        form->addWidget(new QLabel(QString::fromUtf8("技能 2"), this), 3, 0);
        form->addWidget(m_skill2, 3, 1);
        form->addWidget(new QLabel(QString::fromUtf8("点数"), this), 3, 2);
        form->addWidget(m_points2, 3, 3);
        form->setColumnStretch(1, 1);
        root->addLayout(form);

        m_status = new QLabel(this);
        m_status->setWordWrap(true);
        m_status->setMinimumHeight(150);
        root->addWidget(m_status);
        root->addStretch();
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        QPushButton *fromBox = buttons->addButton(QString::fromUtf8("从装备箱选择…"), QDialogButtonBox::ActionRole);
        const bool saveLoaded = saveEditor && saveEditor->loaded() && saveEditor->savedata;
        fromBox->setEnabled(saveLoaded);
        fromBox->setToolTip(saveLoaded ? QString::fromUtf8("按原页/格选择护石，并复制技能、孔位和装饰珠。")
            : QString::fromUtf8("请先读取存档，才能从装备箱选择护石。"));
        buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("使用此护石"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消"));
        root->addWidget(buttons);

        if (current.selected)
        {
            m_class->setCurrentIndex(qMax(0, m_class->findData(current.classId)));
            m_slots->setCurrentIndex(qMax(0, m_slots->findData(current.slotCount)));
            m_skill1->setCurrentIndex(qMax(0, m_skill1->findData(current.skill1Id)));
            m_points1->setValue(current.skill1Points);
            m_skill2->setCurrentIndex(qMax(0, m_skill2->findData(current.skill2Id)));
            m_points2->setValue(current.skill2Points);
        }

        connect(m_class, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { refreshStatus(); });
        connect(m_slots, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { refreshStatus(); });
        connect(m_skill1, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { refreshStatus(); });
        connect(m_skill2, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { refreshStatus(); });
        connect(m_points1, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { refreshStatus(); });
        connect(m_points2, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { refreshStatus(); });
        connect(fromBox, &QPushButton::clicked, [this, saveEditor]() {
            EquipmentPickerDialog boxDialog(MH3U_Type::CharmType, -1, -1, m_platform, saveEditor, this, true);
            if (boxDialog.exec() != QDialog::Accepted) return;
            m_selected = boxDialog.selectedCandidate();
            accept();
        });
        connect(buttons, &QDialogButtonBox::accepted, [this]() { m_selected = candidate(); accept(); });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refreshStatus();
    }

    loadout_candidate_t selectedCandidate() const { return m_selected; }

private:
    save_format_e m_platform;
    QComboBox *m_class;
    QComboBox *m_slots;
    QComboBox *m_skill1;
    QComboBox *m_skill2;
    QSpinBox *m_points1;
    QSpinBox *m_points2;
    QLabel *m_status;
    loadout_candidate_t m_selected;

    QComboBox *createSkillCombo()
    {
        QComboBox *combo = new QComboBox(this);
        combo->addItem(QString::fromUtf8("（无）"), 0);
        const QList<skill_tree_data_t> skills = GameDataRepository::instance().skillTreesDetailed();
        for (int index = 0; index < skills.size(); ++index)
            combo->addItem(QString("%1 (%2)").arg(skills.at(index).name, skills.at(index).english), skills.at(index).id);
        configureSearchableComboBox(combo);
        return combo;
    }

    loadout_candidate_t candidate() const
    {
        return GameDataRepository::instance().charmCandidate(
            searchableComboBoxCurrentData(m_class).toInt(), m_slots->currentData().toInt(),
            searchableComboBoxCurrentData(m_skill1).toInt(), m_points1->value(),
            searchableComboBoxCurrentData(m_skill2).toInt(), m_points2->value());
    }

    void refreshStatus()
    {
        loadout_model_t model;
        model.charm.selected = true;
        model.charm.classId = searchableComboBoxCurrentData(m_class).toInt();
        model.charm.slotCount = m_slots->currentData().toInt();
        model.charm.skill1Id = searchableComboBoxCurrentData(m_skill1).toInt();
        model.charm.skill1Points = m_points1->value();
        model.charm.skill2Id = searchableComboBoxCurrentData(m_skill2).toInt();
        model.charm.skill2Points = m_points2->value();
        equipment_t raw;
        QString error;
        if (!LoadoutCalculator::buildEquipment(model, LoadoutCharm, raw, &error))
        {
            m_status->setText(error);
            m_status->setStyleSheet("color:#7a271a;background:#fee4e2;border:1px solid #f0a09a;padding:8px;");
            return;
        }
        const equipment_validation_t validation = EquipmentValidator::validate(raw, m_platform);
        m_status->setText(QString::fromUtf8("合法性：%1\n%2").arg(validation.statusText(), validation.details()));
        m_status->setStyleSheet(validation.status == EquipmentInvalid ?
            "color:#7a271a;background:#fee4e2;border:1px solid #f0a09a;padding:8px;" :
            validation.status == EquipmentUnknown ?
            "color:#8a4b08;background:#fff3cd;border:1px solid #eccb78;padding:8px;" :
            "color:#17643a;background:#eaf8f0;border:1px solid #bce6cd;padding:8px;");
    }
};

class DecorationEditorDialog : public QDialog
{
public:
    DecorationEditorDialog(const QList<int> &current, int capacity, QWidget *parent = 0,
                           bool confirmedOnly = false)
        : QDialog(parent), m_values(current), m_capacity(capacity), m_confirmedOnly(confirmedOnly)
    {
        setWindowTitle(QString::fromUtf8("配置装饰珠")); resize(1120, 620);
        QVBoxLayout *root = new QVBoxLayout(this); m_usage = new QLabel(this); root->addWidget(m_usage);
        QHBoxLayout *filters = new QHBoxLayout; m_search = new QLineEdit(this); m_search->setPlaceholderText(QString::fromUtf8("搜索装饰珠"));
        m_skill = new QComboBox(this); m_skill->addItem(QString::fromUtf8("全部技能"), 0);
        const QList<skill_tree_data_t> skills = GameDataRepository::instance().skillTreesDetailed();
        for (int i = 0; i < skills.size(); ++i) m_skill->addItem(skills.at(i).name, skills.at(i).id);
        configureSearchableComboBox(m_skill);
        m_points = new QSpinBox(this); m_points->setRange(-128, 127); m_points->setValue(1);
        filters->addWidget(m_search, 1); filters->addWidget(m_skill); filters->addWidget(new QLabel(QString::fromUtf8("技能点 ≥"), this)); filters->addWidget(m_points);
        root->addLayout(filters);
        QSplitter *splitter = new QSplitter(this); m_candidates = new QTableWidget(splitter); m_installed = new QTableWidget(splitter);
        m_candidates->setMinimumWidth(560); m_installed->setMinimumWidth(330);
        QWidget *transferPanel = new QWidget(splitter); QVBoxLayout *transferLayout = new QVBoxLayout(transferPanel);
        transferLayout->setContentsMargins(5, 5, 5, 5); transferLayout->addStretch();
        m_slotIndicator = new QLabel(transferPanel); m_slotIndicator->setAlignment(Qt::AlignCenter);
        m_slotIndicator->setObjectName("decorationSlotIndicator");
        m_slotIndicator->setTextFormat(Qt::RichText); m_slotIndicator->setToolTip(QString::fromUtf8("○ 可装孔　● 已占孔　⊘ 不可装孔"));
        transferLayout->addWidget(m_slotIndicator);
        QPushButton *add = new QPushButton(QString::fromUtf8("→ 加入"), transferPanel);
        QPushButton *remove = new QPushButton(QString::fromUtf8("← 移除"), transferPanel);
        add->setObjectName("decorationAddButton"); remove->setObjectName("decorationRemoveButton");
        add->setToolTip(QString::fromUtf8("将左侧选中的装饰珠加入当前装备"));
        remove->setToolTip(QString::fromUtf8("移除右侧选中的装饰珠"));
        add->setMinimumWidth(92); remove->setMinimumWidth(92);
        transferLayout->addWidget(add); transferLayout->addWidget(remove); transferLayout->addStretch();
        splitter->addWidget(m_candidates); splitter->addWidget(transferPanel); splitter->addWidget(m_installed); root->addWidget(splitter, 1);
        splitter->setSizes(QList<int>() << 650 << 120 << 330);
        QHBoxLayout *actions = new QHBoxLayout; actions->addStretch();
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("应用")); buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("放弃"));
        actions->addWidget(buttons); root->addLayout(actions);
        connect(m_search, &QLineEdit::textChanged, [this]() { refreshCandidates(); });
        connect(m_skill, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { refreshCandidates(); });
        connect(m_points, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), [this]() { refreshCandidates(); });
        connect(add, &QPushButton::clicked, [this]() { addSelected(); });
        connect(m_candidates, &QTableWidget::cellDoubleClicked, [this](int, int) { addSelected(); });
        connect(remove, &QPushButton::clicked, [this]() { int row = m_installed->currentRow(); if (row >= 0 && row < m_values.size()) { m_values.removeAt(row); refreshInstalled(); } });
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        refreshCandidates(); refreshInstalled();
    }
    QList<int> values() const { return m_values; }
private:
    QList<int> m_values; int m_capacity; bool m_confirmedOnly; QLabel *m_usage; QLabel *m_slotIndicator; QLineEdit *m_search; QComboBox *m_skill;
    QSpinBox *m_points; QTableWidget *m_candidates; QTableWidget *m_installed; QList<loadout_candidate_t> m_rows;
    void refreshCandidates()
    {
        const QString search = m_search->text().trimmed(); const int skill = searchableComboBoxCurrentData(m_skill).toInt();
        const QList<loadout_candidate_t> all = GameDataRepository::instance().decorationCandidates(); m_rows.clear();
        for (int i = 0; i < all.size(); ++i)
        {
            const loadout_candidate_t &row = all.at(i);
            if (m_confirmedOnly && !row.confirmed) continue;
            if (!search.isEmpty() && !row.name.contains(search, Qt::CaseInsensitive) && !row.english.contains(search, Qt::CaseInsensitive)) continue;
            if (skill > 0 && row.skillPoints.value(skill, 0) < m_points->value()) continue;
            m_rows.append(row);
        }
        m_candidates->clear(); m_candidates->setColumnCount(2); m_candidates->setHorizontalHeaderLabels(QStringList()
            << QString::fromUtf8("名称") << QString::fromUtf8("技能"));
        m_candidates->setRowCount(m_rows.size()); m_candidates->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_candidates->setEditTriggers(QAbstractItemView::NoEditTriggers); m_candidates->verticalHeader()->setVisible(false);
        for (int r = 0; r < m_rows.size(); ++r)
        {
            const loadout_candidate_t &row = m_rows.at(r); QStringList effects;
            QMap<int, int>::const_iterator it = row.skillPoints.constBegin();
            for (; it != row.skillPoints.constEnd(); ++it) effects << QString("%1 %2").arg(skillName(it.key()), signedText(it.value()));
            m_candidates->setItem(r, 0, new QTableWidgetItem(row.name));
            m_candidates->setItem(r, 1, new QTableWidgetItem(effects.join("  ")));
        }
        m_candidates->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_candidates->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    void refreshInstalled()
    {
        int used = 0; bool unknown = false;
        m_installed->clear(); m_installed->setColumnCount(3); m_installed->setHorizontalHeaderLabels(QStringList()
            << QString::fromUtf8("已安装") << QString::fromUtf8("ID") << QString::fromUtf8("占孔")); m_installed->setRowCount(m_values.size());
        for (int i = 0; i < m_values.size(); ++i)
        {
            decoration_data_t detail = GameDataRepository::instance().decoration(m_values.at(i));
            if (detail.slotCount < 0) unknown = true; else used += detail.slotCount;
            m_installed->setItem(i, 0, new QTableWidgetItem(detail.name)); m_installed->setItem(i, 1, new QTableWidgetItem(QString::number(m_values.at(i))));
            m_installed->setItem(i, 2, new QTableWidgetItem(detail.slotCount < 0 ? "—" : QString::number(detail.slotCount)));
        }
        m_installed->setSelectionBehavior(QAbstractItemView::SelectRows); m_installed->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_installed->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        const bool over = !unknown && used > m_capacity;
        m_slotIndicator->setText(slotIndicatorText(m_capacity, used));
        m_usage->setText(QString::fromUtf8("天然孔位：%1　已占用：%2　剩余：%3%4").arg(m_capacity).arg(unknown ? QString::fromUtf8("未知") : QString::number(used))
            .arg(unknown ? QString::fromUtf8("未知") : QString::number(m_capacity - used)).arg(over ? QString::fromUtf8("　⚠ 孔位超限（仍可应用）") : QString()));
        m_usage->setStyleSheet(over || unknown ? "color:#8a4b08;background:#fff3cd;padding:6px;" : "color:#17643a;background:#eaf8f0;padding:6px;");
    }
    void addSelected()
    {
        int row = m_candidates->currentRow(); if (row < 0 || row >= m_rows.size()) return;
        if (m_values.size() >= 3) { QMessageBox::information(this, windowTitle(), QString::fromUtf8("每件装备最多记录三个装饰珠。")); return; }
        m_values.append(m_rows.at(row).saveId); refreshInstalled();
    }
};

class AutoLoadoutDialog : public QDialog
{
public:
    AutoLoadoutDialog(const loadout_model_t &current, MH3U_SE *saveEditor,
                      const std::function<void(const loadout_model_t &)> &applyResult,
                      QWidget *parent = 0)
        : QDialog(parent), m_saveEditor(saveEditor), m_applyResult(applyResult), m_fixedArmor(5),
          m_thread(0), m_worker(0), m_running(false), m_paused(false), m_closeWhenFinished(false),
          m_sortColumn(2), m_sortOrder(Qt::DescendingOrder)
    {
        qRegisterMetaType<loadout_search_result_t>("loadout_search_result_t");
        qRegisterMetaType<loadout_search_progress_t>("loadout_search_progress_t");
        setObjectName("autoLoadoutDialog");
        setWindowTitle(QString::fromUtf8("自动配装 · MH3G"));
        setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
        resize(1180, 720); setMinimumSize(980, 620);
        QHBoxLayout *root = new QHBoxLayout(this);
        QSplitter *splitter = new QSplitter(this); splitter->setObjectName("autoLoadoutSplitter"); root->addWidget(splitter);
        QScrollArea *formScroll = new QScrollArea(splitter); formScroll->setWidgetResizable(true);
        formScroll->setFrameShape(QFrame::NoFrame); formScroll->setMinimumWidth(240);
        QWidget *formPanel = new QWidget; formScroll->setWidget(formPanel);
        QVBoxLayout *formRoot = new QVBoxLayout(formPanel);
        QLabel *formTitle = new QLabel(QString::fromUtf8("搜索条件"), formPanel);
        formTitle->setStyleSheet("font-size:18px;font-weight:700;"); formRoot->addWidget(formTitle);
        QLabel *legalHint = new QLabel(QString::fromUtf8("自动部分只使用自然合法装备；手动珠子允许超孔，固定护石允许非自然技能组合。"), formPanel);
        legalHint->setWordWrap(true); legalHint->setStyleSheet("color:#8a4b08;background:#fff3cd;padding:8px;border-radius:6px;");
        formRoot->addWidget(legalHint);
        QFormLayout *form = new QFormLayout;
        QWidget *weaponField = new QWidget(formPanel); QHBoxLayout *weaponLayout = new QHBoxLayout(weaponField);
        weaponLayout->setContentsMargins(0, 0, 0, 0);
        m_weaponButton = new QPushButton(QString::fromUtf8("选择武器…"), weaponField);
        m_weaponDecorations = new QPushButton(QString::fromUtf8("珠子"), weaponField);
        m_weaponDecorations->setObjectName("autoLoadoutWeaponDecorations");
        m_weaponButton->setMinimumHeight(36); weaponLayout->addWidget(m_weaponButton, 1); weaponLayout->addWidget(m_weaponDecorations);
        form->addRow(QString::fromUtf8("武器"), weaponField);
        m_gender = new QComboBox(formPanel); m_gender->addItem(QString::fromUtf8("不限（分别计算男/女）"), -1);
        m_gender->addItem(QString::fromUtf8("男性"), 0); m_gender->addItem(QString::fromUtf8("女性"), 1);
        m_gender->setCurrentIndex(m_gender->findData(current.gender)); form->addRow(QString::fromUtf8("性别"), m_gender);
        const QList<skill_tree_data_t> trees = GameDataRepository::instance().skillTreesDetailed();
        for (int t = 0; t < trees.size(); ++t)
        {
            const QList<active_skill_data_t> active = GameDataRepository::instance().activeSkills(trees.at(t).id);
            for (int a = 0; a < active.size(); ++a)
            {
                if (active.at(a).points <= 0) continue;
                loadout_search_skill_t value = {active.at(a).id, trees.at(t).id, active.at(a).points,
                    active.at(a).name};
                m_skillValues.insert(value.activeSkillId, value);
            }
        }
        m_form = form;
        m_addSkill = new QPushButton(QString::fromUtf8("＋ 添加技能"), formPanel);
        m_addSkill->setObjectName("autoLoadoutAddSkill");
        m_form->addRow(QString(), m_addSkill);
        addSkillRow(formPanel);
        connect(m_addSkill, &QPushButton::clicked, [this, formPanel]() {
            addSkillRow(formPanel);
            if (!m_skills.isEmpty()) m_skills.last()->combo->setFocus();
        });
        m_minutes = new QSpinBox(formPanel); m_minutes->setRange(1, 60); m_minutes->setValue(1);
        m_minutes->setSuffix(QString::fromUtf8(" 分钟")); form->addRow(QString::fromUtf8("最大时间"), m_minutes);
        formRoot->addLayout(form);
        QGroupBox *fixedBox = new QGroupBox(QString::fromUtf8("固定装备（可选）"), formPanel);
        QGridLayout *fixedLayout = new QGridLayout(fixedBox);
        const QString fixedLabels[] = {QString::fromUtf8("头部"), QString::fromUtf8("胸部"),
            QString::fromUtf8("腕部"), QString::fromUtf8("腰部"), QString::fromUtf8("腿部"), QString::fromUtf8("护石")};
        for (int i = 0; i < 6; ++i)
        {
            m_fixedSelect[i] = new QPushButton(QString::fromUtf8("任意"), fixedBox);
            m_fixedSelect[i]->setObjectName(QString("autoLoadoutFixedSelect%1").arg(i));
            m_fixedClear[i] = new QPushButton(QString::fromUtf8("清除"), fixedBox);
            m_fixedClear[i]->setObjectName(QString("autoLoadoutFixedClear%1").arg(i));
            fixedLayout->addWidget(new QLabel(fixedLabels[i], fixedBox), i, 0);
            m_fixedJewels[i] = new QPushButton(QString::fromUtf8("珠子"), fixedBox);
            m_fixedJewels[i]->setObjectName(QString("autoLoadoutFixedJewels%1").arg(i));
            fixedLayout->addWidget(m_fixedSelect[i], i, 1); fixedLayout->addWidget(m_fixedJewels[i], i, 2);
            fixedLayout->addWidget(m_fixedClear[i], i, 3);
            connect(m_fixedClear[i], &QPushButton::clicked, [this, i]() { clearFixed(i); });
            connect(m_fixedSelect[i], &QPushButton::clicked, [this, i]() { chooseFixed(i); });
            connect(m_fixedJewels[i], &QPushButton::clicked, [this, i]() { editFixedDecorations(i); });
        }
        m_clearFixed = new QPushButton(QString::fromUtf8("清空固定装备"), fixedBox);
        m_clearFixed->setObjectName("autoLoadoutClearFixed");
        fixedLayout->addWidget(m_clearFixed, 6, 0, 1, 4);
        connect(m_clearFixed, &QPushButton::clicked, [this]() { clearFixed(); });
        formRoot->addWidget(fixedBox);
        QHBoxLayout *formActions = new QHBoxLayout;
        m_clear = new QPushButton(QString::fromUtf8("清空"), formPanel);
        m_start = new QPushButton(QString::fromUtf8("开始搜索"), formPanel); m_start->setObjectName("primaryButton");
        formActions->addWidget(m_clear); formActions->addWidget(m_start); formRoot->addLayout(formActions); formRoot->addStretch();

        QWidget *resultPanel = new QWidget(splitter); QVBoxLayout *resultsRoot = new QVBoxLayout(resultPanel);
        m_stage = new QLabel(QString::fromUtf8("设置条件后开始搜索。"), resultPanel); m_stage->setStyleSheet("font-size:16px;font-weight:700;");
        m_stage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        resultsRoot->addWidget(m_stage);
        m_progress = new QProgressBar(resultPanel); m_progress->setRange(0, 1000); m_progress->setValue(0);
        m_progress->setFormat(QString::fromUtf8("尚未开始")); resultsRoot->addWidget(m_progress);
        QHBoxLayout *taskRow = new QHBoxLayout;
        m_counts = new QLabel(QString::fromUtf8("已检查 0 个状态"), resultPanel); taskRow->addWidget(m_counts); taskRow->addStretch();
        m_pause = new QPushButton(QString::fromUtf8("暂停"), resultPanel); m_cancel = new QPushButton(QString::fromUtf8("取消任务"), resultPanel);
        m_pause->setEnabled(false); m_cancel->setEnabled(false); taskRow->addWidget(m_pause); taskRow->addWidget(m_cancel);
        resultsRoot->addLayout(taskRow);
        m_empty = new QLabel(QString::fromUtf8("搜索结果会显示在这里。"), resultPanel); m_empty->setAlignment(Qt::AlignCenter);
        m_empty->setStyleSheet("color:#69758a;background:#eef2f7;border:1px solid #dce3ed;border-radius:8px;padding:20px;");
        m_empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        resultsRoot->addWidget(m_empty, 1);
        m_resultsTable = new QTableWidget(resultPanel); m_resultsTable->setObjectName("autoLoadoutResults");
        m_resultsTable->setColumnCount(9);
        m_resultsTable->setHorizontalHeaderLabels(QStringList() << QString::fromUtf8("装备组合")
            << QString::fromUtf8("空余孔数") << QString::fromUtf8("防御力") << QString::fromUtf8("火抗")
            << QString::fromUtf8("水抗") << QString::fromUtf8("雷抗") << QString::fromUtf8("冰抗")
            << QString::fromUtf8("龙抗") << QString::fromUtf8("操作"));
        m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers); m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_resultsTable->setAlternatingRowColors(true); m_resultsTable->verticalHeader()->setVisible(false);
        m_resultsTable->verticalHeader()->setMinimumSectionSize(32); m_resultsTable->verticalHeader()->setDefaultSectionSize(32);
        m_resultsTable->horizontalHeader()->setSectionsClickable(true); m_resultsTable->horizontalHeader()->setSortIndicatorShown(true);
        m_resultsTable->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder); m_resultsTable->hide();
        m_resultsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < 8; ++c)
        {
            m_resultsTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
            m_resultsTable->horizontalHeaderItem(c)->setToolTip(QString::fromUtf8("点击按此数值排序"));
        }
        m_resultsTable->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Fixed);
        m_resultsTable->setColumnWidth(8, 116);
        resultsRoot->addWidget(m_resultsTable, 1);
        // Equal stretch factors preserve the explicit 2:8 initial sizes when the
        // window grows (Qt multiplies each factor by that pane's initial size).
        splitter->setStretchFactor(0, 1); splitter->setStretchFactor(1, 1);
        splitter->setCollapsible(0, false); splitter->setCollapsible(1, false);
        splitter->setSizes(QList<int>() << 236 << 944);
        m_resultRefreshTimer = new QTimer(this); m_resultRefreshTimer->setSingleShot(true);
        m_resultRefreshTimer->setInterval(500);
        connect(m_resultRefreshTimer, &QTimer::timeout, [this]() { refreshResults(); });
        m_countdownTimer = new QTimer(this); m_countdownTimer->setObjectName("autoLoadoutCountdownTimer");
        m_countdownTimer->setInterval(1000);
        m_countdownTimer->setTimerType(Qt::PreciseTimer);
        connect(m_countdownTimer, &QTimer::timeout, [this]() { updateCountdown(); });

        m_weaponDecorations->setEnabled(false);
        for (int i = 0; i < 6; ++i) refreshFixed(i);
        if (current.weapon.selected)
        {
            m_weapon = GameDataRepository::instance().candidate(current.weapon.saveType, current.weapon.saveId);
            m_weapon.decorations = current.weapon.decorations;
            refreshWeapon();
        }
        connect(m_weaponButton, &QPushButton::clicked, [this]() { chooseWeapon(); });
        connect(m_weaponDecorations, &QPushButton::clicked, [this]() { editWeaponDecorations(); });
        connect(m_clear, &QPushButton::clicked, [this]() { clearForm(); });
        connect(m_start, &QPushButton::clicked, [this]() { startSearch(); });
        connect(m_pause, &QPushButton::clicked, [this]() { togglePause(); });
        connect(m_cancel, &QPushButton::clicked, [this]() { requestCancel(false); });
        connect(m_resultsTable->horizontalHeader(), &QHeaderView::sectionClicked, [this](int section) { sortResultsBy(section); });
    }

    ~AutoLoadoutDialog() override
    {
        if (m_worker) m_worker->cancel();
        if (m_thread && m_thread->isRunning()) m_thread->wait();
        qDeleteAll(m_skills); m_skills.clear();
    }

protected:
    void reject() override
    {
        if (!m_running) { QDialog::reject(); return; }
        if (!confirmRunningClose()) return;
        m_closeWhenFinished = true; requestCancel(true);
    }
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_running) { event->accept(); return; }
        if (!confirmRunningClose())
        { event->ignore(); return; }
        event->ignore(); m_closeWhenFinished = true; requestCancel(true);
    }

private:
    MH3U_SE *m_saveEditor;
    std::function<void(const loadout_model_t &)> m_applyResult;
    loadout_candidate_t m_weapon;
    QMap<int, loadout_search_skill_t> m_skillValues;
    struct skill_row_t { QWidget *field; QLabel *label; QComboBox *combo; QPushButton *remove; };
    QComboBox *m_gender; QList<skill_row_t *> m_skills; QFormLayout *m_form; QPushButton *m_addSkill; QSpinBox *m_minutes;
    QPushButton *m_weaponButton; QPushButton *m_weaponDecorations; QPushButton *m_clear; QPushButton *m_start;
    QVector<loadout_candidate_t> m_fixedArmor;
    loadout_candidate_t m_fixedCharm;
    QPushButton *m_fixedSelect[6]; QPushButton *m_fixedJewels[6]; QPushButton *m_fixedClear[6]; QPushButton *m_clearFixed;
    QPushButton *m_pause; QPushButton *m_cancel; QLabel *m_stage; QLabel *m_counts; QLabel *m_empty;
    QProgressBar *m_progress; QTableWidget *m_resultsTable;
    QTimer *m_resultRefreshTimer; QTimer *m_countdownTimer;
    QThread *m_thread; LoadoutSearchWorker *m_worker;
    bool m_running, m_paused, m_closeWhenFinished;
    int m_sortColumn;
    Qt::SortOrder m_sortOrder;
    int m_searchMaxSeconds;
    qint64 m_uiPausedMs, m_uiPauseStartedMs;
    QElapsedTimer m_uiClock;
    QVector<loadout_search_result_t> m_results;

    bool confirmRunningClose()
    {
        return QMessageBox::question(this, QString::fromUtf8("结束自动配装？"),
            QString::fromUtf8("关闭弹窗将结束本次搜索。已添加到配装器的结果不受影响。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
    }

    void updateCountdown()
    {
        if (!m_uiClock.isValid() || m_searchMaxSeconds <= 0) return;
        const qint64 clockNow = m_paused ? m_uiPauseStartedMs : m_uiClock.elapsed();
        const qint64 elapsed = qMax<qint64>(0, clockNow - m_uiPausedMs);
        const qint64 total = (qint64)m_searchMaxSeconds * 1000;
        const qint64 remaining = qMax<qint64>(0, total - elapsed);
        m_progress->setValue(total > 0 ? (int)qBound<qint64>(0, elapsed * 1000 / total, 1000) : 0);
        m_progress->setFormat(QString::fromUtf8("已用 %1 · 剩余 %2")
            .arg(formatTime(elapsed, false), formatTime(remaining, true)));
    }

    void addSkillRow(QWidget *parent)
    {
        skill_row_t *row = new skill_row_t;
        row->field = new QWidget(parent); row->label = new QLabel(parent);
        QHBoxLayout *layout = new QHBoxLayout(row->field); layout->setContentsMargins(0, 0, 0, 0);
        row->combo = new QComboBox(row->field); row->combo->setObjectName("autoLoadoutSkill");
        row->combo->addItem(QString::fromUtf8("请选择发动技能"), 0);
        QMap<int, loadout_search_skill_t>::const_iterator item = m_skillValues.constBegin();
        for (; item != m_skillValues.constEnd(); ++item)
            row->combo->addItem(QString("%1（%2 点）").arg(item.value().name).arg(item.value().threshold), item.key());
        configureSearchableComboBox(row->combo); row->combo->setMinimumWidth(180);
        row->label->setBuddy(row->combo);
        row->remove = new QPushButton(QString::fromUtf8("删除"), row->field);
        row->remove->setObjectName("autoLoadoutRemoveSkill");
        row->remove->setMinimumWidth(52); layout->addWidget(row->combo, 1); layout->addWidget(row->remove);
        int insertAt = m_form->rowCount(); QFormLayout::ItemRole role;
        m_form->getWidgetPosition(m_addSkill, &insertAt, &role);
        m_form->insertRow(insertAt, row->label, row->field);
        m_skills.append(row); renumberSkillRows();
        connect(row->remove, &QPushButton::clicked, [this, row]() {
            if (m_skills.size() <= 1 || m_running) return;
            removeSkillRow(row);
        });
    }

    void removeSkillRow(skill_row_t *row)
    {
        if (!row || !m_skills.contains(row)) return;
        QFormLayout::TakeRowResult taken = m_form->takeRow(row->field);
        delete taken.labelItem; delete taken.fieldItem;
        m_skills.removeOne(row);
        row->label->deleteLater(); row->field->deleteLater(); delete row; renumberSkillRows();
    }

    void renumberSkillRows()
    {
        for (int i = 0; i < m_skills.size(); ++i)
        {
            skill_row_t *row = m_skills.at(i);
            row->label->setText(QString::fromUtf8("技能 %1").arg(i + 1));
            row->remove->setEnabled(m_skills.size() > 1 && !m_running);
        }
    }

    void refreshWeapon()
    {
        if (!m_weapon.found) { m_weaponButton->setText(QString::fromUtf8("选择武器…")); m_weaponButton->setToolTip(QString());
            m_weaponDecorations->setText(QString::fromUtf8("珠子")); m_weaponDecorations->setEnabled(false); return; }
        m_weaponButton->setText(QString("%1 · %2 孔").arg(m_weapon.name).arg(qMax(0, m_weapon.slotCount)));
        m_weaponButton->setToolTip(QString("%1 (%2)\nType %3 / ID %4").arg(m_weapon.name, m_weapon.english).arg(m_weapon.saveType).arg(m_weapon.saveId));
        m_weaponDecorations->setText(QString::fromUtf8("珠子 %1/%2").arg(m_weapon.decorations.size()).arg(decorationUsed(m_weapon.decorations)));
        m_weaponDecorations->setEnabled(true);
    }
    static int decorationUsed(const QList<int> &ids)
    {
        int used = 0;
        for (int i = 0; i < ids.size(); ++i)
            used += qMax(0, GameDataRepository::instance().decoration(ids.at(i)).slotCount);
        return used;
    }
    void editWeaponDecorations()
    {
        if (!m_weapon.found || m_running) return;
        DecorationEditorDialog dialog(m_weapon.decorations, qMax(0, m_weapon.slotCount), this, true);
        if (dialog.exec() == QDialog::Accepted) { m_weapon.decorations = dialog.values(); refreshWeapon(); }
    }
    void editFixedDecorations(int index)
    {
        if (m_running) return;
        if (index == 5)
        {
            if (!m_fixedCharm.found) return;
            DecorationEditorDialog dialog(m_fixedCharm.decorations, qMax(0, m_fixedCharm.slotCount), this, true);
            if (dialog.exec() == QDialog::Accepted) m_fixedCharm.decorations = dialog.values();
        }
        else
        {
            if (!m_fixedArmor[index].found) return;
            DecorationEditorDialog dialog(m_fixedArmor[index].decorations, qMax(0, m_fixedArmor[index].slotCount), this, true);
            if (dialog.exec() == QDialog::Accepted) m_fixedArmor[index].decorations = dialog.values();
        }
        refreshFixed(index);
    }
    void chooseWeapon()
    {
        const save_format_e platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
        EquipmentPickerDialog picker(-1, -1, m_gender->currentData().toInt(), platform, m_saveEditor, this);
        if (picker.exec() != QDialog::Accepted) return;
        m_weapon = picker.selectedCandidate(); refreshWeapon();
    }
    void refreshFixed(int index)
    {
        if (index == 5)
        {
            if (!m_fixedCharm.found) m_fixedSelect[index]->setText(QString::fromUtf8("任意"));
            else m_fixedSelect[index]->setText(QString("%1 · %2孔").arg(m_fixedCharm.name).arg(m_fixedCharm.slotCount));
            m_fixedJewels[index]->setText(m_fixedCharm.found ? QString::fromUtf8("珠子 %1/%2")
                .arg(m_fixedCharm.decorations.size()).arg(decorationUsed(m_fixedCharm.decorations)) : QString::fromUtf8("珠子"));
            m_fixedJewels[index]->setEnabled(m_fixedCharm.found && !m_running); return;
        }
        const loadout_candidate_t &candidate = m_fixedArmor[index];
        m_fixedSelect[index]->setText(candidate.found ? candidate.name : QString::fromUtf8("任意"));
        m_fixedSelect[index]->setToolTip(candidate.found ? QString("Type %1 / ID %2").arg(candidate.saveType).arg(candidate.saveId) : QString());
        m_fixedJewels[index]->setText(candidate.found ? QString::fromUtf8("珠子 %1/%2")
            .arg(candidate.decorations.size()).arg(decorationUsed(candidate.decorations)) : QString::fromUtf8("珠子"));
        m_fixedJewels[index]->setEnabled(candidate.found && !m_running);
    }
    void chooseFixed(int index)
    {
        const save_format_e platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
        if (index == 5)
        {
            loadout_charm_t current;
            current.selected = m_fixedCharm.found; current.classId = m_fixedCharm.classId;
            current.slotCount = m_fixedCharm.slotCount; current.skill1Id = m_fixedCharm.skill1Id;
            current.skill1Points = m_fixedCharm.skill1Points; current.skill2Id = m_fixedCharm.skill2Id;
            current.skill2Points = m_fixedCharm.skill2Points;
            CharmPickerDialog dialog(current, platform, m_saveEditor, this);
            if (dialog.exec() != QDialog::Accepted) return;
            m_fixedCharm = dialog.selectedCandidate();
            DecorationEditorDialog decorations(m_fixedCharm.decorations, qMax(0, m_fixedCharm.slotCount), this, true);
            if (decorations.exec() == QDialog::Accepted) m_fixedCharm.decorations = decorations.values();
            refreshFixed(index); return;
        }
        const int types[] = {MH3U_Type::HeadType, MH3U_Type::ChestType, MH3U_Type::ArmsType,
            MH3U_Type::WaistType, MH3U_Type::LegsType};
        const int combat = !m_weapon.found ? -1 : LoadoutCalculator::isRangedWeapon(m_weapon.saveType) ? 2 : 1;
        EquipmentPickerDialog picker(types[index], combat, m_gender->currentData().toInt(), platform,
            m_saveEditor, this, false, true);
        if (picker.exec() != QDialog::Accepted) return;
        m_fixedArmor[index] = picker.selectedCandidate();
        DecorationEditorDialog decorations(m_fixedArmor[index].decorations,
            qMax(0, m_fixedArmor[index].slotCount), this, true);
        if (decorations.exec() == QDialog::Accepted)
            m_fixedArmor[index].decorations = decorations.values();
        refreshFixed(index);
    }
    void clearFixed(int index = -1)
    {
        if (m_running) return;
        if (index < 0)
        {
            m_fixedArmor = QVector<loadout_candidate_t>(5); m_fixedCharm = loadout_candidate_t();
            for (int i = 0; i < 6; ++i) refreshFixed(i);
            return;
        }
        if (index == 5) m_fixedCharm = loadout_candidate_t(); else m_fixedArmor[index] = loadout_candidate_t();
        refreshFixed(index);
    }
    void clearForm()
    {
        if (m_running) return;
        m_weapon = loadout_candidate_t(); refreshWeapon(); m_gender->setCurrentIndex(0);
        clearFixed();
        while (m_skills.size() > 1) removeSkillRow(m_skills.last());
        if (!m_skills.isEmpty()) m_skills.first()->combo->setCurrentIndex(0);
        m_minutes->setValue(1); m_results.clear(); refreshResults();
        m_stage->setText(QString::fromUtf8("设置条件后开始搜索。")); m_progress->setValue(0); m_progress->setFormat(QString::fromUtf8("尚未开始"));
        m_counts->setText(QString::fromUtf8("已检查 0 个状态"));
    }
    void setFormEnabled(bool enabled)
    {
        m_weaponButton->setEnabled(enabled); m_weaponDecorations->setEnabled(enabled && m_weapon.found);
        m_gender->setEnabled(enabled); m_minutes->setEnabled(enabled);
        m_clear->setEnabled(enabled); m_start->setEnabled(enabled); m_addSkill->setEnabled(enabled);
        m_clearFixed->setEnabled(enabled);
        for (int i = 0; i < 6; ++i) { m_fixedSelect[i]->setEnabled(enabled); m_fixedClear[i]->setEnabled(enabled);
            m_fixedJewels[i]->setEnabled(enabled && (i == 5 ? m_fixedCharm.found : m_fixedArmor[i].found)); }
        for (int i = 0; i < m_skills.size(); ++i)
        {
            m_skills.at(i)->combo->setEnabled(enabled);
            m_skills.at(i)->remove->setEnabled(enabled && m_skills.size() > 1);
        }
    }
    void startSearch()
    {
        if (m_running) return;
        if (!m_weapon.found) { QMessageBox::information(this, windowTitle(), QString::fromUtf8("请先选择具体武器。")); return; }
        loadout_search_request_t request; request.weaponSaveType = m_weapon.saveType; request.weaponSaveId = m_weapon.saveId;
        request.gender = m_gender->currentData().toInt(); request.maxSeconds = m_minutes->value() * 60;
        request.fixedWeaponDecorations = m_weapon.decorations;
        request.fixedArmor = m_fixedArmor; request.fixedCharm = m_fixedCharm;
        request.fixedCharmSelected = m_fixedCharm.found;
        request.platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
        QSet<int> trees;
        for (int i = 0; i < m_skills.size(); ++i)
        {
            const int id = searchableComboBoxCurrentData(m_skills.at(i)->combo).toInt();
            if (!id) continue;
            const loadout_search_skill_t value = m_skillValues.value(id);
            if (trees.contains(value.skillTreeId))
            { QMessageBox::information(this, windowTitle(), QString::fromUtf8("同一技能系只能选择一个发动等级。")); return; }
            trees.insert(value.skillTreeId); request.skills.append(value);
        }
        if (request.skills.isEmpty()) { QMessageBox::information(this, windowTitle(), QString::fromUtf8("请至少选择一个需要发动的技能。")); return; }
        loadout_search_snapshot_t snapshot; QString error;
        setFormEnabled(false); m_stage->setText(QString::fromUtf8("正在准备本地候选数据…")); QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool ready = buildLoadoutSearchSnapshot(request, &snapshot, &error);
        QApplication::restoreOverrideCursor();
        if (!ready) { setFormEnabled(true); QMessageBox::critical(this, windowTitle(), error); return; }
        m_results.clear(); refreshResults(); m_running = true; m_paused = false;
        m_searchMaxSeconds = request.maxSeconds; m_uiPausedMs = 0; m_uiPauseStartedMs = 0;
        m_uiClock.start(); updateCountdown(); m_countdownTimer->start();
        m_start->setText(QString::fromUtf8("搜索进行中…"));
        m_pause->setText(QString::fromUtf8("暂停")); m_pause->setEnabled(true); m_cancel->setEnabled(true);
        m_thread = new QThread(this); m_worker = new LoadoutSearchWorker(snapshot); m_worker->moveToThread(m_thread);
        connect(m_thread, &QThread::started, m_worker, &LoadoutSearchWorker::run);
        connect(m_worker, &LoadoutSearchWorker::progress, this, [this, request](const loadout_search_progress_t &value) {
            m_stage->setText(value.stage); m_counts->setText(QString::fromUtf8("已检查 %1 个状态 · 已找到 %2 条结果").arg(value.checked).arg(m_results.size()));
            Q_UNUSED(request);
        });
        connect(m_worker, &LoadoutSearchWorker::result, this, [this](const loadout_search_result_t &value) { addResult(value); });
        connect(m_worker, &LoadoutSearchWorker::finished, this, [this](bool cancelled, bool found) {
            if (m_resultRefreshTimer->isActive()) m_resultRefreshTimer->stop();
            m_countdownTimer->stop(); updateCountdown();
            refreshResults();
            m_running = false; m_pause->setEnabled(false); m_cancel->setEnabled(false); setFormEnabled(true);
            m_start->setText(QString::fromUtf8("开始搜索"));
            if (cancelled) m_stage->setText(QString::fromUtf8("搜索已取消"));
            else if (!found || m_results.isEmpty()) { m_stage->setText(QString::fromUtf8("暂无找到合适的配装")); m_empty->setText(QString::fromUtf8("暂无找到合适的配装。可减少目标技能、降低技能等级或增加计算时间。")); m_empty->show(); }
            else m_stage->setText(QString::fromUtf8("搜索完成 · 共保留 %1 条结果").arg(m_results.size()));
            if (m_thread) m_thread->quit();
        });
        connect(m_worker, &LoadoutSearchWorker::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &QObject::destroyed, this, [this]() { m_worker = 0; });
        QThread *thread = m_thread;
        connect(thread, &QThread::finished, this, [this, thread]() {
            if (m_thread == thread) { m_thread = 0; m_worker = 0; }
            thread->deleteLater();
            if (m_closeWhenFinished) { m_closeWhenFinished = false; QTimer::singleShot(0, this, &QDialog::accept); }
        });
        m_thread->start();
    }
    static QString formatTime(qint64 milliseconds, bool roundUp)
    {
        const qint64 seconds = qMax<qint64>(0, (milliseconds + (roundUp ? 999 : 0)) / 1000);
        return QString("%1:%2").arg(seconds / 60, 2, 10, QLatin1Char('0')).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    }
    void togglePause()
    {
        if (!m_worker || !m_running) return;
        m_paused = !m_paused;
        if (m_paused)
        {
            m_uiPauseStartedMs = m_uiClock.elapsed(); m_worker->pause();
            m_pause->setText(QString::fromUtf8("继续")); m_stage->setText(QString::fromUtf8("搜索已暂停"));
        }
        else
        {
            m_uiPausedMs += m_uiClock.elapsed() - m_uiPauseStartedMs; m_worker->resume();
            m_pause->setText(QString::fromUtf8("暂停"));
        }
        updateCountdown();
    }
    void requestCancel(bool closing)
    {
        if (!m_worker || !m_running) { if (closing) accept(); return; }
        m_stage->setText(QString::fromUtf8("正在安全结束搜索…")); m_pause->setEnabled(false); m_cancel->setEnabled(false);
        m_countdownTimer->stop();
        m_worker->cancel();
    }
    void addResult(const loadout_search_result_t &value)
    {
        loadout_search_result_t evaluated = value;
        for (int i = 0; i < m_results.size(); ++i)
            if (m_results.at(i).fingerprint == evaluated.fingerprint) return;
        m_results.append(evaluated);
        std::sort(m_results.begin(), m_results.end(), [](const loadout_search_result_t &a, const loadout_search_result_t &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.fingerprint < b.fingerprint;
        });
        if (m_results.size() > 100) m_results.resize(100);
        sortResults();
        if (!m_resultRefreshTimer->isActive()) m_resultRefreshTimer->start();
    }
    QString equipmentNames(const loadout_search_result_t &result) const
    {
        return result.equipmentNames.join(QString::fromUtf8(" / "));
    }
    static int remainingSlots(const loadout_search_result_t &result)
    {
        return result.summary.totalSlots - result.summary.usedSlots;
    }
    static int totalResistance(const loadout_search_result_t &result)
    {
        return result.summary.fireRes + result.summary.waterRes + result.summary.thunderRes +
            result.summary.iceRes + result.summary.dragonRes;
    }
    static int resultValue(const loadout_search_result_t &result, int column)
    {
        switch (column)
        {
        case 1: return remainingSlots(result);
        case 2: return result.summary.maxDefense;
        case 3: return result.summary.fireRes;
        case 4: return result.summary.waterRes;
        case 5: return result.summary.thunderRes;
        case 6: return result.summary.iceRes;
        case 7: return result.summary.dragonRes;
        default: return 0;
        }
    }
    void sortResults()
    {
        const int column = m_sortColumn; const Qt::SortOrder order = m_sortOrder;
        std::sort(m_results.begin(), m_results.end(), [column, order](const loadout_search_result_t &a, const loadout_search_result_t &b) {
            const int aPrimary = resultValue(a, column), bPrimary = resultValue(b, column);
            if (aPrimary != bPrimary) return order == Qt::DescendingOrder ? aPrimary > bPrimary : aPrimary < bPrimary;
            if (a.summary.maxDefense != b.summary.maxDefense) return a.summary.maxDefense > b.summary.maxDefense;
            if (remainingSlots(a) != remainingSlots(b)) return remainingSlots(a) > remainingSlots(b);
            if (totalResistance(a) != totalResistance(b)) return totalResistance(a) > totalResistance(b);
            if (a.summary.fireRes != b.summary.fireRes) return a.summary.fireRes > b.summary.fireRes;
            if (a.summary.waterRes != b.summary.waterRes) return a.summary.waterRes > b.summary.waterRes;
            if (a.summary.thunderRes != b.summary.thunderRes) return a.summary.thunderRes > b.summary.thunderRes;
            if (a.summary.iceRes != b.summary.iceRes) return a.summary.iceRes > b.summary.iceRes;
            if (a.summary.dragonRes != b.summary.dragonRes) return a.summary.dragonRes > b.summary.dragonRes;
            return a.fingerprint < b.fingerprint;
        });
    }
    void sortResultsBy(int column)
    {
        if (column < 1 || column > 7) return;
        if (m_sortColumn == column)
            m_sortOrder = m_sortOrder == Qt::DescendingOrder ? Qt::AscendingOrder : Qt::DescendingOrder;
        else
        {
            m_sortColumn = column;
            m_sortOrder = Qt::DescendingOrder;
        }
        m_resultsTable->horizontalHeader()->setSortIndicator(m_sortColumn, m_sortOrder);
        sortResults(); refreshResults();
    }
    static QTableWidgetItem *numericItem(int value)
    {
        QTableWidgetItem *item = new QTableWidgetItem(QString::number(value));
        item->setTextAlignment(Qt::AlignCenter);
        return item;
    }
    void refreshResults()
    {
        m_resultsTable->setUpdatesEnabled(false);
        m_resultsTable->clearContents(); m_resultsTable->setRowCount(m_results.size());
        for (int row = 0; row < m_results.size(); ++row)
        {
            const loadout_search_result_t value = m_results.at(row);
            QTableWidgetItem *equipment = new QTableWidgetItem(equipmentNames(value)); equipment->setToolTip(equipment->text());
            m_resultsTable->setItem(row, 0, equipment);
            m_resultsTable->setItem(row, 1, numericItem(remainingSlots(value)));
            m_resultsTable->setItem(row, 2, numericItem(value.summary.maxDefense));
            m_resultsTable->setItem(row, 3, numericItem(value.summary.fireRes));
            m_resultsTable->setItem(row, 4, numericItem(value.summary.waterRes));
            m_resultsTable->setItem(row, 5, numericItem(value.summary.thunderRes));
            m_resultsTable->setItem(row, 6, numericItem(value.summary.iceRes));
            m_resultsTable->setItem(row, 7, numericItem(value.summary.dragonRes));
            QWidget *actionCell = new QWidget(m_resultsTable);
            QHBoxLayout *actionLayout = new QHBoxLayout(actionCell); actionLayout->setContentsMargins(3, 2, 3, 2);
            QPushButton *apply = new QPushButton(QString::fromUtf8("添加到配装器"), actionCell);
            apply->setStyleSheet("QPushButton{padding:1px 8px;min-height:0px;}");
            apply->setFixedHeight(26); actionLayout->addWidget(apply);
            connect(apply, &QPushButton::clicked, [this, value]() { m_applyResult(value.model); });
            m_resultsTable->setCellWidget(row, 8, actionCell);
        }
        const bool any = !m_results.isEmpty(); m_resultsTable->setVisible(any); m_empty->setVisible(!any);
        if (!any && !m_running) m_empty->setText(QString::fromUtf8("搜索结果会显示在这里。"));
        m_resultsTable->setUpdatesEnabled(true); m_resultsTable->viewport()->update();
    }
};

bool skillRowLess(const loadout_skill_row_t &left, const loadout_skill_row_t &right)
{
    int leftGroup = left.positiveActive ? 0 : left.negativeActive ? 1 : 2;
    int rightGroup = right.positiveActive ? 0 : right.negativeActive ? 1 : 2;
    if (leftGroup != rightGroup) return leftGroup < rightGroup;
    if (leftGroup == 2 && left.distanceToNext != right.distanceToNext) return left.distanceToNext < right.distanceToNext;
    return left.skillTreeId < right.skillTreeId;
}
}

QLoadout::QLoadout(MH3U_SE *saveEditor, QWidget *parent)
    : QWidget(parent), m_saveEditor(saveEditor), m_dirty(false), m_loading(false)
{
    setObjectName("pageSurface"); QVBoxLayout *root = new QVBoxLayout(this); root->setContentsMargins(8, 8, 8, 8);
    QHBoxLayout *toolbar = new QHBoxLayout; m_name = new QLineEdit(this); m_name->setPlaceholderText(QString::fromUtf8("配装名称"));
    m_gender = new QComboBox(this); m_gender->addItem(QString::fromUtf8("男性"), 0); m_gender->addItem(QString::fromUtf8("女性"), 1);
    QPushButton *newButton = new QPushButton(QString::fromUtf8("新建配装"), this); QPushButton *openButton = new QPushButton(QString::fromUtf8("打开配装"), this);
    QPushButton *saveButton = new QPushButton(QString::fromUtf8("导出配装"), this);
    QPushButton *automaticButton = new QPushButton(QString::fromUtf8("自动配装"), this);
    automaticButton->setObjectName("primaryButton");
    automaticButton->setAccessibleName(QString::fromUtf8("打开 MH3G 自动配装"));
    m_publish = new QPushButton(QString::fromUtf8("发布到广场"), this);
    m_apply = new QPushButton(QString::fromUtf8("一键加入装备箱"), this);
    m_apply->setObjectName("saveButton"); m_localState = new QLabel(this);
    m_detailEditControls << m_name << m_gender << newButton << openButton << saveButton << automaticButton << m_publish;
    toolbar->addWidget(m_name, 1); toolbar->addWidget(m_gender); toolbar->addWidget(newButton); toolbar->addWidget(openButton); toolbar->addWidget(saveButton); toolbar->addWidget(automaticButton); toolbar->addWidget(m_publish); toolbar->addWidget(m_apply);
    root->addLayout(toolbar); root->addWidget(m_localState);
    QHBoxLayout *cards = new QHBoxLayout; cards->setSpacing(4);
    for (int index = 0; index < LoadoutSlotCount; ++index)
    {
        loadout_slot_e slot = (loadout_slot_e)index; slot_widgets_t &widgets = m_slots[index];
        widgets.frame = new QFrame(this); widgets.frame->setObjectName("loadoutSlotCard"); QVBoxLayout *card = new QVBoxLayout(widgets.frame);
        widgets.frame->setMinimumWidth(104);
        card->setContentsMargins(5, 6, 5, 6); card->setSpacing(3); QLabel *title = new QLabel(slotLabel(slot), widgets.frame); title->setStyleSheet("font-weight:700;");
        widgets.name = new ElidedLabel(QString::fromUtf8("（未选择）"), widgets.frame); widgets.name->setWordWrap(false); widgets.name->setFixedHeight(22);
        widgets.meta = new QLabel(QString::fromUtf8("⊘ ⊘ ⊘"), widgets.frame);
        widgets.meta->setObjectName(QString("loadoutSlotIndicator%1").arg(index));
        widgets.meta->setTextFormat(Qt::RichText); widgets.meta->setStyleSheet("color:#69758a;font-size:11px;");
        widgets.select = new QPushButton(QString::fromUtf8("选择"), widgets.frame);
        widgets.decorations = new QPushButton(QString::fromUtf8("珠子"), widgets.frame); widgets.clear = new QPushButton(QString::fromUtf8("清空"), widgets.frame);
        m_detailEditControls << widgets.select << widgets.decorations << widgets.clear;
        widgets.select->setMinimumHeight(28); widgets.decorations->setMinimumHeight(28); widgets.clear->setMinimumHeight(28);
        widgets.select->setToolTip(QString::fromUtf8("选择%1").arg(slotLabel(slot))); widgets.decorations->setToolTip(QString::fromUtf8("配置装饰珠")); widgets.clear->setToolTip(QString::fromUtf8("清空此格"));
        QHBoxLayout *secondaryButtons = new QHBoxLayout; secondaryButtons->setSpacing(3);
        secondaryButtons->addWidget(widgets.decorations, 1); secondaryButtons->addWidget(widgets.clear, 1);
        card->addWidget(title); card->addWidget(widgets.name); card->addWidget(widgets.meta);
        card->addWidget(widgets.select); card->addLayout(secondaryButtons); cards->addWidget(widgets.frame, 1);
        connect(widgets.select, &QPushButton::clicked, [this, slot]() { chooseEquipment(slot); });
        connect(widgets.decorations, &QPushButton::clicked, [this, slot]() { editDecorations(slot); });
        connect(widgets.clear, &QPushButton::clicked, [this, slot]() { clearSlot(slot); });
    }
    root->addLayout(cards);
    m_showAllSkills = new QCheckBox(QString::fromUtf8("显示全部技能系"), this);
    m_detailEditControls << m_showAllSkills;
    root->addWidget(m_showAllSkills);
    QSplitter *bottom = new QSplitter(this); m_skillTable = new QTableWidget(bottom); m_skillTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_skillTable->setAlternatingRowColors(true); m_skillTable->verticalHeader()->setVisible(false); m_summary = new QLabel(bottom); m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignTop); m_summary->setMinimumWidth(230); m_summary->setObjectName("loadoutSummary"); bottom->addWidget(m_skillTable); bottom->addWidget(m_summary);
    bottom->setStretchFactor(0, 3); bottom->setStretchFactor(1, 1); root->addWidget(bottom, 1);
    connect(newButton, &QPushButton::clicked, this, &QLoadout::newLoadout); connect(openButton, &QPushButton::clicked, this, &QLoadout::openLoadout);
    connect(saveButton, &QPushButton::clicked, this, &QLoadout::saveLoadout); connect(m_apply, &QPushButton::clicked, this, &QLoadout::applyToEquipmentBox);
    connect(automaticButton, &QPushButton::clicked, this, &QLoadout::automaticLoadout);
    connect(m_publish, &QPushButton::clicked, this, &QLoadout::publishRequested);
    connect(m_name, &QLineEdit::textChanged, this, &QLoadout::nameChanged);
    connect(m_gender, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &QLoadout::genderChanged);
    connect(m_showAllSkills, &QCheckBox::toggled, [this]() { refreshSummary(); });
    refresh(); updateSaveContext();
}

bool QLoadout::hasSelections() const
{
    return m_model.weapon.selected || m_model.head.selected || m_model.chest.selected || m_model.arms.selected ||
           m_model.waist.selected || m_model.legs.selected || m_model.charm.selected;
}

void QLoadout::automaticLoadout()
{
    if (m_autoLoadoutDialog)
    {
        m_autoLoadoutDialog->showNormal();
        m_autoLoadoutDialog->raise();
        m_autoLoadoutDialog->activateWindow();
        return;
    }
    AutoLoadoutDialog *dialog = new AutoLoadoutDialog(m_model, m_saveEditor,
        [this](const loadout_model_t &model) { applyAutomaticResult(model); }, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    m_autoLoadoutDialog = dialog;
    connect(dialog, &QObject::destroyed, this, [this]() { m_autoLoadoutDialog = 0; });
    dialog->show();
}

void QLoadout::applyAutomaticResult(const loadout_model_t &model)
{
    if (m_dirty && QMessageBox::question(this, QString::fromUtf8("覆盖当前配装？"),
        QString::fromUtf8("当前配装有尚未导出的修改。是否用这条自动搜索结果替换七个装备？\n配装名称和导出路径会保留。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const QString name = m_model.name;
    m_model = model;
    m_model.name = name;
    m_loading = true;
    m_gender->setCurrentIndex(m_gender->findData(m_model.gender));
    m_loading = false;
    setDirty(true);
    refresh();
}

bool QLoadout::smokeTestLayout(QString *error) const
{
    QPushButton *automaticButton = 0;
    const QList<QPushButton *> buttons = findChildren<QPushButton *>();
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons.at(i)->text() == QString::fromUtf8("自动配装")) { automaticButton = buttons.at(i); break; }
    if (!isVisible() || !m_skillTable->isVisible() || !m_summary->isVisible() || !m_apply->isVisible() ||
        !automaticButton || !automaticButton->isVisible())
    {
        if (error) *error = QString::fromUtf8("配装器主要区域未显示。");
        return false;
    }
    for (int index = 0; index < LoadoutSlotCount; ++index)
    {
        if (!m_slots[index].frame->isVisible() ||
            m_slots[index].frame->mapTo(const_cast<QLoadout *>(this), QPoint(0, 0)).x() < 0 ||
            m_slots[index].frame->mapTo(const_cast<QLoadout *>(this), QPoint(m_slots[index].frame->width(), 0)).x() > width())
        {
            if (error) *error = QString::fromUtf8("第 %1 个配装格在默认窗口中不完整可见。").arg(index + 1);
            return false;
        }
        if (!findChild<QLabel *>(QString("loadoutSlotIndicator%1").arg(index)))
        {
            if (error) *error = QString::fromUtf8("第 %1 个配装格缺少孔位状态指示。").arg(index + 1);
            return false;
        }
    }
    if (m_skillTable->height() < 100 || m_summary->height() < 100)
    {
        if (error) *error = QString::fromUtf8("技能矩阵或汇总区域高度不足。");
        return false;
    }
    bool dynamicFormChecked = false;
    QString dynamicFormError;
    QTimer::singleShot(0, [&dynamicFormChecked, &dynamicFormError]() {
        QDialog *dialog = 0;
        const QList<QWidget *> topLevels = QApplication::topLevelWidgets();
        for (int i = 0; i < topLevels.size(); ++i)
            if (topLevels.at(i)->objectName() == "autoLoadoutDialog")
            { dialog = qobject_cast<QDialog *>(topLevels.at(i)); break; }
        if (!dialog || dialog->objectName() != "autoLoadoutDialog")
        { dynamicFormError = QString::fromUtf8("自动配装弹窗未打开。"); dynamicFormChecked = true; return; }
        QPushButton *addSkill = dialog->findChild<QPushButton *>("autoLoadoutAddSkill");
        QSplitter *splitter = dialog->findChild<QSplitter *>("autoLoadoutSplitter");
        QTimer *countdown = dialog->findChild<QTimer *>("autoLoadoutCountdownTimer");
        QTableWidget *results = dialog->findChild<QTableWidget *>("autoLoadoutResults");
        QPushButton *clearFixed = dialog->findChild<QPushButton *>("autoLoadoutClearFixed");
        QPushButton *weaponJewels = dialog->findChild<QPushButton *>("autoLoadoutWeaponDecorations");
        if (dialog->isModal() || dialog->windowModality() != Qt::NonModal)
            dynamicFormError = QString::fromUtf8("自动配装弹窗不应阻塞主界面。");
        else if (!(dialog->windowFlags() & Qt::WindowMinimizeButtonHint))
            dynamicFormError = QString::fromUtf8("自动配装弹窗缺少最小化按钮。");
        else if (!(dialog->windowFlags() & Qt::WindowMaximizeButtonHint))
            dynamicFormError = QString::fromUtf8("自动配装弹窗缺少最大化按钮。");
        else if (!splitter || splitter->sizes().size() != 2)
            dynamicFormError = QString::fromUtf8("自动配装左右分栏未建立。");
        else if (!countdown || countdown->timerType() != Qt::PreciseTimer)
            dynamicFormError = QString::fromUtf8("自动配装倒计时未使用独立的每秒定时器。");
        else if (!results || results->columnCount() != 9 ||
            results->horizontalHeaderItem(1)->text() != QString::fromUtf8("空余孔数") ||
            results->horizontalHeaderItem(2)->text() != QString::fromUtf8("防御力") ||
            results->horizontalHeaderItem(7)->text() != QString::fromUtf8("龙抗") ||
            results->horizontalHeader()->sortIndicatorSection() != 2 ||
            results->horizontalHeader()->sortIndicatorOrder() != Qt::DescendingOrder)
            dynamicFormError = QString::fromUtf8("自动配装结果列或默认排序不正确。");
        if (!dynamicFormError.isEmpty()) { dynamicFormChecked = true; dialog->reject(); return; }
        if (!weaponJewels)
            dynamicFormError = QString::fromUtf8("自动配装缺少武器珠子入口。");
        else if (!clearFixed)
            dynamicFormError = QString::fromUtf8("自动配装缺少清空固定装备按钮。");
        else
            for (int i = 0; i < 6; ++i)
                if (!dialog->findChild<QPushButton *>(QString("autoLoadoutFixedSelect%1").arg(i)))
                { dynamicFormError = QString::fromUtf8("自动配装缺少固定装备入口。"); break; }
                else if (!dialog->findChild<QPushButton *>(QString("autoLoadoutFixedJewels%1").arg(i)))
                { dynamicFormError = QString::fromUtf8("自动配装缺少固定装备珠子入口。"); break; }
        if (dynamicFormError.isEmpty() && !addSkill)
            dynamicFormError = QString::fromUtf8("自动配装缺少添加技能按钮。");
        else if (dynamicFormError.isEmpty())
        {
            for (int i = 0; i < 4; ++i) addSkill->click();
            if (dialog->findChildren<QComboBox *>("autoLoadoutSkill").size() != 5)
                dynamicFormError = QString::fromUtf8("自动配装技能条件不能持续添加。");
            const QList<QPushButton *> removeButtons = dialog->findChildren<QPushButton *>("autoLoadoutRemoveSkill");
            if (dynamicFormError.isEmpty() && !removeButtons.isEmpty()) removeButtons.last()->click();
            QCoreApplication::sendPostedEvents(0, QEvent::DeferredDelete);
            if (dynamicFormError.isEmpty() && dialog->findChildren<QComboBox *>("autoLoadoutSkill").size() != 4)
                dynamicFormError = QString::fromUtf8("自动配装技能条件不能逐行删除。");
        }
        dynamicFormChecked = true; dialog->reject();
    });
    automaticButton->click();
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(0, QEvent::DeferredDelete);
    if (!dynamicFormChecked || !dynamicFormError.isEmpty())
    {
        if (error) *error = dynamicFormError.isEmpty()
            ? QString::fromUtf8("自动配装动态技能表单未完成检查。") : dynamicFormError;
        return false;
    }
    return true;
}

void QLoadout::setDirty(bool dirty)
{
    m_dirty = dirty; m_localState->setText(dirty ? QString::fromUtf8("本地配装：未导出") :
        m_currentPath.isEmpty() ? QString::fromUtf8("本地配装：新建") : QString::fromUtf8("本地配装：已导出 · %1").arg(m_currentPath));
    m_localState->setStyleSheet(dirty ? "color:#8a4b08;background:#fff7df;padding:5px;" : "color:#17643a;background:#eaf8f0;padding:5px;");
}

bool QLoadout::maybeLeaveDirty()
{
    if (!m_dirty) return true;
    QMessageBox box(QMessageBox::Warning, QString::fromUtf8("配装尚未导出"), QString::fromUtf8("当前本地配装有尚未导出的修改。"), QMessageBox::NoButton, this);
    QAbstractButton *save = box.addButton(QString::fromUtf8("保存配装"), QMessageBox::AcceptRole);
    QAbstractButton *discard = box.addButton(QString::fromUtf8("放弃"), QMessageBox::DestructiveRole); box.addButton(QString::fromUtf8("取消"), QMessageBox::RejectRole); box.exec();
    if (box.clickedButton() == save) return saveLoadout();
    if (box.clickedButton() == discard) return true;
    return false;
}

void QLoadout::newLoadout()
{
    if (!maybeLeaveDirty()) return;
    int gender = m_model.gender;
    if (m_saveEditor && m_saveEditor->loaded()) gender = m_saveEditor->savedata->sex;
    m_model = loadout_model_t(); m_model.gender = gender; m_currentPath.clear(); m_loading = true;
    m_name->clear(); m_gender->setCurrentIndex(m_gender->findData(gender)); m_loading = false; setDirty(false); refresh();
}

void QLoadout::openLoadout()
{
    if (!maybeLeaveDirty()) return;
    QString path = QFileDialog::getOpenFileName(this, QString::fromUtf8("打开配装"), QString(), QString::fromUtf8("MH配装 (*.mhloadout.json);;JSON (*.json)"));
    if (path.isEmpty()) return;
    loadout_model_t loaded;
    bool versionWarning = false;
    QString error;
    if (!LoadoutFile::load(path, &loaded, &versionWarning, &error)) { QMessageBox::critical(this, QString::fromUtf8("打开失败"), error); return; }
    m_model = loaded; m_currentPath = path; m_loading = true; m_name->setText(m_model.name); m_gender->setCurrentIndex(m_gender->findData(m_model.gender)); m_loading = false;
    setDirty(false); refresh(); if (versionWarning) QMessageBox::warning(this, QString::fromUtf8("数据版本不同"), QString::fromUtf8("装备ID仍可解析，已使用当前数据库重新计算；请检查黄色提示。"));
}

bool QLoadout::writeLoadout(const QString &path)
{
    QString error; m_model.name = m_name->text();
    if (!LoadoutFile::save(path, m_model, &error)) { QMessageBox::critical(this, QString::fromUtf8("导出失败"), error); return false; }
    m_currentPath = path; setDirty(false); return true;
}

bool QLoadout::saveLoadout()
{
    QString path = m_currentPath;
    if (path.isEmpty()) path = QFileDialog::getSaveFileName(this, QString::fromUtf8("导出配装"), QString::fromUtf8("mh3g-loadout.mhloadout.json"), QString::fromUtf8("MH配装 (*.mhloadout.json)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(".mhloadout.json", Qt::CaseInsensitive)) path += ".mhloadout.json";
    return writeLoadout(path);
}

void QLoadout::chooseEquipment(loadout_slot_e slot)
{
    const save_format_e platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
    if (slot == LoadoutCharm)
    {
        CharmPickerDialog dialog(m_model.charm, platform, m_saveEditor, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const loadout_candidate_t candidate = dialog.selectedCandidate();
        m_model.charm.selected = true; m_model.charm.classId = candidate.classId; m_model.charm.slotCount = candidate.slotCount;
        m_model.charm.skill1Id = candidate.skill1Id; m_model.charm.skill1Points = candidate.skill1Points;
        m_model.charm.skill2Id = candidate.skill2Id; m_model.charm.skill2Points = candidate.skill2Points;
        m_model.charm.decorations = candidate.decorations;
        setDirty(); refresh();
        return;
    }
    if (slot >= LoadoutHead && slot <= LoadoutLegs && !m_model.weapon.selected)
    { QMessageBox::information(this, QString::fromUtf8("请先选择武器"), QString::fromUtf8("武器决定近战/远程防具筛选。")); return; }
    int expected = slot == LoadoutWeapon ? -1 : LoadoutCalculator::expectedSaveType(slot);
    int combat = m_model.weapon.selected ? (LoadoutCalculator::isRangedWeapon(m_model.weapon.saveType) ? 2 : 1) : -1;
    EquipmentPickerDialog dialog(expected, combat, m_model.gender, platform, m_saveEditor, this); if (dialog.exec() != QDialog::Accepted) return;
    loadout_candidate_t candidate = dialog.selectedCandidate();
    loadout_piece_t *piece = m_model.piece(slot); piece->selected = true; piece->saveType = candidate.saveType;
    piece->saveId = candidate.saveId; piece->decorations = candidate.decorations;
    setDirty(); refresh();
}

void QLoadout::editDecorations(loadout_slot_e slot)
{
    QList<int> current; int capacity = 0;
    if (slot == LoadoutCharm)
    { if (!m_model.charm.selected) return; current = m_model.charm.decorations; capacity = m_model.charm.slotCount; }
    else
    { loadout_piece_t *piece = m_model.piece(slot); if (!piece || !piece->selected) return; current = piece->decorations;
      capacity = GameDataRepository::instance().candidate(piece->saveType, piece->saveId).slotCount; }
    DecorationEditorDialog dialog(current, qMax(0, capacity), this); if (dialog.exec() != QDialog::Accepted) return;
    if (slot == LoadoutCharm) m_model.charm.decorations = dialog.values(); else m_model.piece(slot)->decorations = dialog.values();
    setDirty(); refresh();
}

void QLoadout::clearSlot(loadout_slot_e slot)
{
    if (slot == LoadoutCharm) m_model.charm = loadout_charm_t(); else { loadout_piece_t *piece = m_model.piece(slot); if (piece) *piece = loadout_piece_t(); }
    setDirty(); refresh();
}

void QLoadout::nameChanged(const QString &name) { if (!m_loading) { m_model.name = name; setDirty(); } }
void QLoadout::genderChanged(int) { if (!m_loading) { m_model.gender = m_gender->currentData().toInt(); setDirty(); refresh(); } }

void QLoadout::updateSaveContext()
{
    const bool loaded = m_saveEditor && m_saveEditor->loaded();
    if (loaded && !hasSelections() && !m_dirty)
    { m_model.gender = m_saveEditor->savedata->sex; m_loading = true; m_gender->setCurrentIndex(m_gender->findData(m_model.gender)); m_loading = false; }
    refresh();
}

QByteArray QLoadout::currentPayload(QString *error) const
{
    loadout_model_t value = m_model;
    value.name = m_name->text().trimmed();
    if (!value.complete())
    {
        if (error) *error = QString::fromUtf8("请先在配装器中选齐武器、五件防具和护石。");
        return QByteArray();
    }
    if (value.name.isEmpty())
    {
        if (error) *error = QString::fromUtf8("请先填写配装名称。");
        return QByteArray();
    }
    return LoadoutFile::serialize(value);
}

bool QLoadout::importPayload(const QByteArray &bytes, QString *error)
{
    if (!maybeLeaveDirty())
    {
        if (error) *error = QString::fromUtf8("已取消覆盖当前配装。");
        return false;
    }
    loadout_model_t loaded;
    bool versionWarning = false;
    if (!LoadoutFile::deserialize(bytes, &loaded, &versionWarning, error)) return false;
    m_model = loaded;
    m_currentPath.clear();
    m_loading = true;
    m_name->setText(m_model.name);
    m_gender->setCurrentIndex(m_gender->findData(m_model.gender));
    m_loading = false;
    setDirty(true);
    refresh();
    if (versionWarning)
        QMessageBox::warning(this, QString::fromUtf8("数据版本不同"),
            QString::fromUtf8("已使用当前数据库重新计算，请检查全部风险提示。"));
    return true;
}

bool QLoadout::showPayloadDialog(const QByteArray &bytes, QString *error)
{
    QDialog dialog(this);
    const QString loadoutName = QJsonDocument::fromJson(bytes).object().value("name").toString();
    dialog.setWindowTitle(loadoutName.isEmpty() ? QString::fromUtf8("配装详情") : QString::fromUtf8("配装详情 · %1").arg(loadoutName));
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QLoadout *editor = new QLoadout(m_saveEditor, &dialog);
    editor->m_publish->hide();
    if (!editor->importPayload(bytes, error)) return false;
    editor->setDirty(false);
    editor->setDetailReadOnlyMode();
    bool equipmentBoxModified = false;
    connect(editor, &QLoadout::saveModified, &dialog, [&equipmentBoxModified]() {
        equipmentBoxModified = true;
    });
    layout->addWidget(editor, 1);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    layout->addWidget(buttons);
    dialog.resize(1400, 820);
    dialog.setMinimumSize(1000, 680);
    dialog.exec();
    return equipmentBoxModified;
}

void QLoadout::setDetailReadOnlyMode()
{
    for (int index = 0; index < m_detailEditControls.size(); ++index)
        m_detailEditControls.at(index)->hide();
    m_localState->hide();
    m_apply->show();
    m_apply->setEnabled(true);
    const bool saveLoaded = m_saveEditor && m_saveEditor->loaded();
    m_apply->setToolTip(saveLoaded
        ? QString::fromUtf8("将这套配装一次性写入当前存档的七个空装备格。")
        : QString::fromUtf8("尚未读取存档；点击后会提示先读取存档。"));
}

void QLoadout::applyToEquipmentBox()
{
    if (!m_saveEditor || !m_saveEditor->loaded())
    {
        QMessageBox::information(this, QString::fromUtf8("尚未读取存档"),
            QString::fromUtf8("请先关闭配装详情，在主窗口点击“读取存档”；读取成功后再打开此配装并加入装备箱。"));
        return;
    }
    QList<int> indexes;
    QString error;
    if (!LoadoutSaveBridge::appendCompleteLoadout(m_model, m_saveEditor->savedata, &indexes, &error))
    { QMessageBox::critical(this, QString::fromUtf8("加入失败"), error); return; }
    emit saveModified(); QStringList writtenSlots; for (int i = 0; i < indexes.size(); ++i) writtenSlots << QString::number(indexes.at(i) + 1);
    QMessageBox::information(this, QString::fromUtf8("已加入装备箱"), QString::fromUtf8("已写入内存中的装备格：%1。\n请使用主窗口“保存修改”提交存档。").arg(writtenSlots.join(", ")));
}

void QLoadout::refresh()
{
    refreshCards(); refreshSummary(); setDirty(m_dirty);
}

void QLoadout::refreshCards()
{
    for (int index = 0; index < LoadoutSlotCount; ++index)
    {
        loadout_slot_e slot = (loadout_slot_e)index; bool selected = false; QString name = QString::fromUtf8("（未选择）");
        int capacity = -1; int jewelCount = 0; QList<int> decorations;
        equipment_validity_e cardStatus = EquipmentValid;
        loadout_candidate_t candidate;
        if (slot == LoadoutCharm)
        { selected = m_model.charm.selected; if (selected) { candidate = GameDataRepository::instance().charmCandidate(m_model.charm.classId, m_model.charm.slotCount,
              m_model.charm.skill1Id, m_model.charm.skill1Points, m_model.charm.skill2Id, m_model.charm.skill2Points);
              name = QString("%1 · %2 %3 / %4 %5").arg(candidate.name, skillName(m_model.charm.skill1Id), signedText(m_model.charm.skill1Points),
                  skillName(m_model.charm.skill2Id), signedText(m_model.charm.skill2Points)); capacity = m_model.charm.slotCount;
              decorations = m_model.charm.decorations; jewelCount = decorations.size(); } }
        else
        { const loadout_piece_t *piece = m_model.piece(slot); selected = piece && piece->selected; if (selected) { candidate = GameDataRepository::instance().candidate(piece->saveType, piece->saveId);
              name = candidate.name; capacity = candidate.slotCount; decorations = piece->decorations; jewelCount = decorations.size(); } }
        static_cast<ElidedLabel *>(m_slots[index].name)->setFullText(name); m_slots[index].name->setToolTip(selected ? QString("%1 (%2)\nType %3 / ID %4").arg(candidate.name, candidate.english).arg(candidate.saveType).arg(candidate.saveId) : QString());
        const int usedSlots = decorationSlotUsage(decorations);
        QString meta = selected ? slotMetaText(capacity, usedSlots, candidate.rarity, jewelCount)
            : slotIndicatorText(0, 0);
        m_slots[index].meta->setText(meta);
        m_slots[index].meta->setToolTip(selected ? QString::fromUtf8("○ 可装孔　● 已占孔　⊘ 不可装孔\n天然孔：%1　已占孔：%2　装饰珠：%3%4")
            .arg(capacity < 0 ? QString::fromUtf8("未知") : QString::number(capacity)).arg(usedSlots).arg(jewelCount)
            .arg(capacity >= 0 && usedSlots > capacity ? QString::fromUtf8("　⚠ 超孔") : QString()) :
            QString::fromUtf8("○ 可装孔　● 已占孔　⊘ 不可装孔"));
        m_slots[index].decorations->setEnabled(selected); m_slots[index].clear->setEnabled(selected);
        if (slot >= LoadoutHead && slot <= LoadoutLegs) m_slots[index].select->setEnabled(m_model.weapon.selected);
        if (selected)
        {
            equipment_t raw;
            QString buildError;
            if (LoadoutCalculator::buildEquipment(m_model, slot, raw, &buildError))
            {
                const save_format_e platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
                cardStatus = EquipmentValidator::validate(raw, platform, m_model.gender).status;
            }
            else cardStatus = EquipmentInvalid;
            if (slot >= LoadoutHead && slot <= LoadoutLegs && m_model.weapon.selected &&
                ((candidate.combat > 0 && candidate.combat != (LoadoutCalculator::isRangedWeapon(m_model.weapon.saveType) ? 2 : 1)) ||
                 (candidate.gender > 0 && candidate.gender != m_model.gender + 1)))
                cardStatus = EquipmentInvalid;
        }
        QString style = "QFrame#loadoutSlotCard{background:#fbfcfe;border:1px solid #b9c5d3;border-radius:8px;}";
        if (selected && (candidate.placeholder || cardStatus == EquipmentInvalid)) style = "QFrame#loadoutSlotCard{background:#fee4e2;border:2px solid #d92d20;border-radius:8px;}";
        else if (selected && (!candidate.confirmed || cardStatus == EquipmentUnknown)) style = "QFrame#loadoutSlotCard{background:#fff3cd;border:2px solid #d6a530;border-radius:8px;}";
        m_slots[index].frame->setStyleSheet(style);
    }
    const bool saveLoaded = m_saveEditor && m_saveEditor->loaded();
    m_apply->setEnabled(saveLoaded);
    m_apply->setToolTip(saveLoaded ? QString::fromUtf8("一次性写入内存中的七个空装备格；如有缺项，点击后会说明。") :
        QString::fromUtf8("请先读取 3DS 或 Wii U 存档。"));
}

void QLoadout::refreshSummary()
{
    save_format_e platform = m_saveEditor && m_saveEditor->loaded() ? m_saveEditor->format() : SAVE_FORMAT_UNKNOWN;
    loadout_summary_t summary = LoadoutCalculator::calculate(m_model, platform);
    if (m_showAllSkills->isChecked())
    {
        const QList<skill_tree_data_t> allSkills = GameDataRepository::instance().skillTreesDetailed();
        for (int index = 0; index < allSkills.size(); ++index)
        {
            bool exists = false;
            for (int row = 0; row < summary.skills.size(); ++row)
                if (summary.skills.at(row).skillTreeId == allSkills.at(index).id) { exists = true; break; }
            if (!exists)
            {
                loadout_skill_row_t row;
                row.skillTreeId = allSkills.at(index).id; row.name = allSkills.at(index).name;
                row.columns = QVector<int>(LoadoutSlotCount, 0); row.total = 0; row.distanceToNext = 0;
                row.positiveActive = false; row.negativeActive = false; summary.skills.append(row);
            }
        }
    }
    std::sort(summary.skills.begin(), summary.skills.end(), skillRowLess);
    int positiveSkills = 0;
    int negativeSkills = 0;
    m_skillTable->clear(); m_skillTable->setColumnCount(10); m_skillTable->setHorizontalHeaderLabels(QStringList()
        << QString::fromUtf8("技能系") << QString::fromUtf8("武器") << QString::fromUtf8("头") << QString::fromUtf8("胸") << QString::fromUtf8("腕")
        << QString::fromUtf8("腰") << QString::fromUtf8("腿") << QString::fromUtf8("护石") << QString::fromUtf8("合计") << QString::fromUtf8("发动技能"));
    m_skillTable->setRowCount(summary.skills.size());
    for (int row = 0; row < summary.skills.size(); ++row)
    {
        const loadout_skill_row_t &skill = summary.skills.at(row); m_skillTable->setItem(row, 0, new QTableWidgetItem(skill.name));
        for (int column = 0; column < LoadoutSlotCount; ++column)
        {
            int points = skill.columns.value(column); QTableWidgetItem *item = new QTableWidgetItem(points == 0 ? "—" : QString("%1%2").arg(points > 0 ? "+" : "").arg(points));
            item->setTextAlignment(Qt::AlignCenter); if (points > 0) item->setForeground(QColor("#1858a8")); else if (points < 0) item->setForeground(QColor("#b42318"));
            if (skill.skillTreeId == 1 && points > 0) item->setToolTip(QString::fromUtf8("胴系统倍加：复制胸部固有技能和胸部装饰珠技能。"));
            m_skillTable->setItem(row, column + 1, item);
        }
        QTableWidgetItem *total = new QTableWidgetItem(QString::number(skill.total)); total->setTextAlignment(Qt::AlignCenter); m_skillTable->setItem(row, 8, total);
        QString active = skill.activeSkill;
        if (skill.distanceToNext > 0 && !skill.nextSkill.isEmpty())
        {
            const QString distance = QString::fromUtf8("距「%1」还差 %2").arg(skill.nextSkill).arg(skill.distanceToNext);
            active = active.isEmpty() ? distance : active + QString::fromUtf8("（%1）").arg(distance);
        }
        QTableWidgetItem *activeItem = new QTableWidgetItem(active.isEmpty() ? "—" : active);
        if (skill.positiveActive) { activeItem->setForeground(QColor("#1858a8")); activeItem->setBackground(QColor("#dcecff")); }
        if (skill.negativeActive) { activeItem->setForeground(QColor("#b42318")); activeItem->setBackground(QColor("#fee4e2")); }
        if (skill.positiveActive) ++positiveSkills;
        if (skill.negativeActive) ++negativeSkills;
        m_skillTable->setItem(row, 9, activeItem);
    }
    m_skillTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch); m_skillTable->horizontalHeader()->setSectionResizeMode(9, QHeaderView::Stretch);
    for (int c = 1; c < 9; ++c) m_skillTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    QString defense = summary.defenseUnknown ? QString::fromUtf8("部分未知") : QString("%1 / %2").arg(summary.baseDefense).arg(summary.maxDefense);
    QString resist = summary.resistanceUnknown ? QString::fromUtf8("部分未知") : QString("%1 / %2 / %3 / %4 / %5").arg(summary.fireRes).arg(summary.waterRes)
        .arg(summary.thunderRes).arg(summary.iceRes).arg(summary.dragonRes);
    QString slotSummary = summary.slotsUnknown ? QString::fromUtf8("部分未知") : QString("%1 / %2 / %3").arg(summary.totalSlots).arg(summary.usedSlots).arg(summary.totalSlots - summary.usedSlots);
    QStringList diagnosticPreview = summary.diagnostics.mid(0, 5);
    for (int index = 0; index < diagnosticPreview.size(); ++index)
        diagnosticPreview[index] = diagnosticPreview.at(index).toHtmlEscaped().replace("\n", "<br>");
    m_summary->setText(QString::fromUtf8("<b>配装汇总</b><br><br>初始 / 最终防御：%1<br>武器防御加成：%2<br><br>火 / 水 / 雷 / 冰 / 龙：<br>%3<br><br>孔位 总 / 已用 / 剩余：<br>%4<br><br>发动技能：正面 %5　负面 %6<br>非法：%7　未确认：%8<br><br>%9")
        .arg(defense).arg(summary.weaponDefense).arg(resist).arg(slotSummary).arg(positiveSkills).arg(negativeSkills)
        .arg(summary.invalidCount).arg(summary.unknownCount).arg(diagnosticPreview.join("<br>")));
    m_summary->setStyleSheet(summary.invalidCount > 0 ? "color:#7a271a;background:#fee4e2;border:1px solid #f0a09a;padding:10px;" :
        summary.unknownCount > 0 ? "color:#8a4b08;background:#fff3cd;border:1px solid #eccb78;padding:10px;" :
        "color:#17643a;background:#eaf8f0;border:1px solid #bce6cd;padding:10px;");
}

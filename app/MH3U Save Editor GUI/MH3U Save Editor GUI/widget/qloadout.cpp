#include "qloadout.hpp"

#include "equipment_validator.hpp"
#include "game_data_repository.hpp"
#include "mh3u_se.hpp"
#include "../main.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

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

QString skillName(int id)
{
    const QList<skill_tree_data_t> skills = GameDataRepository::instance().skillTreesDetailed();
    for (int i = 0; i < skills.size(); ++i) if (skills.at(i).id == id) return skills.at(i).name;
    return id == 0 ? QString::fromUtf8("（无）") : QString::fromUtf8("技能 %1").arg(id);
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
                          QWidget *parent = 0)
        : QDialog(parent), m_expectedSaveType(expectedSaveType), m_combat(combat), m_genderValue(gender),
          m_platform(platform), m_page(0), m_total(0), m_selectedRow(-1)
    {
        const bool armor = expectedSaveType >= 1 && expectedSaveType <= 5;
        const bool charm = expectedSaveType == MH3U_Type::CharmType;
        setWindowTitle(armor ? QString::fromUtf8("选择%1部防具").arg(slotLabel(slotForType(expectedSaveType))) :
                       charm ? QString::fromUtf8("选择护石") : QString::fromUtf8("选择武器"));
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
        m_confirmedOnly->setVisible(!charm); baseFilters->addWidget(m_confirmedOnly);
        m_showIncompatible = new QCheckBox(QString::fromUtf8("显示不适用装备"), this);
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
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8("选择此装备"));
        buttons->button(QDialogButtonBox::Cancel)->setText(QString::fromUtf8("取消")); pager->addWidget(buttons); root->addLayout(pager);
        m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(200);
        connect(m_timer, &QTimer::timeout, [this]() { m_page = 0; refresh(); });
        connect(m_search, &QLineEdit::textChanged, [this]() { scheduleRefresh(); });
        connect(m_weaponType, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this]() { scheduleRefresh(); });
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
    int m_page, m_total, m_selectedRow;
    QLineEdit *m_search; QComboBox *m_weaponType; QSpinBox *m_rarityMin; QSpinBox *m_slotsMin;
    QCheckBox *m_confirmedOnly; QCheckBox *m_showIncompatible; QGroupBox *m_skillBox;
    QGridLayout *m_skillLayout; QList<skill_row_t *> m_skillRows; QTableWidget *m_table;
    QLabel *m_detail; QLabel *m_pageLabel; QPushButton *m_previous; QPushButton *m_next; QTimer *m_timer;
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
        m_candidates = GameDataRepository::instance().queryCandidates(m_expectedSaveType, query, &m_total);
        QStringList headers;
        const bool armor = m_expectedSaveType >= 1 && m_expectedSaveType <= 5;
        const bool charm = m_expectedSaveType == MH3U_Type::CharmType;
        if (armor) headers << QString::fromUtf8("名称") << QString::fromUtf8("稀有度") << QString::fromUtf8("孔")
                           << QString::fromUtf8("初始/最终防御") << QString::fromUtf8("五耐性");
        else if (charm) headers << QString::fromUtf8("护石") << QString::fromUtf8("孔") << QString::fromUtf8("技能1") << QString::fromUtf8("技能2");
        else headers << QString::fromUtf8("名称") << QString::fromUtf8("类型") << QString::fromUtf8("稀有度")
                     << QString::fromUtf8("攻击") << QString::fromUtf8("孔") << QString::fromUtf8("防御");
        const QList<skill_filter_t> filters = query.skills;
        for (int i = 0; i < filters.size(); ++i) headers << skillName(filters.at(i).skillTreeId);
        headers << QString::fromUtf8("合法性");
        m_table->clear(); m_table->setColumnCount(headers.size()); m_table->setHorizontalHeaderLabels(headers);
        m_table->setRowCount(m_candidates.size());
        for (int row = 0; row < m_candidates.size(); ++row)
        {
            const loadout_candidate_t &candidate = m_candidates.at(row); int column = 0;
            m_table->setItem(row, column++, new QTableWidgetItem(candidate.name));
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
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int c = 1; c < m_table->columnCount(); ++c) m_table->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
        m_previous->setEnabled(m_page > 0); m_next->setEnabled((m_page + 1) * 200 < m_total);
        m_pageLabel->setText(QString::fromUtf8("第 %1 页，共 %2 条").arg(m_page + 1).arg(m_total));
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
        m_detail->setText(QString("%1 (%2) · Type %3 / ID %4 · %5\n%6").arg(candidate.name, candidate.english)
            .arg(candidate.saveType).arg(candidate.saveId).arg(statusText(candidate, m_platform), skills.join("  ")));
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
    CharmPickerDialog(const loadout_charm_t &current, save_format_e platform, QWidget *parent = 0)
        : QDialog(parent), m_platform(platform)
    {
        setWindowTitle(QString::fromUtf8("选择护石"));
        resize(680, 330);
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
        m_status->setMinimumHeight(62);
        root->addWidget(m_status);
        root->addStretch();
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
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
    DecorationEditorDialog(const QList<int> &current, int capacity, QWidget *parent = 0)
        : QDialog(parent), m_values(current), m_capacity(capacity)
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
        m_candidates->setMinimumWidth(700); m_installed->setMinimumWidth(330);
        splitter->addWidget(m_candidates); splitter->addWidget(m_installed); root->addWidget(splitter, 1);
        splitter->setSizes(QList<int>() << 750 << 350);
        QHBoxLayout *actions = new QHBoxLayout; QPushButton *add = new QPushButton(QString::fromUtf8("加入 →"), this);
        QPushButton *remove = new QPushButton(QString::fromUtf8("移除"), this); actions->addWidget(add); actions->addWidget(remove); actions->addStretch();
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
    QList<int> m_values; int m_capacity; QLabel *m_usage; QLineEdit *m_search; QComboBox *m_skill;
    QSpinBox *m_points; QTableWidget *m_candidates; QTableWidget *m_installed; QList<loadout_candidate_t> m_rows;
    void refreshCandidates()
    {
        const QString search = m_search->text().trimmed(); const int skill = searchableComboBoxCurrentData(m_skill).toInt();
        const QList<loadout_candidate_t> all = GameDataRepository::instance().decorationCandidates(); m_rows.clear();
        for (int i = 0; i < all.size(); ++i)
        {
            const loadout_candidate_t &row = all.at(i);
            if (!search.isEmpty() && !row.name.contains(search, Qt::CaseInsensitive) && !row.english.contains(search, Qt::CaseInsensitive)) continue;
            if (skill > 0 && row.skillPoints.value(skill, 0) < m_points->value()) continue;
            m_rows.append(row);
        }
        m_candidates->clear(); m_candidates->setColumnCount(5); m_candidates->setHorizontalHeaderLabels(QStringList()
            << QString::fromUtf8("名称") << QString::fromUtf8("ID") << QString::fromUtf8("占孔") << QString::fromUtf8("技能") << QString::fromUtf8("状态"));
        m_candidates->setRowCount(m_rows.size()); m_candidates->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_candidates->setEditTriggers(QAbstractItemView::NoEditTriggers); m_candidates->verticalHeader()->setVisible(false);
        for (int r = 0; r < m_rows.size(); ++r)
        {
            const loadout_candidate_t &row = m_rows.at(r); QStringList effects;
            QMap<int, int>::const_iterator it = row.skillPoints.constBegin();
            for (; it != row.skillPoints.constEnd(); ++it) effects << QString("%1 %2").arg(skillName(it.key()), signedText(it.value()));
            m_candidates->setItem(r, 0, new QTableWidgetItem(row.name)); m_candidates->setItem(r, 1, new QTableWidgetItem(QString::number(row.saveId)));
            m_candidates->setItem(r, 2, new QTableWidgetItem(row.slotCount < 0 ? "—" : QString::number(row.slotCount)));
            m_candidates->setItem(r, 3, new QTableWidgetItem(effects.join("  "))); QTableWidgetItem *status = new QTableWidgetItem(statusText(row));
            colorStatusItem(status, row); m_candidates->setItem(r, 4, status);
        }
        m_candidates->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        m_candidates->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
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
    QPushButton *saveButton = new QPushButton(QString::fromUtf8("导出配装"), this); m_apply = new QPushButton(QString::fromUtf8("一键加入装备箱"), this);
    m_apply->setObjectName("saveButton"); m_localState = new QLabel(this);
    toolbar->addWidget(m_name, 1); toolbar->addWidget(m_gender); toolbar->addWidget(newButton); toolbar->addWidget(openButton); toolbar->addWidget(saveButton); toolbar->addWidget(m_apply);
    root->addLayout(toolbar); root->addWidget(m_localState);
    QHBoxLayout *cards = new QHBoxLayout; cards->setSpacing(4);
    for (int index = 0; index < LoadoutSlotCount; ++index)
    {
        loadout_slot_e slot = (loadout_slot_e)index; slot_widgets_t &widgets = m_slots[index];
        widgets.frame = new QFrame(this); widgets.frame->setObjectName("loadoutSlotCard"); QVBoxLayout *card = new QVBoxLayout(widgets.frame);
        widgets.frame->setMinimumWidth(104);
        card->setContentsMargins(5, 6, 5, 6); card->setSpacing(3); QLabel *title = new QLabel(slotLabel(slot), widgets.frame); title->setStyleSheet("font-weight:700;");
        widgets.name = new ElidedLabel(QString::fromUtf8("（未选择）"), widgets.frame); widgets.name->setWordWrap(false); widgets.name->setFixedHeight(22);
        widgets.meta = new QLabel(QString::fromUtf8("孔 —"), widgets.frame); widgets.meta->setStyleSheet("color:#69758a;font-size:11px;");
        widgets.select = new QPushButton(QString::fromUtf8("选择"), widgets.frame);
        widgets.decorations = new QPushButton(QString::fromUtf8("珠子"), widgets.frame); widgets.clear = new QPushButton(QString::fromUtf8("清空"), widgets.frame);
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
    root->addWidget(m_showAllSkills);
    QSplitter *bottom = new QSplitter(this); m_skillTable = new QTableWidget(bottom); m_skillTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_skillTable->setAlternatingRowColors(true); m_skillTable->verticalHeader()->setVisible(false); m_summary = new QLabel(bottom); m_summary->setWordWrap(true);
    m_summary->setAlignment(Qt::AlignTop); m_summary->setMinimumWidth(230); m_summary->setObjectName("loadoutSummary"); bottom->addWidget(m_skillTable); bottom->addWidget(m_summary);
    bottom->setStretchFactor(0, 3); bottom->setStretchFactor(1, 1); root->addWidget(bottom, 1);
    connect(newButton, &QPushButton::clicked, this, &QLoadout::newLoadout); connect(openButton, &QPushButton::clicked, this, &QLoadout::openLoadout);
    connect(saveButton, &QPushButton::clicked, this, &QLoadout::saveLoadout); connect(m_apply, &QPushButton::clicked, this, &QLoadout::applyToEquipmentBox);
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

bool QLoadout::smokeTestLayout(QString *error) const
{
    if (!isVisible() || !m_skillTable->isVisible() || !m_summary->isVisible() || !m_apply->isVisible())
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
    }
    if (m_skillTable->height() < 100 || m_summary->height() < 100)
    {
        if (error) *error = QString::fromUtf8("技能矩阵或汇总区域高度不足。");
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
        CharmPickerDialog dialog(m_model.charm, platform, this);
        if (dialog.exec() != QDialog::Accepted) return;
        const loadout_candidate_t candidate = dialog.selectedCandidate();
        m_model.charm.selected = true; m_model.charm.classId = candidate.classId; m_model.charm.slotCount = candidate.slotCount;
        m_model.charm.skill1Id = candidate.skill1Id; m_model.charm.skill1Points = candidate.skill1Points;
        m_model.charm.skill2Id = candidate.skill2Id; m_model.charm.skill2Points = candidate.skill2Points;
        m_model.charm.decorations.clear();
        setDirty(); refresh();
        return;
    }
    if (slot >= LoadoutHead && slot <= LoadoutLegs && !m_model.weapon.selected)
    { QMessageBox::information(this, QString::fromUtf8("请先选择武器"), QString::fromUtf8("武器决定近战/远程防具筛选。")); return; }
    int expected = slot == LoadoutWeapon ? -1 : LoadoutCalculator::expectedSaveType(slot);
    int combat = m_model.weapon.selected ? (LoadoutCalculator::isRangedWeapon(m_model.weapon.saveType) ? 2 : 1) : -1;
    EquipmentPickerDialog dialog(expected, combat, m_model.gender, platform, this); if (dialog.exec() != QDialog::Accepted) return;
    loadout_candidate_t candidate = dialog.selectedCandidate();
    loadout_piece_t *piece = m_model.piece(slot); piece->selected = true; piece->saveType = candidate.saveType;
    piece->saveId = candidate.saveId; piece->decorations.clear();
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

void QLoadout::applyToEquipmentBox()
{
    if (!m_saveEditor || !m_saveEditor->loaded()) return;
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
        loadout_slot_e slot = (loadout_slot_e)index; bool selected = false; QString name = QString::fromUtf8("（未选择）"); int capacity = -1; int jewelCount = 0;
        equipment_validity_e cardStatus = EquipmentValid;
        loadout_candidate_t candidate;
        if (slot == LoadoutCharm)
        { selected = m_model.charm.selected; if (selected) { candidate = GameDataRepository::instance().charmCandidate(m_model.charm.classId, m_model.charm.slotCount,
              m_model.charm.skill1Id, m_model.charm.skill1Points, m_model.charm.skill2Id, m_model.charm.skill2Points);
              name = QString("%1 · %2 %3 / %4 %5").arg(candidate.name, skillName(m_model.charm.skill1Id), signedText(m_model.charm.skill1Points),
                  skillName(m_model.charm.skill2Id), signedText(m_model.charm.skill2Points)); capacity = m_model.charm.slotCount; jewelCount = m_model.charm.decorations.size(); } }
        else
        { const loadout_piece_t *piece = m_model.piece(slot); selected = piece && piece->selected; if (selected) { candidate = GameDataRepository::instance().candidate(piece->saveType, piece->saveId);
              name = candidate.name; capacity = candidate.slotCount; jewelCount = piece->decorations.size(); } }
        static_cast<ElidedLabel *>(m_slots[index].name)->setFullText(name); m_slots[index].name->setToolTip(selected ? QString("%1 (%2)\nType %3 / ID %4").arg(candidate.name, candidate.english).arg(candidate.saveType).arg(candidate.saveId) : QString());
        QString meta = selected ? QString::fromUtf8("孔 %1 · 珠 %2").arg(capacity < 0 ? QString::fromUtf8("未知") : QString::number(capacity)).arg(jewelCount) : QString::fromUtf8("孔 —");
        if (selected && candidate.rarity >= 0) meta = QString::fromUtf8("R%1 · %2").arg(candidate.rarity).arg(meta);
        m_slots[index].meta->setText(meta);
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
    m_summary->setText(QString::fromUtf8("<b>配装汇总</b><br><br>初始 / 最终防御：%1<br>武器防御加成：%2<br><br>火 / 水 / 雷 / 冰 / 龙：<br>%3<br><br>孔位 总 / 已用 / 剩余：<br>%4<br><br>发动技能：正面 %5　负面 %6<br>非法：%7　未确认：%8<br><br>%9")
        .arg(defense).arg(summary.weaponDefense).arg(resist).arg(slotSummary).arg(positiveSkills).arg(negativeSkills)
        .arg(summary.invalidCount).arg(summary.unknownCount).arg(summary.diagnostics.mid(0, 5).join("<br>")));
    m_summary->setStyleSheet(summary.invalidCount > 0 ? "color:#7a271a;background:#fee4e2;border:1px solid #f0a09a;padding:10px;" :
        summary.unknownCount > 0 ? "color:#8a4b08;background:#fff3cd;border:1px solid #eccb78;padding:10px;" :
        "color:#17643a;background:#eaf8f0;border:1px solid #bce6cd;padding:10px;");
}

#include "encyclopedia_page.hpp"
#include "save_action_bridge.hpp"
#include "weapon_model_widget.hpp"

#include <QComboBox>
#include <QFrame>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace
{
QIcon typeIcon(const QString &name, int index)
{
    static const QColor colors[] = {QColor("#315f9c"), QColor("#9b4c55"), QColor("#347c68"),
        QColor("#7b579b"), QColor("#9a6b2e"), QColor("#39758a")};
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(colors[index % 6]);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(1, 1, 26, 26), 6, 6);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(13);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, name.left(1));
    return QIcon(pixmap);
}

QString gunlanceShell(int value)
{
    if (value < 1 || value > 15) return QString::fromUtf8("砲击 ID %1").arg(value);
    static const char *types[] = {"通常型", "扩散型", "放射型"};
    return QString::fromUtf8("%1 Lv%2").arg(QString::fromUtf8(types[(value - 1) / 5])).arg((value - 1) % 5 + 1);
}

QString switchAxePhial(int value)
{
    static const char *names[] = {"", "强击瓶", "麻痹瓶", "毒瓶", "灭龙瓶", "强属性瓶", "灭气瓶"};
    return value >= 1 && value <= 6 ? QString::fromUtf8(names[value]) : QString::fromUtf8("瓶 ID %1").arg(value);
}

QString bowCharge(int value)
{
    if (value < 16 || value > 30) return QString::fromUtf8("箭种 ID %1").arg(value);
    static const char *types[] = {"连射", "扩散", "贯通"};
    return QString::fromUtf8("%1 Lv%2").arg(QString::fromUtf8(types[(value - 16) / 5])).arg((value - 16) % 5 + 1);
}
}

SharpnessWidget::SharpnessWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(26);
    setMaximumHeight(34);
}

void SharpnessWidget::setSegments(const QVector<int> &segments)
{
    m_segments = segments;
    update();
}

void SharpnessWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), QColor("#d8dee8"));
    const QColor colors[] = {QColor("#d34b4b"), QColor("#e88a36"), QColor("#e8c84c"),
        QColor("#54a867"), QColor("#4b78c2"), QColor("#f5f7fa"), QColor("#9a67c7")};
    int total = 0;
    for (int index = 0; index < m_segments.size(); ++index) total += qMax(0, m_segments[index]);
    if (total <= 0) return;
    int left = 0;
    for (int index = 0; index < m_segments.size() && index < 7; ++index)
    {
        const int width = index == m_segments.size() - 1
            ? rect().width() - left : qRound(double(rect().width()) * m_segments[index] / total);
        painter.fillRect(left, 3, width, qMax(1, height() - 6), colors[index]);
        left += width;
    }
    painter.setPen(QColor("#66758a"));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

WeaponTreeView::WeaponTreeView(QWidget *parent) : QGraphicsView(parent)
{
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
}

void WeaponTreeView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        scale(factor, factor);
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier)
    {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - event->angleDelta().y());
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

EncyclopediaPage::EncyclopediaPage(SaveActionBridge *bridge, QWidget *parent)
    : QWidget(parent), m_bridge(bridge), m_historyIndex(-1), m_internalSelection(false),
      m_currentWeapon(-1), m_currentItem(-1), m_currentArmor(-1),
      m_characterFace(0), m_characterHair(0)
{
    setObjectName("encyclopediaPage");
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    QFrame *filters = new QFrame(this);
    filters->setObjectName("contentCard");
    filters->setFixedWidth(190);
    QVBoxLayout *filterLayout = new QVBoxLayout(filters);
    m_filterTitle = new QLabel(QString::fromUtf8("武器资料库"), filters);
    m_filterTitle->setObjectName("sectionTitle");
    m_category = new QComboBox(filters);
    m_category->setObjectName("encyclopediaCategory");
    m_category->addItem(QString::fromUtf8("武器图鉴"), "weapon");
    m_category->addItem(QString::fromUtf8("防具图鉴"), "armor");
    m_search = new QLineEdit(filters);
    m_search->setPlaceholderText(QString::fromUtf8("搜索中 / 英 / 日文名称"));
    m_rarity = new QComboBox(filters);
    m_rarity->addItem(QString::fromUtf8("全部稀有度"), 0);
    for (int rarity = 1; rarity <= 10; ++rarity) m_rarity->addItem(QString("Rare %1").arg(rarity), rarity);
    m_attribute = new QComboBox(filters);
    m_attribute->addItem(QString::fromUtf8("全部属性"), -2);
    m_types = new QListWidget(filters);
    m_armorCombat = new QComboBox(filters);
    m_armorCombat->addItem(QString::fromUtf8("全部职业"), "all");
    m_armorCombat->addItem(QString::fromUtf8("剑士"), "blade");
    m_armorCombat->addItem(QString::fromUtf8("枪手"), "gunner");
    m_armorGender = new QComboBox(filters);
    m_armorGender->addItem(QString::fromUtf8("男性"), "male");
    m_armorGender->addItem(QString::fromUtf8("女性"), "female");
    m_armorCombat->hide();
    m_armorGender->hide();
    filterLayout->addWidget(m_filterTitle);
    filterLayout->addWidget(m_category);
    filterLayout->addWidget(m_search);
    filterLayout->addWidget(m_rarity);
    filterLayout->addWidget(m_attribute);
    filterLayout->addWidget(m_armorCombat);
    filterLayout->addWidget(m_armorGender);
    filterLayout->addWidget(m_types, 1);

    QFrame *browser = new QFrame(this);
    browser->setObjectName("contentCard");
    QVBoxLayout *browserLayout = new QVBoxLayout(browser);
    QHBoxLayout *toolbar = new QHBoxLayout;
    m_back = new QPushButton(QString::fromUtf8("← 返回"), browser);
    m_forward = new QPushButton(QString::fromUtf8("前进 →"), browser);
    QPushButton *fit = new QPushButton(QString::fromUtf8("重置缩放"), browser);
    m_breadcrumb = new QLabel(QString::fromUtf8("资料库 / 武器"), browser);
    m_breadcrumb->setObjectName("sectionTitle");
    toolbar->addWidget(m_back);
    toolbar->addWidget(m_forward);
    toolbar->addWidget(fit);
    toolbar->addStretch();
    toolbar->addWidget(m_breadcrumb);
    m_scene = new QGraphicsScene(browser);
    m_tree = new WeaponTreeView(browser);
    m_tree->setScene(m_scene);
    browserLayout->addLayout(toolbar);
    browserLayout->addWidget(m_tree, 1);

    QFrame *armorBrowser = new QFrame(this);
    armorBrowser->setObjectName("contentCard");
    QVBoxLayout *armorBrowserLayout = new QVBoxLayout(armorBrowser);
    m_armorBreadcrumb = new QLabel(QString::fromUtf8("资料库 / 防具"), armorBrowser);
    m_armorBreadcrumb->setObjectName("sectionTitle");
    armorBrowserLayout->addWidget(m_armorBreadcrumb);
    m_armorScroll = new QScrollArea(armorBrowser);
    m_armorScroll->setWidgetResizable(true);
    m_armorScroll->setFrameShape(QFrame::NoFrame);
    QWidget *armorList = new QWidget(m_armorScroll);
    m_armorListLayout = new QVBoxLayout(armorList);
    m_armorListLayout->setContentsMargins(0, 0, 0, 0);
    m_armorListLayout->setSpacing(8);
    m_armorListLayout->addStretch();
    m_armorScroll->setWidget(armorList);
    armorBrowserLayout->addWidget(m_armorScroll, 1);

    m_browserStack = new QStackedWidget(this);
    m_browserStack->addWidget(browser);
    m_browserStack->addWidget(armorBrowser);

    QFrame *details = new QFrame(this);
    details->setObjectName("contentCard");
    details->setFixedWidth(370);
    QVBoxLayout *detailShell = new QVBoxLayout(details);
    QScrollArea *detailScroll = new QScrollArea(details);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    QWidget *detailBody = new QWidget(detailScroll);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailBody);
    m_modelViewer = new WeaponModelWidget(detailBody);
    m_detailTitle = new QLabel(detailBody);
    m_detailTitle->setObjectName("detailTitle");
    m_detailTitle->setWordWrap(true);
    m_detailSubtitle = new QLabel(detailBody);
    m_detailSubtitle->setObjectName("appSubtitle");
    m_detailSubtitle->setWordWrap(true);
    m_properties = new QLabel(detailBody);
    m_properties->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_properties->setWordWrap(true);
    m_sharpness = new SharpnessWidget(detailBody);
    m_materialTitle = new QLabel(QString::fromUtf8("生产素材"), detailBody);
    m_materialTitle->setObjectName("sectionTitle");
    QWidget *materialBody = new QWidget(detailBody);
    m_materialLinks = new QVBoxLayout(materialBody);
    m_materialLinks->setContentsMargins(0, 0, 0, 0);
    m_upgradeTitle = new QLabel(QString::fromUtf8("强化素材"), detailBody);
    m_upgradeTitle->setObjectName("sectionTitle");
    m_upgradeBody = new QWidget(detailBody);
    m_upgradeLinks = new QVBoxLayout(m_upgradeBody);
    m_upgradeLinks->setContentsMargins(0, 0, 0, 0);
    m_addButton = new QPushButton(QString::fromUtf8("加入装备箱"), detailBody);
    m_addButton->setObjectName("primaryButton");
    m_addButton->setEnabled(false);
    m_addButton->setToolTip(QString::fromUtf8("快速加入将在存档桥接阶段启用。"));
    m_addSetButton = new QPushButton(QString::fromUtf8("加入整套"), detailBody);
    m_addSetButton->setObjectName("primaryButton");
    m_addSetButton->hide();
    m_tryOnSetButton = new QPushButton(QString::fromUtf8("试穿整套"), detailBody);
    m_resetPreviewButton = new QPushButton(QString::fromUtf8("恢复基础装"), detailBody);
    m_tryOnSetButton->setToolTip(QString::fromUtf8("只改变右侧预览，不修改存档。"));
    m_resetPreviewButton->setToolTip(QString::fromUtf8("清空五个试穿部位，只保留基础人物、脸型和发型。"));
    QHBoxLayout *previewActions = new QHBoxLayout;
    previewActions->addWidget(m_tryOnSetButton);
    previewActions->addWidget(m_resetPreviewButton);
    m_tryOnSetButton->hide();
    m_resetPreviewButton->hide();
    detailLayout->addWidget(m_modelViewer);
    detailLayout->addLayout(previewActions);
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailSubtitle);
    detailLayout->addWidget(m_properties);
    detailLayout->addWidget(m_sharpness);
    detailLayout->addWidget(m_materialTitle);
    detailLayout->addWidget(materialBody);
    detailLayout->addWidget(m_upgradeTitle);
    detailLayout->addWidget(m_upgradeBody);
    detailLayout->addStretch();
    detailLayout->addWidget(m_addButton);
    detailLayout->addWidget(m_addSetButton);
    detailScroll->setWidget(detailBody);
    detailShell->addWidget(detailScroll);

    root->addWidget(filters);
    root->addWidget(m_browserStack, 1);
    root->addWidget(details);

    connect(m_types, SIGNAL(currentRowChanged(int)), this, SLOT(typeChanged(int)));
    connect(m_search, SIGNAL(textChanged(QString)), this, SLOT(filtersChanged()));
    connect(m_rarity, SIGNAL(currentIndexChanged(int)), this, SLOT(filtersChanged()));
    connect(m_attribute, SIGNAL(currentIndexChanged(int)), this, SLOT(filtersChanged()));
    connect(m_scene, SIGNAL(selectionChanged()), this, SLOT(sceneSelectionChanged()));
    connect(m_back, SIGNAL(clicked()), this, SLOT(goBack()));
    connect(m_forward, SIGNAL(clicked()), this, SLOT(goForward()));
    connect(fit, SIGNAL(clicked()), this, SLOT(fitTree()));
    connect(m_addButton, SIGNAL(clicked()), this, SLOT(addCurrent()));
    connect(m_addSetButton, SIGNAL(clicked()), this, SLOT(addCurrentArmorSet()));
    connect(m_tryOnSetButton, SIGNAL(clicked()), this, SLOT(tryOnCurrentArmorSet()));
    connect(m_resetPreviewButton, SIGNAL(clicked()), this, SLOT(resetFittingRoom()));
    connect(m_category, SIGNAL(currentIndexChanged(int)), this, SLOT(categoryChanged(int)));
    connect(m_armorCombat, SIGNAL(currentIndexChanged(int)), this, SLOT(armorFiltersChanged()));
    connect(m_armorGender, SIGNAL(currentIndexChanged(int)), this, SLOT(armorFiltersChanged()));

    if (!m_repository.open())
    {
        m_detailTitle->setText(QString::fromUtf8("图鉴数据加载失败"));
        m_detailSubtitle->setText(m_repository.error());
        filters->setEnabled(false);
        browser->setEnabled(false);
        return;
    }
    const QVector<EncyclopediaWeaponType> types = m_repository.weaponTypes();
    for (int index = 0; index < types.size(); ++index)
    {
        QListWidgetItem *item = new QListWidgetItem(typeIcon(types[index].name, index),
            QString("%1  ·  %2").arg(types[index].name, types[index].english), m_types);
        item->setData(Qt::UserRole, types[index].dexType);
    }
    const QVector<int> attributeIds = m_repository.attributeIds();
    for (int index = 0; index < attributeIds.size(); ++index)
    {
        if (attributeIds[index] >= 0)
            m_attribute->addItem(m_repository.attributeName(attributeIds[index]), attributeIds[index]);
    }
    m_types->setCurrentRow(0);
}

bool EncyclopediaPage::available() const { return m_repository.available(); }
QString EncyclopediaPage::error() const { return m_repository.error(); }
void EncyclopediaPage::updateSaveState()
{
    refreshAddButton();
}

void EncyclopediaPage::setCharacterAppearance(int gender, int face, int hair)
{
    const QString oldGender = selectedArmorGender();
    const QString newGender = gender == 1 ? "female" : "male";
    m_characterFace = qBound(0, face, 10);
    m_characterHair = qBound(0, hair, 13);
    if (oldGender != newGender)
    {
        m_armorGender->blockSignals(true);
        m_armorGender->setCurrentIndex(newGender == "female" ? 1 : 0);
        m_armorGender->blockSignals(false);
        for (auto it = m_previewArmorByPart.begin(); it != m_previewArmorByPart.end(); )
        {
            const EncyclopediaArmor armor = m_repository.armor(it.value());
            const EncyclopediaArmorModel model = m_repository.armorModel(it.value(), newGender);
            if (armor.gender != "both" || !model.available()) it = m_previewArmorByPart.erase(it);
            else ++it;
        }
        if (m_category->currentData().toString() == "armor") rebuildArmorList();
    }
    rebuildCharacterPreview();
}

void EncyclopediaPage::typeChanged(int)
{
    if (m_category->currentData().toString() == "armor") rebuildArmorList();
    else rebuildTree();
}

void EncyclopediaPage::categoryChanged(int)
{
    const bool armorMode = m_category->currentData().toString() == "armor";
    m_filterTitle->setText(armorMode ? QString::fromUtf8("防具资料库") : QString::fromUtf8("武器资料库"));
    m_browserStack->setCurrentIndex(armorMode ? 1 : 0);
    m_rarity->setVisible(!armorMode);
    m_attribute->setVisible(!armorMode);
    m_armorCombat->setVisible(armorMode);
    m_armorGender->setVisible(armorMode);
    m_addSetButton->setVisible(armorMode);
    m_tryOnSetButton->setVisible(armorMode);
    m_resetPreviewButton->setVisible(armorMode);
    m_types->blockSignals(true);
    m_types->clear();
    if (armorMode)
    {
        const struct { const char *name; const char *rank; } ranks[] = {
            {"下位", "low"}, {"上位", "high"}, {"G 位", "g"}, {"特殊", "special"}
        };
        for (int index = 0; index < 4; ++index)
        {
            QListWidgetItem *item = new QListWidgetItem(typeIcon(QString::fromUtf8(ranks[index].name), index),
                QString::fromUtf8(ranks[index].name), m_types);
            item->setData(Qt::UserRole, ranks[index].rank);
        }
        m_addButton->setText(QString::fromUtf8("加入当前防具"));
    }
    else
    {
        const QVector<EncyclopediaWeaponType> types = m_repository.weaponTypes();
        for (int index = 0; index < types.size(); ++index)
        {
            QListWidgetItem *item = new QListWidgetItem(typeIcon(types[index].name, index),
                QString("%1  ·  %2").arg(types[index].name, types[index].english), m_types);
            item->setData(Qt::UserRole, types[index].dexType);
        }
        m_addButton->setText(QString::fromUtf8("加入装备箱"));
    }
    m_types->blockSignals(false);
    m_types->setCurrentRow(0);
    if (armorMode) rebuildCharacterPreview();
    refreshAddButton();
}

void EncyclopediaPage::addBranch(int dexId, int depth, QMap<int, int> &depths)
{
    if (depths.contains(dexId) && depths[dexId] >= depth) return;
    depths[dexId] = depth;
    const QVector<int> children = m_repository.childIds(dexId);
    for (int index = 0; index < children.size(); ++index) addBranch(children[index], depth + 1, depths);
}

void EncyclopediaPage::assignBranchRows(int dexId, int row, QMap<int, int> &rows, int &nextRow)
{
    if (rows.contains(dexId)) return;
    rows[dexId] = row;
    nextRow = qMax(nextRow, row + 1);
    QVector<int> children = m_repository.childIds(dexId);
    std::sort(children.begin(), children.end(), [this](int left, int right) {
        const int leftOrder = m_repository.weapon(left).displayOrder;
        const int rightOrder = m_repository.weapon(right).displayOrder;
        return leftOrder == rightOrder ? left < right : leftOrder < rightOrder;
    });
    for (int index = 0; index < children.size(); ++index)
    {
        const int childRow = index == 0 ? row : nextRow;
        assignBranchRows(children[index], childRow, rows, nextRow);
    }
}

void EncyclopediaPage::rebuildTree()
{
    m_scene->clear();
    m_nodeItems.clear();
    m_depths.clear();
    const int row = m_types->currentRow();
    if (row < 0) return;
    const int dexType = m_types->item(row)->data(Qt::UserRole).toInt();
    QVector<int> roots = m_repository.rootIdsForType(dexType);
    std::sort(roots.begin(), roots.end(), [this](int left, int right) {
        const int leftOrder = m_repository.weapon(left).displayOrder;
        const int rightOrder = m_repository.weapon(right).displayOrder;
        return leftOrder == rightOrder ? left < right : leftOrder < rightOrder;
    });
    for (int index = 0; index < roots.size(); ++index) addBranch(roots[index], 0, m_depths);
    const QVector<int> weapons = m_repository.weaponIdsForType(dexType);
    QMap<int, int> rows;
    int nextRow = 0;
    for (int index = 0; index < roots.size(); ++index)
    {
        assignBranchRows(roots[index], nextRow, rows, nextRow);
        ++nextRow;
    }
    for (int index = 0; index < weapons.size(); ++index)
    {
        if (rows.contains(weapons[index])) continue;
        assignBranchRows(weapons[index], nextRow, rows, nextRow);
        ++nextRow;
    }
    for (int index = 0; index < weapons.size(); ++index)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(weapons[index]);
        const qreal x = m_depths.value(weapon.dexId, 0) * 185.0;
        const qreal y = rows.value(weapon.dexId, 0) * 70.0;
        QGraphicsRectItem *card = m_scene->addRect(QRectF(0, 0, 156, 54), QPen(QColor("#8191a6")), QBrush(QColor("#fbfdff")));
        card->setPos(x, y);
        card->setFlag(QGraphicsItem::ItemIsSelectable, true);
        card->setData(0, weapon.dexId);
        card->setToolTip(QString("%1\n%2\nDex %3 · Save %4:%5")
            .arg(weapon.name, weapon.english).arg(weapon.dexId).arg(weapon.saveType).arg(weapon.saveId));
        QGraphicsSimpleTextItem *name = new QGraphicsSimpleTextItem(weapon.name, card);
        name->setBrush(QColor("#172033"));
        name->setPos(8, 6);
        QGraphicsSimpleTextItem *stats = new QGraphicsSimpleTextItem(
            QString("R%1  ATK %2  %3孔").arg(weapon.rarity).arg(weapon.attack).arg(weapon.slotCount), card);
        stats->setBrush(QColor("#59677b"));
        stats->setPos(8, 29);
        m_nodeItems[weapon.dexId] = card;
    }
    for (int index = 0; index < weapons.size(); ++index)
    {
        const int parentId = weapons[index];
        const QVector<int> children = m_repository.childIds(parentId);
        for (int childIndex = 0; childIndex < children.size(); ++childIndex)
        {
            QGraphicsRectItem *parent = m_nodeItems.value(parentId, 0);
            QGraphicsRectItem *child = m_nodeItems.value(children[childIndex], 0);
            if (!parent || !child) continue;
            const QPointF start = parent->sceneBoundingRect().center() + QPointF(parent->rect().width() / 2.0, 0);
            const QPointF end = child->sceneBoundingRect().center() - QPointF(child->rect().width() / 2.0, 0);
            QPainterPath path(start);
            const qreal middle = (start.x() + end.x()) / 2.0;
            path.lineTo(middle, start.y());
            path.lineTo(middle, end.y());
            path.lineTo(end);
            QGraphicsPathItem *edge = m_scene->addPath(path, QPen(QColor("#a8b5c7"), 1.5));
            edge->setZValue(-1);
        }
    }
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
    applyFilters();
    fitTree();
    if (!weapons.isEmpty()) selectWeaponNode(weapons.first());
}

void EncyclopediaPage::filtersChanged()
{
    if (m_category->currentData().toString() == "armor") rebuildArmorList();
    else applyFilters();
}

void EncyclopediaPage::armorFiltersChanged()
{
    const QString gender = selectedArmorGender();
    for (auto it = m_previewArmorByPart.begin(); it != m_previewArmorByPart.end(); )
    {
        const EncyclopediaArmor armor = m_repository.armor(it.value());
        const EncyclopediaArmorModel model = m_repository.armorModel(it.value(), gender);
        if (!armorSupportsGender(armor, gender) || !model.available()) it = m_previewArmorByPart.erase(it);
        else ++it;
    }
    rebuildArmorList();
    rebuildCharacterPreview();
}

void EncyclopediaPage::applyFilters()
{
    const QString query = m_search->text().trimmed();
    const int rarity = m_rarity->currentData().toInt();
    const int attribute = m_attribute->currentData().toInt();
    int firstMatch = -1;
    for (QMap<int, QGraphicsRectItem *>::const_iterator it = m_nodeItems.constBegin(); it != m_nodeItems.constEnd(); ++it)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(it.key());
        const bool nameMatch = query.isEmpty() || weapon.name.contains(query, Qt::CaseInsensitive)
            || weapon.english.contains(query, Qt::CaseInsensitive) || weapon.japanese.contains(query, Qt::CaseInsensitive)
            || QString::number(weapon.saveId) == query;
        const bool rarityMatch = rarity == 0 || weapon.rarity == rarity;
        const bool attributeMatch = attribute == -2 || weapon.attribute1Id == attribute || weapon.attribute2Id == attribute;
        const bool match = nameMatch && rarityMatch && attributeMatch;
        it.value()->setOpacity((query.isEmpty() && rarity == 0 && attribute == -2) || match ? 1.0 : 0.16);
        if (match && firstMatch < 0) firstMatch = it.key();
    }
    if ((!query.isEmpty() || rarity != 0 || attribute != -2) && firstMatch >= 0) selectWeaponNode(firstMatch);
}

QString EncyclopediaPage::selectedArmorGender() const
{
    return m_armorGender->currentData().toString();
}

bool EncyclopediaPage::armorSupportsGender(const EncyclopediaArmor &armor, const QString &gender) const
{
    return armor.dexId > 0 && (armor.gender == "both" || armor.gender == gender);
}

bool EncyclopediaPage::tryOnArmor(int dexId)
{
    const EncyclopediaArmor armor = m_repository.armor(dexId);
    const QString gender = selectedArmorGender();
    if (!armorSupportsGender(armor, gender)) return false;
    const EncyclopediaArmorModel model = m_repository.armorModel(dexId, gender);
    if (!model.available() || model.mappingStatus == "unmapped")
    {
        m_modelViewer->showModelMessage(QString::fromUtf8("模型映射待确认\n该防具不会替换当前试穿部位"));
        return false;
    }
    m_previewArmorByPart[armor.part] = dexId;
    rebuildCharacterPreview();
    return true;
}

void EncyclopediaPage::rebuildCharacterPreview()
{
    if (!m_repository.available() || m_category->currentData().toString() != "armor") return;
    const QString gender = selectedArmorGender();
    static const QStringList parts = QStringList() << "head" << "chest" << "arms" << "waist" << "legs";
    QVector<Mh3gModelReference> components;
    QStringList keyParts;
    keyParts << gender << QString("face%1").arg(m_characterFace) << QString("hair%1").arg(m_characterHair);
    for (const QString &part : parts)
    {
        EncyclopediaArmorModel model;
        const int dexId = m_previewArmorByPart.value(part, 0);
        if (dexId > 0) model = m_repository.armorModel(dexId, gender);
        else model = m_repository.baseArmorModel(gender, part);
        if (!model.available())
        {
            m_modelViewer->showModelMessage(QString::fromUtf8("人物组件缺失：%1\n请检查 Resources v3 与图鉴数据库。")
                .arg(part), true);
            return;
        }
        Mh3gModelReference reference;
        reference.modelKey = model.modelKey;
        reference.arcRelativePath = model.arcRelativePath;
        components.append(reference);
        keyParts << QString("%1=%2").arg(part).arg(dexId > 0 ? dexId : 0);
    }
    const EncyclopediaCharacterModel face = m_repository.characterModel(gender, "face", m_characterFace);
    const EncyclopediaCharacterModel hair = m_repository.characterModel(gender, "hair", m_characterHair);
    if (face.modelKey.isEmpty() || hair.modelKey.isEmpty())
    {
        m_modelViewer->showModelMessage(QString::fromUtf8("脸型或发型资源映射缺失。"), true);
        return;
    }
    Mh3gModelReference faceRef; faceRef.modelKey = face.modelKey; faceRef.arcRelativePath = face.arcRelativePath;
    Mh3gModelReference hairRef; hairRef.modelKey = hair.modelKey; hairRef.arcRelativePath = hair.arcRelativePath;
    components.append(faceRef); components.append(hairRef);
    m_modelViewer->setCharacterModel("character|" + keyParts.join("|"), components);
}

void EncyclopediaPage::resetFittingRoom()
{
    m_previewArmorByPart.clear();
    rebuildCharacterPreview();
}

void EncyclopediaPage::tryOnCurrentArmorSet()
{
    const EncyclopediaArmorSet set = m_repository.armorSet(m_currentArmorSet);
    const QVector<int> members = visibleArmorMembers(set);
    if (members.isEmpty()) return;
    const QString gender = selectedArmorGender();
    QMap<QString, int> replacement;
    for (int dexId : members)
    {
        const EncyclopediaArmor armor = m_repository.armor(dexId);
        const EncyclopediaArmorModel model = m_repository.armorModel(dexId, gender);
        if (!armorSupportsGender(armor, gender) || !model.available() || model.mappingStatus == "unmapped")
        {
            QMessageBox::warning(this, QString::fromUtf8("无法试穿整套"),
                QString::fromUtf8("%1 的模型映射尚未确认，预览保持不变。") .arg(armor.name));
            return;
        }
        replacement[armor.part] = dexId;
    }
    m_previewArmorByPart = replacement;
    rebuildCharacterPreview();
}

QVector<int> EncyclopediaPage::visibleArmorMembers(const EncyclopediaArmorSet &set) const
{
    QVector<int> result;
    const QString gender = selectedArmorGender();
    static const QStringList partOrder = QStringList() << "head" << "chest" << "arms" << "waist" << "legs";
    for (const QString &part : partOrder)
    {
        for (int dexId : set.members)
        {
            const EncyclopediaArmor armor = m_repository.armor(dexId);
            if (armor.part == part && (armor.gender == "both" || armor.gender == gender))
            { result.append(dexId); break; }
        }
    }
    return result;
}

void EncyclopediaPage::rebuildArmorList()
{
    if (!m_repository.available() || m_category->currentData().toString() != "armor") return;
    clearLayout(m_armorListLayout);
    const QString rank = m_types->currentItem() ? m_types->currentItem()->data(Qt::UserRole).toString() : "low";
    const QString combat = m_armorCombat->currentData().toString();
    const QString search = m_search->text().trimmed();
    const QString retainedPart = m_selectedArmorPart;
    int fallback = -1;
    int retained = -1;
    static const QStringList parts = QStringList() << "head" << "chest" << "arms" << "waist" << "legs";
    for (const EncyclopediaArmorSet &set : m_repository.armorSets())
    {
        if (set.rank != rank || (combat != "all" && set.combat != "both" && set.combat != combat)) continue;
        const QVector<int> members = visibleArmorMembers(set);
        if (members.isEmpty()) continue;
        bool searchMatch = search.isEmpty() || set.name.contains(search, Qt::CaseInsensitive)
            || set.english.contains(search, Qt::CaseInsensitive);
        for (int dexId : members)
        {
            const EncyclopediaArmor armor = m_repository.armor(dexId);
            searchMatch = searchMatch || armor.name.contains(search, Qt::CaseInsensitive)
                || armor.english.contains(search, Qt::CaseInsensitive) || armor.japanese.contains(search, Qt::CaseInsensitive)
                || QString::number(armor.saveId) == search;
        }
        if (!searchMatch) continue;

        QFrame *row = new QFrame(m_armorScroll->widget());
        row->setObjectName("armorSetRow");
        QVBoxLayout *rowLayout = new QVBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 9);
        const QString rankName = set.rank == "low" ? QString::fromUtf8("下位")
            : set.rank == "high" ? QString::fromUtf8("上位")
            : set.rank == "g" ? QString::fromUtf8("G 位") : QString::fromUtf8("特殊");
        bool hasMale = false, hasFemale = false, hasBoth = false;
        for (int dexId : set.members)
        {
            const QString memberGender = m_repository.armor(dexId).gender;
            hasMale = hasMale || memberGender == "male";
            hasFemale = hasFemale || memberGender == "female";
            hasBoth = hasBoth || memberGender == "both";
        }
        const QString genderName = hasMale && !hasFemale && !hasBoth ? QString::fromUtf8("男性限定")
            : hasFemale && !hasMale && !hasBoth ? QString::fromUtf8("女性限定") : QString::fromUtf8("男女适用");
        QLabel *title = new QLabel(QString::fromUtf8("%1  ·  %2  ·  %3  ·  %4  ·  %5")
            .arg(set.name, rankName, set.combat == "blade" ? QString::fromUtf8("剑士")
                : set.combat == "gunner" ? QString::fromUtf8("枪手") : QString::fromUtf8("通用"),
                genderName, set.english), row);
        title->setObjectName("armorSetTitle");
        rowLayout->addWidget(title);
        QHBoxLayout *cards = new QHBoxLayout;
        cards->setSpacing(6);
        for (int partIndex = 0; partIndex < parts.size(); ++partIndex)
        {
            int found = -1;
            for (int dexId : members) if (m_repository.armor(dexId).part == parts[partIndex]) { found = dexId; break; }
            QPushButton *card = new QPushButton(row);
            card->setObjectName("armorPieceCard");
            card->setCheckable(found >= 0);
            card->setFixedHeight(42);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            if (found < 0)
            {
                card->setText(QString::fromUtf8("—"));
                card->setEnabled(false);
            }
            else
            {
                const EncyclopediaArmor armor = m_repository.armor(found);
                card->setText(armor.name);
                card->setToolTip(QString("%1\n%2\nDex %3 · Save %4:%5")
                    .arg(armor.name, armor.english).arg(armor.dexId).arg(armor.saveType).arg(armor.saveId));
                card->setProperty("armorDexId", found);
                card->setChecked(found == m_currentArmor);
                connect(card, &QPushButton::clicked, this, [this, found]() { selectArmor(found); });
                if (fallback < 0) fallback = found;
                if (set.setId == m_currentArmorSet && armor.part == retainedPart) retained = found;
            }
            cards->addWidget(card, 1);
        }
        rowLayout->addLayout(cards);
        m_armorListLayout->addWidget(row);
    }
    m_armorListLayout->addStretch();
    if (retained >= 0) selectArmor(retained, false);
    else if (fallback >= 0) selectArmor(fallback, false);
    else
    {
        m_currentArmor = -1; m_currentArmorSet.clear();
        m_detailTitle->setText(QString::fromUtf8("没有符合筛选条件的防具"));
        m_modelViewer->showItemPlaceholder();
        refreshAddButton();
    }
}

QString EncyclopediaPage::armorUri(const EncyclopediaArmor &armor) const
{
    return QString("mhdb://mh3g/armor/%1").arg(armor.dexId);
}

void EncyclopediaPage::selectArmor(int dexId, bool pushHistory)
{
    const EncyclopediaArmor armor = m_repository.armor(dexId);
    if (armor.dexId <= 0) return;
    if (pushHistory)
    {
        tryOnArmor(dexId);
        navigate(armorUri(armor));
    }
    else showArmor(dexId);
}

void EncyclopediaPage::showArmor(int dexId)
{
    const EncyclopediaArmor armor = m_repository.armor(dexId);
    if (armor.dexId <= 0) return;
    const EncyclopediaArmorSet set = m_repository.armorSet(armor.setId);
    m_currentArmor = dexId; m_currentArmorSet = armor.setId; m_selectedArmorPart = armor.part;
    m_currentWeapon = -1; m_currentItem = -1;
    m_addSetButton->show();
    m_tryOnSetButton->show();
    m_resetPreviewButton->show();
    const QList<QPushButton *> cards = m_armorScroll->widget()->findChildren<QPushButton *>("armorPieceCard");
    for (QPushButton *card : cards) card->setChecked(card->property("armorDexId").toInt() == dexId);
    m_detailTitle->setText(armor.name);
    m_detailSubtitle->setText(QString("%1\n%2\n%3").arg(armor.english, armor.japanese, armorUri(armor)));
    const QString combat = armor.combat == "blade" ? QString::fromUtf8("剑士")
        : armor.combat == "gunner" ? QString::fromUtf8("枪手") : QString::fromUtf8("通用");
    const QString gender = armor.gender == "male" ? QString::fromUtf8("男性")
        : armor.gender == "female" ? QString::fromUtf8("女性") : QString::fromUtf8("男女通用");
    const EncyclopediaArmorModel previewModel = m_repository.armorModel(dexId, selectedArmorGender());
    const QString modelState = armor.modelMappingStatus == "confirmed_exefs"
        ? QString::fromUtf8("游戏参数确认")
        : armor.modelMappingStatus == "exact_shared_appearance"
            ? QString::fromUtf8("完全一致的共享外观") : QString::fromUtf8("待确认");
    m_properties->setText(QString::fromUtf8(
        "套装：%1\n稀有度：%2 · 孔位：%3\n职业：%4 · 性别：%5\n防御：%6 → %7\n"
        "耐性：火 %8 / 水 %9 / 冰 %10 / 雷 %11 / 龙 %12\n生产价格：%13 z\n"
        "存档映射：类型 %14 / ID %15\n模型映射：%16%17")
        .arg(set.name).arg(armor.rarity).arg(armor.slotCount).arg(combat, gender)
        .arg(armor.defense).arg(armor.maxDefense).arg(armor.resistances[0]).arg(armor.resistances[1])
        .arg(armor.resistances[2]).arg(armor.resistances[3]).arg(armor.resistances[4]).arg(armor.price)
        .arg(armor.saveType).arg(armor.saveId).arg(modelState)
        .arg(previewModel.modelId >= 0 ? QString::fromUtf8(" · pl%1").arg(previewModel.modelId, 3, 10, QChar('0')) : QString()));
    m_sharpness->hide();
    if (!previewModel.available() || previewModel.mappingStatus == "unmapped")
        m_modelViewer->showModelMessage(QString::fromUtf8("模型映射待确认\n当前试穿部位保持不变"));
    else
        rebuildCharacterPreview();
    clearLayout(m_materialLinks);
    for (const EncyclopediaMaterial &material : m_repository.armorMaterials(dexId))
        m_materialLinks->addWidget(makeLink(QString("%1 × %2").arg(material.item.name).arg(material.quantity), itemUri(material.item)));
    if (m_repository.armorMaterials(dexId).isEmpty())
        m_materialLinks->addWidget(new QLabel(QString::fromUtf8("无生产素材记录"), this));
    m_materialTitle->setText(QString::fromUtf8("生产素材"));

    clearLayout(m_upgradeLinks);
    for (const EncyclopediaArmorSkill &skill : m_repository.armorSkills(dexId))
    {
        QString text = QString::fromUtf8("%1：%2").arg(skill.treeName).arg(skill.points > 0 ? "+" + QString::number(skill.points) : QString::number(skill.points));
        QStringList thresholds;
        for (const EncyclopediaActiveSkill &active : skill.thresholds)
            thresholds << QString("%1 (%2%3)").arg(active.name).arg(active.points > 0 ? "+" : "").arg(active.points);
        if (!thresholds.isEmpty()) text += QString::fromUtf8("\n发动：") + thresholds.join(QString::fromUtf8("、"));
        QLabel *label = new QLabel(text, this); label->setWordWrap(true); m_upgradeLinks->addWidget(label);
    }
    if (m_repository.armorSkills(dexId).isEmpty())
        m_upgradeLinks->addWidget(new QLabel(QString::fromUtf8("无技能点"), this));
    m_upgradeTitle->setText(QString::fromUtf8("技能点与发动条件"));
    m_upgradeTitle->show(); m_upgradeBody->show();
    m_addButton->setText(QString::fromUtf8("加入当前防具"));
    m_addSetButton->setText(QString::fromUtf8("加入整套（%1 件）").arg(visibleArmorMembers(set).size()));
    m_armorBreadcrumb->setText(QString::fromUtf8("资料库 / 防具 / %1 / %2").arg(set.name, armor.name));
    refreshAddButton();
}

void EncyclopediaPage::sceneSelectionChanged()
{
    if (m_internalSelection) return;
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty()) return;
    const int dexId = selected.first()->data(0).toInt();
    navigate(weaponUri(m_repository.weapon(dexId)));
}

void EncyclopediaPage::selectWeaponNode(int dexId)
{
    QGraphicsRectItem *item = m_nodeItems.value(dexId, 0);
    if (!item) return;
    m_internalSelection = true;
    m_scene->clearSelection();
    item->setSelected(true);
    m_internalSelection = false;
    m_tree->ensureVisible(item, 40, 40);
    showWeapon(dexId);
}

QString EncyclopediaPage::weaponUri(const EncyclopediaWeapon &weapon) const
{
    return QString("mhdb://mh3g/weapon/%1/%2").arg(weapon.saveType).arg(weapon.saveId);
}

QString EncyclopediaPage::itemUri(const EncyclopediaItem &item) const
{
    return item.saveId >= 0 ? QString("mhdb://mh3g/item/%1").arg(item.saveId)
        : QString("mhdb://mh3g/item-dex/%1").arg(item.dexId);
}

void EncyclopediaPage::navigate(const QString &uri, bool pushHistory)
{
    const QUrl url(uri);
    const QStringList parts = url.path().split('/', Qt::SkipEmptyParts);
    if (url.scheme() != "mhdb" || url.host() != "mh3g" || parts.isEmpty()) return;
    if (pushHistory)
    {
        while (m_history.size() > m_historyIndex + 1) m_history.removeLast();
        if (m_historyIndex < 0 || m_history.value(m_historyIndex) != uri)
        {
            m_history.append(uri);
            m_historyIndex = m_history.size() - 1;
        }
    }
    if (parts[0] == "weapon" && parts.size() == 3)
    {
        const EncyclopediaWeapon weapon = m_repository.weaponBySaveId(parts[1].toInt(), parts[2].toInt());
        if (weapon.dexId > 0)
        {
            for (int row = 0; row < m_types->count(); ++row)
            {
                if (m_types->item(row)->data(Qt::UserRole).toInt() == weapon.dexType && m_types->currentRow() != row)
                {
                    m_types->setCurrentRow(row);
                    break;
                }
            }
            selectWeaponNode(weapon.dexId);
        }
    }
    else if (parts[0] == "item" && parts.size() == 2)
    {
        const EncyclopediaItem item = m_repository.itemBySaveId(parts[1].toInt());
        if (item.dexId >= 0) showItem(item.dexId);
    }
    else if (parts[0] == "item-dex" && parts.size() == 2)
    {
        const EncyclopediaItem item = m_repository.item(parts[1].toInt());
        if (item.dexId >= 0) showItem(item.dexId);
    }
    else if (parts[0] == "armor" && parts.size() == 2)
    {
        const EncyclopediaArmor armor = m_repository.armor(parts[1].toInt());
        if (armor.dexId > 0)
        {
            if (m_category->currentData().toString() != "armor") m_category->setCurrentIndex(1);
            const EncyclopediaArmorSet set = m_repository.armorSet(armor.setId);
            for (int row = 0; row < m_types->count(); ++row)
                if (m_types->item(row)->data(Qt::UserRole).toString() == set.rank) { m_types->setCurrentRow(row); break; }
            if (armor.gender == "male") m_armorGender->setCurrentIndex(0);
            else if (armor.gender == "female") m_armorGender->setCurrentIndex(1);
            showArmor(armor.dexId);
        }
    }
    m_back->setEnabled(m_historyIndex > 0);
    m_forward->setEnabled(m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size());
}

void EncyclopediaPage::clearLayout(QVBoxLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

QPushButton *EncyclopediaPage::makeLink(const QString &text, const QString &uri)
{
    QPushButton *button = new QPushButton(text, this);
    button->setObjectName("linkButton");
    button->setCursor(Qt::PointingHandCursor);
    connect(button, &QPushButton::clicked, this, [this, uri]() { navigate(uri); });
    return button;
}

void EncyclopediaPage::showWeapon(int dexId)
{
    const EncyclopediaWeapon weapon = m_repository.weapon(dexId);
    if (weapon.dexId <= 0) return;
    m_currentWeapon = dexId;
    m_currentItem = -1;
    m_currentArmor = -1;
    m_addSetButton->hide();
    m_tryOnSetButton->hide();
    m_resetPreviewButton->hide();
    m_detailTitle->setText(weapon.name);
    m_detailSubtitle->setText(QString("%1\n%2\n%3")
        .arg(weapon.english, weapon.japanese, weaponUri(weapon)));
    QString attributes;
    if (weapon.attribute1Id >= 0 && weapon.attribute1Value != 0)
        attributes += QString::fromUtf8(" · %1 %2%3").arg(m_repository.attributeName(weapon.attribute1Id))
            .arg(qAbs(weapon.attribute1Value)).arg(weapon.attribute1Value < 0 ? QString::fromUtf8("（觉醒）") : QString());
    if (weapon.attribute2Id >= 0 && weapon.attribute2Value != 0)
        attributes += QString::fromUtf8(" · %1 %2%3").arg(m_repository.attributeName(weapon.attribute2Id))
            .arg(qAbs(weapon.attribute2Value)).arg(weapon.attribute2Value < 0 ? QString::fromUtf8("（觉醒）") : QString());
    QStringList propertyLines;
    propertyLines << QString::fromUtf8("稀有度：%1").arg(weapon.rarity)
                  << QString::fromUtf8("攻击：%1%2").arg(weapon.attack).arg(attributes)
                  << QString::fromUtf8("会心：%1%").arg(weapon.affinity * 100.0, 0, 'f', 0)
                  << QString::fromUtf8("防御：%1").arg(weapon.defense)
                  << QString::fromUtf8("孔位：%1").arg(weapon.slotCount)
                  << QString::fromUtf8("生产：%1 z").arg(weapon.productionPrice)
                  << QString::fromUtf8("强化：%1 z").arg(weapon.upgradePrice)
                  << QString::fromUtf8("存档映射：类型 %1 / ID %2").arg(weapon.saveType).arg(weapon.saveId)
                  << (weapon.modelKey.isEmpty() ? QString::fromUtf8("3D 模型：映射待确认")
                      : QString::fromUtf8("3D 模型：%1（已确认）").arg(weapon.modelKey));
    if (weapon.dexType == 6)
        propertyLines << QString::fromUtf8("音色 ID：%1 / %2 / %3")
            .arg(weapon.huntingNotes[0]).arg(weapon.huntingNotes[1]).arg(weapon.huntingNotes[2]);
    else if (weapon.dexType == 8)
        propertyLines << QString::fromUtf8("砲击：%1").arg(gunlanceShell(weapon.gunlanceType));
    else if (weapon.dexType == 9)
        propertyLines << QString::fromUtf8("瓶：%1").arg(switchAxePhial(weapon.switchAxePhial));
    else if (weapon.dexType == 10 || weapon.dexType == 11)
        propertyLines << QString::fromUtf8("装填 / 偏移 / 反动 ID：%1 / %2 / %3")
            .arg(weapon.gunReload).arg(weapon.gunSteadiness).arg(weapon.gunRecoil);
    else if (weapon.dexType == 12)
    {
        static const char *arcShots[] = {"", "放散型", "集中型", "爆裂型"};
        const QString arc = weapon.bowShot >= 1 && weapon.bowShot <= 3
            ? QString::fromUtf8(arcShots[weapon.bowShot]) : QString::number(weapon.bowShot);
        propertyLines << QString::fromUtf8("曲射：%1").arg(arc)
                      << QString::fromUtf8("蓄力：%1 / %2 / %3 / %4")
                            .arg(bowCharge(weapon.bowCharges[0]), bowCharge(weapon.bowCharges[1]),
                                 bowCharge(weapon.bowCharges[2]), bowCharge(weapon.bowCharges[3]));
    }
    m_properties->setText(propertyLines.join('\n'));
    m_sharpness->setVisible(weapon.dexType <= 9);
    m_sharpness->setSegments(weapon.sharpness);
    m_modelViewer->setModel(weapon.modelKey, weapon.modelArcPath);
    clearLayout(m_materialLinks);
    clearLayout(m_upgradeLinks);
    const QVector<EncyclopediaMaterial> materials = m_repository.materials(dexId);
    int productionCount = 0;
    int upgradeCount = 0;
    for (int index = 0; index < materials.size(); ++index)
    {
        const EncyclopediaMaterial material = materials[index];
        QVBoxLayout *target = material.kind == "production" ? m_materialLinks : m_upgradeLinks;
        target->addWidget(makeLink(QString("%1 × %2").arg(material.item.name).arg(material.quantity), itemUri(material.item)));
        if (material.kind == "production") ++productionCount;
        else ++upgradeCount;
    }
    if (productionCount == 0) m_materialLinks->addWidget(new QLabel(QString::fromUtf8("不可直接生产"), this));
    if (upgradeCount == 0) m_upgradeLinks->addWidget(new QLabel(QString::fromUtf8("无强化素材记录"), this));
    m_materialTitle->setText(QString::fromUtf8("生产素材"));
    m_upgradeTitle->show();
    m_upgradeBody->show();
    m_addButton->setText(QString::fromUtf8("加入装备箱"));
    m_breadcrumb->setText(QString::fromUtf8("资料库 / 武器 / %1").arg(weapon.name));
    highlightRoute(dexId);
    refreshAddButton();
}

void EncyclopediaPage::showItem(int dexId)
{
    const EncyclopediaItem item = m_repository.item(dexId);
    if (item.dexId < 0) return;
    m_currentItem = dexId;
    m_currentWeapon = -1;
    m_currentArmor = -1;
    m_addSetButton->hide();
    m_tryOnSetButton->hide();
    m_resetPreviewButton->hide();
    m_detailTitle->setText(item.name);
    m_detailSubtitle->setText(QString("%1\n%2\n%3").arg(item.english, item.japanese, itemUri(item)));
    m_properties->setText(QString::fromUtf8("稀有度：%1\n持有上限：%2\n买入：%3 z\n卖出：%4 z\n存档 ID：%5")
        .arg(item.rarity).arg(item.maxCount).arg(item.buyPrice).arg(item.sellPrice).arg(item.saveId));
    m_sharpness->hide();
    m_modelViewer->showItemPlaceholder();
    clearLayout(m_materialLinks);
    clearLayout(m_upgradeLinks);
    const QVector<int> uses = m_repository.weaponUses(dexId);
    for (int index = 0; index < uses.size(); ++index)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(uses[index]);
        m_materialLinks->addWidget(makeLink(weapon.name, weaponUri(weapon)));
    }
    const QVector<int> armorUses = m_repository.armorUses(dexId);
    for (int armorId : armorUses)
    {
        const EncyclopediaArmor armor = m_repository.armor(armorId);
        m_materialLinks->addWidget(makeLink(QString::fromUtf8("防具 · %1").arg(armor.name), armorUri(armor)));
    }
    m_materialTitle->setText(QString::fromUtf8("用于以下武器 / 防具"));
    m_upgradeTitle->hide();
    m_upgradeBody->hide();
    m_addButton->setText(QString::fromUtf8("加入道具箱"));
    m_breadcrumb->setText(QString::fromUtf8("资料库 / 道具 / %1").arg(item.name));
    refreshAddButton();
}

void EncyclopediaPage::refreshAddButton()
{
    bool writable = false;
    if (m_currentWeapon >= 0) writable = m_repository.weapon(m_currentWeapon).writable;
    else if (m_currentItem >= 0) writable = m_repository.item(m_currentItem).writable;
    else if (m_currentArmor >= 0) writable = m_repository.armor(m_currentArmor).writable;
    const bool loaded = m_bridge != 0 && m_bridge->hasOpenSave();
    m_addButton->setEnabled(writable && loaded);
    if (!writable)
        m_addButton->setToolTip(QString::fromUtf8("该条目的存档 ID 待验证，不能快速加入。"));
    else if (!loaded)
        m_addButton->setToolTip(QString::fromUtf8("读取 3DS 或 Wii U 角色存档后即可加入。"));
    else
        m_addButton->setToolTip(QString::fromUtf8("写入第一个空箱格；不会自动保存或穿戴。"));

    bool setWritable = m_currentArmor >= 0;
    const QVector<int> members = setWritable ? visibleArmorMembers(m_repository.armorSet(m_currentArmorSet)) : QVector<int>();
    if (members.isEmpty()) setWritable = false;
    for (int dexId : members) if (!m_repository.armor(dexId).writable) setWritable = false;
    m_addSetButton->setEnabled(setWritable && loaded);
    if (!setWritable) m_addSetButton->setToolTip(QString::fromUtf8("套装中存在存档 ID 待验证的成员，不能部分加入。"));
    else if (!loaded) m_addSetButton->setToolTip(QString::fromUtf8("读取存档后即可将整套加入装备箱。"));
    else m_addSetButton->setToolTip(QString::fromUtf8("先预留全部空格，再一次性写入内存；不会自动保存。"));
}

void EncyclopediaPage::addCurrent()
{
    if (m_bridge == 0 || !m_bridge->hasOpenSave()) return;
    SaveActionResult preview;
    SaveActionResult result;
    QString name;
    if (m_currentWeapon >= 0)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(m_currentWeapon);
        if (!weapon.writable) return;
        name = weapon.name;
        preview = m_bridge->previewAddWeapon(quint8(weapon.saveType), quint16(weapon.saveId));
        if (!preview.success)
        {
            QMessageBox::warning(this, QString::fromUtf8("无法加入装备箱"), preview.error);
            return;
        }
        const QString prompt = QString::fromUtf8("将“%1”加入装备箱的%2。\n\n新记录只写入已验证的类型和 ID，其他字段清零；不会自动穿戴或保存磁盘。")
            .arg(name, preview.slotLabel());
        if (QMessageBox::question(this, QString::fromUtf8("确认加入武器"), prompt,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        result = m_bridge->addWeapon(quint8(weapon.saveType), quint16(weapon.saveId));
        if (result.success) emit weaponAdded();
    }
    else if (m_currentArmor >= 0)
    {
        addCurrentArmor();
        return;
    }
    else if (m_currentItem >= 0)
    {
        const EncyclopediaItem item = m_repository.item(m_currentItem);
        if (!item.writable) return;
        name = item.name;
        bool accepted = false;
        const int maximum = qMax(1, item.maxCount);
        const int count = QInputDialog::getInt(this, QString::fromUtf8("加入道具箱"),
            QString::fromUtf8("%1\n数量（上限 %2）").arg(name).arg(maximum), 1, 1, maximum, 1, &accepted);
        if (!accepted) return;
        preview = m_bridge->previewAddItem(quint16(item.saveId), quint16(count));
        if (!preview.success)
        {
            QMessageBox::warning(this, QString::fromUtf8("无法加入道具箱"), preview.error);
            return;
        }
        const QString prompt = QString::fromUtf8("将“%1”× %2 加入道具箱的%3。\n\n只修改内存，仍需点击主窗口的“保存修改”。")
            .arg(name).arg(count).arg(preview.slotLabel());
        if (QMessageBox::question(this, QString::fromUtf8("确认加入道具"), prompt,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        result = m_bridge->addItem(quint16(item.saveId), quint16(count));
        if (result.success) emit itemAdded();
    }
    else return;

    if (!result.success)
    {
        QMessageBox::warning(this, QString::fromUtf8("快速加入失败"), result.error);
        return;
    }
    emit modified();
    QMessageBox::information(this, QString::fromUtf8("已加入（尚未保存）"),
        QString::fromUtf8("“%1”已加入%2。\n请点击主窗口的“保存修改”写入磁盘。")
            .arg(name, result.slotLabel()));
}

void EncyclopediaPage::addCurrentArmor()
{
    if (m_bridge == 0 || !m_bridge->hasOpenSave() || m_currentArmor < 0) return;
    const EncyclopediaArmor armor = m_repository.armor(m_currentArmor);
    if (!armor.writable) return;
    const SaveActionResult preview = m_bridge->previewAddArmor(quint8(armor.saveType), quint16(armor.saveId));
    if (!preview.success)
    {
        QMessageBox::warning(this, QString::fromUtf8("无法加入装备箱"), preview.error);
        return;
    }
    const QString prompt = QString::fromUtf8("将“%1”加入装备箱的%2。\n\n只写入已验证的类型和 ID，其他字段清零；不会自动穿戴或保存磁盘。")
        .arg(armor.name, preview.slotLabel());
    if (QMessageBox::question(this, QString::fromUtf8("确认加入防具"), prompt,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const SaveActionResult result = m_bridge->addArmor(quint8(armor.saveType), quint16(armor.saveId));
    if (!result.success) { QMessageBox::warning(this, QString::fromUtf8("快速加入失败"), result.error); return; }
    emit armorAdded(); emit modified();
    QMessageBox::information(this, QString::fromUtf8("已加入（尚未保存）"),
        QString::fromUtf8("“%1”已加入%2。\n请点击主窗口的“保存修改”写入磁盘。")
            .arg(armor.name, result.slotLabel()));
}

void EncyclopediaPage::addCurrentArmorSet()
{
    if (m_bridge == 0 || !m_bridge->hasOpenSave() || m_currentArmor < 0) return;
    const EncyclopediaArmorSet set = m_repository.armorSet(m_currentArmorSet);
    const QVector<int> members = visibleArmorMembers(set);
    QVector<ArmorSaveRef> refs;
    QStringList names;
    for (int dexId : members)
    {
        const EncyclopediaArmor armor = m_repository.armor(dexId);
        if (!armor.writable)
        {
            QMessageBox::warning(this, QString::fromUtf8("无法加入整套"),
                QString::fromUtf8("“%1”的存档 ID 尚未确认，未写入任何内容。").arg(armor.name));
            return;
        }
        ArmorSaveRef ref; ref.saveType = quint8(armor.saveType); ref.saveId = quint16(armor.saveId);
        refs.append(ref); names.append(armor.name);
    }
    const SaveActionBatchResult preview = m_bridge->previewAddArmorSet(refs);
    if (!preview.success) { QMessageBox::warning(this, QString::fromUtf8("无法加入整套"), preview.error); return; }
    const QString prompt = QString::fromUtf8("将“%1”当前性别下的 %2 件防具一次性加入装备箱？\n\n%3\n\n"
        "全部空格预留成功后才写入内存；不会自动穿戴或保存磁盘。")
        .arg(set.name).arg(refs.size()).arg(names.join(QString::fromUtf8("、")));
    if (QMessageBox::question(this, QString::fromUtf8("确认加入整套"), prompt,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const SaveActionBatchResult result = m_bridge->addArmorSet(refs);
    if (!result.success) { QMessageBox::warning(this, QString::fromUtf8("加入整套失败"), result.error); return; }
    emit armorAdded(); emit modified();
    QMessageBox::information(this, QString::fromUtf8("整套已加入（尚未保存）"),
        QString::fromUtf8("“%1”的 %2 件防具已加入装备箱。\n请点击主窗口的“保存修改”写入磁盘。")
            .arg(set.name).arg(refs.size()));
}

void EncyclopediaPage::highlightRoute(int dexId)
{
    QSet<int> highlighted;
    highlighted.insert(dexId);
    int current = dexId;
    while (!m_repository.parentIds(current).isEmpty())
    {
        current = m_repository.parentIds(current).first();
        highlighted.insert(current);
    }
    const QVector<int> children = m_repository.childIds(dexId);
    for (int index = 0; index < children.size(); ++index) highlighted.insert(children[index]);
    for (QMap<int, QGraphicsRectItem *>::const_iterator it = m_nodeItems.constBegin(); it != m_nodeItems.constEnd(); ++it)
    {
        if (highlighted.contains(it.key()))
        {
            it.value()->setPen(QPen(it.key() == dexId ? QColor("#2563ad") : QColor("#5b86bd"), it.key() == dexId ? 2.5 : 1.8));
            it.value()->setBrush(QBrush(it.key() == dexId ? QColor("#e8f2ff") : QColor("#f3f7fc")));
        }
        else
        {
            it.value()->setPen(QPen(QColor("#aab6c5")));
            it.value()->setBrush(QBrush(QColor("#fbfdff")));
        }
    }
}

void EncyclopediaPage::goBack()
{
    if (m_historyIndex <= 0) return;
    --m_historyIndex;
    navigate(m_history[m_historyIndex], false);
}

void EncyclopediaPage::goForward()
{
    if (m_historyIndex + 1 >= m_history.size()) return;
    ++m_historyIndex;
    navigate(m_history[m_historyIndex], false);
}

void EncyclopediaPage::fitTree()
{
    m_tree->resetTransform();
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (!selected.isEmpty()) m_tree->ensureVisible(selected.first(), 40, 40);
}

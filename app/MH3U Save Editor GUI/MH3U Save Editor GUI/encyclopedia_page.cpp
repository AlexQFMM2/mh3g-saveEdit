#include "encyclopedia_page.hpp"
#include "save_action_bridge.hpp"

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
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

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
    : QWidget(parent), m_bridge(bridge), m_historyIndex(-1), m_internalSelection(false), m_currentWeapon(-1), m_currentItem(-1)
{
    setObjectName("encyclopediaPage");
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    QFrame *filters = new QFrame(this);
    filters->setObjectName("contentCard");
    filters->setFixedWidth(190);
    QVBoxLayout *filterLayout = new QVBoxLayout(filters);
    QLabel *filterTitle = new QLabel(QString::fromUtf8("武器资料库"), filters);
    filterTitle->setObjectName("sectionTitle");
    m_search = new QLineEdit(filters);
    m_search->setPlaceholderText(QString::fromUtf8("搜索中 / 英 / 日文名称"));
    m_rarity = new QComboBox(filters);
    m_rarity->addItem(QString::fromUtf8("全部稀有度"), 0);
    for (int rarity = 1; rarity <= 10; ++rarity) m_rarity->addItem(QString("Rare %1").arg(rarity), rarity);
    m_attribute = new QComboBox(filters);
    m_attribute->addItem(QString::fromUtf8("全部属性"), -2);
    m_types = new QListWidget(filters);
    filterLayout->addWidget(filterTitle);
    filterLayout->addWidget(m_search);
    filterLayout->addWidget(m_rarity);
    filterLayout->addWidget(m_attribute);
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

    QFrame *details = new QFrame(this);
    details->setObjectName("contentCard");
    details->setFixedWidth(330);
    QVBoxLayout *detailShell = new QVBoxLayout(details);
    QScrollArea *detailScroll = new QScrollArea(details);
    detailScroll->setWidgetResizable(true);
    detailScroll->setFrameShape(QFrame::NoFrame);
    QWidget *detailBody = new QWidget(detailScroll);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailBody);
    m_imagePlaceholder = new QLabel(QString::fromUtf8("武器详情图\n资源管线预留"), detailBody);
    m_imagePlaceholder->setObjectName("encyclopediaImage");
    m_imagePlaceholder->setAlignment(Qt::AlignCenter);
    m_imagePlaceholder->setMinimumHeight(110);
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
    m_materialTitle = new QLabel(QString::fromUtf8("生产 / 强化素材"), detailBody);
    m_materialTitle->setObjectName("sectionTitle");
    QWidget *materialBody = new QWidget(detailBody);
    m_materialLinks = new QVBoxLayout(materialBody);
    m_materialLinks->setContentsMargins(0, 0, 0, 0);
    m_relationTitle = new QLabel(QString::fromUtf8("强化路线"), detailBody);
    m_relationTitle->setObjectName("sectionTitle");
    QWidget *relationBody = new QWidget(detailBody);
    m_relationLinks = new QVBoxLayout(relationBody);
    m_relationLinks->setContentsMargins(0, 0, 0, 0);
    m_addButton = new QPushButton(QString::fromUtf8("加入装备箱"), detailBody);
    m_addButton->setObjectName("primaryButton");
    m_addButton->setEnabled(false);
    m_addButton->setToolTip(QString::fromUtf8("快速加入将在存档桥接阶段启用。"));
    detailLayout->addWidget(m_imagePlaceholder);
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailSubtitle);
    detailLayout->addWidget(m_properties);
    detailLayout->addWidget(m_sharpness);
    detailLayout->addWidget(m_materialTitle);
    detailLayout->addWidget(materialBody);
    detailLayout->addWidget(m_relationTitle);
    detailLayout->addWidget(relationBody);
    detailLayout->addStretch();
    detailLayout->addWidget(m_addButton);
    detailScroll->setWidget(detailBody);
    detailShell->addWidget(detailScroll);

    root->addWidget(filters);
    root->addWidget(browser, 1);
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
void EncyclopediaPage::updateSaveState() { refreshAddButton(); }

void EncyclopediaPage::typeChanged(int) { rebuildTree(); }

void EncyclopediaPage::addBranch(int dexId, int depth, QMap<int, int> &depths)
{
    if (depths.contains(dexId) && depths[dexId] >= depth) return;
    depths[dexId] = depth;
    const QVector<int> children = m_repository.childIds(dexId);
    for (int index = 0; index < children.size(); ++index) addBranch(children[index], depth + 1, depths);
}

void EncyclopediaPage::rebuildTree()
{
    m_scene->clear();
    m_nodeItems.clear();
    m_depths.clear();
    const int row = m_types->currentRow();
    if (row < 0) return;
    const int dexType = m_types->item(row)->data(Qt::UserRole).toInt();
    const QVector<int> roots = m_repository.rootIdsForType(dexType);
    for (int index = 0; index < roots.size(); ++index) addBranch(roots[index], 0, m_depths);
    const QVector<int> weapons = m_repository.weaponIdsForType(dexType);
    for (int index = 0; index < weapons.size(); ++index)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(weapons[index]);
        const qreal x = m_depths.value(weapon.dexId, 0) * 185.0;
        const qreal y = weapon.displayOrder * 70.0;
        QGraphicsRectItem *card = m_scene->addRect(QRectF(x, y, 156, 54), QPen(QColor("#8191a6")), QBrush(QColor("#fbfdff")));
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

void EncyclopediaPage::filtersChanged() { applyFilters(); }

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
    return QString("mhdb://mh3g/item/%1").arg(item.saveId);
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
                  << QString::fromUtf8("存档映射：类型 %1 / ID %2").arg(weapon.saveType).arg(weapon.saveId);
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
    m_imagePlaceholder->setText(QString::fromUtf8("%1\n详情图片资源预留").arg(weapon.imageKey));
    clearLayout(m_materialLinks);
    const QVector<EncyclopediaMaterial> materials = m_repository.materials(dexId);
    for (int index = 0; index < materials.size(); ++index)
    {
        const EncyclopediaMaterial material = materials[index];
        const QString kind = material.kind == "production" ? QString::fromUtf8("生产") : QString::fromUtf8("强化");
        m_materialLinks->addWidget(makeLink(QString("[%1] %2 × %3").arg(kind, material.item.name).arg(material.quantity), itemUri(material.item)));
    }
    if (materials.isEmpty()) m_materialLinks->addWidget(new QLabel(QString::fromUtf8("无素材记录"), this));
    clearLayout(m_relationLinks);
    const QVector<int> parents = m_repository.parentIds(dexId);
    const QVector<int> children = m_repository.childIds(dexId);
    for (int index = 0; index < parents.size(); ++index)
    {
        const EncyclopediaWeapon parent = m_repository.weapon(parents[index]);
        m_relationLinks->addWidget(makeLink(QString::fromUtf8("← %1").arg(parent.name), weaponUri(parent)));
    }
    for (int index = 0; index < children.size(); ++index)
    {
        const EncyclopediaWeapon child = m_repository.weapon(children[index]);
        m_relationLinks->addWidget(makeLink(QString::fromUtf8("→ %1").arg(child.name), weaponUri(child)));
    }
    if (parents.isEmpty() && children.isEmpty()) m_relationLinks->addWidget(new QLabel(QString::fromUtf8("独立生产武器"), this));
    m_relationTitle->setText(QString::fromUtf8("强化路线"));
    m_materialTitle->setText(QString::fromUtf8("生产 / 强化素材"));
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
    m_detailTitle->setText(item.name);
    m_detailSubtitle->setText(QString("%1\n%2\n%3").arg(item.english, item.japanese, itemUri(item)));
    m_properties->setText(QString::fromUtf8("稀有度：%1\n持有上限：%2\n买入：%3 z\n卖出：%4 z\n存档 ID：%5")
        .arg(item.rarity).arg(item.maxCount).arg(item.buyPrice).arg(item.sellPrice).arg(item.saveId));
    m_sharpness->hide();
    m_imagePlaceholder->setText(QString::fromUtf8("道具图标\n资源管线预留"));
    clearLayout(m_materialLinks);
    const QVector<int> uses = m_repository.weaponUses(dexId);
    for (int index = 0; index < uses.size(); ++index)
    {
        const EncyclopediaWeapon weapon = m_repository.weapon(uses[index]);
        m_materialLinks->addWidget(makeLink(weapon.name, weaponUri(weapon)));
    }
    clearLayout(m_relationLinks);
    m_relationLinks->addWidget(new QLabel(QString::fromUtf8("点击上方武器返回其强化路线。"), this));
    m_relationTitle->setText(QString::fromUtf8("相关跳转"));
    m_materialTitle->setText(QString::fromUtf8("用于以下武器"));
    m_addButton->setText(QString::fromUtf8("加入道具箱"));
    m_breadcrumb->setText(QString::fromUtf8("资料库 / 道具 / %1").arg(item.name));
    refreshAddButton();
}

void EncyclopediaPage::refreshAddButton()
{
    bool writable = false;
    if (m_currentWeapon >= 0) writable = m_repository.weapon(m_currentWeapon).writable;
    else if (m_currentItem >= 0) writable = m_repository.item(m_currentItem).writable;
    const bool loaded = m_bridge != 0 && m_bridge->hasOpenSave();
    m_addButton->setEnabled(writable && loaded);
    if (!writable)
        m_addButton->setToolTip(QString::fromUtf8("该条目的存档 ID 待验证，不能快速加入。"));
    else if (!loaded)
        m_addButton->setToolTip(QString::fromUtf8("读取 3DS 或 Wii U 角色存档后即可加入。"));
    else
        m_addButton->setToolTip(QString::fromUtf8("写入第一个空箱格；不会自动保存或穿戴。"));
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

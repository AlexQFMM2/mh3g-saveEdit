#ifndef ENCYCLOPEDIA_PAGE_HPP
#define ENCYCLOPEDIA_PAGE_HPP

#include "encyclopedia_data.hpp"

#include <QGraphicsView>
#include <QMap>
#include <QStringList>
#include <QWidget>

class QComboBox;
class QFrame;
class QGraphicsRectItem;
class QGraphicsScene;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class SaveActionBridge;
class WeaponModelWidget;

class SharpnessWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SharpnessWidget(QWidget *parent = 0);
    void setSegments(const QVector<int> &segments);
protected:
    void paintEvent(QPaintEvent *event);
private:
    QVector<int> m_segments;
};

class WeaponTreeView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit WeaponTreeView(QWidget *parent = 0);
protected:
    void wheelEvent(QWheelEvent *event);
};

class EncyclopediaPage : public QWidget
{
    Q_OBJECT
public:
    explicit EncyclopediaPage(SaveActionBridge *bridge, QWidget *parent = 0);
    bool available() const;
    QString error() const;
    void updateSaveState();

signals:
    void modified();
    void itemAdded();
    void weaponAdded();
    void armorAdded();

private slots:
    void typeChanged(int row);
    void filtersChanged();
    void sceneSelectionChanged();
    void goBack();
    void goForward();
    void fitTree();
    void addCurrent();
    void categoryChanged(int index);
    void armorFiltersChanged();
    void addCurrentArmor();
    void addCurrentArmorSet();

private:
    void rebuildTree();
    void addBranch(int dexId, int depth, QMap<int, int> &depths);
    void assignBranchRows(int dexId, int row, QMap<int, int> &rows, int &nextRow);
    void applyFilters();
    void selectWeaponNode(int dexId);
    void navigate(const QString &uri, bool pushHistory = true);
    void showWeapon(int dexId);
    void showItem(int dexId);
    void highlightRoute(int dexId);
    void refreshAddButton();
    void clearLayout(QVBoxLayout *layout);
    QPushButton *makeLink(const QString &text, const QString &uri);
    QString weaponUri(const EncyclopediaWeapon &weapon) const;
    QString itemUri(const EncyclopediaItem &item) const;
    QString armorUri(const EncyclopediaArmor &armor) const;
    void rebuildArmorList();
    void selectArmor(int dexId, bool pushHistory = true);
    void showArmor(int dexId);
    QVector<int> visibleArmorMembers(const EncyclopediaArmorSet &set) const;
    QString selectedArmorGender() const;

    EncyclopediaRepository m_repository;
    SaveActionBridge *m_bridge;
    QListWidget *m_types;
    QComboBox *m_category;
    QLabel *m_filterTitle;
    QLineEdit *m_search;
    QComboBox *m_rarity;
    QComboBox *m_attribute;
    QComboBox *m_armorCombat;
    QComboBox *m_armorGender;
    QStackedWidget *m_browserStack;
    WeaponTreeView *m_tree;
    QGraphicsScene *m_scene;
    QPushButton *m_back;
    QPushButton *m_forward;
    QLabel *m_breadcrumb;
    QLabel *m_detailTitle;
    QLabel *m_detailSubtitle;
    WeaponModelWidget *m_modelViewer;
    QLabel *m_properties;
    QLabel *m_materialTitle;
    QLabel *m_upgradeTitle;
    QWidget *m_upgradeBody;
    SharpnessWidget *m_sharpness;
    QVBoxLayout *m_materialLinks;
    QVBoxLayout *m_upgradeLinks;
    QPushButton *m_addButton;
    QPushButton *m_addSetButton;
    QScrollArea *m_armorScroll;
    QVBoxLayout *m_armorListLayout;
    QLabel *m_armorBreadcrumb;
    QMap<int, QGraphicsRectItem *> m_nodeItems;
    QMap<int, int> m_depths;
    QStringList m_history;
    int m_historyIndex;
    bool m_internalSelection;
    int m_currentWeapon;
    int m_currentItem;
    int m_currentArmor;
    QString m_currentArmorSet;
    QString m_selectedArmorPart;
};

#endif

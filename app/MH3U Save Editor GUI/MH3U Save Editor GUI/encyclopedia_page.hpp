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
class QVBoxLayout;

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
    explicit EncyclopediaPage(QWidget *parent = 0);
    bool available() const;
    QString error() const;

private slots:
    void typeChanged(int row);
    void filtersChanged();
    void sceneSelectionChanged();
    void goBack();
    void goForward();
    void fitTree();

private:
    void rebuildTree();
    void addBranch(int dexId, int depth, QMap<int, int> &depths);
    void applyFilters();
    void selectWeaponNode(int dexId);
    void navigate(const QString &uri, bool pushHistory = true);
    void showWeapon(int dexId);
    void showItem(int dexId);
    void highlightRoute(int dexId);
    void clearLayout(QVBoxLayout *layout);
    QPushButton *makeLink(const QString &text, const QString &uri);
    QString weaponUri(const EncyclopediaWeapon &weapon) const;
    QString itemUri(const EncyclopediaItem &item) const;

    EncyclopediaRepository m_repository;
    QListWidget *m_types;
    QLineEdit *m_search;
    QComboBox *m_rarity;
    QComboBox *m_attribute;
    WeaponTreeView *m_tree;
    QGraphicsScene *m_scene;
    QPushButton *m_back;
    QPushButton *m_forward;
    QLabel *m_breadcrumb;
    QLabel *m_detailTitle;
    QLabel *m_detailSubtitle;
    QLabel *m_imagePlaceholder;
    QLabel *m_properties;
    QLabel *m_relationTitle;
    SharpnessWidget *m_sharpness;
    QVBoxLayout *m_materialLinks;
    QVBoxLayout *m_relationLinks;
    QPushButton *m_addButton;
    QMap<int, QGraphicsRectItem *> m_nodeItems;
    QMap<int, int> m_depths;
    QStringList m_history;
    int m_historyIndex;
    bool m_internalSelection;
    int m_currentWeapon;
    int m_currentItem;
};

#endif

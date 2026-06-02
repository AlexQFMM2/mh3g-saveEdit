#ifndef QCHEST_HPP
#define QCHEST_HPP

#include "main.hpp"

#include "qitem.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QWidget>

class QChest : public QWidget
{
    Q_OBJECT
public:
    explicit QChest(MH3U_SE *mh3u, QWidget *parent = 0);
    ~QChest();

private:
    MH3U_SE *mh3u;
    QTableWidget *m_table;
    QLineEdit *m_search;
    QCheckBox *m_nonEmptyOnly;
    QLabel *m_selectedInfo;
    QPushButton *m_editButton;
    QPushButton *m_addButton;

    void populateTable();
    void editSlot(uint32_t panel, uint32_t slot);
    item_t& itemAt(uint32_t panel, uint32_t slot) const;
    bool itemMatchesFilters(const item_t &item) const;

public slots:
    void buttonClicked(int id);
    void tableCellDoubleClicked(int row, int column);
    void editSelectedItem();
    void addItemToFirstEmptySlot();
    void updateSelectedInfo();
    void refreshFilters();
};

#endif // QCHEST_HPP

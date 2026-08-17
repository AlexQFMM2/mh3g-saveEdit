#ifndef QCHEST_HPP
#define QCHEST_HPP

#include "main.hpp"

#include "qitem.hpp"

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

class QTableView;
class ItemChestTableModel;

class QChest : public QWidget
{
    Q_OBJECT
public:
    explicit QChest(MH3U_SE *mh3u, QWidget *parent = 0);
    ~QChest();
    void loadFromModel();
    bool commitToModel(QString *error = 0);

signals:
    void modified();

private:
    friend class ItemChestTableModel;
    MH3U_SE *mh3u;
    QTableView *m_table;
    ItemChestTableModel *m_tableModel;
    QLineEdit *m_search;
    QCheckBox *m_nonEmptyOnly;
    QLabel *m_selectedInfo;
    QPushButton *m_editButton;
    QPushButton *m_addButton;
    QPushButton *m_exportButton;
    QPushButton *m_importButton;

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
    void exportChestForm();
    void importChestForm();
};

#endif // QCHEST_HPP

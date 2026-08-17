#ifndef QLOADOUT_HPP
#define QLOADOUT_HPP

#include "loadout.hpp"

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class QFrame;
class QCheckBox;
class MH3U_SE;

class QLoadout : public QWidget
{
    Q_OBJECT
public:
    explicit QLoadout(MH3U_SE *saveEditor, QWidget *parent = 0);
    bool maybeLeaveDirty();
    void updateSaveContext();
    bool smokeTestLayout(QString *error = 0) const;

signals:
    void saveModified();

private slots:
    void newLoadout();
    void openLoadout();
    bool saveLoadout();
    void applyToEquipmentBox();
    void nameChanged(const QString &name);
    void genderChanged(int index);

private:
    struct slot_widgets_t
    {
        QFrame *frame;
        QLabel *name;
        QLabel *meta;
        QPushButton *select;
        QPushButton *decorations;
        QPushButton *clear;
    };

    MH3U_SE *m_saveEditor;
    loadout_model_t m_model;
    QString m_currentPath;
    bool m_dirty;
    bool m_loading;
    QLineEdit *m_name;
    QComboBox *m_gender;
    QPushButton *m_apply;
    QLabel *m_localState;
    QTableWidget *m_skillTable;
    QCheckBox *m_showAllSkills;
    QLabel *m_summary;
    slot_widgets_t m_slots[LoadoutSlotCount];

    void chooseEquipment(loadout_slot_e slot);
    void editDecorations(loadout_slot_e slot);
    void clearSlot(loadout_slot_e slot);
    void setDirty(bool dirty = true);
    void refresh();
    void refreshCards();
    void refreshSummary();
    bool writeLoadout(const QString &path);
    bool hasSelections() const;
};

#endif

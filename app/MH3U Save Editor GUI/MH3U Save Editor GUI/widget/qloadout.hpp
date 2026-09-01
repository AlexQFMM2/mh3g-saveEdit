#ifndef QLOADOUT_HPP
#define QLOADOUT_HPP

#include "loadout.hpp"
#include "loadout_search.hpp"

#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTableWidget;
class QFrame;
class QCheckBox;
class QDialog;
class MH3U_SE;

class QLoadout : public QWidget
{
    Q_OBJECT
public:
    explicit QLoadout(MH3U_SE *saveEditor, QWidget *parent = 0);
    bool maybeLeaveDirty();
    void updateSaveContext();
    bool smokeTestLayout(QString *error = 0) const;
    QByteArray currentPayload(QString *error = 0) const;
    bool importPayload(const QByteArray &bytes, QString *error = 0);
    bool showPayloadDialog(const QByteArray &bytes, QString *error = 0);

signals:
    void saveModified();
    void publishRequested();

private slots:
    void automaticLoadout();
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
    QPushButton *m_publish;
    QLabel *m_localState;
    QTableWidget *m_skillTable;
    QCheckBox *m_showAllSkills;
    QLabel *m_summary;
    QList<QWidget *> m_detailEditControls;
    QPointer<QDialog> m_autoLoadoutDialog;
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
    void setDetailReadOnlyMode();
    void applyAutomaticResult(const loadout_model_t &model);
};

#endif

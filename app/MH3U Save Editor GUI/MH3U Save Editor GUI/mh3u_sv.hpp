#ifndef MH3U_SV_HPP
#define MH3U_SV_HPP

#include "main.hpp"
#include "widget/qcharacter.hpp"
#include "widget/qchest.hpp"
#include "widget/qbox.hpp"

#include <QMainWindow>

class QLabel;
class QPushButton;
class QStackedWidget;
class QCloseEvent;
class EncyclopediaPage;
class SaveActionBridge;

class MH3U_SV : public QMainWindow
{
    Q_OBJECT
public:
    explicit MH3U_SV(QWidget *parent = 0);
    ~MH3U_SV();

protected:
    void closeEvent(QCloseEvent *event);

private:
    MH3U_SE *mh3u;
    SaveActionBridge *saveBridge;
    QPushButton *characterButton;
    QPushButton *chestButton;
    QPushButton *boxButton;
    QPushButton *encyclopediaButton;
    QPushButton *loadButton;
    QPushButton *saveButton;
    QLabel *statusLabel;
    QLabel *pageTitle;
    QStackedWidget *pageStack;
    QWidget *emptyPage;
    EncyclopediaPage *encyclopediaPage;
    QCharacter *characterPage;
    QChest *chestPage;
    QBox *boxPage;
    bool dirty;

    void createPages();
    void loadPages();
    bool commitPages(QString *error = 0);
    bool maybeLeaveDirty();
    bool discardChanges();
    void setCurrentPage(QWidget *page, QPushButton *button, const QString &title);
    void updateState();

private slots:
    void showCharacter();
    void showChest();
    void showBox();
    void showEncyclopedia();
    void markModified();
    void refreshChestFromModel();
    void refreshBoxFromModel();
    void loadFile();
    bool saveFile();
};

#endif

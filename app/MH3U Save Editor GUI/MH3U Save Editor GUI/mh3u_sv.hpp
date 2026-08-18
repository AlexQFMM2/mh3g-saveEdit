#ifndef MH3U_SV_HPP
#define MH3U_SV_HPP

#include "main.hpp"
#include "widget/qcharacter.hpp"
#include "widget/qchest.hpp"
#include "widget/qbox.hpp"
#include "widget/qloadout.hpp"
#include "widget/qcommunity.hpp"

#include <QMainWindow>

class QLabel;
class QPushButton;
class QStackedWidget;
class QCloseEvent;

class MH3U_SV : public QMainWindow
{
    Q_OBJECT
public:
    explicit MH3U_SV(QWidget *parent = 0);
    ~MH3U_SV();
    bool smokeTestLoadout(QString *error = 0);
    bool smokeTestAccount(QString *error = 0);

protected:
    void closeEvent(QCloseEvent *event);

private:
    MH3U_SE *mh3u;
    QPushButton *characterButton;
    QPushButton *chestButton;
    QPushButton *boxButton;
    QPushButton *loadoutButton;
    QPushButton *communityButton;
    QPushButton *accountButton;
    QPushButton *aboutButton;
    QPushButton *loadButton;
    QPushButton *saveButton;
    QLabel *statusLabel;
    QLabel *pageTitle;
    QStackedWidget *pageStack;
    QWidget *emptyPage;
    QCharacter *characterPage;
    QChest *chestPage;
    QBox *boxPage;
    QLoadout *loadoutPage;
    QCommunity *communityPage;
    QWidget *accountPage;
    QWidget *aboutPage;
    bool dirty;
    bool dataReady;

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
    void showLoadout();
    void showCommunity();
    void showAccount();
    void showAbout();
    void loadoutApplied();
    void markModified();
    void loadFile();
    bool saveFile();
};

#endif

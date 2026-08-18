#ifndef QCOMMUNITY_HPP
#define QCOMMUNITY_HPP

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QStackedWidget;
class QTimer;
class QLoadout;
class TagSelectWidget;

class QCommunity : public QWidget
{
    Q_OBJECT
public:
    explicit QCommunity(QLoadout *loadout, QWidget *parent = 0);
    QWidget *accountPage() const;
    bool smokeTestAccount(QString *error = 0);

public slots:
    void uploadCurrent();
    void refreshProfile();

signals:
    void equipmentBoxModified();

private slots:
    void refreshLoadouts();
    void importSelected();
    void toggleLike();
    void reportSelected();
    void login();
    void showLoginForm();
    void showRegisterForm();
    void sendRegisterCode();
    void registerAccount();
    void forgotPassword();
    void bindEmail();
    void logout();
    void saveNickname();
    void changePassword();

private:
    struct profile_t
    {
        qint64 publicId;
        QString nickname;
        QString email;
        bool emailVerified;
        qint64 receivedLikeCount;
        bool mustChangePassword;
        profile_t() : publicId(0), emailVerified(false), receivedLikeCount(0), mustChangePassword(false) {}
    };

    QLoadout *m_loadout;
    QNetworkAccessManager *m_network;
    QString m_baseUrl;
    QString m_token;
    profile_t m_profile;
    QWidget *m_accountPage;
    QLineEdit *m_search;
    QCheckBox *m_legalOnly;
    TagSelectWidget *m_equipmentFilter;
    TagSelectWidget *m_skillFilter;
    QTableWidget *m_table;
    QLabel *m_resultState;
    QLabel *m_accountState;
    QLabel *m_accountError;
    QStackedWidget *m_accountStack;
    QStackedWidget *m_authForms;
    QPushButton *m_loginTab;
    QPushButton *m_registerTab;
    QLineEdit *m_username;
    QLineEdit *m_password;
    QPushButton *m_login;
    QPushButton *m_logout;
    QLineEdit *m_nickname;
    QPushButton *m_saveNickname;
    QPushButton *m_changePassword;
    QLineEdit *m_registerUsername;
    QLineEdit *m_registerEmail;
    QLineEdit *m_registerPassword;
    QLineEdit *m_registerConfirm;
    QLineEdit *m_registerCode;
    QPushButton *m_sendRegisterCode;
    QPushButton *m_register;
    QPushButton *m_forgotPassword;
    QLabel *m_profileAvatar;
    QLabel *m_profileName;
    QLabel *m_profileMeta;
    QLabel *m_profileEmail;
    QLabel *m_receivedLikes;
    QPushButton *m_bindEmail;
    QTimer *m_resendTimer;
    int m_resendSeconds;
    QString m_registerChallenge;

    QNetworkReply *request(const QString &path, const QByteArray &method = "GET",
                           const QByteArray &body = QByteArray());
    bool responseObject(QNetworkReply *reply, QJsonObject *object, bool quiet = false);
    QString selectedId() const;
    bool selectedLiked() const;
    void populateFilters();
    void applyProfile(const QJsonObject &user);
    void updateAccountUi();
    void restoreSession();
    void storeSession(const QJsonObject &response);
    void startRegisterCountdown(int seconds);
    void setAccountError(const QString &message);
};

#endif

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
class QLoadout;
class TagSelectWidget;

class QCommunity : public QWidget
{
    Q_OBJECT
public:
    explicit QCommunity(QLoadout *loadout, QWidget *parent = 0);
    QWidget *accountPage() const;

public slots:
    void uploadCurrent();

signals:
    void equipmentBoxModified();

private slots:
    void refreshLoadouts();
    void importSelected();
    void toggleLike();
    void reportSelected();
    void login();
    void logout();
    void saveNickname();
    void changePassword();

private:
    struct profile_t
    {
        qint64 publicId;
        QString nickname;
        bool mustChangePassword;
        profile_t() : publicId(0), mustChangePassword(false) {}
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
    QLineEdit *m_username;
    QLineEdit *m_password;
    QPushButton *m_login;
    QPushButton *m_logout;
    QLineEdit *m_nickname;
    QPushButton *m_saveNickname;
    QPushButton *m_changePassword;

    QNetworkReply *request(const QString &path, const QByteArray &method = "GET",
                           const QByteArray &body = QByteArray());
    bool responseObject(QNetworkReply *reply, QJsonObject *object, bool quiet = false);
    QString selectedId() const;
    bool selectedLiked() const;
    void populateFilters();
    void applyProfile(const QJsonObject &user);
    void updateAccountUi();
    void restoreSession();
};

#endif

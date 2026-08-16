#ifndef QCHARACTER_HPP
#define QCHARACTER_HPP

#include "main.hpp"

#include <QWidget>
#include <QSpinBox>
#include <QComboBox>
#include <QLineEdit>

class QCharacter : public QWidget
{
    Q_OBJECT
public:
    explicit QCharacter(MH3U_SE *mh3u, QWidget *parent = 0);
    ~QCharacter();
    void loadFromModel();
    bool commitToModel(QString *error = 0);

signals:
    void modified();

private slots:
    void notifyModified();

private:
    MH3U_SE *mh3u;
    QComboBox *m_sexs;
    QComboBox *m_faces;
    QComboBox *m_hairs;
    QLineEdit *m_name;
    QSpinBox *m_money;
    QComboBox *m_voices;
    QSpinBox *m_mogapoint;

    bool m_loading;
};

#endif // QCHARACTER_HPP

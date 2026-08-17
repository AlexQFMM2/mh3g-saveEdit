#ifndef SEARCHABLE_COMBOBOX_HPP_IMPORTED
#define SEARCHABLE_COMBOBOX_HPP_IMPORTED

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QModelIndex>
#include <QObject>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>
#include <QtGlobal>

namespace SearchableComboBoxProperty
{
static const char Configured[] = "mh3gSearchableConfigured";
static const char CommittedData[] = "mh3gSearchableCommittedData";
static const char CommittedIndex[] = "mh3gSearchableCommittedIndex";
}

class SearchableComboBoxController : public QObject
{
public:
    explicit SearchableComboBoxController(QComboBox *comboBox)
        : QObject(comboBox), m_comboBox(comboBox), m_editor(NULL), m_completer(NULL), m_searching(false), m_restoring(false)
    {
        if (m_comboBox == NULL)
        {
            return;
        }

        m_comboBox->setEditable(true);
        m_comboBox->setInsertPolicy(QComboBox::NoInsert);
        m_comboBox->setMaxVisibleItems(20);
        m_comboBox->setMinimumContentsLength(20);
        m_comboBox->setSizeAdjustPolicy(QComboBox::AdjustToContentsOnFirstShow);

        m_editor = m_comboBox->lineEdit();
        m_comboBox->setCompleter(NULL);
        m_completer = new QCompleter(m_comboBox->model(), m_comboBox);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setMaxVisibleItems(20);
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
        m_completer->setFilterMode(Qt::MatchContains);
#endif
        if (m_editor != NULL)
        {
            m_editor->setCompleter(m_completer);
            m_editor->installEventFilter(this);
        }
        m_comboBox->setProperty(SearchableComboBoxProperty::Configured, true);
        rememberCurrentSelection();

        if (m_editor != NULL)
        {
            connect(m_editor, &QLineEdit::textEdited, this, [this](const QString &text) {
                if (m_restoring || m_completer == NULL)
                {
                    return;
                }

                m_searching = true;
                m_completer->setCompletionPrefix(text);
                if (text.isEmpty())
                {
                    m_completer->popup()->hide();
                }
                else
                {
                    m_completer->complete();
                }
            });
            connect(m_editor, &QLineEdit::editingFinished, this, [this]() {
                QTimer::singleShot(0, this, [this]() {
                    if (m_searching)
                    {
                        restoreCommittedSelection();
                    }
                });
            });
        }

        connect(m_comboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                this, [this](int index) { commitIndex(index); });
        connect(m_comboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, [this](int) {
                    if (!m_searching && !m_restoring)
                    {
                        rememberCurrentSelection();
                    }
                });
        connect(m_completer, static_cast<void (QCompleter::*)(const QModelIndex &)>(&QCompleter::activated),
                this, [this](const QModelIndex &index) { commitCompletion(index); });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event)
    {
        if (watched == m_editor && event != NULL && event->type() == QEvent::KeyPress && m_searching)
        {
            QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            {
                QModelIndex completion;
                if (m_completer != NULL)
                {
                    completion = m_completer->popup()->currentIndex();
                    if (!completion.isValid() && m_completer->completionModel()->rowCount() > 0)
                    {
                        completion = m_completer->completionModel()->index(0, m_completer->completionColumn());
                    }
                }
                if (completion.isValid())
                {
                    commitCompletion(completion);
                }
                else
                {
                    restoreCommittedSelection();
                }
                return true;
            }
            if (keyEvent->key() == Qt::Key_Escape && m_completer != NULL && m_completer->popup()->isVisible())
            {
                m_completer->popup()->hide();
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QComboBox *m_comboBox;
    QLineEdit *m_editor;
    QCompleter *m_completer;
    bool m_searching;
    bool m_restoring;

    void rememberCurrentSelection()
    {
        if (m_comboBox == NULL || m_comboBox->currentIndex() < 0)
        {
            return;
        }

        m_comboBox->setProperty(SearchableComboBoxProperty::CommittedIndex, m_comboBox->currentIndex());
        m_comboBox->setProperty(SearchableComboBoxProperty::CommittedData, m_comboBox->currentData());
    }

    void commitIndex(int index)
    {
        if (m_comboBox == NULL || index < 0 || index >= m_comboBox->count())
        {
            return;
        }

        m_searching = false;
        if (m_comboBox->currentIndex() != index)
        {
            m_comboBox->setCurrentIndex(index);
        }
        rememberCurrentSelection();
        if (m_comboBox->lineEdit() != NULL)
        {
            m_comboBox->lineEdit()->setText(m_comboBox->itemText(index));
        }
    }

    void commitCompletion(const QModelIndex &completionIndex)
    {
        if (m_comboBox == NULL || !completionIndex.isValid())
        {
            return;
        }

        const QVariant selectedData = completionIndex.data(Qt::UserRole);
        int index = selectedData.isValid() ? m_comboBox->findData(selectedData) : -1;
        if (index < 0)
        {
            index = m_comboBox->findText(completionIndex.data(Qt::DisplayRole).toString(), Qt::MatchFixedString);
        }
        commitIndex(index);
    }

    void restoreCommittedSelection()
    {
        if (m_comboBox == NULL)
        {
            return;
        }

        m_restoring = true;
        m_searching = false;
        if (m_completer != NULL)
        {
            m_completer->popup()->hide();
        }

        int index = m_comboBox->property(SearchableComboBoxProperty::CommittedIndex).toInt();
        const QVariant data = m_comboBox->property(SearchableComboBoxProperty::CommittedData);
        if (index < 0 || index >= m_comboBox->count() || m_comboBox->itemData(index) != data)
        {
            index = m_comboBox->findData(data);
        }

        if (index >= 0)
        {
            const QSignalBlocker blocker(m_comboBox);
            m_comboBox->setCurrentIndex(index);
            if (m_comboBox->lineEdit() != NULL)
            {
                m_comboBox->lineEdit()->setText(m_comboBox->itemText(index));
            }
        }
        m_restoring = false;
    }
};

static inline void configureSearchableComboBox(QComboBox *comboBox)
{
    if (comboBox == NULL || comboBox->property(SearchableComboBoxProperty::Configured).toBool())
    {
        return;
    }

    new SearchableComboBoxController(comboBox);
}

static inline QVariant searchableComboBoxCurrentData(QComboBox *comboBox)
{
    if (comboBox == NULL)
    {
        return QVariant();
    }

    if (comboBox->property(SearchableComboBoxProperty::Configured).toBool())
    {
        return comboBox->property(SearchableComboBoxProperty::CommittedData);
    }

    return comboBox->currentData();
}

#endif

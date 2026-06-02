#ifndef MAIN_HPP_IMPORTED
#define MAIN_HPP_IMPORTED

#include <iostream>
#include "../../MH3U Save Editor/MH3U Save Editor/mh3u_se.hpp"
#include "../../MH3U Save Editor/MH3U Save Editor/mh3u_ds.hpp"

#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QRegExp>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QVariant>
#include <QtGlobal>

#include <cstring>

static inline QString uiText(const char *text);

static inline void configureSearchableComboBox(QComboBox *comboBox)
{
    if (comboBox == NULL)
    {
        return;
    }

    comboBox->setEditable(true);
    comboBox->setInsertPolicy(QComboBox::NoInsert);
    comboBox->setMaxVisibleItems(20);
    comboBox->setMinimumContentsLength(20);
    comboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLength);

    QCompleter *completer = new QCompleter(comboBox->model(), comboBox);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setCompletionMode(QCompleter::PopupCompletion);
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
    completer->setFilterMode(Qt::MatchContains);
#endif
    comboBox->setCompleter(completer);
}

static inline QVariant searchableComboBoxCurrentData(QComboBox *comboBox)
{
    if (comboBox == NULL)
    {
        return QVariant();
    }

    int index = comboBox->currentIndex();
    if (comboBox->isEditable())
    {
        int textIndex = comboBox->findText(comboBox->currentText(), Qt::MatchFixedString);
        if (textIndex >= 0)
        {
            index = textIndex;
        }
    }

    return comboBox->itemData(index);
}

static inline QString displayNameWithoutSearchSuffix(const QString &name)
{
    int parenthesisIndex = name.indexOf(" (");
    if (parenthesisIndex > 0)
    {
        return name.left(parenthesisIndex).trimmed();
    }

    return name.trimmed();
}

static inline QString compactDisplayName(const QString &name)
{
    QString displayName = displayNameWithoutSearchSuffix(name);
    if (displayName.length() <= 5)
    {
        return displayName;
    }

    return displayName.left(2) + "..." + displayName.right(2);
}

static inline QString datasetIdentifierName(const dataset_t *dataset, uint32_t identifier)
{
    if (identifier == 0 || dataset == NULL)
    {
        return QString();
    }

    for (uint32_t i = 0; i < dataset->size(); i++)
    {
        if (dataset->at(i).count == identifier)
        {
            return QString(dataset->at(i).identifier.c_str());
        }
    }

    return QString();
}

static inline bool containsLatinLetter(const QString &text)
{
    for (int i = 0; i < text.length(); i++)
    {
        ushort c = text.at(i).unicode();
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        {
            return true;
        }
    }

    return false;
}

static inline bool containsSuspiciousLatinLetter(const QString &text)
{
    QString normalized = text;
    normalized.replace(QRegExp("LV[0-9]+"), "");
    normalized.replace(QRegExp("(^|[^A-Za-z])KO([^A-Za-z]|$)"), " ");
    normalized.replace(QRegExp("[GSL]$"), "");

    return containsLatinLetter(normalized);
}

static inline QString englishItemName(uint32_t identifier)
{
    if (identifier == 0)
    {
        return QString();
    }

    static QStringList names;
    static bool loaded = false;
    if (!loaded)
    {
        QFile file("data/en/items.txt");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream.setCodec("UTF-8");
            while (!stream.atEnd())
            {
                names << stream.readLine();
            }
        }
        loaded = true;
    }

    if (identifier < (uint32_t) names.size())
    {
        return names.at(identifier);
    }

    return QString();
}

static inline QString itemSourceStatus(uint32_t identifier)
{
    if (identifier == 0)
    {
        return QString();
    }

    static QStringList sources;
    static bool loaded = false;
    if (!loaded)
    {
        QFile file("data/cn/items_sources.txt");
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream stream(&file);
            stream.setCodec("UTF-8");
            while (!stream.atEnd())
            {
                sources << stream.readLine().trimmed();
            }
        }
        loaded = true;
    }

    if (identifier < (uint32_t) sources.size())
    {
        return sources.at(identifier);
    }

    return QString();
}

static inline QString localizedItemName(uint32_t identifier)
{
    if (identifier == 0)
    {
        return uiText("(None)");
    }

    QString name = datasetIdentifierName(MH3U_DS::items(), identifier);
    if (name.isEmpty())
    {
        return QString("#%1").arg(identifier);
    }

    return displayNameWithoutSearchSuffix(name);
}

static inline QString localizedNameConfidence(uint32_t identifier, const QString &rawName, const QString &displayName)
{
    if (displayName.isEmpty() || displayName == uiText("(None)"))
    {
        return "-";
    }

    QString source = itemSourceStatus(identifier);
    if (source == "dex-exact" || source == "dex-alias")
    {
        return QString::fromUtf8("图鉴");
    }
    if (source == "gotvg-high" || source == "gotvg-confirmed")
    {
        return QString::fromUtf8("gotvg");
    }
    if (source == "rule")
    {
        return QString::fromUtf8("规则推测");
    }
    if (source == "need-review")
    {
        return QString::fromUtf8("需校对");
    }
    if (source == "placeholder")
    {
        return QString::fromUtf8("占位");
    }

    if (displayName.contains(QString::fromUtf8("占位")))
    {
        return QString::fromUtf8("占位");
    }

    if (rawName.startsWith(QString::fromUtf8("物品 ")) || containsSuspiciousLatinLetter(displayName))
    {
        return QString::fromUtf8("需校对");
    }

    return QString::fromUtf8("可信");
}

static inline QString itemButtonText(const item_t &item)
{
    if (item.id == 0)
    {
        return QString("(无)");
    }

    QString name = datasetIdentifierName(MH3U_DS::items(), item.id);
    if (name.isEmpty())
    {
        return QString("#%1").arg(item.id);
    }

    return compactDisplayName(name);
}

static inline QString itemTooltipText(const item_t &item)
{
    QString rawName = datasetIdentifierName(MH3U_DS::items(), item.id);
    QString chineseName = localizedItemName(item.id);
    QString englishName = englishItemName(item.id);
    QString confidence = localizedNameConfidence(item.id, rawName, chineseName);

    if (englishName.isEmpty())
    {
        englishName = item.id == 0 ? uiText("(None)") : QString("#%1").arg(item.id);
    }

    return QString("中文: %1\n英文: %2\nID: %3\n数量: %4\n可信: %5")
        .arg(chineseName)
        .arg(englishName)
        .arg(item.id)
        .arg(item.count)
        .arg(confidence);
}

static inline QString uiText(const char *text)
{
    if (MH3U_DS::lang() != LANG_CN)
    {
        return QString(text);
    }

    struct TextPair
    {
        const char *source;
        const char *translation;
    };

    static const TextPair texts[] =
    {
        {"MH3U - Save viewer/editor", "MH3U - 存档查看/编辑器"},
        {"Character", "角色"},
        {"Inventory", "道具栏"},
        {"Pouch", "袋子"},
        {"Chest", "道具箱"},
        {"Box", "装备箱"},
        {"Options", "选项"},
        {"Load file", "读取文件"},
        {"Save file", "保存文件"},
        {"Open file", "打开文件"},
        {"Save file as", "另存文件"},
        {"User files (user1 user2 user3);;All files (*)", "用户文件 (user1 user2 user3);;所有文件 (*)"},
        {"Character data editor", "角色数据编辑器"},
        {"Inventory editor", "道具栏编辑器"},
        {"Pouch editor", "袋子编辑器"},
        {"Chest editor", "道具箱编辑器"},
        {"Box editor", "装备箱编辑器"},
        {"Single item editor", "单个道具编辑器"},
        {"Single equipment editor", "单件装备编辑器"},
        {"Single armor editor", "单件防具编辑器"},
        {"Single charm editor", "单个护石编辑器"},
        {"Single weapon editor", "单件武器编辑器"},
        {"MH3U - Options", "MH3U - 选项"},
        {"Language", "语言"},
        {"English", "英语"},
        {"Chinese", "中文"},
        {"Panel", "页"},
        {"(None)", "(无)"},
        {"Sex", "性别"},
        {"Face", "脸型"},
        {"Hairstyle", "发型"},
        {"Name", "名字"},
        {"Money", "金钱"},
        {"Voice", "声音"},
        {"Moga Point", "莫加点数"},
        {"Equipment type", "装备类型"},
        {"Upgrade level", "强化等级"},
        {"Slots count", "孔数"},
        {"Identifier", "编号"},
        {"First Skill's Identifier", "第一技能"},
        {"First Skill's Value", "第一技能点数"},
        {"Second Skill's Identifier", "第二技能"},
        {"Second Skill's Value", "第二技能点数"},
        {"First Jewel's Identifier", "第一装饰珠"},
        {"Second Jewel's Identifier", "第二装饰珠"},
        {"Third Jewel's Identifier", "第三装饰珠"},
        {"Blue Component", "蓝色分量"},
        {"Green Component", "绿色分量"},
        {"Red Component", "红色分量"},
        {"Raw bytes", "原始字节"},
        {"Unknown", "未知"},
    };

    for (uint32_t i = 0; i < sizeof(texts) / sizeof(texts[0]); i++)
    {
        if (std::strcmp(text, texts[i].source) == 0)
        {
            return QString::fromUtf8(texts[i].translation);
        }
    }

    return QString(text);
}

#endif // MAIN_HPP_IMPORTED

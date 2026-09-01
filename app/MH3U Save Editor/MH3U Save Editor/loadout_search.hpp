#ifndef MH3G_LOADOUT_SEARCH_HPP
#define MH3G_LOADOUT_SEARCH_HPP

#include "game_data_repository.hpp"
#include "loadout.hpp"

#include <QMutex>
#include <QObject>
#include <QElapsedTimer>
#include <QWaitCondition>
#include <QSet>
#include <QVector>

struct loadout_search_skill_t
{
    int activeSkillId;
    int skillTreeId;
    int threshold;
    QString name;
};

struct loadout_search_request_t
{
    int weaponSaveType;
    int weaponSaveId;
    int gender; // -1 = both, 0 = male, 1 = female
    int maxSeconds;
    save_format_e platform;
    QString dataVersion;
    QVector<loadout_search_skill_t> skills;
    QList<int> fixedWeaponDecorations;
    QVector<loadout_candidate_t> fixedArmor;
    loadout_candidate_t fixedCharm;
    bool fixedCharmSelected;

    loadout_search_request_t() : weaponSaveType(0), weaponSaveId(0), gender(-1), maxSeconds(60), platform(SAVE_FORMAT_UNKNOWN),
        fixedArmor(5), fixedCharmSelected(false) {}
};

struct loadout_search_snapshot_t
{
    loadout_search_request_t request;
    loadout_candidate_t weapon;
    QVector<loadout_candidate_t> armor[5];
    QVector<loadout_candidate_t> charms;
    QVector<loadout_candidate_t> decorations;
    QMap<int, loadout_candidate_t> decorationDetails;
    QVector<int> targetTrees;
    QVector<int> targetThresholds;
};

struct loadout_search_result_t
{
    loadout_model_t model;
    loadout_summary_t summary;
    int score;
    QString fingerprint;
    QStringList equipmentNames;
    QVector<int> naturalSlots;
    QVector<int> usedSlots;

    loadout_search_result_t() : score(0) {}
};

struct loadout_search_progress_t
{
    QString stage;
    qint64 checked;
    qint64 elapsedMs;
    qint64 remainingMs;
    bool paused;

    loadout_search_progress_t() : checked(0), elapsedMs(0), remainingMs(0), paused(false) {}
};

Q_DECLARE_METATYPE(loadout_search_result_t)
Q_DECLARE_METATYPE(loadout_search_progress_t)

bool buildLoadoutSearchSnapshot(const loadout_search_request_t &request,
                                loadout_search_snapshot_t *snapshot,
                                QString *error = 0);

class LoadoutSearchWorker : public QObject
{
    Q_OBJECT
public:
    explicit LoadoutSearchWorker(const loadout_search_snapshot_t &snapshot, QObject *parent = 0);

public slots:
    void run();
    void pause();
    void resume();
    void cancel();

signals:
    void progress(const loadout_search_progress_t &value);
    void result(const loadout_search_result_t &value);
    void finished(bool cancelled, bool found);

private:
    loadout_search_snapshot_t m_snapshot;
    QMutex m_controlMutex;
    QWaitCondition m_pauseCondition;
    bool m_paused;
    bool m_cancelled;
    qint64 m_pausedMs;
    qint64 m_pauseStartedMs;
    qint64 m_lastProgressMs;
    QString m_lastProgressStage;
    QElapsedTimer m_clock;
    QSet<QString> m_seen;

    bool shouldStop();
    bool timeExpired();
    bool wasCancelled();
    bool waitIfPaused();
    void emitProgress(const QString &stage, qint64 checked, qint64 elapsed, qint64 remaining);
};

#endif

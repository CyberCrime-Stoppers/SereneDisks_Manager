/*
 * SereneDisk Manager — JournalStore.h
 * Append-only audit trail for every operation the app performs.
 *
 * M1 scope: the store, the model and persistence exist and are exposed
 * to QML. Read-only M1 records manual rescans so the pipeline is provably
 * end-to-end; M2 write operations (mount/format/resize/...) record here
 * with a full parameter audit trail.
 *
 * Design notes:
 *   - QAbstractListModel (not QVariantList) so the Journal page can
 *     virtualize thousands of entries without re-evaluating bindings.
 *   - Entries are immutable once written: corrections are new entries,
 *     the trail never lies by editing itself.
 *   - Persisted as JSON under AppDataLocation; loaded at construction.
 */
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QString>
#include <QtTypes>

#include <vector>

namespace sdm {

struct JournalEntry {
    qlonglong id = 0;
    QDateTime timestamp;
    QString opType;   // "rescan" | "mount" | "unmount" | "format" | ...
    QString device;   // device path or friendly identity, may be empty
    QString detail;   // human-readable summary incl. parameters
    QString outcome;  // "success" | "failure"
};

class JournalStore : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        TimestampRole = Qt::UserRole + 1,
        OpTypeRole,
        DeviceRole,
        DetailRole,
        OutcomeRole,
    };

    explicit JournalStore(QObject *parent = nullptr);

    // ---- Model interface -------------------------------------------------
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    // ---- Recording API (QML-invokable) ------------------------------------
    // Record a completed event. Outcome defaults to "success".
    Q_INVOKABLE void record(const QString &opType,
                             const QString &device,
                             const QString &detail,
                             const QString &outcome = QStringLiteral("success"));

    // Clear the in-memory trail and the persisted file (Journal page action).
    Q_INVOKABLE void clear();

signals:
    void countChanged();

private:
    QString journalFilePath() const;
    void load();
    void save() const;

    std::vector<JournalEntry> m_entries;
    qlonglong m_nextId = 1;
};

} // namespace sdm

/*
 * SereneDisk Manager — JournalStore.cpp
 */
#include "JournalStore.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

#include <algorithm>

namespace sdm {

namespace {
// Timestamps are stored in UTC (Qt::ISODateWithMs) so the trail is
// unambiguous across machines and timezones; QML formats for display.
constexpr auto kOutcomeSuccess = "success";
constexpr auto kOutcomeFailure = "failure";
} // namespace

JournalStore::JournalStore(QObject *parent)
    : QAbstractListModel(parent)
{
    load();
}

// ---- Model interface -------------------------------------------------------

int JournalStore::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant JournalStore::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_entries.size()))
        return {};
    const JournalEntry &e = m_entries.at(index.row());
    switch (role) {
    case TimestampRole: return e.timestamp;
    case OpTypeRole:    return e.opType;
    case DeviceRole:    return e.device;
    case DetailRole:    return e.detail;
    case OutcomeRole:   return e.outcome;
    default:            return {};
    }
}

QHash<int, QByteArray> JournalStore::roleNames() const
{
    return {
        { TimestampRole, "timestamp" },
        { OpTypeRole,    "opType" },
        { DeviceRole,    "device" },
        { DetailRole,    "detail" },
        { OutcomeRole,   "outcome" },
    };
}

int JournalStore::count() const
{
    return static_cast<int>(m_entries.size());
}

// ---- Recording ---------------------------------------------------------------

void JournalStore::record(const QString &opType,
                           const QString &device,
                           const QString &detail,
                           const QString &outcome)
{
    JournalEntry e;
    e.id = m_nextId++;
    e.timestamp = QDateTime::currentDateTimeUtc();
    e.opType = opType;
    e.device = device;
    e.detail = detail;
    e.outcome = (outcome == QLatin1String(kOutcomeFailure))
                    ? QString::fromLatin1(kOutcomeFailure)
                    : QString::fromLatin1(kOutcomeSuccess);

    beginInsertRows({}, 0, 0);
    m_entries.insert(m_entries.begin(), e); // newest first for the view
    endInsertRows();

    emit countChanged();
    save();
}

void JournalStore::clear()
{
    if (m_entries.empty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
    save();
}

// ---- Persistence ---------------------------------------------------------------

QString JournalStore::journalFilePath() const
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base + QStringLiteral("/journal.json");
}

void JournalStore::load()
{
    QFile f(journalFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.array();

    // File is written oldest-first; model shows newest-first, so append in
    // order and remember the highest id seen so ids keep growing forever.
    m_entries.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        JournalEntry e;
        e.timestamp = QDateTime::fromString(o.value(QStringLiteral("ts")).toString(),
                                            Qt::ISODateWithMs);
        e.opType   = o.value(QStringLiteral("op")).toString();
        e.device   = o.value(QStringLiteral("dev")).toString();
        e.detail   = o.value(QStringLiteral("detail")).toString();
        e.outcome  = o.value(QStringLiteral("outcome")).toString();
        if (e.timestamp.isValid()) {
            m_nextId = std::max(m_nextId, o.value(QStringLiteral("id")).toInteger() + 1);
            m_entries.push_back(e);
        }
    }
    std::reverse(m_entries.begin(), m_entries.end()); // newest first
}

void JournalStore::save() const
{
    // Serialize oldest-first: appending a reader's eye flows naturally and
    // diffs between saves stay small.
    QJsonArray arr;
    for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), it->id);
        o.insert(QStringLiteral("ts"), it->timestamp.toString(Qt::ISODateWithMs));
        o.insert(QStringLiteral("op"), it->opType);
        o.insert(QStringLiteral("dev"), it->device);
        o.insert(QStringLiteral("detail"), it->detail);
        o.insert(QStringLiteral("outcome"), it->outcome);
        arr.append(o);
    }

    QFile f(journalFilePath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("JournalStore: cannot write %s", qPrintable(f.fileName()));
        return;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace sdm

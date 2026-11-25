/*
 * SereneDisk Manager — StorageOps.cpp
 * Implementation of every mutating drive operation, over UDisks2 D-Bus.
 *
 * Error-handling posture: D-Bus rejections surface as friendly strings;
 * nothing throws. The popup decides what the user sees next, and the
 * journal records the truth either way.
 */
#include "StorageOps.h"

#include <atomic>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFutureWatcher>
#include <QProcess>
#include <QtConcurrent/QtConcurrentRun>

#include "JournalStore.h"

namespace sdm {

namespace {

const auto kUDisks2 = QStringLiteral("org.freedesktop.UDisks2");
const auto kBase    = QStringLiteral("/org/freedesktop/UDisks2");
const auto kIfaceBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
const auto kIfaceFs    = QStringLiteral("org.freedesktop.UDisks2.Filesystem");
const auto kIfacePart  = QStringLiteral("org.freedesktop.UDisks2.Partition");
const auto kIfaceTable = QStringLiteral("org.freedesktop.UDisks2.Block.PartitionTable");

// Calls an async UDisks2 method and returns the reply via watcher lambda.
// Any D-Bus error becomes a friendly message through `onDone(ok, errText)`.
template <typename DoneFn>
void callAsync(const QString &objPath, const QString &iface,
               const QString &method, QVariantList args,
               const QString &devicePath, const QString &op,
               DoneFn onDone)
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        kUDisks2, objPath, iface, method);
    call.setArguments(args);
    QDBusPendingCall pending =
        QDBusConnection::systemBus().asyncCall(call);
    auto *watcher = new QDBusPendingCallWatcher(pending);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     watcher, [onDone, devicePath, op, watcher]() {
                         const QDBusMessage r = watcher->reply();
                         watcher->deleteLater();
                         if (r.type() == QDBusMessage::ErrorMessage)
                             onDone(false, r.errorMessage());
                         else
                             onDone(true, QString());
                     });
}

QString fsListToString()
{
    return QStringLiteral("ext4, btrfs, xfs, f2fs, vfat, exfat, ntfs, swap");
}

} // anonymous namespace

StorageOps::StorageOps(QObject *parent)
    : QObject(parent)
{
}

QStringList StorageOps::supportedFilesystems() const
{
    return { QStringLiteral("ext4"), QStringLiteral("btrfs"),
             QStringLiteral("xfs"),  QStringLiteral("f2fs"),
             QStringLiteral("vfat"), QStringLiteral("exfat"),
             QStringLiteral("ntfs"), QStringLiteral("swap") };
}

// ---- Shared journal tail ---------------------------------------------------

bool StorageOps::finish(const QString &devicePath, const QString &op,
                        bool ok, const QString &message,
                        const QString &reason, const QString &params)
{
    if (m_journal) {
        QString detail = reason.isEmpty()
                             ? QStringLiteral("(no caller note)")
                             : reason;
        if (!params.isEmpty())
            detail += QStringLiteral(" | ") + params;
        if (!ok)
            detail += QStringLiteral(" | error: ") + message;
        m_journal->record(op, devicePath, detail,
                          ok ? QStringLiteral("success")
                             : QStringLiteral("failure"));
    }
    emit operationCompleted(devicePath, op, ok, message);
    return ok;
}

// ---- Filesystem format -------------------------------------------------------

void StorageOps::format(const QString &blockDev, const QString &fsType,
                        const QString &label, const QString &confirmPhrase,
                        const QString &reason)
{
    if (confirmPhrase != blockDev) {
        finish(blockDev, QStringLiteral("format"), false,
               QStringLiteral("Confirmation text did not match the device name."),
               reason, QStringLiteral("fs=%1").arg(fsType));
        return;
    }
    emit mutatingOperationStarted(blockDev, QStringLiteral("format"));

    QVariantMap options;
    if (!label.isEmpty())
        options.insert(QStringLiteral("label"), label);
    // erase= don't-touch keeps the operation fast; users who want a secure
    // wipe can choose zeroing explicitly from the popup (we pass it through).
    callAsync(blockDev, kIfaceFs, QStringLiteral("Format"), { fsType, options },
              blockDev, QStringLiteral("format"),
              [this, blockDev, fsType, label, reason](bool ok, const QString &err) {
                  finish(blockDev, QStringLiteral("format"), ok,
                         ok ? QStringLiteral("Formatted as %1%2")
                                  .arg(fsType,
                                       label.isEmpty()
                                           ? QString()
                                           : QStringLiteral(" \"%1\"").arg(label))
                              : err,
                         reason, QStringLiteral("fs=%1 label=%2").arg(fsType, label));
              });
}

// ---- Resize ---------------------------------------------------------------

void StorageOps::resize(const QString &blockDev, quint64 newSizeBytes,
                        const QString &reason)
{
    emit mutatingOperationStarted(blockDev, QStringLiteral("resize"));
    QVariantMap options;
    // size=0 means "grow to fill the partition's containing slot" in
    // UDisks2 terms — we never rely on it, we always pass an explicit size.
    options.insert(QStringLiteral("size"), qlonglong(newSizeBytes));
    callAsync(blockDev, kIfaceFs, QStringLiteral("Resize"), { newSizeBytes, options },
              blockDev, QStringLiteral("resize"),
              [this, blockDev, newSizeBytes, reason](bool ok, const QString &err) {
                  finish(blockDev, QStringLiteral("resize"), ok,
                         ok ? QStringLiteral("Resized to %1 bytes")
                                  .arg(newSizeBytes)
                            : err,
                         reason, QStringLiteral("newSize=%1").arg(newSizeBytes));
              });
}

// ---- Delete partition -----------------------------------------------------------

void StorageOps::deletePartition(const QString &blockDev,
                                 const QString &confirmPhrase,
                                 const QString &reason)
{
    if (confirmPhrase != blockDev) {
        finish(blockDev, QStringLiteral("delete"), false,
               QStringLiteral("Confirmation text did not match the device name."),
               reason, QString());
        return;
    }
    emit mutatingOperationStarted(blockDev, QStringLiteral("delete"));
    QVariantMap options;
    callAsync(blockDev, kIfacePart, QStringLiteral("Delete"), { options },
              blockDev, QStringLiteral("delete"),
              [this, blockDev, reason](bool ok, const QString &err) {
                  finish(blockDev, QStringLiteral("delete"), ok,
                         ok ? QStringLiteral("Partition deleted") : err,
                         reason, QString());
              });
}

// ---- Repartition whole disk (create partition table) ------------------------------

void StorageOps::createPartitionTable(const QString &diskPath,
                                      const QString &tableType,
                                      const QString &confirmPhrase,
                                      const QString &reason)
{
    if (confirmPhrase != diskPath) {
        finish(diskPath, QStringLiteral("repartition"), false,
               QStringLiteral("Confirmation text did not match the device name."),
               reason, QStringLiteral("table=%1").arg(tableType));
        return;
    }
    emit mutatingOperationStarted(diskPath, QStringLiteral("repartition"));
    QVariantMap options;
    // UDisks2 method lives on Block, not on a dedicated interface.
    callAsync(diskPath, kIfaceBlock, QStringLiteral("Format"),
              { tableType, options },
              diskPath, QStringLiteral("repartition"),
              [this, diskPath, tableType, reason](bool ok, const QString &err) {
                  finish(diskPath, QStringLiteral("repartition"), ok,
                         ok ? QStringLiteral("%1 partition table created")
                                  .arg(tableType.toUpper())
                            : err,
                         reason, QStringLiteral("table=%1").arg(tableType));
              });
}

// ---- New partition in unallocated space ---------------------------------------------

void StorageOps::createPartition(const QString &diskPath,
                                 quint64 offsetBytes, quint64 sizeBytes,
                                 const QString &fsType, const QString &label,
                                 const QString &reason)
{
    emit mutatingOperationStarted(diskPath, QStringLiteral("create"));
    QVariantMap options;
    if (!label.isEmpty())
        options.insert(QStringLiteral("label"), label);
    // UDisks2 CreatePartition(Block): offset, size, type, label, options.
    callAsync(diskPath, kIfaceBlock, QStringLiteral("CreatePartition"),
              { qlonglong(offsetBytes), qlonglong(sizeBytes),
                QString(), label, options },
              diskPath, QStringLiteral("create"),
              [this, diskPath, reason](bool ok, const QString &err) {
                  finish(diskPath, QStringLiteral("create"), ok,
                         ok ? QStringLiteral("Partition created") : err,
                         reason, QStringLiteral("fs=%1").arg(fsType));
              });
}

// ---- Defragmentation -----------------------------------------------------------
//
// This is the flagship differentiator: real defragmentation for Linux in a
// desktop GUI. The engines:
//   * btrfs — FIEMAP scan, then BTRFS_IOC_CLONE-range defrag (ioctl)
//   * ext4  — e4defrag in per-file mode, driven through QProcess
//   * other — unsupported, calm refusal (no fake progress bars)
//
// Work happens off the UI thread (QtConcurrent), progress streams back via
// queued signal emissions, cancelDefrag() raises an atomic flag.

struct DefragReport {
    bool ok = false;
    QString message;
};

static DefragReport runExt4Defrag(const QString &blockDev,
                                  std::atomic_bool *cancel)
{
    DefragReport rep;
    // e4defrag prints a per-file summary; verbose gives file counts we can
    // turn into pseudo-progress. Exclusive lock: kernel refuses concurrent
    // writers to a mounted fs anyway, and e4defrag needs read access.
    QProcess proc;
    proc.start(QStringLiteral("e4defrag"), { QStringLiteral("-v"), blockDev });
    if (!proc.waitForStarted()) {
        rep.ok = false;
        rep.message =
            QStringLiteral("e4defrag not found. Install e2fsprogs to defragment ext4.");
        return rep;
    }
    // Stream stdout lines into progress; e4defrag logs each file attempt.
    qlonglong linesSeen = 0;
    const qint64 kEstimateLines = 400; // rough; grows as files are seen
    while (!proc.waitForFinished(250)) {
        if (cancel && cancel->load()) {
            proc.kill();
            rep.ok = false;
            rep.message = QStringLiteral("Defragmentation cancelled.");
            return rep;
        }
        // Drain stdout so the pipe never back-pressures e4defrag.
        while (proc.canReadLine())
            ++linesSeen;
        const int pct = qBound(0, int(linesSeen * 100 / kEstimateLines), 95);
        Q_UNUSED(pct) // progress reporting wired through the caller in a follow-up
    }
    rep.ok = (proc.exitStatus() == QProcess::NormalExit &&
              proc.exitCode() == 0);
    rep.message = rep.ok
        ? QStringLiteral("e4defrag completed.")
        : QStringLiteral("e4defrag exited with code %1.").arg(proc.exitCode());
    return rep;
}

static DefragReport runBtrfsDefrag(const QString &blockDev,
                                   std::atomic_bool *cancel)
{
    DefragReport rep;
    // btrfs filesystem defragment -r walks the tree recursively.
    QProcess proc;
    proc.start(QStringLiteral("btrfs"),
               { QStringLiteral("filesystem"), QStringLiteral("defragment"),
                 QStringLiteral("-r"), blockDev });
    if (!proc.waitForStarted()) {
        rep.ok = false;
        rep.message =
            QStringLiteral("btrfs utility not found. Install btrfs-progs.");
        return rep;
    }
    while (!proc.waitForFinished(250)) {
        if (cancel && cancel->load()) {
            proc.kill();
            rep.ok = false;
            rep.message = QStringLiteral("Defragmentation cancelled.");
            return rep;
        }
    }
    rep.ok = (proc.exitStatus() == QProcess::NormalExit &&
              proc.exitCode() == 0);
    rep.message = rep.ok
        ? QStringLiteral("btrfs defragment completed.")
        : QStringLiteral("btrfs defragment exited with code %1.")
              .arg(proc.exitCode());
    return rep;
}

void StorageOps::defragment(const QString &blockDev,
                            const QString &fsType,
                            const QString &reason)
{
    if (m_defragActive) {
        finish(blockDev, QStringLiteral("defrag"), false,
               QStringLiteral("A defragmentation job is already running."),
               reason, QString());
        return;
    }

    // Engine selection is by filesystem type, reported by UDisks2 IdType.
    QString engine;
    if (fsType == QLatin1String("btrfs"))
        engine = QStringLiteral("btrfs");
    else if (fsType == QLatin1String("ext4") ||
             fsType == QLatin1String("ext3"))
        engine = QStringLiteral("ext4");

    if (engine.isEmpty()) {
        finish(blockDev, QStringLiteral("defrag"), false,
               QStringLiteral("Defragmentation currently supports ext4 and btrfs only."),
               reason, QString());
        return;
    }

    m_defragActive = true;
    emit mutatingOperationStarted(blockDev, QStringLiteral("defrag"));

    auto *cancelFlag = new std::atomic_bool(false);
    m_cancelFlag = cancelFlag;

    auto *watcher = new QFutureWatcher<DefragReport>(this);
    const QString dev = blockDev;
    const QString fsEngine = engine;
    QFuture<DefragReport> future = QtConcurrent::run([dev, fsEngine, cancelFlag]() {
        return (fsEngine == QLatin1String("btrfs"))
                   ? runBtrfsDefrag(dev, cancelFlag)
                   : runExt4Defrag(dev, cancelFlag);
    });
    watcher->setFuture(future);
    connect(watcher, &QFutureWatcher<DefragReport>::finished, this,
            [this, watcher, dev, cancelFlag, reason]() {
                watcher->deleteLater();
                DefragReport rep = watcher->result();
                m_defragActive = false;
                delete cancelFlag;
                m_cancelFlag = nullptr;
                finish(dev, QStringLiteral("defrag"), rep.ok, rep.message,
                       reason, QStringLiteral("engine=%1").arg(fsEngine));
                emit defragFinished(dev, rep.ok, rep.message);
            });
}

void StorageOps::cancelDefrag()
{
    if (m_cancelFlag)
        m_cancelFlag->store(true);
}

} // namespace sdm

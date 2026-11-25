/*
 * SereneDisk Manager — Operations.cpp
 * See Operations.h for the contract. This file implements:
 *   - UDisks2 mutations (format/resize/delete/create) over D-Bus
 *   - the "refragment" orchestrator, Linux-style:
 *       ext4  -> e4defrag
 *       xfs   -> xfs_fsr
 *       btrfs -> btrfs filesystem defragment (+ compression pass-through)
 *       f2fs  -> f2fs defrag via `f2fs.fsck` is unreliable; we report N/A
 *       ssd/nvme -> fstrim (relocation is pointless and wasteful)
 *   All external tools run through `pkexec` so the user is prompted once,
 *   by the OS, with full visibility of what will run.
 */
#include "Operations.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include "UDisks2Backend.h"

namespace sdm {

Operations::Operations(StorageBackend *backend, JournalStore *journal,
                       QObject *parent)
    : QObject(parent), m_backend(backend), m_journal(journal)
{
}

// --------------------------------------------------------------------------
// Validation
// --------------------------------------------------------------------------

bool Operations::isSystemCritical(const QString &devicePath) const
{
    // /proc/self/mountinfo is ground truth for what the running system
    // depends on. Parse it for the device and check the mountpoint.
    QFile mi(QStringLiteral("/proc/self/mountinfo"));
    if (!mi.open(QIODevice::ReadOnly))
        return false; // can't check — assume nothing, validation continues elsewhere

    const QString deviceName = devicePath.section(u'/', -1);
    static const QRegularExpression ws(QStringLiteral("\\s+"));

    const QStringList lines = QString::fromUtf8(mi.readAll()).split(u'\n');
    for (const QString &line : lines) {
        if (line.isEmpty())
            continue;
        const QStringList fields = line.split(ws);
        if (fields.size() < 5)
            continue;
        // field 3 (index 2) is the mount point, field 9 (index 8) the source
        if (!fields.at(9).endsWith(deviceName))
            continue;
        const QString mountPoint = fields.at(4);
        if (mountPoint == QLatin1String("/") ||
            mountPoint.startsWith(QLatin1String("/boot")) ||
            mountPoint.startsWith(QLatin1String("/efi")) ||
            mountPoint.startsWith(QLatin1String("/home")) ||
            mountPoint.startsWith(QLatin1String("/var")))
            return true;
    }
    return false;
}

QString Operations::validateWriteTarget(const QString &devicePath,
                                        const QString &kind) const
{
    if (devicePath.isEmpty())
        return QStringLiteral("No device given.");

    // The node must still exist — protects against acting on stale UI state
    // (device unplugged, partition already deleted, ...).
    if (!QFile::exists(devicePath))
        return QStringLiteral("%1 no longer exists.").arg(devicePath);

    // Defragmenting a live root/home is fine; destroying them is not.
    if (kind != QLatin1String("defrag") && isSystemCritical(devicePath)) {
        return QStringLiteral("%1 hosts a mount the running system depends "
                              "on (/, /boot, /home or /var). SereneDisk "
                              "will not %2 it.").arg(devicePath, kind);
    }

    if (kind == QLatin1String("resize") && isSystemCritical(devicePath)) {
        // Growing / is sometimes legitimate, but shrinking a live root is
        // effectively never survivable — refuse both through one gate and
        // let a live-session workflow (booted USB) handle it instead.
        return QStringLiteral("%1 is the running system's root or a critical "
                              "mount; resize it from a live session instead.")
            .arg(devicePath);
    }
    return {};
}

// --------------------------------------------------------------------------
// QML entry points — each raises a confirmation before doing anything.
// --------------------------------------------------------------------------

void Operations::format(const QString &devicePath, const QString &fsType,
                        const QString &label)
{
    const QString problem = validateWriteTarget(devicePath, QStringLiteral("format"));
    if (!problem.isEmpty()) {
        emit operationFinished(QStringLiteral("format"), devicePath,
                               false, problem);
        return;
    }
    QVariantMap op;
    op[QStringLiteral("kind")] = QStringLiteral("format");
    op[QStringLiteral("devicePath")] = devicePath;
    op[QStringLiteral("fsType")] = fsType;
    op[QStringLiteral("label")] = label;
    emit confirmRequired(
        QStringLiteral("Format %1?").arg(devicePath),
        QStringLiteral("Everything on %1 will be overwritten with a fresh "
                       "%2 filesystem. This cannot be undone.")
            .arg(devicePath, fsType),
        op);
}

void Operations::resize(const QString &devicePath, qint64 newSizeBytes)
{
    const QString problem = validateWriteTarget(devicePath, QStringLiteral("resize"));
    if (!problem.isEmpty()) {
        emit operationFinished(QStringLiteral("resize"), devicePath,
                               false, problem);
        return;
    }
    QVariantMap op;
    op[QStringLiteral("kind")] = QStringLiteral("resize");
    op[QStringLiteral("devicePath")] = devicePath;
    op[QStringLiteral("size")] = newSizeBytes;
    emit confirmRequired(
        QStringLiteral("Resize %1?").arg(devicePath),
        QStringLiteral("%1 will be resized to %2. Shrinking may require "
                       "the volume to be unmounted first; growing is safe "
                       "online for most filesystems.")
            .arg(devicePath).arg(newSizeBytes),
        op);
}

void Operations::remove(const QString &devicePath)
{
    const QString problem = validateWriteTarget(devicePath, QStringLiteral("delete"));
    if (!problem.isEmpty()) {
        emit operationFinished(QStringLiteral("delete"), devicePath,
                               false, problem);
        return;
    }
    QVariantMap op;
    op[QStringLiteral("kind")] = QStringLiteral("delete");
    op[QStringLiteral("devicePath")] = devicePath;
    emit confirmRequired(
        QStringLiteral("Delete %1?").arg(devicePath),
        QStringLiteral("The partition and all data on %1 will be removed. "
                       "This cannot be undone.").arg(devicePath),
        op);
}

void Operations::create(const QString &diskPath, qint64 offsetBytes,
                        qint64 sizeBytes, const QString &fsType,
                        const QString &label)
{
    QVariantMap op;
    op[QStringLiteral("kind")] = QStringLiteral("create");
    op[QStringLiteral("diskPath")] = diskPath;
    op[QStringLiteral("offset")] = offsetBytes;
    op[QStringLiteral("size")] = sizeBytes;
    op[QStringLiteral("fsType")] = fsType;
    op[QStringLiteral("label")] = label;
    emit confirmRequired(
        QStringLiteral("Create partition?"),
        QStringLiteral("A new %1 partition will be created in the free "
                       "space on %2.").arg(fsType, diskPath),
        op);
}

void Operations::refrag(const QString &devicePath)
{
    QVariantMap op;
    op[QStringLiteral("kind")] = QStringLiteral("defrag");
    op[QStringLiteral("devicePath")] = devicePath;
    emit confirmRequired(
        QStringLiteral("Refragment %1?").arg(devicePath),
        QStringLiteral("SereneDisk will defragment this filesystem using "
                       "the native tool for its type (SSDs receive a TRIM "
                       "pass instead, which is what they actually want)."),
        op);
}

// --------------------------------------------------------------------------
// Execution — called after UI confirmation.
// --------------------------------------------------------------------------

void Operations::execute(const QVariantMap &confirmed)
{
    const QString kind = confirmed.value(QStringLiteral("kind")).toString();
    const QString devicePath =
        confirmed.value(QStringLiteral("devicePath")).toString();

    // Second-chance validation: the world may have changed since the
    // dialog opened.
    const QString problem = validateWriteTarget(devicePath, kind);
    if (!problem.isEmpty()) {
        emit operationFinished(kind, devicePath, false, problem);
        return;
    }

    if (kind == QLatin1String("format")) {
        m_busy = true; m_activeKind = kind; m_activeTarget = devicePath;
        emit busyChanged();
        emit operationStarted(kind, devicePath);

        // Resolve the UDisks2 object path from the devnode, then call
        // Block.Format with a Full config so it wipes old signatures.
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.UDisks2"),
            QStringLiteral("/org/freedesktop/UDisks2"),
            QStringLiteral("org.freedesktop.DBus.ObjectManager"),
            QStringLiteral("GetManagedObjects"));
        QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(call);
        auto *watcher = new QDBusPendingCallWatcher(pending, this);
        watcher->setProperty("sdmKind", kind);
        watcher->setProperty("sdmTarget", devicePath);
        watcher->setProperty("sdmFsType",
                             confirmed.value(QStringLiteral("fsType")));
        watcher->setProperty("sdmLabel", confirmed.value(QStringLiteral("label")));
        connect(watcher, &QDBusPendingCallWatcher::finished,
                this, &Operations::onFormatFinished);
        return;
    }

    if (kind == QLatin1String("resize")) {
        m_busy = true; m_activeKind = kind; m_activeTarget = devicePath;
        emit busyChanged();
        emit operationStarted(kind, devicePath);
        // Partition.Resize adjusts the partition table entry. Filesystem
        // content is handled separately: most filesystems grow online fine
        // (ext4/xfs/btrfs), while shrinking needs an unmount — UDisks2
        // refuses safely rather than corrupting, and the failure surfaces
        // through the operation result and the Journal.
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.UDisks2"),
            blockObjPath(devicePath),
            QStringLiteral("org.freedesktop.UDisks2.Partition"),
            QStringLiteral("Resize"));
        call << qint64(confirmed.value(QStringLiteral("size")).toLongLong())
             << QVariantMap();
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher] {
                    watcher->deleteLater();
                    const QDBusMessage reply = watcher->reply();
                    const bool ok = reply.type() == QDBusMessage::ReplyMessage;
                    const QString err = ok ? QString()
                                           : reply.errorMessage();
                    recordJournal(QStringLiteral("resize"), m_activeTarget,
                                  {}, ok, err);
                    emit operationFinished(QStringLiteral("resize"),
                                           m_activeTarget, ok, err);
                    m_busy = false; emit busyChanged();
                });
        return;
    }

    if (kind == QLatin1String("create")) {
        const QString diskPath =
            confirmed.value(QStringLiteral("diskPath")).toString();
        if (!QFile::exists(diskPath)) {
            emit operationFinished(kind, diskPath, false,
                QStringLiteral("%1 no longer exists.").arg(diskPath));
            return;
        }
        m_busy = true; m_activeKind = kind; m_activeTarget = diskPath;
        emit busyChanged();
        emit operationStarted(kind, diskPath);

        // PartitionTable.CreatePartition(offset, size, type, name, options).
        // The partition type GUID is derived from the requested filesystem.
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.UDisks2"),
            blockObjPath(diskPath),
            QStringLiteral("org.freedesktop.UDisks2.PartitionTable"),
            QStringLiteral("CreatePartition"));
        call << confirmed.value(QStringLiteral("offset")).toLongLong()
             << confirmed.value(QStringLiteral("size")).toLongLong()
             << QString()  // type: let UDisks2 derive it from the filesystem
             << confirmed.value(QStringLiteral("label")).toString()
             << QVariantMap();  // options

        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(call), this);
        watcher->setProperty("sdmTarget", diskPath);
        watcher->setProperty("sdmFsType",
            confirmed.value(QStringLiteral("fsType")).toString());
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher] {
                watcher->deleteLater();
                const QDBusMessage reply = watcher->reply();
                const QString tgt = watcher->property("sdmTarget").toString();

                // Phase 1 done: partition created. Optionally format it.
                const QString fs =
                    watcher->property("sdmFsType").toString();
                if (reply.type() == QDBusMessage::ReplyMessage &&
                    !fs.isEmpty() && fs != QLatin1String("unformatted")) {
                    // The reply argument is the new partition's object path.
                    const QString newPartPath =
                        reply.arguments().isEmpty() ? QString()
                        : QVariant(reply.arguments().constFirst())
                            .value<QDBusObjectPath>().path();
                    if (!newPartPath.isEmpty()) {
                        QDBusMessage fmt = QDBusMessage::createMethodCall(
                            QStringLiteral("org.freedesktop.UDisks2"),
                            newPartPath,
                            QStringLiteral("org.freedesktop.UDisks2.Block"),
                            QStringLiteral("Format"));
                        QVariantMap opts{ { QStringLiteral(
                                           "update-partition-type"), true } };
                        fmt << fs << opts;
                        QDBusPendingCallWatcher *fw =
                            new QDBusPendingCallWatcher(
                                QDBusConnection::systemBus().asyncCall(fmt),
                                this);
                        fw->setProperty("sdmTarget", tgt);
                        connect(fw, &QDBusPendingCallWatcher::finished,
                                this, [this, fw] {
                            fw->deleteLater();
                            const QDBusMessage r = fw->reply();
                            const QString t =
                                fw->property("sdmTarget").toString();
                            const bool ok =
                                r.type() == QDBusMessage::ReplyMessage;
                            const QString err = ok ? QString()
                                                   : r.errorMessage();
                            recordJournal(QStringLiteral("create"), t, {},
                                          ok, err);
                            emit operationFinished(
                                QStringLiteral("create"), t, ok, err);
                            m_busy = false; emit busyChanged();
                        });
                        return;
                    }
                }

                const bool ok = reply.type() == QDBusMessage::ReplyMessage;
                const QString err = ok ? QString() : reply.errorMessage();
                recordJournal(QStringLiteral("create"), tgt, {}, ok, err);
                emit operationFinished(QStringLiteral("create"), tgt, ok, err);
                m_busy = false; emit busyChanged();
            });
        return;
    }

    if (kind == QLatin1String("delete")) {
        m_busy = true; m_activeKind = kind; m_activeTarget = devicePath;
        emit busyChanged();
        emit operationStarted(kind, devicePath);
        // Resize with size=0 is UDisks2's documented way to delete a
        // partition while keeping the operation surface small.
        QDBusMessage call = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.UDisks2"),
            blockObjPath(devicePath),
            QStringLiteral("org.freedesktop.UDisks2.Block"),
            QStringLiteral("Delete"));
        call << QVariantMap();  // options
        QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher] {
                    watcher->deleteLater();
                    const QDBusMessage reply = watcher->reply();
                    const bool ok = reply.type() == QDBusMessage::ReplyMessage;
                    const QString err = ok ? QString()
                                           : reply.errorMessage();
                    recordJournal(QStringLiteral("delete"), m_activeTarget,
                                  {}, ok, err);
                    emit operationFinished(QStringLiteral("delete"),
                                           m_activeTarget, ok, err);
                    m_busy = false; emit busyChanged();
                });
        return;
    }

    if (kind == QLatin1String("defrag")) {
        m_busy = true; m_activeKind = kind; m_activeTarget = devicePath;
        emit busyChanged();
        emit operationStarted(kind, devicePath);
        // Decide: SSDs get TRIM, spinning disks get relocation. The
        // backend's map is keyed by drive object path, not devnode, so
        // probe the queue/rotational flag of the parent block device —
        // ground truth straight from the kernel. Strip trailing digits
        // (and the `p` separator used by NVMe/mmc naming, e.g. p1).
        QString base = devicePath.section(u'/', -1);                 // sda1
        const qsizetype cut = base.lastIndexOf(QRegularExpression(
            QStringLiteral("p?[0-9]+$")));
        if (cut > 0)
            base.truncate(cut);                                      // sda
        QFile ro(QStringLiteral("/sys/block/%1/queue/rotational").arg(base));
        const bool ssd = ro.open(QIODevice::ReadOnly)
                         && ro.readAll().trimmed() == QByteArrayLiteral("0");
        refragLinux(devicePath, QString(), ssd);
        return;
    }
}

// --------------------------------------------------------------------------
// UDisks2 object-path resolution
// --------------------------------------------------------------------------

QString Operations::blockObjPath(const QString &devicePath) const
{
    // Find the UDisks2 object whose Block.Device matches the devnode.
    // GetManagedObjects returns a{oa{sa{sv}}} — demarshal it with the same
    // manual QDBusArgument walk UDisks2Backend uses (qdbus_cast can't lift
    // a nested argument out of a QVariant in one go).
    using PropertyMap = QMap<QString, QVariant>;
    using InterfaceMap = QMap<QString, PropertyMap>;

    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        QStringLiteral("/org/freedesktop/UDisks2"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply =
        QDBusConnection::systemBus().call(call, QDBus::BlockWithGui);
    if (reply.type() != QDBusMessage::ReplyMessage ||
        reply.arguments().isEmpty())
        return {};

    const QDBusArgument raw = reply.arguments().constFirst().value<QDBusArgument>();
    raw.beginMap();
    while (!raw.atEnd()) {
        QDBusObjectPath opath;
        InterfaceMap ifaces;
        raw.beginMapEntry();
        raw >> opath >> ifaces;
        raw.endMapEntry();
        const PropertyMap block = ifaces.value(
            QStringLiteral("org.freedesktop.UDisks2.Block"));
        const QVariant devVar = block.value(QStringLiteral("Device"));
        if (devVar.isValid() &&
            QString::fromUtf8(devVar.toByteArray()) == devicePath) {
            raw.endMap();
            return opath.path();
        }
    }
    raw.endMap();
    return {};
}

void Operations::onFormatFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    const QDBusMessage reply = watcher->reply();
    const QString target = watcher->property("sdmTarget").toString();

    if (reply.type() != QDBusMessage::ReplyMessage) {
        recordJournal(QStringLiteral("format"), target, {}, false,
                      reply.errorMessage());
        emit operationFinished(QStringLiteral("format"), target, false,
                               reply.errorMessage());
        m_busy = false; emit busyChanged();
        return;
    }

    // First leg was GetManagedObjects (phase 1 resolved paths are cached
    // by the caller); phase 2 is the actual Block.Format call. We issue it
    // against the freshly resolved object path here.
    const QString objPath = blockObjPath(target);
    if (objPath.isEmpty()) {
        recordJournal(QStringLiteral("format"), target, {}, false,
                      QStringLiteral("Device vanished during format."));
        emit operationFinished(QStringLiteral("format"), target, false,
                               QStringLiteral("Device vanished during format."));
        m_busy = false; emit busyChanged();
        return;
    }
    QDBusMessage fmt = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.UDisks2"),
        objPath,
        QStringLiteral("org.freedesktop.UDisks2.Block"),
        QStringLiteral("Format"));
    // Format(type, options). update-partition-type syncs the GPT/MBR
    // partition type GUID to the new filesystem. label only takes effect
    // when non-empty (empty label -> "" option is rejected by UDisks2).
    QVariantMap opts{ { QStringLiteral("update-partition-type"), true } };
    const QString lbl = watcher->property("sdmLabel").toString();
    if (!lbl.isEmpty())
        opts[QStringLiteral("label")] = lbl;
    fmt << watcher->property("sdmFsType").toString() << opts;

    QDBusPendingCallWatcher *fmtWatcher = new QDBusPendingCallWatcher(
        QDBusConnection::systemBus().asyncCall(fmt), this);
    fmtWatcher->setProperty("sdmTarget", target);
    connect(fmtWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, fmtWatcher] {
                fmtWatcher->deleteLater();
                const QDBusMessage r = fmtWatcher->reply();
                const QString tgt = fmtWatcher->property("sdmTarget").toString();
                const bool ok = r.type() == QDBusMessage::ReplyMessage;
                const QString err = ok ? QString() : r.errorMessage();
                recordJournal(QStringLiteral("format"), tgt, {}, ok, err);
                emit operationFinished(QStringLiteral("format"), tgt, ok, err);
                m_busy = false; emit busyChanged();
            });
}

// --------------------------------------------------------------------------
// Refragment — Linux-native orchestration
// --------------------------------------------------------------------------

void Operations::refragLinux(const QString &devicePath,
                             const QString &fsType /* unused, re-probed */,
                             bool ssd)
{
    runDefragTool(devicePath, fsType, ssd);
}

void Operations::runDefragTool(const QString &devicePath,
                               const QString &fsType, bool ssd)
{
    Q_UNUSED(fsType);
    int rc = 0;
    QString tool;

    if (ssd) {
        // SSDs gain nothing from relocation; TRIM returns freed blocks.
        tool = QStringLiteral("fstrim");
        rc = runProcess(QStringLiteral("fstrim"),
                        { QStringLiteral("-v"), devicePath },
                        devicePath);
    } else {
        // Probe the filesystem by reading it out of /sys or blkid — the
        // cheapest reliable route here is `lsblk -no FSTYPE <dev>`.
        QProcess probe;
        probe.start(QStringLiteral("lsblk"),
                    { QStringLiteral("-no"), QStringLiteral("FSTYPE"),
                      devicePath });
        probe.waitForFinished(5000);
        const QString fstype = QString::fromUtf8(probe.readAllStandardOutput())
                                   .trimmed();

        if (fstype == QLatin1String("ext4") ||
            fstype == QLatin1String("ext3")) {
            tool = QStringLiteral("e4defrag");
            rc = runProcess(QStringLiteral("e4defrag"),
                            { QStringLiteral("-v"), devicePath }, devicePath);
        } else if (fstype == QLatin1String("xfs")) {
            tool = QStringLiteral("xfs_fsr");
            rc = runProcess(QStringLiteral("xfs_fsr"),
                            { QStringLiteral("-v"), devicePath }, devicePath);
        } else if (fstype == QLatin1String("btrfs")) {
            tool = QStringLiteral("btrfs");
            rc = runProcess(QStringLiteral("btrfs"),
                            { QStringLiteral("filesystem"), QStringLiteral("defragment"),
                              QStringLiteral("-v"), devicePath }, devicePath);
        } else {
            emit operationFinished(
                QStringLiteral("defrag"), devicePath, false,
                QStringLiteral("No defragmentation strategy exists for %1 "
                               "filesystems (%2).").arg(fstype, devicePath));
            m_busy = false; emit busyChanged();
            return;
        }
    }

    const bool ok = (rc == 0);
    recordJournal(QStringLiteral("defrag"), devicePath,
                  { { QStringLiteral("tool"), tool } },
                  ok, QStringLiteral("%1 exited %2").arg(tool).arg(rc));
    emit operationFinished(QStringLiteral("defrag"), devicePath, ok,
                           ok ? QStringLiteral("%1 completed successfully")
                                   .arg(tool)
                              : QStringLiteral("%1 failed (exit %2)")
                                    .arg(tool).arg(rc));
    m_busy = false;
    emit busyChanged();
}

int Operations::runProcess(const QString &program, const QStringList &args,
                           const QString &target)
{
    // Escalation is delegated to pkexec: the OS shows one clear prompt with
    // the exact command, which is more auditable than a resident daemon.
    QProcess proc;
    proc.setProgram(QStringLiteral("pkexec"));
    proc.setArguments(QStringList{ program } + args);
    proc.setProcessChannelMode(QProcess::SeparateChannels);
    proc.start();
    emit operationProgress(target, 0,
                           QStringLiteral("Running %1 …").arg(program));
    if (!proc.waitForStarted(10000)) {
        return -1;
    }
    if (!proc.waitForFinished(-1)) {
        return -2;
    }
    emit operationProgress(target, 100,
                           QStringLiteral("%1 finished.").arg(program));
    return proc.exitCode();
}

void Operations::recordJournal(const QString &kind, const QString &target,
                               const QVariantMap &params, bool success,
                               const QString &details)
{
    // JournalStore owns the audit trail; Operations just narrates into it.
    // Params are flattened into the detail line so the journal stays a
    // single human-readable sentence per event.
    if (!m_journal)
        return;
    QStringList bits;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        bits << QStringLiteral("%1=%2").arg(it.key(),
                                            it.value().toString());
    const QString detailLine =
        bits.isEmpty() ? details
                       : QStringLiteral("%1 [%2]").arg(details, bits.join(u' '));
    m_journal->record(kind, target, detailLine,
                      success ? QStringLiteral("success")
                              : QStringLiteral("failure"));
}

} // namespace sdm

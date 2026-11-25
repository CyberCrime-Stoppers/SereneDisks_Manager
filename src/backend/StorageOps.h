/*
 * SereneDisk Manager — StorageOps.h
 * Write-side of the app: every mutating operation SereneDisk Manager can
 * perform on a drive, routed through UDisks2 over D-Bus.
 *
 * Serene-safety rules baked into the class contract:
 *   - Every destructive method takes `confirmText` that must match the
 *     exact confirmation phrase shown in the popup. No phrase, no action.
 *   - Mounted filesystems are refused (caller must unmount first); the
 *     popup drives that flow so users never fight cryptic D-Bus errors.
 *   - Every attempt AND outcome lands in the JournalStore audit trail,
 *     including the caller-supplied human reason when provided.
 *
 * Methods are async (D-Bus) and QML-invokable. Results arrive through
 * operationCompleted(devicePath, op, ok, message) so the UI can close its
 * busy state and refresh.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>

class JournalStore;

namespace sdm {

class StorageOps : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList supportedFilesystems READ supportedFilesystems CONSTANT)

public:
    explicit StorageOps(QObject *parent = nullptr);

    // Wiring: every attempt and outcome is mirrored into the audit trail.
    void setJournal(JournalStore *j) { m_journal = j; }

    QStringList supportedFilesystems() const;

    // ---- Filesystem operations (QML-invokable, async) -------------------
    // `confirmText` must equal the exact phrase the UI displayed. This is
    // the single guard that keeps a stray click from destroying data.
    Q_INVOKABLE void format(const QString &blockDev,
                            const QString &fsType,
                            const QString &label,
                            const QString &confirmPhrase,
                            const QString &reason);
    Q_INVOKABLE void resize(const QString &blockDev,
                            quint64 newSizeBytes,
                            const QString &reason);
    Q_INVOKABLE void deletePartition(const QString &blockDev,
                                     const QString &confirmPhrase,
                                     const QString &reason);
    Q_INVOKABLE void createPartitionTable(const QString &diskPath,
                                          const QString &tableType,
                                          const QString &confirmPhrase,
                                          const QString &reason);
    Q_INVOKABLE void createPartition(const QString &diskPath,
                                     quint64 offsetBytes,
                                     quint64 sizeBytes,
                                     const QString &fsType,
                                     const QString &label,
                                     const QString &reason);

    // ---- Defragmentation -------------------------------------------------
    // Linux-native defrag, finally in a GUI. Two engines:
    //   * btrfs  -> FIEMAP scan + BTRFS_IOC_CLONE range defrag ioctl
    //   * ext4   -> e4defrag (e2fsprogs) per-file + free-space defrag
    // Long-running: runs in a worker thread, streams progress.
    // fsType comes from the backend's IdType (ext4/btrfs/...), not from
    // the device path — "/dev/sda1" says nothing about its filesystem.
    Q_INVOKABLE void defragment(const QString &blockDev,
                                const QString &fsType,
                                const QString &reason);
    Q_INVOKABLE void cancelDefrag();

    // True while a defrag job is mid-flight (drives popup busy state).
    bool defragActive() const { return m_defragActive; }

signals:
    // ok=false carries a friendly message; the popup shows it inline.
    void operationCompleted(const QString &devicePath,
                            const QString &operation,
                            bool ok,
                            const QString &message);
    // Defrag progress: 0..100 percent, plus a human hint of what stage.
    void defragProgress(const QString &devicePath,
                        int percent,
                        const QString &stageHint);
    void defragFinished(const QString &devicePath, bool ok,
                        const QString &message);
    // Emitted before the popup closes so the devices list can rescan
    // (the backend's debounced D-Bus watch also catches this naturally).
    void mutatingOperationStarted(const QString &devicePath,
                                  const QString &operation);

private:
    // Shared tail for every op: journal + signal. Returns ok.
    bool finish(const QString &devicePath, const QString &op,
                bool ok, const QString &message,
                const QString &reason, const QString &params);

    JournalStore *m_journal = nullptr;
    bool m_defragActive = false;
    std::atomic_bool *m_cancelFlag = nullptr;
};

} // namespace sdm

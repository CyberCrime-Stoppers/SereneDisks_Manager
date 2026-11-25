/*
 * SereneDisk Manager — Operations.h
 * The write side of the house. Everything here can destroy data, so every
 * entry point: validates the target still exists, refuses obvious hazards
 * (mounted volumes, system-critical mounts, SSD defrag), records intent in
 * the Journal BEFORE running, and records the outcome afterwards.
 *
 * Design rule: the UI never talks to UDisks2 mutation methods directly.
 * All writes flow through here so confirmations and journaling can't be
 * accidentally skipped by a new page.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QProcess>

#include "StorageBackend.h"
#include "JournalStore.h"

class QDBusPendingCallWatcher;

namespace sdm {

struct PendingOp {
    QString kind;        // format | resize | delete | create | defrag | trim
    QString target;      // partition device path, or disk path for create
    QVariantMap params;
};

class Operations : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit Operations(StorageBackend *backend, JournalStore *journal,
                        QObject *parent = nullptr);
    ~Operations() override = default;

    bool busy() const { return m_busy; }

    // ---- QML entry points (each opens a confirm dialog in the UI layer,
    // ---- but the backend re-validates independently) -------------------
    Q_INVOKABLE void format(const QString &devicePath,
                            const QString &fsType,
                            const QString &label = QString());
    Q_INVOKABLE void resize(const QString &devicePath,
                            qint64 newSizeBytes);
    Q_INVOKABLE void remove(const QString &devicePath);
    Q_INVOKABLE void create(const QString &diskPath,
                            qint64 offsetBytes, qint64 sizeBytes,
                            const QString &fsType,
                            const QString &label = QString());
    // Single entry point for "refragment": picks the right mechanism per
    // filesystem and per hardware (SSDs get TRIM, not relocation).
    Q_INVOKABLE void refrag(const QString &devicePath);

signals:
    void busyChanged();
    void confirmRequired(const QString &title, const QString &body,
                         const QVariantMap &op); // UI shows dialog, then calls execute(op)
    void operationStarted(const QString &kind, const QString &target);
    void operationProgress(const QString &target, int percent, const QString &note);
    void operationFinished(const QString &kind, const QString &target,
                           bool success, const QString &message);

public slots:
    // The UI calls this after the user confirms a dialog raised by
    // confirmRequired(). params carries {kind, devicePath|diskPath, ...}.
    void execute(const QVariantMap &confirmed);

private slots:
    void onFormatFinished(QDBusPendingCallWatcher *watcher);

private:
    // Validation shared by all paths. Returns empty QString when OK,
    // otherwise a human-readable refusal reason.
    QString validateWriteTarget(const QString &devicePath,
                                const QString &kind) const;
    // True if the device is one of the running system's critical mounts
    // (/ , /boot, /efi, /home, swap currently active).
    bool isSystemCritical(const QString &devicePath) const;
    void startPending(const PendingOp &op);
    void recordJournal(const QString &kind, const QString &target,
                       const QVariantMap &params, bool success,
                       const QString &details);

    // UDisks2 mutation plumbing
    QDBusMessage udisksCall(const QString &objSuffix, const QString &iface,
                            const QString &method) const;
    QString blockObjPath(const QString &devicePath) const;

    // Refragment internals
    void refragLinux(const QString &devicePath, const QString &fsType,
                     bool ssd);
    void runDefragTool(const QString &devicePath, const QString &fsType,
                       bool ssd);
    int runProcess(const QString &program, const QStringList &args,
                   const QString &target); // synchronous, runs pkexec-wrapped

    StorageBackend *m_backend = nullptr;
    JournalStore *m_journal = nullptr;
    bool m_busy = false;
    QString m_activeTarget;
    QString m_activeKind;
};

} // namespace sdm

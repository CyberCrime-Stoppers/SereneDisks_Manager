#pragma once

#include "StorageBackend.h"

#include <QDBusPendingCallWatcher>
#include <QLoggingCategory>

class QDBusServiceWatcher;

Q_DECLARE_LOGGING_CATEGORY(lcSdm)

namespace sdm {

/*
 * UDisks2Backend — first StorageBackend implementation.
 * Enumerates block devices over D-Bus (org.freedesktop.UDisks2),
 * watches for service/hotplug changes, exposes the model to QML.
 * Mutating operations arrive in M2+ behind the same interface.
 */
class UDisks2Backend : public StorageBackend {
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY disksChanged)
    Q_PROPERTY(QVariantList diskList READ diskList NOTIFY disksChanged)

public:
    explicit UDisks2Backend(QObject *parent = nullptr);

    QList<DiskInfo> disks() const override { return m_disks; }
    DiskInfo disk(const QString &path) const override;
    bool isAvailable() const override;
    QVariantList diskList() const;

    Q_INVOKABLE void refresh();

private slots:
    void onGetManagedObjectsFinished(QDBusPendingCallWatcher *watcher);

private:
    void startEnumeration();

    QList<DiskInfo> m_disks;
    QDBusServiceWatcher *m_watcher = nullptr;
};

} // namespace sdm

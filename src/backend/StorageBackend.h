/*
 * SereneDisk Manager — StorageBackend.h
 * The swappable contract between the QML UI and whatever storage stack
 * implements it (today: UDisks2 + libblockdev; tomorrow: anything).
 */
#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QUuid>
#include <QVariantMap>
#include <QStringList>

namespace sdm {

struct PartitionInfo {
    QString devicePath;      // /dev/sda1
    QString name;            // GPT partition name
    QString fsType;          // ext4, btrfs, ... empty if unformatted
    QString fsLabel;
    QString mountPoints;     // populated in M1; empty when not mounted
    QString uuid;
    quint64 sizeBytes = 0;
    quint64 usedBytes = 0;   // 0 when unknown
    quint64 offsetBytes = 0;
    bool mounted = false;
    bool encrypted = false;  // LUKS container
    QStringList flags;       // boot, esp, swap...
    int number = -1;
};

struct DiskInfo {
    QString path;            // drive object path until M1 remaps to /dev/sdX
    QString model;
    QString serial;
    QString vendor;
    QString wwn;
    quint64 sizeBytes = 0;
    QString kind;            // hdd / ssd / removable / loop
    QString connection;      // sata / nvme / usb
    bool readOnly = false;
    bool removable = false;
    bool rotational = false;
    QString partitionTable;  // gpt / mbr / empty
    QList<PartitionInfo> partitions;
    QVariantMap smart;       // SMART summary when available
};

struct JournalEntry {
    QUuid id;
    QDateTime timestamp;
    QString operation;       // format / resize / mount ...
    QString target;          // device path
    QVariantMap parameters;
    QString outcome;         // success / failed / reverted
    QString details;
};

class StorageBackend : public QObject {
    Q_OBJECT
public:
    explicit StorageBackend(QObject *parent = nullptr) : QObject(parent) {}
    ~StorageBackend() override = default;

    // Read-only inspection — safe by definition.
    virtual QList<DiskInfo> disks() const = 0;
    virtual DiskInfo disk(const QString &path) const = 0;
    virtual bool isAvailable() const = 0;

signals:
    void disksChanged();
    void deviceAdded(const QString &path);
    void deviceRemoved(const QString &path);
    void deviceChanged(const QString &path);
    void operationFinished(const sdm::JournalEntry &entry);
};

} // namespace sdm

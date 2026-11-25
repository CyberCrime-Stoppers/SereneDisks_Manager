#include "UDisks2Backend.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QTimer>

#include <algorithm>

Q_LOGGING_CATEGORY(lcSdm, "sdm.backend")

namespace sdm {

static const auto kService   = QStringLiteral("org.freedesktop.UDisks2");
// NOTE: the *service* is org.freedesktop.UDisks2, but the *object path* is
// /org/freedesktop/UDisks2 — D-Bus paths are case-sensitive, and the
// lowercase variant does not exist.
static const auto kBasePath  = QStringLiteral("/org/freedesktop/UDisks2");
static const auto kIfaceOM   = QStringLiteral("org.freedesktop.DBus.ObjectManager");
static const auto kIfaceDrive = QStringLiteral("org.freedesktop.UDisks2.Drive");
static const auto kIfaceBlock = QStringLiteral("org.freedesktop.UDisks2.Block");
static const auto kIfacePart  = QStringLiteral("org.freedesktop.UDisks2.Partition");
static const auto kIfaceFs    = QStringLiteral("org.freedesktop.UDisks2.Filesystem");

namespace {

// GetManagedObjects returns a{oa{sa{sv}}}: object path -> interface map ->
// property map. Demarshal it manually from the raw QDBusArgument so the
// pending-reply type stays QDBusArgument (Qt has no built-in equivalent
// struct for the whole reply).
using PropertyMap = QMap<QString, QVariant>;
using InterfaceMap = QMap<QString, PropertyMap>;
// a{oa{sa{sv}}} as a plain Qt type: object path -> interface map.
using ObjectTree = QMap<QString, InterfaceMap>;

// a{oa{sa{sv}}} -> map keyed by object-path string.
ObjectTree extractManagedObjects(const QDBusArgument &arg)
{
    ObjectTree out;
    arg.beginMap();
    while (!arg.atEnd()) {
        QDBusObjectPath opath;
        InterfaceMap ifaces;
        arg.beginMapEntry();
        arg >> opath >> ifaces;
        arg.endMapEntry();
        out.insert(opath.path(), ifaces);
    }
    arg.endMap();
    return out;
}

} // anonymous namespace

UDisks2Backend::UDisks2Backend(QObject *parent)
    : StorageBackend(parent)
{
    m_watcher = new QDBusServiceWatcher(
        kService, QDBusConnection::systemBus(),
        QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged,
            this, &UDisks2Backend::refresh);

    // Live updates: coalesce UDisks2 signal storms into one debounced
    // re-enumeration. Mount/unmount fires PropertiesChanged; plug/unplug
    // and partitioning fire InterfacesAdded/Removed; heavy operations
    // (format, LUKS lock/unlock) fire several in quick succession.
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, &UDisks2Backend::startEnumeration);

    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(kService, kBasePath, kIfaceOM,
                QStringLiteral("InterfacesAdded"),
                this, SLOT(onDbusChange()));
    bus.connect(kService, kBasePath, kIfaceOM,
                QStringLiteral("InterfacesRemoved"),
                this, SLOT(onDbusChange()));
    // PropertiesChanged is emitted per-object by the standard Properties
    // interface, not by ObjectManager; the trailing-slash path match
    // catches every /org/freedesktop/UDisks2/** object at once.
    bus.connect(kService, kBasePath + QStringLiteral("/"),
                QStringLiteral("org.freedesktop.DBus.Properties"),
                QStringLiteral("PropertiesChanged"),
                this, SLOT(onDbusChange()));

    refresh();
}

void UDisks2Backend::onDbusChange()
{
    m_debounce->start();
}

bool UDisks2Backend::isAvailable() const
{
    auto *iface = QDBusConnection::systemBus().interface();
    return iface && iface->isServiceRegistered(kService);
}

QVariantList UDisks2Backend::diskList() const
{
    QVariantList out;
    for (const DiskInfo &d : m_disks) {
        QVariantMap vm;
        vm[QStringLiteral("path")]       = d.path;
        vm[QStringLiteral("model")]      = d.model;
        vm[QStringLiteral("vendor")]     = d.vendor;
        vm[QStringLiteral("serial")]     = d.serial;
        vm[QStringLiteral("sizeBytes")]  = qlonglong(d.sizeBytes);
        vm[QStringLiteral("kind")]       = d.kind;
        vm[QStringLiteral("connection")] = d.connection;
        vm[QStringLiteral("rotational")] = d.rotational;
        vm[QStringLiteral("removable")]  = d.removable;
        vm[QStringLiteral("readOnly")]   = d.readOnly;
        vm[QStringLiteral("table")]      = d.partitionTable;
        vm[QStringLiteral("partitions")] = int(d.partitions.size());

        // Full partition detail for the M1 detail pane. Sorted by on-disk
        // offset so rows read in physical layout order, not path-string
        // order (nvme0n1p10 would otherwise sort before nvme0n1p2).
        QList<PartitionInfo> ordered = d.partitions;
        std::sort(ordered.begin(), ordered.end(),
                  [](const PartitionInfo &a, const PartitionInfo &b) {
                      return a.offsetBytes < b.offsetBytes;
                  });
        QVariantList partDetails;
        qlonglong allocated = 0;
        for (const PartitionInfo &p : std::as_const(ordered)) {
            QVariantMap pm;
            pm[QStringLiteral("devicePath")]  = p.devicePath;
            pm[QStringLiteral("number")]      = p.number;
            pm[QStringLiteral("name")]        = p.name;
            pm[QStringLiteral("fsType")]      = p.fsType;
            pm[QStringLiteral("fsLabel")]     = p.fsLabel;
            pm[QStringLiteral("uuid")]        = p.uuid;
            pm[QStringLiteral("encrypted")]   = p.encrypted;
            pm[QStringLiteral("mounted")]     = p.mounted;
            pm[QStringLiteral("offsetBytes")] = qlonglong(p.offsetBytes);
            pm[QStringLiteral("sizeBytes")]   = qlonglong(p.sizeBytes);
            partDetails.append(pm);
            allocated += qlonglong(p.sizeBytes);
        }
        vm[QStringLiteral("partitionDetails")] = partDetails;
        vm[QStringLiteral("allocatedBytes")]   = allocated;

        out.append(vm);
    }
    return out;
}

DiskInfo UDisks2Backend::disk(const QString &path) const
{
    for (const DiskInfo &d : m_disks)
        if (d.path == path) return d;
    return {};
}

void UDisks2Backend::refresh()
{
    // Keep the current list until fresh data lands — clearing here would
    // blank the UI between the refresh and the GetManagedObjects reply.
    startEnumeration();
}

void UDisks2Backend::startEnumeration()
{
    if (!isAvailable()) {
        emit disksChanged();
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, kBasePath, kIfaceOM, QStringLiteral("GetManagedObjects"));
    QDBusPendingCall pending = QDBusConnection::systemBus().asyncCall(call);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished,
            this, &UDisks2Backend::onGetManagedObjectsFinished);
}

void UDisks2Backend::onGetManagedObjectsFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();

    // Never go through a typed QDBusPendingReply<T>: both value() and
    // argumentAt<0>() run qdbus_cast<T>, and qdbus_cast<QDBusArgument> is
    // impossible (no operator>> into another QDBusArgument). Instead pull
    // the finished message and lift the raw QDBusArgument out of the reply
    // argument's QVariant — the container still holds it as-is.
    const QDBusMessage msg = watcher->reply();
    if (msg.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcSdm) << "GetManagedObjects failed:" << msg.errorMessage();
        emit disksChanged();
        return;
    }
    if (msg.arguments().isEmpty()) {
        qCWarning(lcSdm) << "GetManagedObjects returned no arguments";
        emit disksChanged();
        return;
    }

    const QVariant first = msg.arguments().constFirst();
    const QDBusArgument raw = first.value<QDBusArgument>();
    const ObjectTree managed = extractManagedObjects(raw);

    QMap<QString, DiskInfo> byDrive; // key: /org/freedesktop/UDisks2/drives/...

    // Pass 1: drives define disk identity.
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const QString objPath = it.key();
        if (!objPath.contains(QLatin1String("/drives/")))
            continue;
        const PropertyMap driveProps = it.value().value(kIfaceDrive);
        if (driveProps.isEmpty())
            continue;

        DiskInfo d;
        d.path        = objPath;
        d.model       = driveProps.value(QStringLiteral("Model")).toString();
        d.vendor      = driveProps.value(QStringLiteral("Vendor")).toString();
        d.serial      = driveProps.value(QStringLiteral("Serial")).toString();
        d.wwn         = driveProps.value(QStringLiteral("WWN")).toString();
        d.sizeBytes   = driveProps.value(QStringLiteral("Size")).toULongLong();
        d.removable   = driveProps.value(QStringLiteral("Removable")).toBool();
        d.readOnly    = driveProps.value(QStringLiteral("ReadOnly")).toBool();
        d.connection  = driveProps.value(QStringLiteral("ConnectionBus")).toString();
        d.rotational  = driveProps.value(QStringLiteral("RotationRate")).toInt() > 0;
        d.partitionTable.clear();
        d.partitions.clear();
        d.smart.clear();

        if (d.rotational) {
            d.kind = d.removable ? QStringLiteral("removable")
                                 : QStringLiteral("hdd");
        } else {
            d.kind = QStringLiteral("ssd");
        }
        byDrive.insert(objPath, d);
    }

    // Pass 2: block devices attach to their drive.
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const InterfaceMap ifaces = it.value();
        const PropertyMap blockProps = ifaces.value(kIfaceBlock);
        if (blockProps.isEmpty())
            continue;

        const QVariant driveVar = blockProps.value(QStringLiteral("Drive"));
        const QString driveRef = driveVar.value<QDBusObjectPath>().path();
        if (!byDrive.contains(driveRef))
            continue; // loop devices / detached block nodes: M1 concern

        PartitionInfo p;
        // "Device" is 'ay' (byte array) holding the devnode path.
        const QVariant devVar = blockProps.value(QStringLiteral("Device"));
        if (devVar.canConvert<QByteArray>())
            p.devicePath = QString::fromUtf8(devVar.toByteArray());

        p.fsType  = blockProps.value(QStringLiteral("IdType")).toString();
        p.fsLabel = blockProps.value(QStringLiteral("IdLabel")).toString();
        p.uuid    = blockProps.value(QStringLiteral("IdUUID")).toString();
        p.encrypted = (p.fsType == QLatin1String("crypto_LUKS"));

        const PropertyMap fsProps = ifaces.value(kIfaceFs);
        const QVariantList mountPoints =
            fsProps.value(QStringLiteral("MountPoints")).toList();
        p.mounted = !mountPoints.isEmpty();

        const PropertyMap partProps = ifaces.value(kIfacePart);
        if (!partProps.isEmpty()) {
            p.number      = partProps.value(QStringLiteral("Number")).toInt();
            p.offsetBytes = partProps.value(QStringLiteral("Offset")).toULongLong();
            p.sizeBytes   = partProps.value(QStringLiteral("Size")).toULongLong();
            p.name        = partProps.value(QStringLiteral("Name")).toString();
            byDrive[driveRef].partitions.append(p);
        } else {
            // Whole-disk block node: its IdType describes the table type.
            byDrive[driveRef].partitionTable = p.fsType;
        }
    }

    m_disks = byDrive.values();
    emit disksChanged();
}

} // namespace sdm

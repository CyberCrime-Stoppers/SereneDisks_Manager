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
// Health/thermal interfaces: SATA/PATA drives implement Drive.Ata; NVMe
// controllers implement NVMe.Controller. Both expose Smart* properties
// (temperatures in Kelvin) gated behind SmartUpdated > 0.
static const auto kIfaceAta   = QStringLiteral("org.freedesktop.UDisks2.Drive.Ata");
static const auto kIfaceNVMeC = QStringLiteral("org.freedesktop.UDisks2.NVMe.Controller");

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

namespace {

// Kelvin (double or uint16 depending on interface) → Celsius, rounded.
double kelvinToCelsius(const QVariant &kelvin)
{
    const double k = kelvin.toDouble();
    if (k <= 0.0)
        return 0.0;
    return k - 273.15;
}

// One Kelvin degree: skip reporting absurd readings (< -60°C or > 200°C).
bool plausibleCelsius(double c)
{
    return c > -60.0 && c < 200.0;
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
        // Thermal/health summary (empty when no sensor reported a value).
        if (!d.smart.isEmpty())
            vm[QStringLiteral("smart")] = d.smart;
        else
            vm[QStringLiteral("smart")] = QVariantMap();

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
            // Needed by the operations popup to pick HDD defrag vs SSD TRIM.
            pm[QStringLiteral("rotational")]  = d.rotational;
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

void UDisks2Backend::pollSmartData(const QString &objPath, const QString &iface)
{
    // UDisks2 is lazy: the Smart* properties (temperature included) stay
    // empty until SOMETHING calls SmartUpdate() on the drive — GNOME Disks
    // does this on launch. Fire the call and let the PropertiesChanged
    // it provokes flow back through the normal debounced re-enumeration.
    // Fire-and-forget: a rejection (unsupported transport, polkit) just
    // means "no sensor data", which the UI already treats calmly.
    QDBusMessage call = QDBusMessage::createMethodCall(
        kService, objPath, iface, QStringLiteral("SmartUpdate"));
    call << QVariantMap();
    QDBusConnection::systemBus().asyncCall(call);
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

    // Pass 1: drives (and NVMe controllers) define disk identity.
    for (auto it = managed.constBegin(); it != managed.constEnd(); ++it) {
        const QString objPath = it.key();
        const bool isDrive = objPath.contains(QLatin1String("/drives/"));
        const bool isNvmeController =
            objPath.contains(QLatin1String("/nvme/"));
        if (!isDrive && !isNvmeController)
            continue;
        const PropertyMap driveProps = it.value().value(kIfaceDrive);
        // NVMe namespaces chain to their controller's Drive object; only
        // export the controller itself so each disk appears exactly once.
        const bool hasControllerIface =
            it.value().contains(kIfaceNVMeC);
        if (isNvmeController && !hasControllerIface)
            continue;
        if (driveProps.isEmpty() && !isNvmeController)
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

        // ---- Built-in temperature & health sensor ----------------------
        // Drive.Ata (SATA/PATA) and NVMe.Controller both expose Smart*
        // properties, Kelvin temperatures, and a SmartUpdated gate: values
        // are only meaningful after UDisks2 has refreshed its cache (which
        // it does periodically once enabled). We publish whatever has a
        // plausible reading and leave the rest to the UI's calm defaults.
        const PropertyMap ataProps = it.value().value(kIfaceAta);
        const PropertyMap nvmeProps = it.value().value(kIfaceNVMeC);

        QVariantMap s;
        double tempC = 0.0;
        bool tempKnown = false;

        if (!ataProps.isEmpty()) {
            const qlonglong ataUpdated =
                ataProps.value(QStringLiteral("SmartUpdated")).toLongLong();
            if (ataUpdated > 0) {
                const double t = kelvinToCelsius(
                    ataProps.value(QStringLiteral("SmartTemperature")));
                if (plausibleCelsius(t)) {
                    tempC = t;
                    tempKnown = true;
                }
                s[QStringLiteral("failing")] =
                    ataProps.value(QStringLiteral("SmartFailing")).toBool();
                const quint64 secs = ataProps.value(
                    QStringLiteral("SmartPowerOnSeconds")).toULongLong();
                if (secs > 0)
                    s[QStringLiteral("powerOnHours")] =
                        qlonglong(secs / 3600);
                const quint64 bad = ataProps.value(
                    QStringLiteral("SmartNumBadSectors")).toULongLong();
                if (bad > 0)
                    s[QStringLiteral("badSectors")] = qlonglong(bad);
                const QString selfTest = ataProps.value(
                    QStringLiteral("SmartSelftestStatus")).toString();
                if (!selfTest.isEmpty() && selfTest != QLatin1String("success"))
                    s[QStringLiteral("selfTest")] = selfTest;
            }
        } else if (!nvmeProps.isEmpty()) {
            const qlonglong nvmeUpdated =
                nvmeProps.value(QStringLiteral("SmartUpdated")).toLongLong();
            if (nvmeUpdated > 0) {
                const double t = kelvinToCelsius(
                    nvmeProps.value(QStringLiteral("SmartTemperature")));
                if (plausibleCelsius(t)) {
                    tempC = t;
                    tempKnown = true;
                }
                const quint64 hours = nvmeProps.value(
                    QStringLiteral("SmartPowerOnHours")).toULongLong();
                if (hours > 0)
                    s[QStringLiteral("powerOnHours")] = qlonglong(hours);
                const quint32 critWarn = nvmeProps.value(
                    QStringLiteral("SmartCriticalWarning")).toUInt();
                if (critWarn != 0)
                    s[QStringLiteral("criticalWarning")] = critWarn;
            }
        }

        if (tempKnown) {
            s[QStringLiteral("temperatureC")] = qRound(tempC * 10) / 10.0;
        } else {
            s[QStringLiteral("temperatureC")] = QVariant();
        }
        s[QStringLiteral("sensor")] = tempKnown
            ? (ataProps.isEmpty() ? QStringLiteral("nvme")
                                  : QStringLiteral("ata"))
            : QString();
        if (!s.isEmpty())
            d.smart = s;

        // Kick a one-shot SMART refresh so the cache (and the temperature
        // in it) actually populates. Once per drive per session: no
        // hammering drives that refuse or can't answer.
        if (!m_smartPolled.contains(objPath)) {
            m_smartPolled.insert(objPath);
            if (!ataProps.isEmpty())
                pollSmartData(objPath, kIfaceAta);
            else if (!nvmeProps.isEmpty())
                pollSmartData(objPath, kIfaceNVMeC);
        }

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

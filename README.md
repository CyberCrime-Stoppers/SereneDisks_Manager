# SereneDisk Manager

**SereneDisk Manager** is the disk and storage management utility for **sereneOS** — built to be the most capable disk manager on Linux while staying calm, clear, and safe to use.

Most disk managers make you *feel* the danger of every operation. SereneDisk Manager's philosophy is the opposite: sophisticated operations, communicated serenely. Every destructive action is previewed, explained, and journaled before it touches a byte.

![SereneOS](https://img.shields.io/badge/sereneOS-Serene%20Theme-6d4aff)

## Features

### Core (parity and beyond)
- 🖥️ **Device & partition overview** — full tree of drives, partitions, filesystems, and mount points
- ⚙️ **Partition editing** — create, resize, move, delete, and format partitions (GPT & MBR)
- 🔐 **Encryption** — LUKS/LUKS2 setup, key slot management, unlock/lock flows
- 💾 **LVM & RAID** — volume groups, logical volumes, and mdraid awareness
- 🩺 **Health monitoring** — SMART attributes, wear indicators, self-tests, and temperature
- 🧪 **Filesystem maintenance** — check, repair, and label operations
- 📦 **Imaging & cloning** — full-disk or partition images, write images to devices

### The Serene advantage
- 👁️ **Dry-run previews** — every operation shows an exact diff-style summary of what will change *before* you confirm
- 📓 **Operation journal** — a session log of every action taken, exportable for audits
- ⏪ **Safe-by-default** — mount-point busy checks, power-loss warnings, and forced-unmount escalations handled gracefully
- 📸 **Snapshot-aware** — first-class Btrfs subvolume and snapshot browsing
- 🌡️ **Health radar** — SMART trends tracked over time, not just a static attribute dump
- 🎨 **Serene Theme** — dark-mode-first, flat, quiet design; warnings styled as calm amber panels, never alarm-red walls

## Architecture

```
┌─────────────────────────────────────┐
│           QML UI Layer              │  Serene Theme, views, wizards
├─────────────────────────────────────┤
│         Application Layer           │  View models, operation queue,
│         (C++ / Qt)                  │  dry-run engine, journal
├─────────────────────────────────────┤
│        StorageBackend API           │  Abstract interface (swappable)
├──────────────────┬──────────────────┤
│  UDisks2 Backend │  BlockDev Backend │  D-Bus orchestration +
│  (Qt D-Bus)      │  (libblockdev)    │  low-level fs/partition ops
└──────────────────┴──────────────────┘
```

The **StorageBackend API** is deliberately abstract. Today it is implemented over
[UDisks2](https://www.freedesktop.org/wiki/Software/udisks/) (D-Bus) with
[libblockdev](https://github.com/storaged-project/libblockdev) providing
low-level filesystem, partition-table, and crypto plugins. If the stack ever
changes direction (Go/GTK4, Rust…), only the backend implementations move.

## Building

Requires: Qt 6.5+, CMake 3.21+, `udisks2` (running service), `libblockdev` with fs/crypto/lvm plugins.

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./SereneDiskManager
```

> Operations that modify disks require authorization. SereneDisk Manager uses
> polkit, so you'll confirm privileged actions through your desktop's standard
> auth agent — the same way GNOME Disks does. Run `pkexec`-free: no sudo needed.

## Status

Early development. See [ROADMAP.md](ROADMAP.md) for milestones.

## License

See [LICENSE](LICENSE).

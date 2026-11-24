# SereneDisk Manager — Roadmap

Goal: ship the most feature-complete disk manager on Linux with a UI that makes
advanced storage work feel serene.

## M0 — Foundation ✅ (current)
- [x] Repo skeleton, build system, CI-ready CMake
- [x] Serene Theme QML singleton (dark-first palette, typography, spacing)
- [x] Main window shell: sidebar + content stack
- [x] StorageBackend abstract interface
- [x] UDisks2 backend skeleton (enumerate block devices over D-Bus)

## M1 — Read-only release ("Look, don't touch")
- [ ] Device list + detail views (model, serial, size, removable/portable flags)
- [ ] Partition table view with mount points, filesystems, usage bars
- [ ] SMART data viewer (attributes, health assessment, temperature)
- [ ] Operation journal viewer (records every session action)
- [ ] Package as Flatpak-friendly + native build for sereneOS

## M2 — Safe operations
- [ ] Mount / unmount with graceful busy-handling and escalation prompts
- [ ] Format single filesystems (ext4, btrfs, xfs, fat32, exfat, ntfs)
- [ ] Filesystem check + repair (libblockdev fs plugin)
- [ ] Dry-run preview engine: shows a human-readable diff before any write
- [ ] Polkit integration for privileged actions

## M3 — Partition surgery
- [ ] Create / delete partitions (GPT, MBR) via libparted/libblockdev
- [ ] Resize / move partitions with pre-flight safety checks
- [ ] Partition table creation and repair
- [ ] Flags management (boot, esp, swap…)

## M4 — Advanced storage
- [ ] LUKS/LUKS2 create, unlock, key slot management
- [ ] LVM: PV/VG/LV create, resize, activate
- [ ] mdraid assembly and management
- [ ] Btrfs subvolumes + snapshot browse, quotas, scrub trigger
- [ ] ZFS pool visibility (read-only first, management later)

## M5 — Imaging, health, polish
- [ ] Disk/partition imaging and restore (.img, compressed variants)
- [ ] Device cloning
- [ ] Benchmarks (read/write throughput per device)
- [ ] SMART trend history (local SQLite), predictive warnings
- [ ] Onboarding wizard for first-time users
- [ ] Full accessibility + keyboard-only flows
- [ ] sereneOS system settings embedding (KCM-style)

## Stretch — Things no Linux disk manager has
- [ ] Undo journal replay for reversible operations
- [ ] Scheduled automated snapshots with retention policies
- [ ] Space-rules engine ("warn me when a disk is 90% full", etc.)
- [ ] CLI companion exposing every GUI action (`serenedisk`)
- [ ] Network storage mounts bookkeeping (NFS/SMB favorites)

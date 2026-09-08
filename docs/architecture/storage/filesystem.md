# Filesystem & Storage Architecture

BoredOS implements a Virtual Filesystem (VFS) layer to support filesystems, virtual filesystems, and device nodes.

## 1. Virtual File System (VFS)

Source: [`fs/vfs.c`](../../fs/vfs.c), [`fs/vfs.h`](../../fs/vfs.h), [`fs/vfs_internal.h`](../../fs/vfs_internal.h), [`fs/vfs_mount.c`](../../fs/vfs_mount.c), [`fs/vfs_path.c`](../../fs/vfs_path.c), [`fs/dev/`](../../fs/dev/)

The BoredOS VFS architecture is structured into modular components:

- **Core Dispatcher ([`fs/vfs.c`](../../fs/vfs.c))**: Handles file handle lifecycle (`vfs_alloc_file`, `vfs_free_file`) and routes high-level operations (`vfs_open`, `vfs_read`, `vfs_write`, `vfs_seek`, `vfs_poll`, `vfs_ioctl`, `vfs_list_directory`, `vfs_mkdir`, `vfs_rmdir`, `vfs_delete`, `vfs_rename`, `vfs_exists`, `vfs_is_directory`, `vfs_statfs`, `vfs_get_info`) to either filesystem mounts or device handlers.
- **Path Resolution ([`fs/vfs_path.c`](../../fs/vfs_path.c))**: Normalizes relative and absolute paths with a zero-stack single-pass normalizer (`vfs_normalize_path`, `vfs_normalize_process_path`) and matches active filesystem mount points (`vfs_resolve_mount`).
- **Mount Namespace ([`fs/vfs_mount.c`](../../fs/vfs_mount.c))**: Manages the global mount table (`mounts[]`, `vfs_mount`, `vfs_umount`, `vfs_get_mount`, `vfs_sync_all`, `vfs_automount_partition`).
- **Device Subsystem ([`fs/dev/`](../../fs/dev/))**: Modular sub-device drivers managing character and block device nodes under `/dev/`:
  - [`fs/dev/dev_tty.c`](../../fs/dev/dev_tty.c): TTYs (`/dev/tty*`, `/dev/ttyS*`, `/dev/console`), PTYs (`/dev/ptmx`, `/dev/pts/*`), and input nodes (`/dev/keyboard*`, `/dev/mouse*`).
  - [`fs/dev/dev_fb.c`](../../fs/dev/dev_fb.c): Framebuffer device (`/dev/fb0`), modesetting, and dirty rect tracking.
  - [`fs/dev/dev_audio.c`](../../fs/dev/dev_audio.c): Audio/DSP (`/dev/dsp`), mixer (`/dev/mixer`), PC speaker (`/dev/pcsk`), and RTC (`/dev/rtc`).
  - [`fs/dev/dev_net.c`](../../fs/dev/dev_net.c): Virtual network TAP/TUN devices (`/dev/net/tun`, `/dev/tun`).
  - [`fs/dev/dev_shm.c`](../../fs/dev/dev_shm.c): POSIX shared memory segments (`/dev/shm/*`).
  - [`fs/dev/dev_disk.c`](../../fs/dev/dev_disk.c): Block storage disks and partitions (`/dev/sda*`).
  - [`fs/dev/dev_core.c`](../../fs/dev/dev_core.c): Unified device router, directory indexer, and query coordinator.

Key operations in `vfs_fs_ops_t`:
- File operations: `open`, `close`, `read`, `write`, `seek`, `poll`, `ioctl`.
- Directory operations: `readdir`, `mkdir`, `rmdir`, `unlink`, `rename`.
- Metadata and sync: `exists`, `is_dir`, `get_info`, `statfs`, `sync_fs`, `unmount`, `writepage`.

### Mount management

- `vfs_mount(mount_path, device, fs_type, ops, fs_private)`: Registers a mount point in the mount table (up to 64 active mounts).
- `vfs_umount(mount_path)`: Unmounts a filesystem.
- `vfs_normalize_path(cwd, path, normalized)`: Resolves relative paths, `.`, and `..`.

---

## 2. Filesystems

### tmpfs

Source: [`fs/tmpfs.c`](../../fs/tmpfs.c), [`fs/tmpfs.h`](../../fs/tmpfs.h)

`tmpfs` is a RAM-backed filesystem where file contents are stored in [Page Cache](../memory/pagecache.md) pages (`address_space_t`).
- Used as the default root filesystem (`/`) during early boot.
- Mounted at `/tmp` for temporary files.

### FAT32

Source: [`fs/fat32.c`](../../fs/fat32.c), [`fs/fat32.h`](../../fs/fat32.h)

FAT32 driver supporting read, write, directory traversal, cluster chain allocation, and volume mounting over disk partitions. Auto-mounted at `/boot` for ESP partitions.

### ext4

Source: [`fs/ext4fs.c`](../../fs/ext4fs.c)

ext4 filesystem driver supporting volume mounting, file reading, and directory traversal on disk partitions.

### Virtual Filesystems

- `procfs` (`/proc`): Exposes process information and system state.
- `sysfs` (`/sys`): Exposes hardware device trees and bus topology.
- `shm` (`/dev/shm`): Shared memory file provider for IPC.

---

## 3. Storage and Disk Manager

Source: [`dev/disk_manager.c`](../../dev/disk_manager.c), [`dev/ahci.c`](../../dev/ahci.c)

- **AHCI**: Probes SATA controllers and performs disk I/O via DMA transfers.
- **Disk Manager**: Scans disks, parses MBR and GPT partition tables, and creates block device entries under `/dev` (such as `/dev/sda1`).
- **Writeback Flusher**: [`fs/flusher.c`](../../fs/flusher.c) runs a background thread that periodically flushes dirty page cache pages to disk using `vfs_sync_all()`.

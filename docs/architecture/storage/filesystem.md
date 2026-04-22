<div align="center">
  <h1>Filesystem Architecture</h1>
  <p><em>Virtual File System layer and FAT32 abstraction in BoredOS.</em></p>
</div>

---

BoredOS implements a rudimentary but functional filesystem layer designed to support reading system assets and user applications during runtime.

## Virtual File System (VFS)

The Virtual File System acts as an abstraction layer across different underlying storage mechanisms (even if, currently, only one type is fully utilized). System calls targeting files (`SYS_FS`) route through the VFS rather than interacting with the disk directly.

Key VFS functionalities include:
-   **File Descriptors**: Mapping integer IDs to internal file structures for userland processes.
-   **Standard Operations**: Standardizing `open()`, `read()`, `write()`, `close()`, `seek()`, and directory listings.
-   **Path Parsing**: Resolving absolute and relative paths.
-   **SMP Safety**: All VFS and underlying FAT32 operations are protected by a global **Spinlock**. This ensures that multiple cores can safely read from the filesystem simultaneously without corrupting internal file seek pointers or directory cache states.

## FAT32 Implementation

The primary filesystem logic in `fat32.c` handles both in-memory RAM-based filesystem simulation and physical ATA block devices.

### Storage Support

BoredOS supports two main types of storage for its FAT32 implementation:

1.  **RAMFS (Boot Modules)**: During boot, Limine loads necessary files (such as userland `.elf` binaries, fonts, and wallpapers) into memory as standard boot modules. The FAT32 code parses these loaded memory modules and automatically constructs a synthetic FAT32 directory tree inside RAM (mounted as `A:`).
2.  **ATA Drives**: The kernel includes a basic PIO-based ATA driver that can detect and read/write to physical IDE/PATA hard disks.
    -   **GPT is NOT supported**: Currently, only **MBR (Master Boot Record)** partition tables or **raw (partitionless)** disks are supported.
    -   **Filesystem**: The partition must be formatted as **FAT32**.

### Auto-detection
The `Disk Manager` automatically probes primary and secondary IDE channels during initialization. If a valid FAT32 partition is found (either directly at sector 0 or via an MBR partition table), the disk is assigned a drive letter (starting from `B:`) and becomes accessible to the VFS.


---

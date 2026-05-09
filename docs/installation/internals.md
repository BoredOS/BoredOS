# Installation Internals: How, Why, and What

This document explains the technical details of the BoredOS installation process, the design decisions behind it, and the resulting system layout.

## How the installation works

The BoredOS installation follows a strict five-phase sequence:

1.  **Partitioning (GPT/MBR)**: The disk is prepared with a partition table. For UEFI systems, we use GPT (GUID Partition Table) and create at least two partitions: an EFI System Partition (ESP) and a data partition.
2.  **Formatting (FAT32)**: Both partitions are formatted with FAT32. 
    - The ESP is labeled `EFI`.
    - The root partition is labeled `BOREDOS`.
3.  **Mounting & Flagging**: The root partition is mounted to `/mnt`. A special marker file is created at `/Library/.boredos_root`. This file is used by the kernel during boot to differentiate between a persistent disk and a live ISO/RAM disk.
4.  **System Mirroring**: The installer performs a recursive copy of the live system's essential including but not limited to:
    - `/bin`: All userland binaries.
    - `/Library`: System assets, fonts, icons, and configuration.
    - `/docs`: Documentation and manuals.
    - `/root`: The default user home directory.
5.  **Bootloader Setup**:
    - The ESP is mounted to `/mnt/boot`.
    - The kernel (`boredos.elf`) and the initial RAM disk (`initrd.tar`) are copied to the root of the ESP.
    - The Limine EFI binary is placed at `/EFI/BOOT/BOOTX64.EFI`.
    - A `limine.conf` is generated with the correct `root=/dev/sdXX` parameter.

## Why it works this way

### FAT32 for Everything
BoredOS currently uses FAT32 as its primary filesystem for both the boot partition and the root partition. 
- **Universal Support**: FAT32 is natively understood by UEFI firmware, making it the only choice for the ESP.
- **Simplicity**: By using FAT32 for the root partition as well, the kernel only needs one robust filesystem driver to handle both boot-time loading and runtime persistent storage.
- **Interoperability**: You can easily mount BoredOS partitions on other operating systems (Windows, Linux, macOS) to transfer files.

### The ESP/Root Split
Even though both use FAT32, we split the disk into two partitions to follow the UEFI specification. The ESP is meant to be small and strictly for bootloader files, while the root partition contains the entire userland. This separation allows for cleaner upgrades and multi-boot scenarios.

### Limine Bootloader
We chose [Limine](https://limine-bootloader.org/) because of its excellent support for modern protocols, its simplicity in configuration, and its lightweight footprint. It handles the transition from UEFI/BIOS environment to the kernel seamlessly.

## What the installation does

After a successful installation, your disk will look like this:

### Partition 1: EFI System Partition (ESP)
- `/boredos.elf`: The actual kernel binary.
- `/initrd.tar`: The basic system image loaded into RAM at boot.
- `/limine.conf`: The bootloader configuration.
- `/EFI/BOOT/BOOTX64.EFI`: The bootloader entry point for UEFI firmware.

### Partition 2: BoredOS Root
- `/bin/`: Executables.
- `/Library/`: System data.
    - `.boredos_root`: Hidden marker file.
    - `fonts/`, `images/`, `man/`: System resources.
- `/docs/`: System documentation.
- `/root/`: Persistent user files.



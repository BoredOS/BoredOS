# Advanced Installation

## Skipping Steps

All major phases can be skipped independently:

```
boredos_install --no-partition --no-format --uefi /dev/sda
```

The destructive warning is only shown when `--no-partition` AND `--no-format` are both absent.

## `/mnt/boot` Operations

The installer mounts the ESP at `/mnt/boot` and unmounts it when finished.

## FAT32 Limitations

FAT32 does not support Unix file permissions, ownership, or timestamps. These attributes are **not** preserved during installation. This is expected and documented.

## Custom Device Names

Use `--esp-dev` and `--root-dev` to bypass auto-detection:

```
boredos_install --uefi --esp-dev sda1 --root-dev sda2 /dev/sda --no-partition --no-format
```

Provided device names are still validated via `sys_disk_get_info` before use.

### Step 1: Partitioning
Run `fdisk` interactively on your target device.

```bash
fdisk /dev/sda
```

Inside the interactive shell:
1.  Type `n` to create a new partition (the ESP).
2.  Press Enter for the default start offset (1mb).
3.  Enter the size using suffixes like `b`, `mb`, or `gb` (e.g., `512mb` for a 512 MB ESP).
4.  Type `n` again for the second partition (the Root).
5.  Press Enter for the default start offset (aligned after the ESP).
6.  Press Enter for the default size (rest of the disk).
7.  Type `w` to write the partition table.
8.  Type 'Q' to quit.



### Step 2: Formatting
Initialize the partitions with FAT32. Use the labels `EFI` and `BOREDOS` to match the expected system layout.

```bash
mkfs_fat -F 32 -n EFI /dev/sda1
mkfs_fat -F 32 -n BOREDOS /dev/sda2
```

### Step 3: Installation 
The easiest way to perform the file copy and bootloader setup is to use the installer in "copy only" mode. This ensures that hidden flags (like the root detection file) are placed correctly.

```bash
boredos_install --no-partition --no-format --uefi --esp-dev sda1 /dev/sda
```

#### What this step does:
1.  **Mounts** `/dev/sda2` to `/mnt` and `/dev/sda1` to `/mnt/boot`.
2.  **Identifies** the root by creating an empty file at `/mnt/Library/.boredos_root`.
3.  **Copies** the system structure: `/bin`, `/Library`, `/docs`, and `/root` are mirrored to `/mnt`.
4.  **Deploys** the kernel and initrd: `boredos.elf` and `initrd.tar` are copied to the ESP (`/mnt/boot/`).
5.  **Configures** Limine: Writes a `limine.conf` to the ESP and copies the EFI bootloader to `/mnt/boot/EFI/BOOT/BOOTX64.EFI`.

For a deep dive into why these steps are performed, see the [Installation Internals](internals.md) guide.

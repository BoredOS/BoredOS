# BoredOS Installation

## Requirements

- Disk with at least **1 GB** (2,097,152 sectors)
- UEFI firmware
- A running BoredOS live environment (ISO)

## Quick Start (UEFI)

```
boredos_install --uefi /dev/sda
```

After installation, reboot and select the target disk from your firmware boot menu.

## Manual Steps

```
fdisk /dev/sda
mkfs_fat -F 32 -n EFI /dev/sda1
mkfs_fat -F 32 -n BOREDOS /dev/sda2
boredos_install --no-partition --no-format --uefi /dev/sda
```


See `install_guide.md` for a full walkthrough and `internals.md` for a deep dive into how the process works.

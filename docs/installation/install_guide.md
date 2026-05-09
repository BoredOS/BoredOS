# Install Guide

## UEFI Installation

1. Boot from the BoredOS ISO.
2. Run `boredos_install --uefi /dev/sda`.
3. Type `y` when prompted and press Enter.
4. After completion, reboot and select the disk.

## limine.conf Notes

**UEFI** uses `boot():/boredos.elf` — the `boot():` URL scheme refers to the EFI System Partition.

**Root selection** uses `root=/dev/<partition>` in `cmdline:` to choose the writable root partition (for example `root=/dev/sda2`).

**TOS acceptance** uses `--accept-tos` in `cmdline:` when you want to bypass the prompt.

**Live vs disk override** supports `--live` and `--disk` in `cmdline:`. Use `--live` for ISO/USB live boots and `--disk` for installed systems.

## Options

| Flag | Description |
|---|---|
| `--no-partition` | Skip fdisk (use existing partitions) |
| `--no-format` | Skip mkfs (use existing filesystem) |
| `--no-files` | Skip file copy |
| `--no-bootloader` | Skip limine.conf and EFI file copy |
| `--esp-size N` | ESP size in MB (default: 512) |
| `--esp-dev DEV` | Explicit ESP device name |
| `--root-dev DEV` | Explicit root device name |
| `-y` | Auto-accept the destructive warning |

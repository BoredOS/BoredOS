# lsblk

`lsblk` lists the block devices detected by BoredOS, including whole disks and their partitions.

## Usage

```sh
lsblk
lsblk /dev/sda
lsblk -r
lsblk --json
```

## Output

By default, `lsblk` prints a compact tree view:

```text
/dev/sda       2 GB  disk
└─ sda1        2 GB  part  FAT32  BOREDOS
```

Fields shown by the default output:

- device name, such as `/dev/sda` or `sda1`
- human-readable size, such as `512 MB` or `2 GB`
- device type, either `disk` or `part`
- filesystem type, currently `FAT32` when detected
- volume label when available
- `[ESP]` flag for EFI System Partitions

> [!NOTE]
> Mount points are not shown yet because BoredOS does not currently expose mountpoint information through the disk info syscall.

## Options

| Option | Description |
| :--- | :--- |
| `-r` | Print raw output without tree characters. |
| `--json` | Print machine-readable JSON output. |
| `/dev/DEVICE` | Show only one disk or partition. |

## Examples

List all block devices:

```sh
lsblk
```

Example output:

```text
/dev/sda       2 GB  disk
└─ sda1        2 GB  part  FAT32  BOREDOS
/dev/sdb      16 GB  disk
```

Show one disk and its partitions:

```sh
lsblk /dev/sda
```

Example output:

```text
/dev/sda       2 GB  disk
└─ sda1        2 GB  part  FAT32  BOREDOS
```

Print raw output for scripts:

```sh
lsblk -r
```

Example output:

```text
/dev/sda 2GB disk
/dev/sda1 2GB part FAT32 BOREDOS
```

Print JSON output:

```sh
lsblk --json
```

Example output:

```json
{"devices":[{"name":"/dev/sda","size":"2 GB","type":"disk","fstype":"","label":"","flags":[],"children":[{"name":"/dev/sda1","size":"2 GB","type":"part","fstype":"FAT32","label":"BOREDOS","flags":[]}]}]}
```

## How It Works

`lsblk` reads disk metadata through the disk syscalls exposed by BoredOS:

- `sys_disk_get_count()` gets the number of registered block devices.
- `sys_disk_get_info()` reads each device's name, size, type, FAT32 status, label, and flags.

The command treats non-partition entries as parent disks, then groups partition entries under the matching disk name. For example, `sda1` is displayed under `/dev/sda`.

Sizes are calculated from sector counts using 512-byte sectors, then formatted as `KB`, `MB`, or `GB`.

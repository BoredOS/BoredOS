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

`lsblk` enumerates block device nodes in `/dev` (such as `sda`, `sdb`, and `nvme*`):

- Disks and partitions are opened and inspected using standard `ioctl` geometry queries and filesystem superblock probing.
- Partition entries are grouped under their corresponding parent disk device (for example, `sda1` is displayed under `/dev/sda`).
- Mount points are cross-referenced with `/proc/mounts`.

Sizes are calculated from sector counts using 512-byte sectors, then formatted as `KB`, `MB`, or `GB`.

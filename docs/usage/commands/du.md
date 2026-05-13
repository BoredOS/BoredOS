# du

`du` (disk usage) reports the disk space used by files and directories.

## Usage

```sh
du [OPTIONS]... [FILE]...
```

## Description

By default, `du` prints human-readable sizes for each file and directory it encounters, starting from the current directory (`.`) if no path is given.

## Options

| Option | Description |
| :--- | :--- |
| `-s, --summarize` | Show only a total for each argument, suppressing per-entry output. |
| `-a, --all` | Write counts for all files, not just directories. |
| `-d, --max-depth=N` | Stop at depth N; show only entries at or above depth N. |
| `-c, --total` | Print a grand total after all arguments have been processed. |
| `-b, --bytes` | Print sizes in exact bytes instead of human-readable units. |
| `-H, --human-readable` | Accepted for compatibility; human-readable is the default. |
| `--help` | Display usage information and exit. |

## Output Format

Each line shows a size followed by the path:

```
SIZE    PATH
```

Sizes are formatted as `B`, `KB`, `MB`, or `GB` by default, with one decimal place when appropriate (e.g., `1.5 GB`). The `-b` option overrides this to show exact byte counts.

## Examples

Show disk usage for the current directory:

```sh
du
```

Show disk usage for a specific path:

```sh
du /bin
```

Show only totals per argument (`-s`):

```sh
du -s /bin /home
```

Show all files and directories recursively (`-a`):

```sh
du -a /bin
```

Limit output to depth 1 (`-d`):

```sh
du -d 1 /
```

Print a grand total after processing (`-c`):

```sh
du -c /bin /home
```

Show exact byte counts (`-b`):

```sh
du -b /bin
```

## How It Works

`du` uses `sys_get_file_info()` to read file sizes and `sys_list()` to enumerate directory contents recursively. The command skips the synthetic `.` and `..` entries and continues processing remaining paths if one path is inaccessible, printing an error for the failed path.

The size reported is the **apparent file size** (the logical size stored in the directory entry), not the allocated disk blocks. This is consistent with how BoredOS reports file sizes through the filesystem API.

## Exit Status

- `0`: Success
- `1`: One or more paths could not be accessed or listed
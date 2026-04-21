<div align="center">
  <h1>ELF App Metadata</h1>
  <p><em>How BoredOS embeds and reads application identity and icon data from <code>.elf</code> binaries.</em></p>
</div>

---

BoredOS supports embedding **application metadata** including a display name, short description, and icon paths directly inside `.elf` executables using a standard ELF NOTE section. The kernel reads this metadata at runtime to display correct icons in the file explorer and on the desktop, without requiring any external sidecar files.

## Overview

When an ELF binary is compiled for BoredOS, the build system automatically injects a special ELF NOTE entry into a dedicated section called `.note.boredos.app`. This note holds a packed C struct (`boredos_app_metadata_t`) containing the app's metadata.

At runtime, the Window Manager (`wm.c`) and File Explorer (`explorer.c`) call `app_metadata_get_primary_image()` to extract the primary icon path from any `.elf` file before rendering its icon. This allows each app to display its own distinct icon instead of the generic binary icon.

---

## The `boredos_app_metadata_t` Structure

Defined in [`src/sys/elf.h`](../../src/sys/elf.h):

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;                                                      // Must be BOREDOS_APP_METADATA_MAGIC (0x414d4431)
    uint16_t version;                                                    // Must be BOREDOS_APP_METADATA_VERSION (1)
    uint16_t image_count;                                                // Number of valid icon paths (0–4)
    uint16_t reserved;                                                   // Padding, set to 0
    char app_name[BOREDOS_APP_METADATA_MAX_APP_NAME];                    // Up to 63 chars + NUL
    char description[BOREDOS_APP_METADATA_MAX_DESCRIPTION];             // Up to 191 chars + NUL
    char images[BOREDOS_APP_METADATA_MAX_IMAGES][BOREDOS_APP_METADATA_MAX_IMAGE_PATH]; // Up to 4 icon paths
} boredos_app_metadata_t;
```

### Field Reference

| Field | Size | Description |
|---|---|---|
| `magic` | 4 bytes | Magic number `0x414D4431` — validates the struct is a real metadata blob. |
| `version` | 2 bytes | Schema version. Currently always `1`. |
| `image_count` | 2 bytes | How many entries in `images[]` are valid (0–4). |
| `reserved` | 2 bytes | Must be 0. Reserved for future use. |
| `app_name` | 64 bytes | Null-terminated display name of the app (e.g., `"Terminal"`). |
| `description` | 192 bytes | Null-terminated short description (e.g., `"Terminal shell and command runner."`). |
| `images[4][160]` | 640 bytes | Up to 4 absolute VFS paths to PNG icons. First entry is the primary icon. |

### Limits

| Constant | Value | Meaning |
|---|---|---|
| `BOREDOS_APP_METADATA_MAX_APP_NAME` | 64 | Max bytes for `app_name` including NUL |
| `BOREDOS_APP_METADATA_MAX_DESCRIPTION` | 192 | Max bytes for `description` including NUL |
| `BOREDOS_APP_METADATA_MAX_IMAGES` | 4 | Max number of icon paths |
| `BOREDOS_APP_METADATA_MAX_IMAGE_PATH` | 160 | Max bytes per icon path including NUL |

---

## The ELF NOTE Format

The metadata is stored inside a standard ELF NOTE entry (defined by `Elf64_Nhdr` in `elf.h`) within the `.note.boredos.app` section.

```
+------------------+
| Elf64_Nhdr       |  namesz, descsz, type
+------------------+
| name: "BOREDOS\0"|  8 bytes (sizeof BOREDOS_APP_NOTE_NAME)
+------------------+
| boredos_app_     |  sizeof(boredos_app_metadata_t)
|   metadata_t     |
+------------------+
```

### Note Constants

| Constant | Value | Description |
|---|---|---|
| `BOREDOS_APP_NOTE_OWNER` | `"BOREDOS"` | The note owner/name string |
| `BOREDOS_APP_NOTE_SECTION` | `".note.boredos.app"` | ELF section name |
| `BOREDOS_APP_NOTE_TYPE` | `0x41505031` | Note type identifier (`"APP1"` in ASCII) |
| `BOREDOS_APP_METADATA_MAGIC` | `0x414D4431` | Metadata struct magic (`"AMD1"`) |
| `BOREDOS_APP_METADATA_VERSION` | `1` | Current schema version |

---

## Embedding Metadata into your applications

Developers declare metadata using **special comment annotations** at the top of their C source file. The build system reads these automatically during compilation.

```c
// BOREDOS_APP_DESC: My application's short description.
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/my-icon.png
```

### `BOREDOS_APP_DESC`

A single-line description of the application. Truncated to 191 characters.

### `BOREDOS_APP_ICONS`

A semicolon-separated list of absolute VFS paths to PNG icons. Up to 4 icons are supported. The **first** entry is used as the primary icon displayed in the File Explorer and on the Desktop.

```c
// BOREDOS_APP_ICONS: /Library/images/icons/colloid/primary.png;/Library/images/icons/colloid/alternate.png
```

> [!TIP]
> If no `BOREDOS_APP_ICONS` annotation is provided, the build tool falls back to `/Library/images/icons/colloid/xterm.png`.
> If no `BOREDOS_APP_DESC` annotation is provided, the build tool uses `"BoredOS userspace application."`.

---

## Build System Integration

### The `gen_userland_note.sh` Tool

Located at [`tools/gen_userland_note.sh`](../../tools/gen_userland_note.sh), this script is invoked automatically by the `src/userland/Makefile` for every compiled application.

**Usage:**
```sh
gen_userland_note.sh <app-name> <source-file> <icon-source-dir> <output.note.c>
```

| Argument | Description |
|---|---|
| `<app-name>` | The base name of the application (e.g., `terminal`) |
| `<source-file>` | Path to the main `.c` source to extract annotations from |
| `<icon-source-dir>` | Directory where icon files are expected to exist on the *host* (build-time validation) |
| `<output.note.c>` | Path for the generated C source file |

The script:
1. Reads `BOREDOS_APP_DESC` and `BOREDOS_APP_ICONS` from the source file.
2. Validates that each declared icon file exists in `<icon-source-dir>` at build time.
3. Generates a C file (e.g., `bin/terminal.note.c`) that defines a `__attribute__((section(".note.boredos.app")))` constant struct containing all metadata.

### Makefile Rules

In `src/userland/Makefile`, the following rules handle metadata generation and linking:

```make
# Generate the .note.c for each app from its source annotations
$(BIN_DIR)/%.note.c: $(APP_METADATA_TOOL) | $(BIN_DIR)
    src="$(call app_source_for,$*)"; \
    sh $(APP_METADATA_TOOL) "$*" "$$src" "$(APP_ICON_SOURCE_DIR)" "$@"

# Compile the generated note C file
$(BIN_DIR)/%.note.o: $(BIN_DIR)/%.note.c
    $(CC) $(CFLAGS) -c $< -o $@

# Link note object into each ELF (generic rule)
$(BIN_DIR)/%.elf: $(LIBC_OBJS) $(BIN_DIR)/%.o $(BIN_DIR)/%.note.o
    $(LD) $(LDFLAGS) $^ -o $@
```

Special-cased apps (`doom`, `lua`, `viewer`, `settings`, `browser`, `screenshot`) also link in their own `.note.o` explicitly.

> [!IMPORTANT]
> The `-I../sys` flag is added to `CFLAGS` so that generated `.note.c` files can `#include "elf.h"` when referencing the metadata constants.

---

## Runtime Parsing: `app_metadata.c`

At runtime, `src/sys/app_metadata.c` provides two public functions:

```c
bool app_metadata_read(const char *path, boredos_app_metadata_t *out_metadata);
bool app_metadata_get_primary_image(const char *path, char *out_path, size_t out_path_size);
```

### `app_metadata_read`

Opens the ELF at `path` via VFS and searches for the `.note.boredos.app` section. It uses a **two-pass strategy**:

1. **Raw scan** (`am_scan_raw_notes`): For files up to 16 MiB, loads the entire binary into memory and byte-scans for a NOTE header matching the `BOREDOS` owner and `BOREDOS_APP_NOTE_TYPE`. This handles cases where the section header table is missing or unreadable.
2. **Section-based scan** (`am_parse_note_section`): Reads the ELF section header table, locates the `.note.boredos.app` section by name, then parses NOTE entries within it.

After a successful parse, the struct is validated via `am_validate_metadata` (checks magic and version fields) and sanitized via `am_sanitize_metadata` (null-terminates all strings).

### `app_metadata_get_primary_image`

A convenience wrapper around `app_metadata_read` that returns just the first icon path:

```c
bool app_metadata_get_primary_image(const char *path, char *out_path, size_t out_path_size);
```

Returns `true` and populates `out_path` if the binary has at least one valid icon declared.

### Metadata Cache

To avoid re-reading ELF files on every frame redraw, results are stored in a **simple FIFO cache** of up to 64 entries:

```c
#define APP_METADATA_CACHE_SIZE 64
```

Both positive (metadata found) and negative (no metadata) results are cached. The cache uses a round-robin eviction strategy — no LRU, no invalidation. This is intentional for a kernel context where metadata does not change while the OS is running.

---


---

## Userspace API

Userspace applications can query the ELF metadata of any `.elf` binary on the VFS through two wrapper functions declared in [`src/userland/libc/syscall.h`](../../src/userland/libc/syscall.h).

### The `boredos_app_metadata_t` struct (userland)

The struct is redefined verbatim in the userland header so that apps do **not** need to include any kernel header:

```c
#define BOREDOS_APP_METADATA_MAX_APP_NAME    64
#define BOREDOS_APP_METADATA_MAX_DESCRIPTION 192
#define BOREDOS_APP_METADATA_MAX_IMAGES      4
#define BOREDOS_APP_METADATA_MAX_IMAGE_PATH  160

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t image_count;
    uint16_t reserved;
    char app_name[BOREDOS_APP_METADATA_MAX_APP_NAME];
    char description[BOREDOS_APP_METADATA_MAX_DESCRIPTION];
    char images[BOREDOS_APP_METADATA_MAX_IMAGES][BOREDOS_APP_METADATA_MAX_IMAGE_PATH];
} boredos_app_metadata_t;
```

### Functions

#### `sys_get_elf_metadata`

```c
int sys_get_elf_metadata(const char *path, boredos_app_metadata_t *out_metadata);
```

Reads the full metadata blob from the `.note.boredos.app` section of the ELF at `path` and writes it into `*out_metadata`.

Returns `1` on success, `0` on failure (file not found, no metadata note, or validation failure).

#### `sys_get_elf_primary_image`

```c
int sys_get_elf_primary_image(const char *path, char *out_path, size_t out_path_size);
```

Convenience wrapper that returns only the first icon path from the metadata. Useful when you just need to display an application icon without allocating a full `boredos_app_metadata_t`.

Returns `1` and writes a null-terminated VFS path into `out_path` if at least one icon was declared. Returns `0` otherwise.

### Syscall IDs

Both functions route through `SYS_SYSTEM` using dedicated command IDs:

| ID | Macro | Function |
|---|---|---|
| 76 | `SYSTEM_CMD_GET_ELF_METADATA` | `sys_get_elf_metadata` |
| 77 | `SYSTEM_CMD_GET_ELF_PRIMARY_IMAGE` | `sys_get_elf_primary_image` |

### Caching

Both calls share the same kernel-side **64-entry FIFO metadata cache** used by the Window Manager and File Explorer. If the metadata for a path has already been read, the result is returned from cache without re-reading the file. Negative results (no metadata) are also cached.

### Example: reading full metadata

```c
#include "syscall.h"
#include "stdio.h"

void print_app_info(const char *elf_path) {
    boredos_app_metadata_t meta;
    if (!sys_get_elf_metadata(elf_path, &meta)) {
        printf("%s: no metadata\n", elf_path);
        return;
    }
    printf("Name:        %s\n", meta.app_name);
    printf("Description: %s\n", meta.description);
    printf("Icons (%u):\n", meta.image_count);
    for (int i = 0; i < (int)meta.image_count; i++) {
        printf("  [%d] %s\n", i, meta.images[i]);
    }
}
```

### Example: fetching just the icon path

```c
#include "syscall.h"

void load_icon_for(const char *elf_path, Image *out_icon) {
    char icon_path[BOREDOS_APP_METADATA_MAX_IMAGE_PATH];
    if (sys_get_elf_primary_image(elf_path, icon_path, sizeof(icon_path))) {
        *out_icon = image_load(icon_path);
    } else {
        *out_icon = image_load("/Library/images/icons/colloid/xterm.png"); // fallback
    }
}
```

> [!NOTE]
> The metadata is read **from the VFS**, so the ELF must already be present as a file. The kernel does **not** read metadata from an already-running process image in memory — it re-opens the file via the filesystem.

---




*See also: [`custom_apps.md`](custom_apps.md) for a full tutorial on building and bundling a new application, [`sdk_reference.md`](sdk_reference.md) for an overview of the SDK, and [`syscalls.md`](syscalls.md) for the complete SYSTEM command ID table.*

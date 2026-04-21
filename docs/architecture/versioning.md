# BoredOS Versioning

BoredOS uses two independent version numbers: one for the **OS release** and one for the **kernel**. They evolve at different rates and follow different conventions.

---

## OS Version

The OS version follows a **date-based** scheme:

```
YY.M[.x]
```

| Component | Meaning |
|-----------|---------|
| `YY`      | Two-digit year (e.g. `26` for 2026) |
| `M`       | Month number, no leading zero (e.g. `4` for April, `12` for December) |
| `.x`      | Optional patch identifier — a small sequential integer that has **no relation to a specific day** |

### Examples

| Version  | Meaning |
|----------|---------|
| `26.4`   | Base release for April 2026 |
| `26.5`   | Base release for May 2026 |
| `26.5.1` | First patch on top of the May 2026 release |
| `26.5.2` | Second patch on top of the May 2026 release |
| `26.12`  | Base release for December 2026 |

### Rules

- The **base release** (`YY.M`) is cut once per month when a milestone is ready.
- Patch releases (`YY.M.x`) are issued for fixes or smaller additions that land between two monthly milestones. The `.x` counter starts at `1` and increments sequentially — it is **not** tied to a calendar day.
- A `-dev` or `-rc` suffix may be appended to any version string during active development (e.g. `26.5-dev`, `26.5.1-rc1`).
- The version string is defined in [`src/core/version.c`](../../src/core/version.c) as `os_version`.

---

## Kernel Version

The kernel version follows **Semantic Versioning**:

```
MAJOR.MINOR.PATCH
```

| Component | When to increment |
|-----------|------------------|
| `MAJOR`   | A breaking or fundamentally large architectural change (e.g. rewriting the syscall layer, introducing a new memory model) |
| `MINOR`   | A meaningful new feature or a notable internal improvement that does not break existing interfaces |
| `PATCH`   | A small fix, refactor, or incremental improvement |

### Examples

| Transition          | Reason |
|---------------------|--------|
| `4.2.0` → `5.0.0`  | Major kernel rework (e.g. full syscall dispatch-table refactor, new scheduler) |
| `4.2.0` → `4.3.0`  | New subsystem or feature addition (e.g. adding Lua runtime, new VFS driver) |
| `4.2.0` → `4.2.1`  | Small fix or minor tweak (e.g. PIT calibration fix, terminal newline correction) |

### Rules

- When `MAJOR` is bumped, `MINOR` and `PATCH` reset to `0`.
- When `MINOR` is bumped, `PATCH` resets to `0`.
- A `-dev` suffix may be appended during active development (e.g. `5.0.0-dev`).
- The version string is defined in [`src/core/version.c`](../../src/core/version.c) as `kernel_version`.

---

## Where Versions Are Declared

Both version strings live in a single file:

```c
// src/core/version.c

const char *os_version     = "26.5-dev";
const char *kernel_version = "4.2.0-dev";
```

When cutting a release, update both strings, remove the `-dev` suffix, tag the commit (`git tag v26.5`), and then immediately bump to the next `-dev` version.

---

## Codename

Each release may carry an informal **codename**. A single word that gives the release a human-friendly identity. Codenames are stored in [`src/core/version.c`](../../src/core/version.c) as `os_codename` and exposed to userspace via the `get_os_info` syscall.

### Convention

- Codenames **generally change with each monthly base release** (`YY.M`), but this is not a hard rule. A codename may carry over into the next month if the release feels like a natural continuation of the previous one.
- Patch releases (`YY.M.x`) **always keep the same codename** as the base release they belong to.
- There is no enforced theme, but names tend to be short, memorable single words.

### Examples

| OS Version | Codename  
|------------|-----------|
| `26.4`     | Voyager   
| `26.5`     | Genesis

### Where It Is Declared

```c
// src/core/version.c

const char *os_codename = "Genesis";
```


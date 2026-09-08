# BoredOS Versioning

BoredOS uses a continuous rolling model where the system identity and version are defined entirely by the build date and git commit hash:

```
YYYY.MM.DD-g<commit_hash>
```

| Component | Meaning |
|---|---|
| `YYYY.MM.DD` | Build date (e.g. `2026.09.08`) |
| `-g<commit_hash>` | Short 8-character git commit hash prefixed with `g` (e.g. `-g664f0d46`) |

### Example

```text
2026.09.08-g664f0d46
```

### System & Tool Outputs

- `uname -s`: `Boredkernel`
- `uname -r`: `2026.09.08-g664f0d46`
- `uname -o`: `BoredOS`
- `uname -a`: `Boredkernel <hostname> 2026.09.08-g664f0d46 <build_date_time> x86_64 x86_64 BoredOS`
- `/proc/version`:
  ```text
  BoredOS
  Kernel: Boredkernel 2026.09.08-g664f0d46
  Build: Sep  8 2026 23:07:00
  ```

---

## Where Versions Are Declared & Generated

- The default constants and struct population live in [`core/version.c`](../../core/version.c).
- During compilation, [`Makefile`](../../Makefile) queries `git rev-parse --short=8 HEAD` and `date +%Y.%m.%d` and generates `$(BUILD_DIR)/kernel_version.h`, defining `BOREDOS_KERNEL_VERSION`.
- `core/version.c` includes `kernel_version.h`, exposing the version dynamically through `get_os_info()`, `/proc/version`, `uname`, and `sysfetch`.


<div align="center">
  <h1>Userland SDK Reference</h1>
    <p><em>Overview and entry point for BoredOS userland development.</em></p>
</div>

---

BoredOS provides a compact userland SDK for building `.elf` applications.
This page is the high-level map; detailed API references now live in dedicated pages.

## SDK Structure

Primary headers are in `src/userland/libc/` and UI helpers are in `src/wm/`.

- `stdlib.h`, `string.h`, `stdio.h`, `unistd.h`: core libc surface
- `syscall.h`: raw syscall wrappers and command constants
- `libui.h`: window creation, drawing, and event polling
- `libwidget.h`: higher-level reusable widgets
- `math.h`: freestanding math helpers

## Detailed References

- [`libc Reference`](libc_reference.md): current libc headers and implemented APIs
- [`Syscalls`](syscalls.md): syscall numbers, FS/SYSTEM command IDs, and wrappers
- [`UI API`](ui_api.md): drawing and event APIs
- [`Widget API`](widget_api.md): common widgets and interaction helpers
- [`Native TCC`](tcc.md): Native C compilation directly on BoredOS

## Typical Include Set

```c
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <syscall.h>
```

For GUI apps:

```c
#include <libui.h>
#include <libwidget.h>
```

## Build and Packaging

- Add app source under `src/userland/` (CLI, GUI, or games subfolder).
- Ensure it is included in the userland build rules/targets.
- Build from repo root with `make`.
- Built binaries are copied into initrd under `/bin` by the top-level `Makefile`.



# Native Development with TCC

BoredOS includes a native port of the **Tiny C Compiler (TCC)**, allowing you to compile and run C programs directly within the operating system.

## Basic Usage

The compiler is available as `tcc`. You can use it much like you would on a standard Unix-like system.

### Compiling a Simple CLI Program

Create a file named `hello.c`:

```c
#include <stdio.h>

int main() {
    printf("Hello from BoredOS native TCC!\n");
    return 0;
}
```

Compile and run it:

```bash
tcc hello.c -o hello.elf
./hello.elf
```

## Developing GUI Applications

To develop applications that use the BoredOS Window Manager and UI library, you need to link against `libboredos`.

### Example GUI App (`hello_gui.c`)

```c
#include <libc/libui.h>
#include <libc/syscall.h>

int main() {
    ui_window_t win = ui_window_create("Hello TCC", 100, 100, 300, 200);
    if (!win) return 1;

    gui_event_t ev;
    while (1) {
        if (ui_get_event(win, &ev)) {
            if (ev.type == GUI_EVENT_PAINT) {
                ui_draw_string(win, 20, 40, "Compiled natively!", 0xFFFFFFFF);
                ui_mark_dirty(win, 0, 0, 300, 200);
            } else if (ev.type == GUI_EVENT_CLOSE) {
                break;
            }
        }
    }
    return 0;
}
```

### Compilation Command

```bash
tcc hello_gui.c -o hello_gui.elf -lboredos
```

> [!NOTE]
> The compiler automatically searches `/usr/include` for headers and `/usr/lib` for libraries. The BoredOS SDK headers and `libboredos.a` are pre-installed in these locations.

## Technical Details

### Standard Paths
- **Headers**: `/usr/include`, `/usr/local/include`
- **Libraries**: `/usr/lib`
- **TCC Internal**: `/usr/lib/tcc`

### Compilation Process
BoredOS TCC generates standard **ELF64** binaries. It automatically links with:
1.  **`crt0.o`**: Entry point initialization.
2.  **`crti.o` / `crtn.o`**: Constructor/Destructor support.
3.  **`libc.a`**: The BoredOS standard C library.
4.  **`libtcc1.a`**: TCC runtime support.

### Memory & Storage Requirements
- **Static Linking Only**: BoredOS currently only supports static linking for native binaries.
- **Live ISO Mode**: You are limited by the 128MB RAMFS capacity. Compiling very large projects may fail if this limit is reached.
- **Disk Installation**: The compiler writes directly to your persistent disk. Your storage capacity is limited only by the size of your partition, and your work persists across reboots.
- **System RAM**: The kernel statically reserves 128MB for the internal RAMFS regardless of boot mode, though this does not limit your storage on a disk install.
- **No JIT**: The `tcc -run` feature is currently unsupported due to kernel memory protection and the lack of `mmap` with execution permissions in userland.

## Troubleshooting

### I/O Error during compilation
If you encounter an "I/O Error" while writing the output file, you may have run out of space. 
- **Live ISO**: You have exceeded the 128MB RAMFS limit.
- **Disk Installation**: Your disk partition is full.

### Missing Headers
Ensure that you are including headers using the standard syntax: `#include <stdio.h>`. If you are using custom paths, use the `-I` flag:
```bash
tcc myapp.c -I/root/my_headers -o myapp.elf
```

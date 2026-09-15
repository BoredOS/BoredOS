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
tcc hello.c -o hello
./hello
```

## Developing Direct Framebuffer (Graphics) Applications

Since the legacy window manager has been removed, graphical applications can write directly to the screen via the `/dev/fb0` framebuffer device. To ensure the text console doesn't overwrite your drawings, configure the console TTY to graphics mode (`KD_GRAPHICS`).

### Example Framebuffer App (`hello_fb.c`)

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/kd.h>
#include <stdint.h>

int main() {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        printf("Error: cannot open /dev/fb0\n");
        return 1;
    }

    // Query screen info
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    ioctl(fd, FBIOGET_VSCREENINFO, &vinfo);
    ioctl(fd, FBIOGET_FSCREENINFO, &finfo);

    // Disable TTY text blitting to prevent the kernel from drawing console text on the screen
    ioctl(0, KDSETMODE, (void*)KD_GRAPHICS);

    // Fill screen with blue
    uint32_t size = finfo.line_length * vinfo.yres;
    uint8_t *fb = malloc(size);
    for (uint32_t y = 0; y < vinfo.yres; y++) {
        uint32_t *row = (uint32_t *)(fb + y * finfo.line_length);
        for (uint32_t x = 0; x < vinfo.xres; x++) {
            row[x] = 0xFF0000FF; // BGRA Blue
        }
    }

    lseek(fd, 0, SEEK_SET);
    write(fd, fb, size);
    free(fb);

    // Hold screen for 3 seconds
    sleep(3000);

    // Restore text console blitting
    ioctl(0, KDSETMODE, (void*)KD_TEXT);

    close(fd);
    return 0;
}
```

### Compilation Command

```bash
tcc hello_fb.c -o hello_fb
```

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
tcc myapp.c -I/root/my_headers -o myapp
```

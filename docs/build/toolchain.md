# Build Toolchain

BoredOS is built cross-compiled from a host system (such as macOS or Linux) to target the generic `x86_64-elf` platform.

## Prerequisites

To build BoredOS, you need the following tools:

1.  **x86_64 ELF GCC Cross-Compiler**:
    -   `x86_64-elf-gcc`: The C compiler targeting the freestanding overarching ELF environment.
    -   `x86_64-elf-ld`: The linker to combine object files into the final `boredos.elf` kernel and userland binaries.

2.  **NASM**:
    -   Required to compile the `.asm` files in `src/arch/` and `src/userland/crt0.asm`. It formats the output as `elf64` objects to be linked alongside the C code.

3.  **xorriso**:
    -   A specialized tool to create ISO 9660 filesystem images.
    -   *Why?* `xorriso` packages the compiled kernel, Limine bootloader, and asset files (fonts, images, userland binaries) into the final bootable `boredos.iso` CD-ROM image.

4.  **QEMU** (Optional but highly recommended for testing):
    -   `qemu-system-x86_64` is used to virtualize the OS for testing or to mess around.

## Building the Cross-Compiler on Linux

### Availability Issue

On most Linux distributions, the `x86_64-elf-gcc` cross-compiler binary is **not pre-packaged** in standard repositories. The only notable exception is **Arch Linux** and Arch-based distributions (Manjaro, EndeavourOS, etc.), where it can be installed via `pacman`:

```bash
pacman -S x86_64-elf-gcc x86_64-elf-binutils
```

For all other Linux distributions (Debian, Ubuntu, Fedora, openSUSE, etc.), you **must build the cross-compiler from source**.

### Building from Source

To build the x86_64-ELF GCC cross-compiler:

1.  **Download prerequisites**:
    -   GNU Binutils source
    -   GCC source

2.  **Configure and build Binutils**:
    ```bash
    ../binutils-*/configure --target=x86_64-elf --prefix=/usr/local/cross
    make && make install
    ```

3.  **Configure and build GCC**:
    ```bash
    ../gcc-*/configure --target=x86_64-elf --prefix=/usr/local/cross \
        --without-headers --enable-languages=c
    make all-gcc && make install-gcc
    ```

4.  **Add to PATH**:
    ```bash
    export PATH="/usr/local/cross/bin:$PATH"
    ```

Verify the installation:

```bash
x86_64-elf-gcc --version
```

> **Note**: Building the cross-compiler can take 20-30 minutes depending on system performance. This is a one-time setup cost.

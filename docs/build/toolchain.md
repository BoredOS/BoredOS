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

## Installing the Toolchain on Windows

### Recommended Environment: MSYS2

On Windows, the recommended way to build BoredOS is using **MSYS2**.  
MSYS2 provides a Unix-like environment with the `pacman` package manager, making it easy to install the required development tools and the `x86_64-elf` cross-toolchain directly from the repositories.

### 1. Install MSYS2

Download and install MSYS2 from the official website:

- https://www.msys2.org/

After installation, launch the **MSYS2 UCRT64** terminal.

---

### 2. Update MSYS2

Before installing packages, fully update the environment:

```bash
pacman -Syu
```

You may be asked to close the terminal after the first update.  
If so:

1. Close the MSYS2 window
2. Reopen **MSYS2 UCRT64**
3. Run the update command again:

```bash
pacman -Syu
```

Repeat until no further updates are available.

---

### 3. Install Required Packages

Install the required development tools:

```bash
pacman -S \
    make \
    git \
    nasm \
    xorriso \
    qemu-system-x86_64
```

---

### 4. Install the x86_64 ELF Toolchain

MSYS2 provides the full `x86_64-elf` cross-compilation toolchain directly through `pacman`.

Install it with:

```bash
pacman -S \
    mingw-w64-ucrt-x86_64-x86_64-elf-gcc \
    mingw-w64-ucrt-x86_64-x86_64-elf-binutils
```

This installs:

- `x86_64-elf-gcc`
- `x86_64-elf-ld`
- other required ELF binutils

# Build Toolchain

BoredOS is built cross-compiled from a host system (such as macOS or Linux) to target the generic `x86_64-elf` platform.


## Table of Contents

- [Prerequisites](#prerequisites)
- [Building the Cross-Compiler on Linux](#building-the-cross-compiler-on-linux)
- [Installing the Toolchain on Windows](#installing-the-toolchain-on-windows)

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
MSYS2 provides a Unix-like environment with the `pacman` package manager, making it easy to install the required development tools.

---

## 1. Install MSYS2

Download and install MSYS2 from the official website:

- https://www.msys2.org/

After installation, launch the **MSYS2 UCRT64** terminal.

---

## 2. Update MSYS2

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

## 3. Install Required Packages

Install the required development tools:

```bash
pacman -S make nasm xorriso git
```

---

## 4. Install QEMU for Windows

Download the Windows version of QEMU from:

- https://qemu.weilnetz.de/w64/

Install QEMU normally and make sure the installation directory is added to your Windows `PATH`.
Note that if it breaks when building, you need too add `qemu-img` to your `PATH`:
`export PATH="/c/Program Files/qemu:$PATH"`

You can verify the installation with:

```bash
qemu-system-x86_64 --version
```

---

## 5. Install the x86_64 ELF Cross Toolchain

Download the prebuilt `x86_64-elf` toolchain for Windows:

- https://github.com/lordmilko/i686-elf-tools/releases/download/15.2.0/x86_64-elf-tools-windows.zip

Extract the archive somewhere convenient.

---

## 6. Add the Toolchain to PATH

Inside the **MSYS2 UCRT64** terminal, add the toolchain binaries to your `PATH`:

```bash
export PATH="/c/Users/your/path/to/the/binaries/x86_64-elf-tools-windows/bin:$PATH"
```

To make this permanent, add the line to your `~/.bashrc` file:

```bash
echo 'export PATH="/c/Users/your/path/to/the/binaries/x86_64-elf-tools-windows/bin:$PATH"' >> ~/.bashrc
```

Then reload the shell:

```bash
source ~/.bashrc
```

---

## 7. Verify the Installation

Verify that the cross compiler is available:

```bash
x86_64-elf-gcc --version
```

You should also verify NASM and QEMU:

```bash
nasm -v
qemu-system-x86_64 --version
```

If all commands work, the development environment is correctly configured.


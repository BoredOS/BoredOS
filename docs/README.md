<div align="center">
  <h1>BoredOS Documentation</h1>
  <p><em>Internal guides, architecture, and application development.</em></p>
</div>

---

Welcome to the documentation for BoredOS! This directory contains detailed guides on how the OS functions, how to build it, and how to develop applications for it.

## Table of Contents

The documentation is organized into three main categories:

### 1. [Architecture](architecture/)
Explains the logical layout of the kernel and internal components.
-   [`Core`](architecture/core.md): Kernel source layout and the boot process (Limine, Multiboot2).
-   [`Memory`](architecture/memory.md): Physical Memory Management (PMM) and Virtual Memory Management (VMM).
-   [`Filesystem`](architecture/filesystem.md): Virtual File System (VFS) and the RAM-based FAT32 simulation.
-   [`Window Manager`](architecture/window_manager.md): How the built-in Window Manager natively handles graphics, events, and compositing.
-   [`Versioning`](architecture/versioning.md): The OS date-based version scheme (`YY.M[.x]`) and kernel semantic versioning (`MAJOR.MINOR.PATCH`).

### 2. [Building and Deployment](build/)
Instructions for compiling the OS from source.
-   [`Toolchain`](build/toolchain.md): Prerequisites and cross-compiler setup (`x86_64-elf-gcc`, `nasm`, `xorriso`).
-   [`Usage`](build/usage.md): Understanding the Makefile targets, QEMU emulation, and flashing to bare metal hardware.

### 3. [Application Development](appdev/)
The SDK and toolchain guides for creating your own `.elf` userland binaries.
-   [`SDK Reference`](appdev/sdk_reference.md): Overview hub for SDK layout, includes, and links to detailed libc/syscall docs.
-   [`Syscalls`](appdev/syscalls.md): Current syscall numbers, FS/SYSTEM command IDs, and wrapper guidance.
-   [`libc Reference`](appdev/libc_reference.md): Current libc headers, implemented APIs, and behavior notes.
-   [`UI API`](appdev/ui_api.md): Drawing on the screen, creating windows, and polling the event loop using `libui.h`.
-   [`Widget API`](appdev/widget_api.md): High-level UI components like buttons, textboxes, and scrollbars using `libwidget.h`.
-   [`Custom Apps`](appdev/custom_apps.md): A step-by-step tutorial on writing a new graphical C application, editing the Makefile, and bundling it into the ISO.
-   [`ELF App Metadata`](appdev/elf_metadata.md): How to declare app icons and descriptions using source annotations, how the build system embeds them into `.note.boredos.app` ELF sections, and how the kernel reads them at runtime.
-   [`Example Apps`](appdev/examples/README.md): A collection of sample C applications ranging from basic terminal output to advanced TCP networking.
-   [`Grapher`](appdev/grapher.md): Full reference for the built-in mathematical graphing application — equation syntax, keyboard controls, architecture, and configuration.

### 4. [Usage](usage/)
General guides on how to interact with the OS.
-   [`Booting`](usage/booting.md): How to use the Limine bootloader and toggle kernel boot flags like `-v`.
-   [`Desktop`](usage/desktop.md): Window management, shortcuts, and desktop interaction.
-   [`Lumos`](usage/lumos.md): Using the system-wide search (`Shift + Ctrl + Space`).
-   [`Terminal`](usage/terminal.md): Command line interface, redirection, and common commands.
-   [`Launching Apps`](usage/launching_apps.md): Ways to launch files and applications, plus a software overview.

---

<div align="center">
  <img src="base/Library/Images/branding/banner.png" alt="BoredOS Logo" width="800" />

  <h3>An operating system made out of infinite boredom.</h3>

  [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
  ![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey)
  [![Donate](https://img.shields.io/badge/Donate-❤️-pink)](https://buymeacoffee.com/boreddevhq)

  [Docs](docs/README.md) · [Contributing](CONTRIBUTING.md) · [Discord](https://discord.gg/J2BxWaFAgY)

</div>

---

BoredOS is a from-scratch x86-64 UNIX-like hobby operating system written in C.

It implements preemptive scheduling, demand-paged virtual memory with COW, an ext4/FAT32-capable VFS, Unix domain sockets, a custom init system (YAWN), and hardware drivers spanning AHCI, AC97 audio, and common network cards (e1000, RTL8139/8111, VirtIO). It targets a practical subset of POSIX, enough to run standard userspace tools and dynamic ports via mlibc. The design aims for a clean, modern UNIX rather than decades of accumulated backwards-compatibility bloat.

## BoredOS sub-projects:

### [Bored Package Manager (BPM)](https://github.com/BoredOS/bpm)

`bpm` is our package manager. It installs, removes, and upgrades software from a remote repository index. Packages are `.bup` files, which are really just tar archives compressed with LZ4, plus a TOML manifest and optional install/remove hook scripts.

There's a community repo, [BUR](https://github.com/boredos/bur), where anyone can submit packages through a pull request. If you want to add something, the [packaging guide](https://github.com/BoredOS/bur/blob/main/PACKAGING.md) walks through the format.

### [Nova](https://github.com/BoredOS/nova)

Nova is a custom compositor built for BoredOS, with its own UI toolkit and a win9x-inspired design language.

### [YAWN](docs/architecture/system/yawn.md)

YAWN (*Yet Another Workload Navigator*) is our custom init and service management system. It bootstraps userspace, supervises login terminals, handles zombie process reaping, and manages daemons through a BSD-style rc system (`/etc/rc.d` and `/etc/rc.conf`). (YAWN also stands for "You Awake? Well, Now what?")

## Features

### Ports

| Port | Notes |
|------|-------|
| **DOOM** (doomgeneric) | What's an operating system without DOOM? |
| **TinyGL** | A software-rendered subset of OpenGL. |
| **TCC** | A small, fast C compiler. |
| **Lua** | Available as a shell tool or embeddable in other programs. |
| **mlibc** | The managarm C standard library. |
| **kilo** | A simple text editor. |
| **kirc** | A simple IRC client. |
| **tvi** | A vi-like text editor. |


## Kernel Summary

| Subsystem | Details |
|-----------|---------|
| **SMP** | Multi-core support via LAPIC. Per-CPU state lives in the GS segment. XSAVE/XRSTOR handle FPU context across switches. |
| **Scheduler** | Preemptive round-robin over a circular process list, with sleep/wake support, per-CPU affinity, and cross-core IPI for AP scheduling. |
| **Memory** | Physical page allocator (PMM), slab allocator, 4-level MMU with COW, VMA tracking with an RB-tree, demand paging, and a page cache. |
| **VFS** | Virtual filesystem layer supporting tmpfs, FAT32, ext4, ProcFS, DevFS, and SysFS. |
| **IPC** | Unix domain sockets, shared memory through `/dev/shm`, wait queues, and work queues. |
| **PTY** | Full pseudo-terminal support. |
| **Devices** | PCI, AHCI (SATA), PS/2, ACPI, I2C, AC97 audio, and RTC. |
| **TTYs** | 10 virtual terminals, each with its own graphics buffer. |
| **ELF** | Loads and runs ELF64 binaries with correct segment mapping. |
| **Networking** | LWIP TCP/IP stack with support for Intel e1000, RTL8139/RTL8111, and VirtIO-net. |

## Documentation

| Guide | Description |
|-------|-------------|
| [Documentation Index](docs/README.md) | Start here if you're new. |
| [Architecture Overview](docs/architecture/README.md) | A deeper look at how the kernel is put together. |
| [Building and Running](docs/build/usage.md) | Get a build environment set up. |
| [AppDev SDK](docs/appdev/custom_apps.md) | Write your own apps for BoredOS. |
| [Packaging Guide](usr/bpm/PACKAGING.md) | Package software for bpm. |

## Support

BoredOS is developed and maintained by a small group of contributors in their spare time, independent of any company or organization. Development isn't tied to funding, but if you find the project useful or interesting, a donation is welcome and goes toward continued development.

<a href="https://buymeacoffee.com/boreddevhq" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="50" style="border-radius: 8px;" />
</a>

## License

Distributed under the **GNU General Public License v3**. See [`LICENSE`](LICENSE) for the full text.

> [!IMPORTANT]
> Keep all copyright headers intact and include the original attribution in any redistribution or derivative work. Details are in the [`NOTICE`](NOTICE) file.

## History

BoredOS grew out of [BrewKernel](https://github.com/boreddevnl/brewkernel), a project [Christiaan](https://github.com/boreddevnl/) started back in 2023. BrewKernel is archived now and will no longer be updated.
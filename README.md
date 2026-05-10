<div align="center">
  <img src="branding/bOS_full_gradient_cropped.png" alt="BoredOS Logo" width="450" />

  <h3>A modern x86_64 hobbyist operating system built from the ground up.</h3>

  [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
  ![Platform: x86_64](https://img.shields.io/badge/Platform-x86_64-lightgrey)
  ![Status: Active](https://img.shields.io/badge/Status-Active-brightgreen)
  ![GitHub all releases](https://img.shields.io/github/downloads/boreddevnl/BoredOS/total?color=brightgreen)

  <br />

  [Docs](docs/README.md) · [Build & Run](docs/build/usage.md) · [AppDev SDK](docs/appdev/sdk_reference.md) · [Discord](https://discord.gg/J2BxWaFAgY) · [Support](https://buymeacoffee.com/boreddevhq)

</div>

---

![Screenshot](branding/screenshot.jpg)

> [!NOTE]
> The screenshot above may represent a previous build and is subject to change as the UI evolves.

---

## Features

### System Architecture
- **64-bit Long Mode** — fully utilizing x86_64
- **Symmetric Multi-Processing (SMP)** — multi-core support via Limine
- **LAPIC & IPI Scheduling** — advanced interrupt handling and inter-processor communication
- **SMP-Safe Spinlocks** — kernel-wide sync for VFS, processes, and GUI
- **FAT32 Filesystem** — persistent and in-memory storage
- **lwIP Networking** — full TCP/IP stack with a basic browser
- **Multiboot2 Compliant** — runs on real hardware and emulators

### Graphical Interface
- **BoredWM** — custom window manager with drag-and-drop
- **Media support** — PNG, GIF, JPEG, TGA, BMP decoding

### Included Applications
| Category | Apps |
|----------|------|
| Productivity | Text Editor, Calculator, Markdown Viewer, BoredWord, Browser |
| Creativity | Paint |
| Utilities | Terminal, Task Manager, Files, Clock, TCC, Grapher etc. |
| Games | Minesweeper, DOOM, 2048, snake  |

</td>
</tr>
</table>

---



## 📚 Documentation

| Guide | Description |
|-------|-------------|
| [Documentation Index](docs/README.md) | Start here! |
| [Architecture Overview](docs/architecture/README.md) | Deep dive into the kernel |
| [Building and Running](docs/build/usage.md) | Set up your build environment |
| [AppDev SDK](docs/appdev/custom_apps.md) | Build your own apps for BoredOS |

---

## Contributors

<table>
<tr>
  <td align="center">
    <a href="https://github.com/boreddevnl">
      <img src="https://github.com/boreddevnl.png?size=80" width="60" /><br />
      <sub><b>BoredDevNL</b></sub>
    </a><br />
    Creator & Lead Maintainer
  </td>
  <td align="center">
    <a href="https://github.com/Lluciocc">
      <img src="https://github.com/Lluciocc.png?size=80" width="60" /><br />
      <sub><b>Lluciocc</b></sub>
    </a><br />
    Maintainer
  </td>
  <td align="center">
    <a href="https://github.com/Mellurboo">
      <img src="https://github.com/Mellurboo.png?size=80" width="60" /><br />
      <sub><b>Mellurboo</b></sub>
    </a><br />
    Contributor
  </td>
  <td align="center">
    <a href="https://github.com/Artemix1508">
      <img src="https://github.com/Artemix1508.png?size=80" width="60" /><br />
      <sub><b>Artemix1508</b></sub>
    </a><br />
    Artwork
  </td>
</tr>
</table>

---

## ☕ Support the Journey

If you find BoredOS interesting or useful, consider fueling development with a coffee!

<a href="https://buymeacoffee.com/boreddevhq" target="_blank">
  <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" height="50" style="border-radius: 8px;" />
</a>

---

## History

**BoredOS** is the successor to **[BrewKernel](https://github.com/boreddevnl/brewkernel)**, a project started in 2023. BrewKernel served as the foundational learning ground but has since been officially deprecated and archived — it no longer receives updates, bug fixes, or pull request reviews.

BoredOS is a complete architectural reboot, applying years of lessons learned to build a cleaner, more modular, and more capable system.

> [!IMPORTANT]
> Please direct all issues, discussions, and contributions to this repository. Legacy BrewKernel code is preserved for historical purposes only and is not compatible with BoredOS.

---

## License

**Copyright (C) 2023–2026 boreddevnl**

Distributed under the **GNU General Public License v3**. See [`LICENSE`](LICENSE) for details.

> [!IMPORTANT]
> You must retain all copyright headers and include the original attribution in any redistributions or derivative works. See the [`NOTICE`](NOTICE) file for more details.

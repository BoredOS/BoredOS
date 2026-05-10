# BoredOS Architecture

This folder gathers the architecture documentation that explains how BoredOS is built from the kernel up.

## Architecture roadmap

The documentation is split by area so you can go directly to the subsystem you want to understand.

| Area | Document | Description |
| --- | --- | --- |
| Graphics | [`graphics/window_manager.md`](architecture/graphics/window_manager.md) | Window manager design and display composition. |
| Hardware | [`hardware/input.md`](architecture/hardware/input.md) | Hardware-level input support and device wiring. |
| Hardware | [`hardware/pci.md`](architecture/hardware/pci.md) | PCI bus management and device enumeration. |
| Input | [`input/keyboard.md`](architecture/input/keyboard.md) | Keyboard input handling and key mapping. |
| Memory | [`memory/memory.md`](architecture/memory/memory.md) | Memory architecture, paging, and address space layout. |
| Memory | [`memory/memory_manager.md`](architecture/memory/memory_manager.md) | Memory allocation and management systems. |
| Network | [`network/network_stack.md`](architecture/network/network_stack.md) | TCP/IP stack design, protocol flow, and packet handling. |
| Network | [`network/network_drivers.md`](architecture/network/network_drivers.md) | Network driver architecture and interface support. |
| Storage | [`storage/filesystem.md`](architecture/storage/filesystem.md) | File system structure and storage access. |
| Storage | [`storage/ahci_drivers.md`](architecture/storage/ahci_drivers.md) | AHCI driver implementation and disk controller support. |
| System | [`system/core.md`](architecture/system/core.md) | Core kernel architecture and main subsystems. |
| System | [`system/interrupts.md`](architecture/system/interrupts.md) | Interrupt handling and low-level event dispatch. |
| System | [`system/processes.md`](architecture/system/processes.md) | Process management, scheduling, and execution model. |
| General | [`versioning.md`](architecture/versioning.md) | Release versioning and project numbering conventions. |

## Quick start

- **Read `system/core.md` first** for the kernel overview.
- Then explore the subsystem area you need: `memory/`, `network/`, `storage/`, `graphics/`, or `system/`.
- Use `versioning.md` to understand BoredOS version rules.

> Note: The links above point directly to the most important architecture documents in this folder.

# PCI Bus Subsystem

The Peripheral Component Interconnect (PCI) subsystem in BoredOS is responsible for discovering, enumerating, and configuring hardware devices connected to the motherboard. It provides the foundation for the OS to load specific device drivers (like Network Interface Cards or AHCI controllers).

## 1. Configuration Space Access

BoredOS interacts with the PCI bus via the legacy x86 I/O ports:
- **`0xCF8`**: Address Port (used to select a specific bus, device, function, and register offset).
- **`0xCFC`**: Data Port (used to read or write the 32-bit value at the selected address).

These are abstracted in `src/dev/pci.c` by the `pci_read_config()` and `pci_write_config()` functions. By writing a formatted 32-bit address to `0xCF8`, the CPU signals the PCI bridge to route the subsequent data read/write on `0xCFC` to the correct hardware device.

## 2. Device Enumeration

During boot, BoredOS recursively scans the PCI buses. The PCI bus topology is hierarchical:
- Up to **256 buses**.
- Each bus has up to **32 devices**.
- Each device has up to **8 functions** (for multi-function devices).

The enumeration process (`pci_enumerate_devices`):
1. Iterates through Bus 0 to 255.
2. For each bus, iterates through Devices 0 to 31.
3. For each device, it reads the `Vendor ID` at offset 0. If the value is `0xFFFF`, no device is present at that slot.
4. If a valid Vendor ID is found, it populates a `pci_device_t` structure containing the:
   - `Vendor ID` and `Device ID` (used to uniquely identify the hardware model).
   - `Class Code`, `Subclass`, and `Prog IF` (used to identify the generic type of the device, e.g., Network Controller, Mass Storage Controller).

## 3. Base Address Registers (BARs)

PCI devices expose memory-mapped I/O (MMIO) regions or I/O port ranges via Base Address Registers (BARs). 
BoredOS provides the `pci_get_bar(dev, bar_num)` function to extract these base addresses.

Drivers use BARs to talk directly to the hardware. For example:
- The AHCI driver reads BAR5 to find the base address of the AHCI memory registers (ABAR).
- The E1000 driver uses a BAR to map the NIC's control registers into the kernel's virtual memory space.

## 4. Hardware Configuration

Once a device is found, drivers can call helper functions to enable specific PCI features:
- **`pci_enable_bus_mastering(dev)`**: Sets the Bus Master bit in the PCI Command Register. This is critical for drivers that use DMA (Direct Memory Access), allowing the hardware to read/write system RAM independently of the CPU (used heavily by AHCI and Network drivers).
- **`pci_enable_mmio(dev)`**: Sets the Memory Space Enable bit, allowing the CPU to access the device's MMIO regions.

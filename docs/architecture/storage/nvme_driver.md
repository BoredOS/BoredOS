# NVMe Storage Driver

BoredOS implements an NVMe (Non-Volatile Memory Express) host controller driver to interface with NVMe SSDs. The driver is located in `src/dev/nvme.c` and registers each active NVMe namespace as a `Disk` through the standard disk manager API, allowing the rest of the kernel to use it transparently alongside AHCI/SATA devices.

## 1. Discovery and Initialization

The NVMe initialization process (`nvme_init`) starts by querying the PCI subsystem:
1. It searches for a PCI device with Class `0x01` (Mass Storage) and Subclass `0x08` (NVMe).
2. It calls `pci_enable_bus_mastering` and `pci_enable_mmio` to enable DMA and MMIO access.
3. It retrieves the controller's **BAR0** (a 64-bit MMIO region) and maps it into the kernel's virtual address space.

The controller is then reset and brought up according to the NVMe 1.3 specification:
1. **CC.EN** is cleared to disable the controller and wait for **CSTS.RDY** to deassert.
2. Admin Submission Queue (ASQ) and Admin Completion Queue (ACQ) are allocated in physically contiguous memory and their addresses written to the controller registers.
3. **CC** is configured (4K page size, NVM command set, queue entry sizes) and **CC.EN** is set to re-enable the controller.
4. The driver polls **CSTS.RDY** to confirm the controller is ready.

## 2. Queue Pairs

The driver uses two queue pairs per controller:

| Queue | Purpose |
|-------|---------|
| Admin SQ / Admin CQ | Controller configuration commands (Identify, Create I/O Queues) |
| I/O SQ / I/O CQ | Data read/write commands |

Each queue holds `NVME_QUEUE_SIZE` (16) entries. The driver uses **polled completion** — it spins on the completion queue phase bit rather than using MSI/MSI-X interrupts, matching the approach used by the AHCI driver.

## 3. Namespace Enumeration

After initialization, the driver sends an **Identify Controller** admin command to retrieve the controller model string, then an **Identify Namespace List** command to find active namespaces.

For each active namespace:
1. An **Identify Namespace** command retrieves the namespace size (`nsze`) and block size.
2. Only 512-byte logical block size namespaces are supported; 4K-native drives are skipped.
3. A `Disk` struct is allocated and registered with the disk manager as `/dev/nvme{n}` (e.g. `/dev/nvme0`).

## 4. Device Naming

NVMe devices follow the standard naming convention:

| Entity | Name format | Example |
|--------|-------------|---------|
| NVMe disk | `/dev/nvme{n}` | `/dev/nvme0` |
| Partition | `/dev/nvme{n}p{p}` | `/dev/nvme0p1` |

This is handled by the NVMe driver setting the `devname` field directly before calling `disk_register`, and by `disk_register_partition` in `disk_manager.c` using a `p` separator for `DISK_TYPE_NVME` parents.

## 5. Read/Write Operations

Data transfer uses standard NVMe Read (`0x02`) and Write (`0x01`) I/O commands submitted to the I/O Submission Queue:

1. The kernel virtual buffer address is translated to a physical address via `v2p()`.
2. The command's PRP1 field is set to the physical buffer address (single-page transfers only).
3. The command is written into the I/O SQ and the tail doorbell is rung.
4. The driver polls the I/O CQ phase bit for completion, checking the status code.

Both single-sector (`read_sector`/`write_sector`) and multi-sector (`read_sectors`/`write_sectors`) interfaces are implemented. Multi-sector transfers issue one NVMe command per sector.

## 6. Limitations

- Single-LBA commands only; multi-LBA batching is a future optimisation.
- Only 512-byte logical block size is supported.
- Polled completion only; no interrupt-driven I/O.
- Maximum of `MAX_NVME_CONTROLLERS` (4) controllers supported.

# AHCI Storage Driver

BoredOS implements an Advanced Host Controller Interface (AHCI) driver to interface with Serial ATA (SATA) mass storage devices. The driver is located in `src/dev/ahci.c` and allows the OS to read and write sectors directly to physical hard drives or solid-state drives using DMA (Direct Memory Access).

## 1. Discovery and Initialization

The AHCI initialization process (`ahci_init`) starts by querying the PCI subsystem:
1. It searches for a PCI device with Class `0x01` (Mass Storage) and Subclass `0x06` (SATA).
2. It calls `pci_enable_bus_mastering` and `pci_enable_mmio` to ensure the controller can perform DMA and its registers are accessible.
3. It retrieves the **ABAR** (AHCI Base Address Register) from PCI BAR5.
4. The ABAR points to the `HBA_MEM` structure (Host Bus Adapter Memory Registers). The kernel iterates through the `pi` (Ports Implemented) bitmask to find active SATA ports.

## 2. Port Configuration

For every active SATA port found, the driver must allocate memory structures that the hardware will use to process commands:
1. **Command List Base (`clb`)**: A 1KB memory region holding 32 Command Headers.
2. **FIS Base (`fb`)**: A 256-byte memory region where the HBA writes incoming Frame Information Structures (FIS) from the drive.
3. **Command Tables (`ctba`)**: A larger memory region allocated for each Command Header, containing the actual SATA command bytes and the scatter/gather lists (PRDT).

*Note:* All AHCI data structures must be allocated in physically contiguous memory and properly aligned (e.g., 1KB or 256-byte boundaries) because the HBA reads them directly from physical RAM via DMA.

## 3. Physical Region Descriptor Tables (PRDT)

When reading or writing data, BoredOS must tell the AHCI controller where in RAM the data should be stored or fetched. This is done using PRDT entries.

Each `HBA_PRDT_ENTRY` specifies:
- A physical Data Base Address (`dba`).
- A Byte Count (`dbc`), limited to a maximum of 4MB per entry.

If a read/write request spans multiple fragmented pages or exceeds 4MB, the driver constructs multiple PRDT entries within the Command Table to form a scatter/gather list. The AHCI hardware seamlessly processes these entries as a single contiguous disk operation.

## 4. Reading and Writing Sectors

To execute a command (e.g., `ahci_read_sectors` or `ahci_write_sectors`):
1. The driver finds a free slot in the Command List.
2. It populates the Command Header, setting the `cfl` (Command FIS Length) and `w` (Write) bit.
3. It builds a Host-to-Device Register FIS (`FIS_REG_H2D`) in the Command Table, issuing the `ATA_CMD_READ_DMA_EX` or `ATA_CMD_WRITE_DMA_EX` command and specifying the starting LBA (Logical Block Address) and sector count.
4. It sets up the PRDT entries pointing to the physical memory of the provided buffer.
5. It sets the corresponding bit in the Port's Command Issue register (`ci`).
6. The driver then polls the `ci` register (or waits for an interrupt) until the bit clears, indicating the hardware has completed the DMA transfer.

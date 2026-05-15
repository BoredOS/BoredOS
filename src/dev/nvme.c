// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

// NVMe (Non-Volatile Memory Express) host controller driver.
//
// This driver discovers an NVMe controller via PCI, initialises it according to
// the NVMe 1.3 specification, and registers each active namespace as a Disk so
// the rest of the kernel can use it through the standard disk_manager API.
//
// Design choices:
//   - Polled completion (no MSI/MSI-X interrupts), matching the AHCI driver.
//   - One Admin queue pair and one I/O queue pair per controller.
//   - Single-LBA commands only; multi-LBA batching is left as a future optimisation.
//   - Only 512-byte logical blocks are supported; 4K-native drives are skipped.
//   - Spinlock discipline: irq-save before touching any queue, matching AHCI pattern.

#include "nvme.h"
#include "pci.h"
#include "disk.h"
#include "../mem/memory_manager.h"
#include "../mem/paging.h"
#include "../sys/spinlock.h"
#include <stddef.h>

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t num);
extern void serial_write_hex(uint32_t val);
extern uint64_t v2p(uint64_t vaddr);   // kernel virtual → physical
extern uint64_t p2v(uint64_t paddr);   // physical → kernel virtual

// ============================================================================
// Internal Constants
// ============================================================================

#define NVME_QUEUE_SIZE      64    // Entries per admin/IO queue (power of two, ≤ CAP.MQES)
#define MAX_NVME_CONTROLLERS  4    // Hard limit on controllers this driver handles
#define MAX_NS_PER_CTRL       8    // Maximum namespaces enumerated per controller

// LBA data size value from LBAF indicating 512-byte logical blocks (2^9 = 512)
#define NVME_LBADS_512       9

// ============================================================================
// NVMe Controller MMIO Register Layout (NVMe 1.3 §3.1)
//
// This volatile struct maps directly onto BAR0. Do not reorder fields.
// ============================================================================

typedef volatile struct {
    uint64_t cap;    // 0x00: Controller Capabilities
    uint32_t vs;     // 0x08: Version
    uint32_t intms;  // 0x0C: Interrupt Mask Set
    uint32_t intmc;  // 0x10: Interrupt Mask Clear
    uint32_t cc;     // 0x14: Controller Configuration
    uint32_t rsv0;   // 0x18: Reserved
    uint32_t csts;   // 0x1C: Controller Status
    uint32_t nssr;   // 0x20: NVM Subsystem Reset
    uint32_t aqa;    // 0x24: Admin Queue Attributes
    uint64_t asq;    // 0x28: Admin Submission Queue Base Address (physical)
    uint64_t acq;    // 0x30: Admin Completion Queue Base Address (physical)
} nvme_regs_t;

// Doorbell registers begin at BAR0 + 0x1000.
// Submission Queue n Tail Doorbell: offset 0x1000 + (2n + 0) * dstrd
// Completion Queue n Head Doorbell: offset 0x1000 + (2n + 1) * dstrd
// n=0: Admin queues, n=1: first I/O queue pair.
#define NVME_SQ_TAIL_DB(base, n, dstrd) \
    ((volatile uint32_t *)((uint8_t *)(base) + 0x1000 + (2*(n) + 0) * (dstrd)))
#define NVME_CQ_HEAD_DB(base, n, dstrd) \
    ((volatile uint32_t *)((uint8_t *)(base) + 0x1000 + (2*(n) + 1) * (dstrd)))

// ============================================================================
// Driver-Private State Structures
// ============================================================================

// Per-controller driver state
typedef struct {
    nvme_regs_t    *regs;        // MMIO register base (virtual address)
    uint32_t        dstrd;       // Doorbell stride in bytes (4 << CAP.DSTRD)

    // Admin queues (queue index 0)
    nvme_sq_entry_t *asq;        // Admin Submission Queue (virtual)
    nvme_cq_entry_t *acq;        // Admin Completion Queue (virtual)
    uint16_t        asq_tail;    // Next slot to write in ASQ
    uint16_t        acq_head;    // Next slot to read in ACQ
    uint8_t         acq_phase;   // Phase bit expected from controller (flips on wrap)

    // I/O queues (queue index 1)
    nvme_sq_entry_t *iosq;       // I/O Submission Queue (virtual)
    nvme_cq_entry_t *iocq;       // I/O Completion Queue (virtual)
    uint16_t        iosq_tail;
    uint16_t        iocq_head;
    uint8_t         iocq_phase;

    uint16_t        next_cid;    // Monotonically increasing Command ID counter
    spinlock_t      lock;        // Guards all queue state; acquired irq-safe for I/O
} nvme_controller_t;

// Stored in Disk.driver_data for each NVMe namespace disk
typedef struct {
    int      ctrl_idx;   // Index into controllers[]
    uint32_t nsid;       // NVMe Namespace Identifier (1-based)
} NVMeDriverData;

// ============================================================================
// Module State
// ============================================================================

static nvme_controller_t controllers[MAX_NVME_CONTROLLERS];
static int nvme_controller_count = 0;
static int nvme_disk_count_total = 0;

// ============================================================================
// String Helpers (no libc in kernel)
// ============================================================================

static void nvme_strcpy(char *dst, const char *src) {
    while ((*dst++ = *src++));
}

static void nvme_memset(void *dst, int val, int len) {
    uint8_t *p = (uint8_t *)dst;
    while (len-- > 0) *p++ = (uint8_t)val;
}

// ============================================================================
// Admin Command: Submit and Poll for Completion
//
// Writes the command into the Admin SQ, rings the tail doorbell, then spins
// on the Admin CQ until the matching completion entry appears (phase bit flips).
// Returns the completion DW0 result on success, or -1 on timeout/error.
// Caller must NOT hold ctrl->lock (admin commands run only during init).
// ============================================================================

static int nvme_admin_submit_poll(nvme_controller_t *ctrl, nvme_sq_entry_t *cmd) {
    uint16_t cid = ctrl->next_cid++;

    // Embed the CID in the high 16 bits of CDW0, leaving the opcode in the low 8 bits
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFFu) | ((uint32_t)cid << 16);

    // Place command in the Admin SQ at the current tail position
    ctrl->asq[ctrl->asq_tail] = *cmd;
    ctrl->asq_tail = (uint16_t)((ctrl->asq_tail + 1) % NVME_QUEUE_SIZE);

    // Notify controller of new SQ entry by writing the new tail to doorbell
    *NVME_SQ_TAIL_DB(ctrl->regs, 0, ctrl->dstrd) = ctrl->asq_tail;

    // Poll the Admin CQ for a completion entry with the correct phase bit and CID
    int timeout = 5000000;
    while (timeout-- > 0) {
        nvme_cq_entry_t *cqe = &ctrl->acq[ctrl->acq_head];

        // The phase bit (bit 0 of status) toggles each time the CQ wraps around.
        // A new completion is valid when phase matches what the host expects.
        if ((cqe->status & 1u) == ctrl->acq_phase && cqe->cid == cid) {
            uint16_t sc = (cqe->status >> 1) & 0x7FFu;  // Status Code field
            uint32_t result = cqe->cdw0;

            // Advance head and update phase if the queue wrapped
            ctrl->acq_head = (uint16_t)((ctrl->acq_head + 1) % NVME_QUEUE_SIZE);
            if (ctrl->acq_head == 0) ctrl->acq_phase ^= 1u;

            // Tell controller we consumed the entry
            *NVME_CQ_HEAD_DB(ctrl->regs, 0, ctrl->dstrd) = ctrl->acq_head;

            if (sc != 0) {
                serial_write("[NVME] Admin command failed, SC=0x");
                serial_write_hex(sc);
                serial_write("\n");
                return -1;
            }
            return (int)result;
        }
        asm volatile("pause");
    }

    serial_write("[NVME] Admin command timed out\n");
    return -1;
}

// ============================================================================
// I/O Command: Submit and Poll for Completion
//
// Identical flow to the admin variant but targets the I/O queue pair (index 1).
// Caller MUST hold ctrl->lock before entering to serialise queue access.
// ============================================================================

static int nvme_io_submit_poll(nvme_controller_t *ctrl, nvme_sq_entry_t *cmd) {
    uint16_t cid = ctrl->next_cid++;
    cmd->cdw0 = (cmd->cdw0 & 0x0000FFFFu) | ((uint32_t)cid << 16);

    ctrl->iosq[ctrl->iosq_tail] = *cmd;
    ctrl->iosq_tail = (uint16_t)((ctrl->iosq_tail + 1) % NVME_QUEUE_SIZE);

    *NVME_SQ_TAIL_DB(ctrl->regs, 1, ctrl->dstrd) = ctrl->iosq_tail;

    int timeout = 5000000;
    while (timeout-- > 0) {
        nvme_cq_entry_t *cqe = &ctrl->iocq[ctrl->iocq_head];

        if ((cqe->status & 1u) == ctrl->iocq_phase && cqe->cid == cid) {
            uint16_t sc = (cqe->status >> 1) & 0x7FFu;
            ctrl->iocq_head = (uint16_t)((ctrl->iocq_head + 1) % NVME_QUEUE_SIZE);
            if (ctrl->iocq_head == 0) ctrl->iocq_phase ^= 1u;
            *NVME_CQ_HEAD_DB(ctrl->regs, 1, ctrl->dstrd) = ctrl->iocq_head;
            return (sc == 0) ? 0 : -1;
        }
        asm volatile("pause");
    }

    serial_write("[NVME] I/O command timed out\n");
    return -1;
}

// ============================================================================
// Disk I/O Callbacks (registered in Disk struct function pointers)
// ============================================================================

// Helper: translate a Disk (possibly a partition) to controller, nsid, and LBA.
// The partition LBA offset is added to the requested sector number.
static void nvme_resolve_disk(Disk *disk, int *ctrl_idx, uint32_t *nsid, uint64_t *base_lba) {
    if (disk->is_partition && disk->parent) {
        NVMeDriverData *pdd = (NVMeDriverData *)disk->parent->driver_data;
        *ctrl_idx  = pdd->ctrl_idx;
        *nsid      = pdd->nsid;
        *base_lba  = disk->partition_lba_offset;
    } else {
        NVMeDriverData *dd = (NVMeDriverData *)disk->driver_data;
        *ctrl_idx  = dd->ctrl_idx;
        *nsid      = dd->nsid;
        *base_lba  = 0;
    }
}

static int nvme_disk_read_sector(Disk *disk, uint32_t sector, uint8_t *buffer) {
    int ctrl_idx;
    uint32_t nsid;
    uint64_t base_lba;
    nvme_resolve_disk(disk, &ctrl_idx, &nsid, &base_lba);

    nvme_controller_t *ctrl = &controllers[ctrl_idx];

    // Translate the virtual buffer address to a physical address for DMA.
    // A 512-byte buffer always fits within a single 4K page, so PRP2 is not needed.
    uint64_t phys = paging_virt2phys(paging_get_pml4_phys(), (uint64_t)buffer);
    if (!phys) return -1;

    nvme_sq_entry_t cmd;
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0  = NVME_IO_READ;
    cmd.nsid  = nsid;
    cmd.prp1  = phys;
    cmd.cdw10 = (uint32_t)((base_lba + sector) & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)((base_lba + sector) >> 32);
    // cdw12 = 0 → NLB field = 0 → transfer 1 logical block (0-based count)

    uint64_t flags = spinlock_acquire_irqsave(&ctrl->lock);
    int ret = nvme_io_submit_poll(ctrl, &cmd);
    spinlock_release_irqrestore(&ctrl->lock, flags);
    return ret;
}

static int nvme_disk_write_sector(Disk *disk, uint32_t sector, const uint8_t *buffer) {
    int ctrl_idx;
    uint32_t nsid;
    uint64_t base_lba;
    nvme_resolve_disk(disk, &ctrl_idx, &nsid, &base_lba);

    nvme_controller_t *ctrl = &controllers[ctrl_idx];

    uint64_t phys = paging_virt2phys(paging_get_pml4_phys(), (uint64_t)buffer);
    if (!phys) return -1;

    nvme_sq_entry_t cmd;
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0  = NVME_IO_WRITE;
    cmd.nsid  = nsid;
    cmd.prp1  = phys;
    cmd.cdw10 = (uint32_t)((base_lba + sector) & 0xFFFFFFFFu);
    cmd.cdw11 = (uint32_t)((base_lba + sector) >> 32);

    uint64_t flags = spinlock_acquire_irqsave(&ctrl->lock);
    int ret = nvme_io_submit_poll(ctrl, &cmd);
    spinlock_release_irqrestore(&ctrl->lock, flags);
    return ret;
}

// Multi-sector variants: loop one sector at a time.
// NVMe can batch sectors in a single command, but that requires PRP lists for
// transfers beyond 8 KB. Sector-at-a-time keeps this first-pass driver simple
// and correct. The lock is held for the entire loop to serialise queue access.
static int nvme_disk_read_sectors(Disk *disk, uint32_t sector, uint32_t count, uint8_t *buffer) {
    int ctrl_idx;
    uint32_t nsid;
    uint64_t base_lba;
    nvme_resolve_disk(disk, &ctrl_idx, &nsid, &base_lba);

    nvme_controller_t *ctrl = &controllers[ctrl_idx];
    uint64_t flags = spinlock_acquire_irqsave(&ctrl->lock);

    for (uint32_t i = 0; i < count; i++) {
        uint64_t phys = paging_virt2phys(paging_get_pml4_phys(), (uint64_t)(buffer + i * 512));
        if (!phys) {
            spinlock_release_irqrestore(&ctrl->lock, flags);
            return -1;
        }

        nvme_sq_entry_t cmd;
        nvme_memset(&cmd, 0, sizeof(cmd));
        cmd.cdw0  = NVME_IO_READ;
        cmd.nsid  = nsid;
        cmd.prp1  = phys;
        cmd.cdw10 = (uint32_t)((base_lba + sector + i) & 0xFFFFFFFFu);
        cmd.cdw11 = (uint32_t)((base_lba + sector + i) >> 32);

        if (nvme_io_submit_poll(ctrl, &cmd) != 0) {
            spinlock_release_irqrestore(&ctrl->lock, flags);
            return -1;
        }
    }

    spinlock_release_irqrestore(&ctrl->lock, flags);
    return 0;
}

static int nvme_disk_write_sectors(Disk *disk, uint32_t sector, uint32_t count, const uint8_t *buffer) {
    int ctrl_idx;
    uint32_t nsid;
    uint64_t base_lba;
    nvme_resolve_disk(disk, &ctrl_idx, &nsid, &base_lba);

    nvme_controller_t *ctrl = &controllers[ctrl_idx];
    uint64_t flags = spinlock_acquire_irqsave(&ctrl->lock);

    for (uint32_t i = 0; i < count; i++) {
        uint64_t phys = paging_virt2phys(paging_get_pml4_phys(), (uint64_t)(buffer + i * 512));
        if (!phys) {
            spinlock_release_irqrestore(&ctrl->lock, flags);
            return -1;
        }

        nvme_sq_entry_t cmd;
        nvme_memset(&cmd, 0, sizeof(cmd));
        cmd.cdw0  = NVME_IO_WRITE;
        cmd.nsid  = nsid;
        cmd.prp1  = phys;
        cmd.cdw10 = (uint32_t)((base_lba + sector + i) & 0xFFFFFFFFu);
        cmd.cdw11 = (uint32_t)((base_lba + sector + i) >> 32);

        if (nvme_io_submit_poll(ctrl, &cmd) != 0) {
            spinlock_release_irqrestore(&ctrl->lock, flags);
            return -1;
        }
    }

    spinlock_release_irqrestore(&ctrl->lock, flags);
    return 0;
}

// Flush: request the controller to write any cached data to non-volatile media.
// Uses NSID=0xFFFFFFFF to flush all namespaces on this controller.
static int nvme_disk_sync(Disk *disk) {
    int ctrl_idx;
    uint32_t nsid;
    uint64_t dummy;
    nvme_resolve_disk(disk, &ctrl_idx, &nsid, &dummy);

    nvme_controller_t *ctrl = &controllers[ctrl_idx];

    nvme_sq_entry_t cmd;
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0 = NVME_IO_FLUSH;
    cmd.nsid = 0xFFFFFFFFu;  // All namespaces

    uint64_t flags = spinlock_acquire_irqsave(&ctrl->lock);
    int ret = nvme_io_submit_poll(ctrl, &cmd);
    spinlock_release_irqrestore(&ctrl->lock, flags);
    return ret;
}

// ============================================================================
// Initialization Helpers
// ============================================================================

// Reset the controller and wait for CSTS.RDY to clear.
// Returns 0 on success, -1 if the controller did not reset in time.
static int nvme_controller_reset(nvme_controller_t *ctrl) {
    // Disable the controller by clearing CC.EN
    ctrl->regs->cc &= ~NVME_CC_EN;

    // Wait for Ready to deassert (controller has fully stopped)
    int timeout = 2000000;
    while ((ctrl->regs->csts & NVME_CSTS_RDY) && timeout-- > 0)
        asm volatile("pause");

    if (timeout <= 0) {
        serial_write("[NVME] Controller did not reset (CSTS.RDY stuck)\n");
        return -1;
    }
    return 0;
}

// Allocate and program admin queues, configure CC, then enable the controller.
// Returns 0 on success, -1 on failure.
static int nvme_controller_enable(nvme_controller_t *ctrl) {
    // Allocate Admin Submission Queue: NVME_QUEUE_SIZE * 64 bytes = 4096 bytes
    ctrl->asq = (nvme_sq_entry_t *)kmalloc_aligned(NVME_QUEUE_SIZE * sizeof(nvme_sq_entry_t), 4096);
    if (!ctrl->asq) return -1;
    nvme_memset(ctrl->asq, 0, NVME_QUEUE_SIZE * sizeof(nvme_sq_entry_t));

    // Allocate Admin Completion Queue: NVME_QUEUE_SIZE * 16 bytes = 1024 bytes
    ctrl->acq = (nvme_cq_entry_t *)kmalloc_aligned(NVME_QUEUE_SIZE * sizeof(nvme_cq_entry_t), 4096);
    if (!ctrl->acq) return -1;
    nvme_memset(ctrl->acq, 0, NVME_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    // Initialise queue head/tail pointers and phase bits
    ctrl->asq_tail  = 0;
    ctrl->acq_head  = 0;
    ctrl->acq_phase = 1;  // Phase starts at 1; controller writes 1 for first batch
    ctrl->next_cid  = 0;

    // Tell the controller the admin queue sizes and physical base addresses
    // AQA: ACQS[27:16] = size-1, ASQS[11:0] = size-1
    ctrl->regs->aqa = (uint32_t)((NVME_QUEUE_SIZE - 1) << 16) | (NVME_QUEUE_SIZE - 1);
    ctrl->regs->asq = v2p((uint64_t)ctrl->asq);
    ctrl->regs->acq = v2p((uint64_t)ctrl->acq);

    // Configure CC: NVM command set, 4K page size, 64-byte SQ entries, 16-byte CQ entries
    ctrl->regs->cc = NVME_CC_CSS_NVM | NVME_CC_MPS_4K | NVME_CC_IOSQES_64 | NVME_CC_IOCQES_16;

    // Enable the controller
    ctrl->regs->cc |= NVME_CC_EN;

    // Wait for CSTS.RDY to assert (controller has come up)
    int timeout = 2000000;
    while (!(ctrl->regs->csts & NVME_CSTS_RDY) && timeout-- > 0)
        asm volatile("pause");

    if (timeout <= 0) {
        serial_write("[NVME] Controller did not become ready after enable\n");
        return -1;
    }
    if (ctrl->regs->csts & NVME_CSTS_CFS) {
        serial_write("[NVME] Controller fatal status after enable\n");
        return -1;
    }
    return 0;
}

// Create one I/O Completion Queue and one I/O Submission Queue (both at index 1).
// Returns 0 on success, -1 on failure.
static int nvme_create_io_queues(nvme_controller_t *ctrl) {
    // Allocate I/O queues in physically contiguous memory (page-aligned)
    ctrl->iosq = (nvme_sq_entry_t *)kmalloc_aligned(NVME_QUEUE_SIZE * sizeof(nvme_sq_entry_t), 4096);
    if (!ctrl->iosq) return -1;
    nvme_memset(ctrl->iosq, 0, NVME_QUEUE_SIZE * sizeof(nvme_sq_entry_t));

    ctrl->iocq = (nvme_cq_entry_t *)kmalloc_aligned(NVME_QUEUE_SIZE * sizeof(nvme_cq_entry_t), 4096);
    if (!ctrl->iocq) return -1;
    nvme_memset(ctrl->iocq, 0, NVME_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    ctrl->iosq_tail  = 0;
    ctrl->iocq_head  = 0;
    ctrl->iocq_phase = 1;

    // Create I/O Completion Queue (admin opcode 0x05)
    // CDW10: QID=1 in low 16 bits, QSIZE-1 in high 16 bits
    // CDW11: PC=1 (physically contiguous), IEN=0 (polling, no interrupt)
    nvme_sq_entry_t cmd;
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0  = NVME_ADMIN_CREATE_IOCQ;
    cmd.prp1  = v2p((uint64_t)ctrl->iocq);
    cmd.cdw10 = 1u | ((uint32_t)(NVME_QUEUE_SIZE - 1) << 16);
    cmd.cdw11 = 1u;  // PC=1

    if (nvme_admin_submit_poll(ctrl, &cmd) < 0) {
        serial_write("[NVME] Failed to create I/O completion queue\n");
        return -1;
    }

    // Create I/O Submission Queue (admin opcode 0x01)
    // CDW11: PC=1, CQID=1 in bits[31:16]
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0  = NVME_ADMIN_CREATE_IOSQ;
    cmd.prp1  = v2p((uint64_t)ctrl->iosq);
    cmd.cdw10 = 1u | ((uint32_t)(NVME_QUEUE_SIZE - 1) << 16);
    cmd.cdw11 = 1u | (1u << 16);  // PC=1, CQID=1

    if (nvme_admin_submit_poll(ctrl, &cmd) < 0) {
        serial_write("[NVME] Failed to create I/O submission queue\n");
        return -1;
    }

    return 0;
}

// Issue an Identify command and write 4096 bytes of response into buf.
// buf must be physically contiguous and page-aligned (use kmalloc_aligned(4096, 4096)).
static int nvme_identify(nvme_controller_t *ctrl, uint32_t nsid, uint32_t cns, void *buf) {
    nvme_sq_entry_t cmd;
    nvme_memset(&cmd, 0, sizeof(cmd));
    cmd.cdw0  = NVME_ADMIN_IDENTIFY;
    cmd.nsid  = nsid;
    cmd.prp1  = v2p((uint64_t)buf);
    cmd.cdw10 = cns;
    return nvme_admin_submit_poll(ctrl, &cmd);
}

// Extract a printable model string from the NVMe Identify Controller data.
// The model string occupies bytes 24-63 (40 bytes) in the identify data,
// stored as ASCII with trailing spaces. We trim those spaces.
static void nvme_extract_model(const uint8_t *id_data, char *model_out, int max_len) {
    int src_len = 40;
    if (max_len < 2) return;
    if (src_len > max_len - 1) src_len = max_len - 1;

    // Copy raw bytes (already correct byte order for ASCII, unlike ATA)
    int i;
    for (i = 0; i < src_len; i++) model_out[i] = (char)id_data[24 + i];

    // Trim trailing spaces
    int end = src_len;
    while (end > 0 && model_out[end - 1] == ' ') end--;
    model_out[end] = '\0';
}

// ============================================================================
// Main Initialisation Entry Point
// ============================================================================

void nvme_init(void) {
    serial_write("[NVME] Scanning PCI for NVMe controller...\n");

    // pci_enumerate_devices uses a 'uint8_t bus < 256' loop which never terminates
    // (uint8_t wraps at 255 back to 0, so the condition is always true). It fills
    // the result buffer with repeated bus-0 entries and never reaches secondary
    // buses where NVMe lives behind PCIe root ports. Bypass it entirely and do a
    // direct scan using int bus so the loop terminates correctly at 256.
    pci_device_t pci_dev;
    int found = 0;

    for (int bus = 0; bus < 256 && !found; bus++) {
        for (int dev = 0; dev < 32 && !found; dev++) {
            // Read vendor/device ID to check if a device is present
            uint32_t id = pci_read_config((uint8_t)bus, (uint8_t)dev, 0, 0x00);
            if ((id & 0xFFFF) == 0xFFFF) continue; // No device at this slot

            // Determine number of functions (bit 7 of header type = multi-function)
            uint32_t hdr = pci_read_config((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            int max_fn = ((hdr >> 16) & 0x80) ? 8 : 1;

            for (int fn = 0; fn < max_fn && !found; fn++) {
                uint32_t fn_id = pci_read_config((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x00);
                if ((fn_id & 0xFFFF) == 0xFFFF) continue;

                uint32_t class_rev = pci_read_config((uint8_t)bus, (uint8_t)dev, (uint8_t)fn, 0x08);
                uint8_t class_code = (uint8_t)((class_rev >> 24) & 0xFF);
                uint8_t subclass   = (uint8_t)((class_rev >> 16) & 0xFF);

                if (class_code == PCI_CLASS_MASS_STORAGE && subclass == PCI_SUBCLASS_NVME) {
                    pci_dev.bus       = (uint8_t)bus;
                    pci_dev.device    = (uint8_t)dev;
                    pci_dev.function  = (uint8_t)fn;
                    pci_dev.vendor_id = (uint16_t)(fn_id & 0xFFFF);
                    pci_dev.device_id = (uint16_t)((fn_id >> 16) & 0xFFFF);
                    pci_dev.class_code = class_code;
                    pci_dev.subclass   = subclass;
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        serial_write("[NVME] No NVMe controller found\n");
        return;
    }

    serial_write("[NVME] Found controller (vendor=0x");
    serial_write_hex(pci_dev.vendor_id);
    serial_write(", device=0x");
    serial_write_hex(pci_dev.device_id);
    serial_write(")\n");

    if (nvme_controller_count >= MAX_NVME_CONTROLLERS) {
        serial_write("[NVME] Controller limit reached\n");
        return;
    }

    // Enable PCI bus mastering (required for DMA) and MMIO access
    pci_enable_bus_mastering(&pci_dev);
    pci_enable_mmio(&pci_dev);

    // Read BAR0. NVMe uses a 64-bit BAR: BAR0 holds low 32 bits, BAR1 holds high 32 bits.
    // Bit 1 of BAR0 is 1 if the BAR is 64-bit.
    uint32_t bar0_raw = pci_get_bar(&pci_dev, 0);
    uint32_t bar1_raw = pci_get_bar(&pci_dev, 1);
    uint64_t bar0_phys;
    if (((bar0_raw >> 1) & 0x3u) == 2u) {
        bar0_phys = ((uint64_t)bar1_raw << 32) | (bar0_raw & 0xFFFFFFF0u);
    } else {
        bar0_phys = bar0_raw & 0xFFFFFFF0u;
    }

    if (!bar0_phys) {
        serial_write("[NVME] BAR0 is zero — controller not properly assigned\n");
        return;
    }

    serial_write("[NVME] BAR0 mapped (phys hi=0x");
    serial_write_hex((uint32_t)(bar0_phys >> 32));
    serial_write(" lo=0x");
    serial_write_hex((uint32_t)(bar0_phys & 0xFFFFFFFFu));
    serial_write(")\n");

    // Map two pages: first covers the NVMe register space (0x000–0xFFF),
    // second covers the doorbell registers (0x1000 onwards).
    uint64_t bar0_virt = p2v(bar0_phys);
    paging_map_page(paging_get_pml4_phys(), bar0_virt,        bar0_phys,        PT_PRESENT | PT_RW | PT_CACHE_DISABLE);
    paging_map_page(paging_get_pml4_phys(), bar0_virt + 4096, bar0_phys + 4096, PT_PRESENT | PT_RW | PT_CACHE_DISABLE);

    nvme_controller_t *ctrl = &controllers[nvme_controller_count];
    ctrl->regs = (nvme_regs_t *)bar0_virt;
    ctrl->lock = SPINLOCK_INIT;

    // Extract doorbell stride from CAP[35:32]. Stride in bytes = 4 << DSTRD.
    uint64_t cap = ctrl->regs->cap;
    ctrl->dstrd = 4u << ((cap >> 32) & 0xFu);

    serial_write("[NVME] NVMe version ");
    serial_write_num((ctrl->regs->vs >> 16) & 0xFFFF);
    serial_write(".");
    serial_write_num((ctrl->regs->vs >> 8) & 0xFF);
    serial_write(", doorbell stride=");
    serial_write_num(ctrl->dstrd);
    serial_write(" bytes\n");

    // Step 1: Reset the controller (clears any prior state)
    if (nvme_controller_reset(ctrl) != 0) return;

    // Step 2: Configure admin queues and re-enable the controller
    if (nvme_controller_enable(ctrl) != 0) return;

    serial_write("[NVME] Controller enabled and ready\n");

    // Step 3: Create one I/O queue pair for data transfers
    if (nvme_create_io_queues(ctrl) != 0) return;

    serial_write("[NVME] I/O queues created\n");

    // Step 4: Identify the controller to get model name and namespace count
    uint8_t *id_buf = (uint8_t *)kmalloc_aligned(4096, 4096);
    if (!id_buf) return;

    if (nvme_identify(ctrl, 0, NVME_IDENTIFY_CNS_CONTROLLER, id_buf) < 0) {
        serial_write("[NVME] Identify controller failed\n");
        kfree(id_buf);
        return;
    }

    char model[64];
    nvme_extract_model(id_buf, model, sizeof(model));

    // Number of namespaces is at byte offset 516 (NN field) in controller identify data
    uint32_t nn = *(uint32_t *)(id_buf + 516);
    serial_write("[NVME] Controller: ");
    serial_write(model);
    serial_write(", namespaces=");
    serial_write_num(nn);
    serial_write("\n");

    kfree(id_buf);

    if (nn == 0) {
        serial_write("[NVME] No namespaces found\n");
        return;
    }

    // Clamp to our per-controller maximum
    if (nn > MAX_NS_PER_CTRL) nn = MAX_NS_PER_CTRL;

    // Step 5: For each namespace, identify it and register a Disk
    uint8_t *ns_buf = (uint8_t *)kmalloc_aligned(4096, 4096);
    if (!ns_buf) return;

    for (uint32_t nsid = 1; nsid <= nn; nsid++) {
        nvme_memset(ns_buf, 0, 4096);

        if (nvme_identify(ctrl, nsid, NVME_IDENTIFY_CNS_NAMESPACE, ns_buf) < 0) {
            serial_write("[NVME] Identify namespace ");
            serial_write_num(nsid);
            serial_write(" failed, skipping\n");
            continue;
        }

        // Namespace size: NSZE at byte 0 (uint64_t), number of logical blocks
        uint64_t nsze = *(uint64_t *)(ns_buf + 0);
        if (nsze == 0) {
            serial_write("[NVME] Namespace ");
            serial_write_num(nsid);
            serial_write(" is empty, skipping\n");
            continue;
        }

        // Determine LBA block size from LBAF[FLBAS].LBADS
        // FLBAS at byte 26: bits[3:0] = index into LBAF array
        uint8_t flbas = ns_buf[26] & 0x0Fu;
        // Each LBAF entry is 4 bytes starting at offset 128
        uint32_t lbaf = *(uint32_t *)(ns_buf + 128 + flbas * 4);
        // LBADS is bits[23:16] of the LBAF entry (log2 of block size in bytes)
        uint8_t lbads = (uint8_t)((lbaf >> 16) & 0xFFu);

        if (lbads != NVME_LBADS_512) {
            serial_write("[NVME] Namespace ");
            serial_write_num(nsid);
            serial_write(" uses non-512B LBA size (LBADS=");
            serial_write_num(lbads);
            serial_write("), skipping\n");
            continue;
        }

        serial_write("[NVME] Namespace ");
        serial_write_num(nsid);
        serial_write(": ");
        serial_write_num((uint32_t)nsze);
        serial_write(" sectors\n");

        // Allocate Disk and driver data structures
        Disk *disk = (Disk *)kmalloc(sizeof(Disk));
        if (!disk) continue;
        nvme_memset(disk, 0, sizeof(Disk));

        NVMeDriverData *dd = (NVMeDriverData *)kmalloc(sizeof(NVMeDriverData));
        if (!dd) { kfree(disk); continue; }

        dd->ctrl_idx = nvme_controller_count;
        dd->nsid     = nsid;

        // devname left empty (0) so disk_register auto-assigns "sda", "sdb", etc.
        disk->type         = DISK_TYPE_NVME;
        disk->total_sectors = (nsze > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)nsze;
        disk->read_sector  = nvme_disk_read_sector;
        disk->write_sector = nvme_disk_write_sector;
        disk->read_sectors = nvme_disk_read_sectors;
        disk->write_sectors= nvme_disk_write_sectors;
        disk->sync         = nvme_disk_sync;
        disk->driver_data  = dd;
        disk->parent       = NULL;
        disk->is_partition = false;
        disk->is_fat32     = false;

        // Use controller model string as the disk label
        nvme_strcpy(disk->label, model);

        disk_register(disk);
        disk_rescan(disk);

        nvme_disk_count_total++;
    }

    kfree(ns_buf);
    nvme_controller_count++;
}

int nvme_get_disk_count(void) {
    return nvme_disk_count_total;
}

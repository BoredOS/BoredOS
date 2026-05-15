// Copyright (c) 2026 Keegan (RicePollution) (https://github.com/RicePollution)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stdbool.h>

// PCI class 0x01 (Mass Storage), subclass 0x08 (NVM Express)
#define PCI_SUBCLASS_NVME  0x08

// ============================================================================
// NVMe Controller Register Bit Definitions
// ============================================================================

// Controller Configuration (CC) bits
#define NVME_CC_EN          (1u << 0)   // Controller Enable
#define NVME_CC_CSS_NVM     (0u << 4)   // Command Set Selected: NVM
#define NVME_CC_MPS_4K      (0u << 7)   // Memory Page Size: 4096 bytes (2^(12+0))
#define NVME_CC_IOSQES_64   (6u << 16)  // I/O Submission Queue Entry Size: 2^6 = 64 bytes
#define NVME_CC_IOCQES_16   (4u << 20)  // I/O Completion Queue Entry Size: 2^4 = 16 bytes

// Controller Status (CSTS) bits
#define NVME_CSTS_RDY       (1u << 0)   // Controller Ready
#define NVME_CSTS_CFS       (1u << 1)   // Controller Fatal Status

// ============================================================================
// NVMe Admin Command Opcodes (sent to Admin Submission Queue)
// ============================================================================

#define NVME_ADMIN_CREATE_IOCQ   0x05   // Create I/O Completion Queue
#define NVME_ADMIN_CREATE_IOSQ   0x01   // Create I/O Submission Queue
#define NVME_ADMIN_IDENTIFY      0x06   // Identify controller or namespace

// CNS field values for the Identify command
#define NVME_IDENTIFY_CNS_NAMESPACE   0x00   // Return namespace data structure
#define NVME_IDENTIFY_CNS_CONTROLLER  0x01   // Return controller data structure

// ============================================================================
// NVMe I/O Command Opcodes (sent to I/O Submission Queue)
// ============================================================================

#define NVME_IO_FLUSH   0x00   // Flush (cache writeback)
#define NVME_IO_WRITE   0x01   // Write LBAs to media
#define NVME_IO_READ    0x02   // Read LBAs from media

// ============================================================================
// NVMe Queue Entry Structures
// ============================================================================

// Submission Queue Entry: 64 bytes, one per I/O or admin command
typedef struct {
    uint32_t cdw0;   // Opcode[7:0], FUSE[9:8], PSDT[15:14], CID[31:16]
    uint32_t nsid;   // Namespace Identifier
    uint32_t cdw2;   // Reserved / command-specific
    uint32_t cdw3;   // Reserved / command-specific
    uint64_t mptr;   // Metadata Pointer (unused for basic I/O)
    uint64_t prp1;   // Physical Region Page 1: first data page physical address
    uint64_t prp2;   // Physical Region Page 2: second page or PRP list pointer
    uint32_t cdw10;  // Command-specific (e.g. LBA low, CNS for Identify)
    uint32_t cdw11;  // Command-specific (e.g. LBA high, queue creation flags)
    uint32_t cdw12;  // Command-specific (e.g. NLB - number of logical blocks, 0-based)
    uint32_t cdw13;  // Reserved / command-specific
    uint32_t cdw14;  // Reserved / command-specific
    uint32_t cdw15;  // Reserved / command-specific
} __attribute__((packed)) nvme_sq_entry_t;

// Completion Queue Entry: 16 bytes, returned by controller for each completed command
typedef struct {
    uint32_t cdw0;      // Command-specific result (e.g. identify data pointer)
    uint32_t cdw1;      // Reserved
    uint16_t sq_head;   // SQ Head Pointer: how far the controller has consumed the SQ
    uint16_t sq_id;     // Submission Queue ID that generated this completion
    uint16_t cid;       // Command Identifier: matches the CID in the original SQE
    uint16_t status;    // Phase bit [0], Status field [15:1]
} __attribute__((packed)) nvme_cq_entry_t;

// ============================================================================
// Public API
// ============================================================================

// Scan PCI for NVMe controller(s), initialize, and register namespaces as disks.
void nvme_init(void);

// Return the number of NVMe disks registered (across all controllers/namespaces).
int nvme_get_disk_count(void);

#endif // NVME_H

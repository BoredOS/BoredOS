// Copyright (c) 2026 Myles Wilson (myles@bleedkernel.com)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "acpi_i2c.h"
#include "acpi.h"
#include "acpi_aml.h"
#include "acpi_structures.h"
#include "kconsole.h"
#include "platform.h"
#include "kutils.h"

static aml_i2c_dev_t  i2c_devices[ACPI_I2C_MAX_DEVICES];
static size_t         i2c_device_count = 0;

// ACPI SDT header is 36 bytes AML starts concurrently, nice and convenient 
#define ACPI_SDT_HEADER_LEN  36

static void walk_sdt(struct acpi_sdt *sdt, aml_walk_ctx_t *ctx) {
    if (!sdt) return;
    if (sdt->length <= ACPI_SDT_HEADER_LEN) return;

    const uint8_t *aml = (const uint8_t *)sdt + ACPI_SDT_HEADER_LEN;
    size_t len = sdt->length - ACPI_SDT_HEADER_LEN;
    aml_walk_table(aml, len, ctx);
}

static void scan_ssdt(struct acpi_sdt *sdt, aml_walk_ctx_t *ctx) {
    if (!sdt) return;
    if (__builtin_memcmp(sdt->signature, "SSDT", 4) != 0) return;
    const uint8_t *aml_ptr = (const uint8_t *)sdt + ACPI_SDT_HEADER_LEN;
    size_t aml_len = sdt->length - ACPI_SDT_HEADER_LEN;
    aml_find_i2c_controllers(aml_ptr, aml_len);
    walk_sdt(sdt, ctx);
}

static void walk_all_ssdts(aml_walk_ctx_t *ctx) {
    struct acpi_rsdp *rsdp = acpi_get_rsdp();
    if (!rsdp) return;

    // Scan XSDT if available
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        struct acpi_xsdt *xsdt = (struct acpi_xsdt *)p2v(rsdp->xsdt_address);
        size_t entries = (xsdt->header.length - sizeof(struct acpi_sdt)) / 8;
        for (size_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = (struct acpi_sdt *)p2v(xsdt->tables[i]);
            if (!sdt) continue;
            scan_ssdt(sdt, ctx);
        }
    }

    // Also scan RSDT
    if (rsdp->rsdt_address) {
        struct acpi_sdt *rsdt = (struct acpi_sdt *)p2v(rsdp->rsdt_address);
        uint32_t *tables = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt));
        size_t entries = (rsdt->length - sizeof(struct acpi_sdt)) / 4;
        for (size_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = (struct acpi_sdt *)p2v(tables[i]);
            if (!sdt) continue;
            scan_ssdt(sdt, ctx);
        }
    }
}

int acpi_i2c_enumerate(void) {
    i2c_device_count = 0;

    aml_walk_ctx_t ctx = {
        .devices  = i2c_devices,
        .capacity = ACPI_I2C_MAX_DEVICES,
        .count    = 0,
    };

    // Walk DSDT - check both 32-bit and 64-bit pointers
    struct acpi_sdt *dsdt = acpi_get_dsdt();
    if (dsdt) {
        walk_sdt(dsdt, &ctx);
    }

    // Walk all SSDTs 
    walk_all_ssdts(&ctx);

    i2c_device_count = ctx.count;

    if (i2c_device_count != 0) {
        serial_write("[acpi_i2c] Enumerated ");
        serial_write_num(i2c_device_count);
        serial_write(" I2C devices from ACPI\n");
    }

    return (int)i2c_device_count;
}

size_t acpi_i2c_count(void) {
    return i2c_device_count;
}

const aml_i2c_dev_t *acpi_i2c_get(size_t index) {
    if (index >= i2c_device_count) return NULL;
    return &i2c_devices[index];
}
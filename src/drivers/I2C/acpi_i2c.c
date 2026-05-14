// Copyright (c) 2026 Myles Wilson (myles@bleedkernel.com)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "acpi_i2c.h"
#include "../ACPI/acpi.h"
#include "../ACPI/acpi_aml.h"
#include "../ACPI/acpi_structures.h"
#include "../core/kconsole.h"
#include "../core/platform.h"

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

static void walk_all_ssdts(aml_walk_ctx_t *ctx) {
    struct acpi_rsdp *rsdp = acpi_get_rsdp();
    if (!rsdp) return;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        struct acpi_xsdt *xsdt = (struct acpi_xsdt *)p2v(rsdp->xsdt_address);
        size_t entries = (xsdt->header.length - sizeof(struct acpi_sdt)) / 8;
        for (size_t i = 0; i < entries; i++) {
            struct acpi_sdt *sdt = (struct acpi_sdt *)p2v(xsdt->tables[i]);
            if (!sdt) continue;
            if (__builtin_memcmp(sdt->signature, "SSDT", 4) == 0)
                walk_sdt(sdt, ctx);
        }
        return;
    }

    // RSDT fallback 
    if (!rsdp->rsdt_address) return;
    struct acpi_sdt *rsdt = (struct acpi_sdt *)p2v(rsdp->rsdt_address);
    uint32_t *tables = (uint32_t *)((uint8_t *)rsdt + sizeof(struct acpi_sdt));
    size_t entries = (rsdt->length - sizeof(struct acpi_sdt)) / 4;
    for (size_t i = 0; i < entries; i++) {
        struct acpi_sdt *sdt = (struct acpi_sdt *)p2v(tables[i]);
        if (!sdt) continue;
        if (__builtin_memcmp(sdt->signature, "SSDT", 4) == 0)
            walk_sdt(sdt, ctx);
    }
}

int acpi_i2c_enumerate(void) {
    i2c_device_count = 0;

    aml_walk_ctx_t ctx = {
        .devices  = i2c_devices,
        .capacity = ACPI_I2C_MAX_DEVICES,
        .count    = 0,
    };

    // Walk DSDT - pointed to by FADT, not listed in XSDT/RSDT 
    struct acpi_sdt *dsdt = acpi_get_dsdt();
    if (dsdt) {
        walk_sdt(dsdt, &ctx);
    } else {
        serial_write("[acpi_i2c] warning: DSDT not found\n");
    }

    // Walk all SSDTs 
    walk_all_ssdts(&ctx);

    i2c_device_count = ctx.count;

    for (size_t i = 0; i < i2c_device_count; i++) {
        const aml_i2c_dev_t *d = &i2c_devices[i];
        serial_write("[acpi_i2c] ");
        serial_write(d->name);
        serial_write("  hid=");
        serial_write(d->hid);
        serial_write("  addr=0x");
        serial_write_hex(d->slave_address);
        serial_write("  speed=");
        serial_write_num(d->speed_hz);
        serial_write(" Hz");
        if (d->ten_bit_addr)  serial_write("  10-bit");
        if (d->power_flags & (AML_PWR_HAS_PS0 | AML_PWR_HAS_PS3)) serial_write("  PS0/3");
        if (d->power_flags & (AML_PWR_HAS_PR0 | AML_PWR_HAS_PR3)) serial_write("  PR0/3");
        if (d->has_dsm) {
            serial_write("  hid_desc_reg=0x");
            serial_write_hex(d->hid_desc_addr);
        }
        serial_write("\n");
    }

    if (i2c_device_count != 0) {
        log_ok("I2C enumeration complete");
    } else {
        log_fail("No I2C devices found in ACPI tables");
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
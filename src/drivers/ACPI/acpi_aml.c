// Copyright (c) 2026 Myles Wilson (myles@bleedkernel.com)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.

#include "acpi_aml.h"
#include "kutils.h"
#include "kconsole.h"
#include <stdint.h>
#include <stddef.h>

/// @brief Decode AML PkgLength field, advance past it
/// @param pp in/out byte pointer, advanced past the field on return
/// @param end hard bound; returns 0 if field exceeds it
/// @return total package length including the PkgLength bytes, 0 on error
static size_t decode_pkglen(const uint8_t **pp, const uint8_t *end) {
    if (*pp >= end) return 0;
    uint8_t b0 = *(*pp)++;
    uint8_t extra = b0 >> 6;
    size_t  len;

    if (extra == 0) {
        len = b0 & 0x3F;   // 1-byte form: bits[5:0] 
    } else {
        len = b0 & 0x0F;   // multi-byte form: bits[3:0] are low nibble 
        if (*pp + extra > end) return 0;
        for (uint8_t i = 0; i < extra; i++)
            len |= (size_t)(*(*pp)++) << (4 + 8 * i);
    }
    return len;
}

/// @brief Decode the last NameSeg of an AML NameString into out
/// @param pp in/out byte pointer, advanced past the full name on return
/// @param end hard bound
/// @param out receives null-terminated 4-char NameSeg
static void decode_nameseg(const uint8_t **pp, const uint8_t *end,
                            char out[AML_NAME_LEN]) {
    out[0] = '\0';
    if (*pp >= end) return;

    while (*pp < end && (**pp == '\\' || **pp == '^')) (*pp)++;
    if (*pp >= end) return;

    uint8_t lead = **pp;

    if (lead == 0x00) {
        (*pp)++;
        return;
    } else if (lead == 0x2E) {   // DualNamePath two 4-char segs 
        (*pp)++;
        if (*pp + 8 > end) return;
        (*pp) += 4;
        for (int i = 0; i < 4; i++) out[i] = (char)(*pp)[i];
        out[4] = '\0';
        (*pp) += 4;
    } else if (lead == 0x2F) {   // MultiNamePath count + N segs 
        (*pp)++;
        if (*pp >= end) return;
        uint8_t count = *(*pp)++;
        if (*pp + (size_t)count * 4 > end) return;
        const uint8_t *last = *pp + (count - 1) * 4;
        for (int i = 0; i < 4; i++) out[i] = (char)last[i];
        out[4] = '\0';
        *pp += (size_t)count * 4;
    } else {                     // plain 4-char NameSeg 
        if (*pp + 4 > end) return;
        for (int i = 0; i < 4; i++) out[i] = (char)(*pp)[i];
        out[4] = '\0';
        (*pp) += 4;
    }
}

/// @brief Decode any AML integer data object (Zero, One, Byte, Word, DWord, QWord)
/// @param pp in/out byte pointer, advanced past the object on return
/// @param end hard bound
/// @return decoded integer value, 0 on unknown opcode (pointer not advanced)
static uint64_t decode_integer(const uint8_t **pp, const uint8_t *end) {
    if (*pp >= end) return 0;
    uint8_t op = *(*pp)++;
    switch (op) {
        case AML_ZERO_OP:     return 0;
        case AML_ONE_OP:      return 1;
        case AML_BYTE_PREFIX:
            if (*pp + 1 > end) return 0;
            return *(*pp)++;
        case AML_WORD_PREFIX: {
            if (*pp + 2 > end) return 0;
            uint16_t v = (uint16_t)((*pp)[0]) | ((uint16_t)((*pp)[1]) << 8);
            *pp += 2; return v;
        }
        case AML_DWORD_PREFIX: {
            if (*pp + 4 > end) return 0;
            uint32_t v = (uint32_t)((*pp)[0])
                       | ((uint32_t)((*pp)[1]) << 8)
                       | ((uint32_t)((*pp)[2]) << 16)
                       | ((uint32_t)((*pp)[3]) << 24);
            *pp += 4; return v;
        }
        case AML_QWORD_PREFIX: {
            if (*pp + 8 > end) return 0;
            uint64_t v = 0;
            for (int i = 0; i < 8; i++) v |= (uint64_t)((*pp)[i]) << (8 * i);
            *pp += 8; return v;
        }
        default:
            (*pp)--;
            return 0;
    }
}

/// @brief Convert 32-bit packed EISAID value to ASCII HID string (e.g. "PNP0C0E")
/// @param id EISAID as decoded by decode_integer (little-endian from AML)
/// @param out receives null-terminated 8-char string
static void eisaid_to_str(uint32_t id, char out[AML_HID_LEN]) {
    uint32_t v = ((id & 0x000000FF) << 24)
               | ((id & 0x0000FF00) << 8)
               | ((id & 0x00FF0000) >> 8)
               | ((id & 0xFF000000) >> 24);

    out[0] = (char)('@' + ((v >> 26) & 0x1F));
    out[1] = (char)('@' + ((v >> 21) & 0x1F));
    out[2] = (char)('@' + ((v >> 16) & 0x1F));

    static const char hex[] = "0123456789ABCDEF";
    out[3] = hex[(v >> 12) & 0xF];
    out[4] = hex[(v >>  8) & 0xF];
    out[5] = hex[(v >>  4) & 0xF];
    out[6] = hex[(v >>  0) & 0xF];
    out[7] = '\0';
}

/// @brief Parse _HID data object (string or EISAID DWord) into dev->hid
/// @param pp in/out byte pointer positioned at the data object
/// @param end hard bound
/// @param dev target device record
static void parse_hid(const uint8_t **pp, const uint8_t *end,
                      aml_i2c_dev_t *dev) {
    if (*pp >= end) return;

    if (**pp == AML_STRING_PREFIX) {
        (*pp)++;
        const char *s = (const char *)*pp;
        strncpy(dev->hid, s, AML_HID_LEN - 1);
        dev->hid[AML_HID_LEN - 1] = '\0';
        while (*pp < end && **pp) (*pp)++;
        if (*pp < end) (*pp)++;
    } else if (**pp == AML_DWORD_PREFIX) {
        uint32_t id = (uint32_t)decode_integer(pp, end);
        eisaid_to_str(id, dev->hid);
    }
}

/// @brief Parse _CRS Buffer for an I2cSerialBusV2 descriptor, fill dev CRS fields
/// @param pp in/out byte pointer positioned at BufferOp
/// @param end hard bound
/// @param dev receives slave_address, speed_hz, ten_bit_addr on success
static void parse_crs(const uint8_t **pp, const uint8_t *end,
                      aml_i2c_dev_t *dev) {
    if (*pp >= end || **pp != AML_BUFFER_OP) return;
    (*pp)++;

    const uint8_t *pkg_start = *pp;
    size_t pkglen = decode_pkglen(pp, end);
    if (!pkglen) return;

    const uint8_t *buf_end = pkg_start + pkglen;
    if (buf_end > end) buf_end = end;

    decode_integer(pp, buf_end);   // skip BufferSize 

    const uint8_t *res = *pp;
    while (res < buf_end) {
        uint8_t tag = *res;

        if (tag == ACPI_RESOURCE_END_TAG) break;

        if (tag == ACPI_LARGE_I2C_SERIAL_BUS) {
            if (res + sizeof(aml_i2c_resource_t) > buf_end) break;
            const aml_i2c_resource_t *r = (const aml_i2c_resource_t *)res;
            if (r->serial_bus_type == ACPI_I2C_SERIAL_BUS_TYPE) {
                dev->slave_address = r->slave_address;
                dev->speed_hz      = r->connection_speed;
                dev->ten_bit_addr  = (r->type_specific_flags & 0x01) ? 1 : 0;
            }
            res += sizeof(uint8_t) + sizeof(uint16_t) + r->length;
            continue;
        }

        if (tag & 0x80) {
            if (res + 3 > buf_end) break;
            uint16_t len = (uint16_t)(res[1]) | ((uint16_t)(res[2]) << 8);
            res += 3 + len;
        } else {
            res += 1 + (tag & 0x07);
        }
    }

    *pp = buf_end;
}

static void record_power_state(const char name[AML_NAME_LEN],
                                aml_i2c_dev_t *dev) {
    if      (memcmp(name, "_PS0", 4) == 0) dev->power_flags |= AML_PWR_HAS_PS0;
    else if (memcmp(name, "_PS3", 4) == 0) dev->power_flags |= AML_PWR_HAS_PS3;
    else if (memcmp(name, "_PR0", 4) == 0) dev->power_flags |= AML_PWR_HAS_PR0;
    else if (memcmp(name, "_PR3", 4) == 0) dev->power_flags |= AML_PWR_HAS_PR3;
}

/// @brief Best-effort skip of one AML data object to keep the scan on track
/// @param pp in/out byte pointer, advanced past the object on return
/// @param end hard bound
static void skip_object(const uint8_t **pp, const uint8_t *end) {
    if (*pp >= end) return;
    uint8_t op = **pp;
    switch (op) {
        case AML_ZERO_OP:
        case AML_ONE_OP:
            (*pp)++; return;
        case AML_BYTE_PREFIX:   (*pp) += 2; return;
        case AML_WORD_PREFIX:   (*pp) += 3; return;
        case AML_DWORD_PREFIX:  (*pp) += 5; return;
        case AML_QWORD_PREFIX:  (*pp) += 9; return;
        case AML_STRING_PREFIX:
            (*pp)++;
            while (*pp < end && **pp) (*pp)++;
            if (*pp < end) (*pp)++;
            return;
        case AML_BUFFER_OP:
        case AML_PACKAGE_OP: {
            (*pp)++;
            const uint8_t *start = *pp;
            size_t len = decode_pkglen(pp, end);
            if (len) *pp = start + len;
            return;
        }
        default:
            (*pp)++;
    }
}

/// @brief Scan one Device() scope body for _HID, _CRS, _DSM, and power objects
/// @param p first byte of the scope body (after NameSeg)
/// @param end one past last byte of the scope
/// @param dev device record to populate
static void scan_device_scope(const uint8_t *p, const uint8_t *end,
                               aml_i2c_dev_t *dev) {
    while (p < end) {
        uint8_t op = *p;

        if (op == AML_NAME_OP) {
            p++;
            char seg[AML_NAME_LEN];
            decode_nameseg(&p, end, seg);

            if      (memcmp(seg, "_HID", 4) == 0) parse_hid(&p, end, dev);
            else if (memcmp(seg, "_CRS", 4) == 0) parse_crs(&p, end, dev);
            else { record_power_state(seg, dev); skip_object(&p, end); }
            continue;
        }

        if (op == AML_METHOD_OP) {
            p++;
            const uint8_t *method_start = p;
            size_t pkglen = decode_pkglen(&p, end);
            if (!pkglen) break;

            const uint8_t *method_end = method_start + pkglen;
            if (method_end > end) method_end = end;

            char seg[AML_NAME_LEN];
            const uint8_t *name_ptr = p;
            decode_nameseg(&name_ptr, method_end, seg);

            if (memcmp(seg, "_DSM", 4) == 0) {
                const uint8_t *body = name_ptr + 1;   // skip MethodFlags we dont care abt it rn
                const uint8_t *scan = body;
                const uint8_t *guid = (const uint8_t *)ACPI_I2C_HID_DSM_GUID;

                while (scan + 16 < method_end) {
                    if (memcmp(scan, guid, 16) != 0) { scan++; continue; }
                    scan += 16;
                    while (scan + 4 < method_end) {
                        if (*scan != AML_RETURN_OP)  { scan++; continue; }
                        scan++;
                        if (*scan != AML_PACKAGE_OP) { continue; }
                        scan++;
                        const uint8_t *ps = scan;
                        size_t pl = decode_pkglen(&scan, method_end);
                        if (!pl) break;
                        const uint8_t *pe = ps + pl;
                        if (pe > method_end || scan >= pe) break;
                        uint8_t nelem = *scan++;
                        if (nelem < 1) break;
                        uint64_t val = decode_integer(&scan, pe);
                        if (val <= 0xFFFF) {
                            dev->hid_desc_addr = (uint16_t)val;
                            dev->has_dsm       = 1;
                        }
                        goto method_done;
                    }
                    break;
                }
            } else {
                record_power_state(seg, dev);
            }

        method_done:
            p = method_end;
            continue;
        }

        // skip nested Device or Scope without recursing 
        if (op == AML_EXTOP_PREFIX && p + 1 < end && *(p + 1) == AML_DEVICE_OP) {
            p += 2;
            const uint8_t *s = p;
            size_t l = decode_pkglen(&p, end);
            if (l) p = s + l; else break;
            continue;
        }

        if (op == AML_SCOPE_OP) {
            p++;
            const uint8_t *s = p;
            size_t l = decode_pkglen(&p, end);
            if (l) p = s + l; else break;
            continue;
        }

        p++;
    }
}


void aml_walk_table(const uint8_t *aml, size_t len, aml_walk_ctx_t *ctx) {
    if (!aml || !len || !ctx || !ctx->devices) return;

    const uint8_t *p   = aml;
    const uint8_t *end = aml + len;

    while (p + 2 < end) {
        if (p[0] != AML_EXTOP_PREFIX || p[1] != AML_DEVICE_OP) { p++; continue; }
        p += 2;

        const uint8_t *scope_start = p;
        size_t pkglen = decode_pkglen(&p, end);
        if (!pkglen) continue;

        const uint8_t *scope_end = scope_start + pkglen;
        if (scope_end > end) scope_end = end;

        char dev_name[AML_NAME_LEN];
        decode_nameseg(&p, scope_end, dev_name);

        if (ctx->count >= ctx->capacity) break;

        aml_i2c_dev_t *dev = &ctx->devices[ctx->count];
        memset(dev, 0, sizeof(aml_i2c_dev_t));
        memcpy(dev->name, dev_name, AML_NAME_LEN);

        scan_device_scope(p, scope_end, dev);

        scan_device_scope(p, scope_end, dev);

        if (dev->hid[0] && dev->speed_hz) {
            dev->valid = 1;
            ctx->count++;
        }

        p = scope_end;
    }
}

int aml_parse_s5(const uint8_t *aml, size_t len,
                 uint16_t *slp_typa, uint16_t *slp_typb) {
    if (!aml || !len || !slp_typa || !slp_typb) return 0;

    const uint8_t *end = aml + len;
    const uint8_t *p   = aml;

    while (p + 4 < end) {
        if (p[0] != '_' || p[1] != 'S' || p[2] != '5' || p[3] != '_') {
            p++; continue;
        }
        p += 4;

        // scan ahead up to 4 bytes for PackageOp; on diff firmware layouts YMMV 
        int found_pkg = 0;
        for (int skip = 0; skip < 4 && p + skip < end; skip++) {
            if (p[skip] == AML_PACKAGE_OP) {
                p += skip + 1;
                found_pkg = 1;
                break;
            }
        }
        if (!found_pkg) continue;

        const uint8_t *pkg_start = p;
        size_t pkglen = decode_pkglen(&p, end);
        if (!pkglen) continue;

        const uint8_t *pkg_end = pkg_start + pkglen;
        if (pkg_end > end) pkg_end = end;

        if (p >= pkg_end) continue;
        uint8_t num = *p++;
        if (num < 2) continue;

        uint64_t typa = decode_integer(&p, pkg_end);
        uint64_t typb = decode_integer(&p, pkg_end);

        *slp_typa = (uint16_t)((typa & 0x7) << 10);
        *slp_typb = (uint16_t)((typb & 0x7) << 10);
        return 1;
    }

    return 0;
}

// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "process.h"
#include "graphics.h"
#include "io.h"
#include "kutils.h"

extern void serial_write(const char *str);

static void draw_string_centered(int y, const char *s, uint32_t color) {
    if (!s) return;
    int len = 0;
    while (s[len]) len++;
    int x = (get_screen_width() - (len * 8)) / 2;
    draw_string(x, y, s, color);
}

void kernel_panic(registers_t *regs, const char *error_name) {
    asm volatile("cli");

    graphics_clear_back_buffer(0x00000000);
    int sh = get_screen_height();
    int cy = sh / 2;

    // Header
    draw_string_centered(cy - 150, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", 0xFFFF0000);
    draw_string_centered(cy - 130, "KERNEL PANIC", 0xFFFFFFFF);
    draw_string_centered(cy - 110, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!", 0xFFFF0000);

    // Error name
    char buf[256];
    int pos = 0;
    const char *prefix = "Error: ";
    while (prefix[pos]) { buf[pos] = prefix[pos]; pos++; }
    int i = 0;
    while (error_name[i]) { buf[pos++] = error_name[i++]; }
    buf[pos] = 0;
    draw_string_centered(cy - 70, buf, 0xFFFFCC00);

    if (regs != NULL) {
        // Exception details
        char info_buf[64];
        const char *digits = "0123456789ABCDEF";

        #define FMT_HEX(prefix_str, value, color, y_offset)                   \
        do {                                                                   \
            int pos = 0;                                                       \
            const char *pfx = (prefix_str);                                   \
            while (pfx[pos]) { info_buf[pos] = pfx[pos]; pos++; }             \
            info_buf[pos++] = '0'; info_buf[pos++] = 'x';                     \
            uint64_t _v = (value);                                             \
            for (int _i = 15; _i >= 0; _i--) {                                \
                info_buf[pos + _i] = digits[_v & 0xF]; _v >>= 4;              \
            }                                                                  \
            info_buf[pos + 16] = 0;                                            \
            draw_string_centered((y_offset), info_buf, (color));               \
        } while (0)

        FMT_HEX("Vector: ",     regs->int_no,   0xFFFFFFFF, cy - 40);
        FMT_HEX("Error Code: ", regs->err_code, 0xFFFFFFFF, cy - 20);
        FMT_HEX("RIP: ",        regs->rip,      0xFFFFFFFF, cy);

        if (regs->int_no == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            FMT_HEX("CR2: ", cr2, 0xFFFF5555, cy + 20);
        }

        #undef FMT_HEX
    }

    draw_string_centered(cy + 100, "The system has been halted to prevent damage.", 0xFFFFFFFF);
    draw_string_centered(cy + 120, "Please restart your computer.", 0xFFAAAAAA);

    graphics_mark_screen_dirty();
    graphics_flip_buffer();

    serial_write("\n*** KERNEL PANIC ***\n");
    serial_write(error_name);
    serial_write("\n");

    if (regs != NULL) {
        char hex_buf[17];

        serial_write("Vector: 0x");
        itoa_hex(regs->int_no, hex_buf);
        serial_write(hex_buf);
        serial_write("\n");

        serial_write("Error Code: 0x");
        itoa_hex(regs->err_code, hex_buf);
        serial_write(hex_buf);
        serial_write("\n");

        serial_write("RIP: 0x");
        itoa_hex(regs->rip, hex_buf);
        serial_write(hex_buf);
        serial_write("\n");

        if (regs->int_no == 14) {
            uint64_t cr2;
            asm volatile("mov %%cr2, %0" : "=r"(cr2));
            serial_write("CR2: 0x");
            itoa_hex(cr2, hex_buf);
            serial_write(hex_buf);
            serial_write("\n");
        }
    }

    while (1) {
        asm volatile("cli; hlt");
    }
}

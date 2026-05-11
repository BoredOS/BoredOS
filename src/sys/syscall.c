// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall.h"
#include "gdt.h"
#include "memory_manager.h"
#include "gui_ipc.h"
#include "process.h"
#include "wm.h"
#include "fat32.h"
#include "vfs.h"
#include "paging.h"
#include "work_queue.h"
#include "smp.h"
#include "platform.h"
#include "io.h"
#include "pci.h"
#include "kutils.h"
#include "network.h"
#include "icmp.h"
#include "cmd.h"
#include "tty.h"
#include "font_manager.h"
#include "graphics.h"
#include "input/keycodes.h"
#include "input/keymap.h"
#include "app_metadata.h"
#include "disk.h"
#include "mkfs_fat32.h"

#define SYSTEM_CMD_DISK_GET_COUNT  100
#define SYSTEM_CMD_DISK_GET_INFO   101
#define SYSTEM_CMD_DISK_WRITE_GPT  102
#define SYSTEM_CMD_DISK_WRITE_MBR  103
#define SYSTEM_CMD_DISK_MKFS_FAT32 104
#define SYSTEM_CMD_DISK_MOUNT      105
#define SYSTEM_CMD_DISK_UMOUNT     106
#define SYSTEM_CMD_DISK_RESCAN     107
#define SYSTEM_CMD_DISK_REPLACE_KERNEL 108
#define SYSTEM_CMD_DISK_SYNC       109

#define SPAWN_FLAG_TERMINAL 0x1
#define SPAWN_FLAG_INHERIT_TTY 0x2
#define SPAWN_FLAG_TTY_ID 0x4

#define SYSTEM_CMD_SET_KEYBOARD_LAYOUT 49
#define SYSTEM_CMD_GET_KEYBOARD_LAYOUT 51

// Read MSR
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

// Write MSR
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    asm volatile("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

extern void isr128_wrapper(void);
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

typedef struct {
    void (*fn)(void *);
    void *arg;
    uint64_t pml4_phys;
    volatile int *completion_counter;
} smp_user_task_t;

static void smp_user_wrapper(void *arg) {
    smp_user_task_t *task = (smp_user_task_t *)arg;
    if (!task) return;

    uint64_t old_cr3;
    asm volatile("mov %%cr3, %0" : "=r"(old_cr3));
    
    // Switch to user address space if necessary
    bool switch_cr3 = (task->pml4_phys != 0 && task->pml4_phys != old_cr3);
    if (switch_cr3) {
        asm volatile("mov %0, %%cr3" :: "r"(task->pml4_phys) : "memory");
    }

    if (task->fn) {
        task->fn(task->arg);
    }

    if (switch_cr3) {
        asm volatile("mov %0, %%cr3" :: "r"(old_cr3) : "memory");
    }

    if (task->completion_counter) {
        __sync_fetch_and_add(task->completion_counter, -1);
    }

}

void syscall_init(void) {
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= 1; 
    wrmsr(MSR_EFER, efer);
    uint64_t star = ((uint64_t)0x001B << 48) | ((uint64_t)0x0008 << 32);
    wrmsr(MSR_STAR, star);
    extern void syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
    wrmsr(MSR_FMASK, 0x200); 
}

static void user_window_close(Window *win) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_CLOSE };
    process_push_gui_event(proc, &ev);
}

static void user_window_paint(Window *win) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_PAINT };
    process_push_gui_event(proc, &ev);
}

static void user_window_click(Window *win, int x, int y) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_CLICK, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_right_click(Window *win, int x, int y) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_RIGHT_CLICK, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_down(Window *win, int x, int y) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_DOWN, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_up(Window *win, int x, int y) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_UP, .arg1 = x, .arg2 = y };
    process_push_gui_event(proc, &ev);
}

static void user_window_mouse_move(Window *win, int x, int y, uint8_t buttons) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;
    gui_event_t ev = { .type = GUI_EVENT_MOUSE_MOVE, .arg1 = x, .arg2 = y, .arg3 = buttons };
    process_push_gui_event(proc, &ev);
}

// Helper function for WM to send mouse events
void syscall_send_mouse_move_event(Window *win, int x, int y, uint8_t buttons) {
    if (!win) return;
    user_window_mouse_move(win, x, y, buttons);
}

void syscall_send_mouse_down_event(Window *win, int x, int y) {
    if (!win) return;
    user_window_mouse_down(win, x, y);
}

void syscall_send_mouse_up_event(Window *win, int x, int y) {
    if (!win) return;
    user_window_mouse_up(win, x, y);
}

static void user_window_key(Window *win, int legacy, uint16_t keycode, uint32_t codepoint, uint32_t mods, bool pressed) {
    process_t *proc = process_get_by_ui_window(win);
    if (!proc) return;

    gui_event_t ev = {
        .type = pressed ? GUI_EVENT_KEY : GUI_EVENT_KEYUP,
        .arg1 = legacy,
        .arg2 = (int)keycode,
        .arg3 = (int)mods,
        .arg4 = (int)codepoint
    };
    process_push_gui_event(proc, &ev);
}

static void user_window_resize(Window *win, int w, int h) {
    if (!win) return;
    if (w <= 0 || h <= 0) return;
    
    extern void* kmalloc(size_t size);
    extern void kfree(void* ptr);
    extern void serial_write(const char *str);
    
    if (win->pixels) kfree(win->pixels);
    if (win->comp_pixels) kfree(win->comp_pixels);
    
    win->pixels = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    win->comp_pixels = (uint32_t *)kmalloc(w * h * sizeof(uint32_t));
    
    win->w = w;
    win->h = h;
    
    if (win->pixels) {
        extern void mem_memset(void *dest, int val, size_t len);
        mem_memset(win->pixels, 0, w * h * sizeof(uint32_t));
    }

    process_t *proc = process_get_by_ui_window(win);
    if (proc) {
        gui_event_t ev = { .type = GUI_EVENT_RESIZE, .arg1 = w, .arg2 = h };
        process_push_gui_event(proc, &ev);
    }
}

typedef struct {
    registers_t *regs;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    uint64_t arg4;
    uint64_t arg5;
} syscall_args_t;

typedef uint64_t (*syscall_handler_fn)(const syscall_args_t *args);
static uint64_t gui_cmd_window_create(const syscall_args_t *args) {
    extern void serial_write(const char *str);
    process_t *proc = process_get_current();
    const char *title = (const char *)args->arg2;
    
    serial_write("[WM] CreateWindow: ");
    serial_write(title ? title : "Unknown");
    serial_write("\n");
    uint64_t *u_params = (uint64_t *)args->arg3;
    if (!u_params) {
        serial_write("[WM] Error - params is NULL\n");
        return 0;
    }

    // Copy params from user space to kernel space for safety
    uint64_t params[4];
    for (int i = 0; i < 4; i++) params[i] = u_params[i];
    
    Window *win = kmalloc(sizeof(Window));
    if (!win) {
        serial_write("[WM] Error - kmalloc failed for Window\n");
        return 0;
    }
    
    extern void mem_memset(void *dest, int val, size_t len);
    mem_memset(win, 0, sizeof(Window));

    // Copy title from user space to kernel space so wm.c can access it safely
    int title_len = 0;
    if (title) {
        while (title[title_len] && title_len < 255) title_len++;
    }
    
    char *kernel_title = kmalloc(title_len + 1);
    if (kernel_title) {
        for (int i = 0; i < title_len; i++) {
            kernel_title[i] = title[i];
        }
        kernel_title[title_len] = '\0';
    } else {
        serial_write("[WM] Warning: kernel_title kmalloc failed\n");
    }
    
    // Basic initialization
    win->title = kernel_title ? kernel_title : "Unknown";
    win->x = (int)params[0];
    win->y = (int)params[1];
    win->w = (int)params[2];
    win->h = (int)params[3];
    
    // Sanity checks for dimensions
    if (win->w <= 0 || win->w > 4096) win->w = 400;
    if (win->h <= 0 || win->h > 4096) win->h = 400;

    win->visible = true;
    win->focused = true;
    win->z_index = 0;
    win->cursor_pos = 0;
    win->data = proc;
    win->font = NULL;
    win->lock = SPINLOCK_INIT;
    
    size_t pixel_size = 0;
    // Safe allocation
    size_t client_h = win->h - 20;
    if (win->w <= 0 || win->h <= 20) {
        // Invalid dimensions, but prevent underflow/bad alloc
        win->pixels = NULL;
        win->comp_pixels = NULL;
    } else {
        pixel_size = (size_t)win->w * client_h * 4;
        win->pixels = kmalloc(pixel_size);
        win->comp_pixels = kmalloc(pixel_size);
    }
    
    if (win->pixels) {
        extern void mem_memset(void *dest, int val, size_t len);
        mem_memset(win->pixels, 0, pixel_size);
    }
    if (win->comp_pixels) {
        extern void mem_memset(void *dest, int val, size_t len);
        mem_memset(win->comp_pixels, 0, pixel_size);
    }
    
    serial_write("[WM] Buffers ready\n");

    // Set callbacks
    win->paint = user_window_paint;
    win->handle_click = user_window_click;
    win->handle_right_click = user_window_right_click;
    win->handle_mouse_down = user_window_mouse_down;
    win->handle_mouse_up = user_window_mouse_up;
    win->handle_mouse_move = user_window_mouse_move;
    win->handle_close = user_window_close;
    win->handle_key = user_window_key;
    win->handle_resize = user_window_resize;
    win->resizable = false; // Default to false, can be enabled via syscall
    
    // Store owner PID to allow safe detachment during window destruction.
    // This prevents Use-After-Free when a process continues drawing after its window is closed.
    win->owner_pid = proc->pid;
    proc->ui_window = win;
    wm_add_window(win);
    wm_mark_dirty(0, 0, get_screen_width(), 30);
    
    return (uint64_t)win;
}

static uint64_t gui_cmd_draw_rect(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t *u_params = (uint64_t *)args->arg3;
    uint32_t color = (uint32_t)args->arg4;
    process_t *proc = process_get_current();

    if (win && u_params && proc && proc->ui_window == win) {
        uint64_t params[4];
        for (int i = 0; i < 4; i++) params[i] = u_params[i];

        extern void draw_rect(int x, int y, int w, int h, uint32_t color);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        uint64_t rflags = spinlock_acquire_irqsave(&win->lock);
        
        if (win->pixels) {
            int rx = (int)params[0]; int ry = (int)params[1];
            int rw = (int)params[2]; int rh = (int)params[3];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > win->w) rw = win->w - rx;
            if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;
            
            if (rw > 0 && rh > 0) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                draw_rect(rx, ry, rw, rh, color);
                graphics_set_render_target(NULL, 0, 0);
            }
        } else {
            uint64_t wflags = wm_lock_acquire();
            draw_rect(win->x + (int)params[0], win->y + (int)params[1], (int)params[2], (int)params[3], color);
            wm_lock_release(wflags);
        }
        
        spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_rounded_rect_filled(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t *u_params = (uint64_t *)args->arg3;
    uint32_t color = (uint32_t)args->arg4;
    process_t *proc = process_get_current();

    if (win && u_params && proc && proc->ui_window == win) {
        uint64_t params[5];
        for (int i = 0; i < 5; i++) params[i] = u_params[i];

        extern void draw_rounded_rect_filled(int x, int y, int w, int h, int radius, uint32_t color);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        uint64_t rflags = spinlock_acquire_irqsave(&win->lock);
        
        if (win->pixels) {
            int rx = (int)params[0]; int ry = (int)params[1];
            int rw = (int)params[2]; int rh = (int)params[3];
            int rr = (int)params[4];
            if (rx < 0) { rw += rx; rx = 0; }
            if (ry < 0) { rh += ry; ry = 0; }
            if (rx + rw > win->w) rw = win->w - rx;
            if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;

            if (rw > 0 && rh > 0) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                draw_rounded_rect_filled(rx, ry, rw, rh, rr, color);
                graphics_set_render_target(NULL, 0, 0);
            }
        }
        
        spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_string(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t coords = args->arg3;
    int ux = coords & 0xFFFFFFFF;
    int uy = coords >> 32;
    const char *user_str = (const char *)args->arg4;
    uint32_t color = (uint32_t)args->arg5;
    process_t *proc = process_get_current();

    if (win && user_str && proc && proc->ui_window == win) {
        extern void draw_string(int x, int y, const char *str, uint32_t color);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        // Copy string safely to kernel stack buffer
        char kernel_str[256];
        int i = 0;
        while (i < 255 && user_str[i]) {
            kernel_str[i] = user_str[i];
            i++;
        }
        kernel_str[i] = 0;

        uint64_t rflags = spinlock_acquire_irqsave(&win->lock);
        
        ttf_font_t *font = win->font ? (ttf_font_t*)win->font : graphics_get_current_ttf();

        if (win->pixels) {
            if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                if (font) {
                    int baseline = uy + font_manager_get_font_ascent_scaled(font, font->pixel_height) - 2;
                    int cur_x = ux;
                    const char *s = kernel_str;
                    int start_x = cur_x;
                    while (*s) {
                        uint32_t codepoint = utf8_decode(&s);
                        if (codepoint == '\n') {
                            cur_x = start_x;
                            baseline += font_manager_get_font_line_height_scaled(font, font->pixel_height);
                        } else if (codepoint == '\t') {
                            cur_x += font_manager_get_codepoint_width_scaled(font, ' ', font->pixel_height) * 4;
                        } else {
                            font_manager_render_char_scaled(font, cur_x, baseline, codepoint, color, font->pixel_height, put_pixel);
                            cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, font->pixel_height);
                        }
                    }
                } else {
                    draw_string(ux, uy, kernel_str, color);
                }
                graphics_set_render_target(NULL, 0, 0);
            }
        } else {
            uint64_t wflags = wm_lock_acquire();
            if (font) {
                int baseline = win->y + uy + font_manager_get_font_ascent_scaled(font, font->pixel_height) - 2;
                int cur_x = win->x + ux;
                const char *s = kernel_str;
                int start_x = cur_x;
                while (*s) {
                    uint32_t codepoint = utf8_decode(&s);
                    if (codepoint == '\n') {
                        cur_x = start_x;
                        baseline += font_manager_get_font_line_height_scaled(font, font->pixel_height);
                    } else if (codepoint == '\t') {
                        cur_x += font_manager_get_codepoint_width_scaled(font, ' ', font->pixel_height) * 4;
                    } else {
                        font_manager_render_char_scaled(font, cur_x, baseline, codepoint, color, font->pixel_height, put_pixel);
                        cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, font->pixel_height);
                    }
                }
            } else {
                draw_string(win->x + ux, win->y + uy, kernel_str, color);
            }
            wm_lock_release(wflags);
        }
        
        spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_string_bitmap(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t coords = args->arg3;
    int ux = coords & 0xFFFFFFFF;
    int uy = coords >> 32;
    const char *user_str = (const char *)args->arg4;
    uint32_t color = (uint32_t)args->arg5;
    if (win && user_str) {
        extern void draw_string_bitmap(int x, int y, const char *str, uint32_t color);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        // Copy string safely to kernel stack buffer
        char kernel_str[256];
        int i = 0;
        while (i < 255 && user_str[i]) {
            kernel_str[i] = user_str[i];
            i++;
        }
        kernel_str[i] = 0;

        uint64_t rflags;
        bool use_wm_lock = (win->pixels == NULL);
        if (use_wm_lock) rflags = wm_lock_acquire();
        else rflags = spinlock_acquire_irqsave(&win->lock);
        
        if (win->pixels) {
            if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                draw_string_bitmap(ux, uy, kernel_str, color);
                graphics_set_render_target(NULL, 0, 0);
            }
        } else {
            draw_string_bitmap(win->x + ux, win->y + uy, kernel_str, color);
        }
        
        if (use_wm_lock) wm_lock_release(rflags);
        else spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_string_scaled(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t coords = args->arg3;
    int ux = coords & 0xFFFFFFFF;
    int uy = coords >> 32;
    const char *user_str = (const char *)args->arg4;
    uint64_t packed = args->arg5;
    uint32_t color = packed & 0xFFFFFFFF;
    uint32_t scale_bits = packed >> 32;
    float scale = *(float*)&scale_bits;

    if (win && user_str) {
        extern void draw_string_scaled(int x, int y, const char *str, uint32_t color, float scale);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        // Copy string safely to kernel stack buffer
        char kernel_str[256];
        int i = 0;
        while (i < 255 && user_str[i]) {
            kernel_str[i] = user_str[i];
            i++;
        }
        kernel_str[i] = 0;

        uint64_t rflags;
        bool use_wm_lock = (win->pixels == NULL);
        if (use_wm_lock) rflags = wm_lock_acquire();
        else rflags = spinlock_acquire_irqsave(&win->lock);
        
        ttf_font_t *font = win->font ? (ttf_font_t*)win->font : graphics_get_current_ttf();

        if (win->pixels) {
            if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                if (font) {
                    int baseline = uy + font_manager_get_font_ascent_scaled(font, scale) - 2;
                    int cur_x = ux;
                    const char *s = kernel_str;
                    int start_x = cur_x;
                    while (*s) {
                        uint32_t codepoint = utf8_decode(&s);
                        if (codepoint == '\n') {
                            cur_x = start_x;
                            baseline += font_manager_get_font_line_height_scaled(font, scale);
                        } else if (codepoint == '\t') {
                            cur_x += font_manager_get_codepoint_width_scaled(font, ' ', scale) * 4;
                        } else {
                            font_manager_render_char_scaled(font, cur_x, baseline, codepoint, color, scale, put_pixel);
                            cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, scale);
                        }
                    }
                } else {
                    draw_string_scaled(ux, uy, kernel_str, color, scale);
                }
                graphics_set_render_target(NULL, 0, 0);
            }
        } else {
            if (font) {
                int baseline = win->y + uy + font_manager_get_font_ascent_scaled(font, scale) - 2;
                int cur_x = win->x + ux;
                const char *s = kernel_str;
                int start_x = cur_x;
                while (*s) {
                    uint32_t codepoint = utf8_decode(&s);
                    if (codepoint == '\n') {
                        cur_x = start_x;
                        baseline += font_manager_get_font_line_height_scaled(font, scale);
                    } else if (codepoint == '\t') {
                        cur_x += font_manager_get_codepoint_width_scaled(font, ' ', scale) * 4;
                    } else {
                        font_manager_render_char_scaled(font, cur_x, baseline, codepoint, color, scale, put_pixel);
                        cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, scale);
                    }
                }
            } else {
                draw_string_scaled(win->x + ux, win->y + uy, kernel_str, color, scale);
            }
        }
        
        if (use_wm_lock) wm_lock_release(rflags);
        else spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_string_scaled_sloped(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t coords = args->arg3;
    int ux = coords & 0xFFFFFFFF;
    int uy = coords >> 32;
    const char *user_str = (const char *)args->arg4;
    
    // Unpack color, scale, slope from arg5
    uint64_t packed1 = args->arg5;
    uint32_t color = packed1 & 0xFFFFFFFF;
    uint32_t scale_bits = packed1 >> 32;
    float scale = *(float*)&scale_bits;
    
    uint64_t arg6 = args->regs->r9;
    uint32_t slope_bits = arg6 & 0xFFFFFFFF;
    float slope = *(float*)&slope_bits;
    
    if (win && user_str) {
        extern void draw_string_scaled_sloped(int x, int y, const char *str, uint32_t color, float scale, float slope);
        extern void graphics_set_render_target(uint32_t *buffer, int w, int h);
        
        // Copy string safely to kernel stack buffer
        char kernel_str[256];
        int i = 0;
        while (i < 255 && user_str[i]) {
            kernel_str[i] = user_str[i];
            i++;
        }
        kernel_str[i] = 0;

        uint64_t rflags;
        bool use_wm_lock = (win->pixels == NULL);
        if (use_wm_lock) rflags = wm_lock_acquire();
        else rflags = spinlock_acquire_irqsave(&win->lock);
        
        ttf_font_t *font = win->font ? (ttf_font_t*)win->font : graphics_get_current_ttf();

        if (win->pixels) {
            if (ux >= -100 && ux < win->w && uy >= -100 && uy < (win->h - 20)) {
                graphics_set_render_target(win->pixels, win->w, win->h - 20);
                if (font) {
                    int baseline = uy + font_manager_get_font_ascent_scaled(font, scale) - 2;
                    int cur_x = ux;
                    const char *s = kernel_str;
                    int start_x = cur_x;
                    while (*s) {
                        extern void font_manager_render_char_sloped(ttf_font_t *font, int x, int y, uint32_t codepoint, uint32_t color, float scale, float slope, void (*put_pixel_fn)(int, int, uint32_t));
                        uint32_t codepoint = utf8_decode(&s);
                        if (codepoint == '\n') {
                            cur_x = start_x;
                            baseline += font_manager_get_font_line_height_scaled(font, scale);
                        } else if (codepoint == '\t') {
                            cur_x += font_manager_get_codepoint_width_scaled(font, ' ', scale) * 4;
                        } else {
                            font_manager_render_char_sloped(font, cur_x, baseline, codepoint, color, scale, slope, put_pixel);
                            cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, scale);
                        }
                    }
                } else {
                    draw_string_scaled_sloped(ux, uy, kernel_str, color, scale, slope);
                }
                graphics_set_render_target(NULL, 0, 0);
            }
        } else {
            if (font) {
                int baseline = win->y + uy + font_manager_get_font_ascent_scaled(font, scale) - 2;
                int cur_x = win->x + ux;
                const char *s = kernel_str;
                int start_x = cur_x;
                while (*s) {
                    extern void font_manager_render_char_sloped(ttf_font_t *font, int x, int y, uint32_t codepoint, uint32_t color, float scale, float slope, void (*put_pixel_fn)(int, int, uint32_t));
                    uint32_t codepoint = utf8_decode(&s);
                    if (codepoint == '\n') {
                        cur_x = start_x;
                        baseline += font_manager_get_font_line_height_scaled(font, scale);
                    } else if (codepoint == '\t') {
                        cur_x += font_manager_get_codepoint_width_scaled(font, ' ', scale) * 4;
                    } else {
                        font_manager_render_char_sloped(font, cur_x, baseline, codepoint, color, scale, slope, put_pixel);
                        cur_x += font_manager_get_codepoint_width_scaled(font, codepoint, scale);
                    }
                }
            } else {
                draw_string_scaled_sloped(win->x + ux, win->y + uy, kernel_str, color, scale, slope);
            }
        }
        
        if (use_wm_lock) wm_lock_release(rflags);
        else spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_draw_image(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t *u_params = (uint64_t *)args->arg3;
    uint32_t *image_data = (uint32_t *)args->arg4;
    process_t *proc = process_get_current();

    if (win && u_params && image_data && proc && proc->ui_window == win) {
        uint64_t params[4];
        for (int i = 0; i < 4; i++) params[i] = u_params[i];
        
        uint64_t rflags = spinlock_acquire_irqsave(&win->lock);
        
        if (win->pixels) {
            int rx = (int)params[0]; int ry = (int)params[1];
            int rw = (int)params[2]; int rh = (int)params[3];
            int src_w = rw;
            int src_x_offset = 0;
            int src_y_offset = 0;

            if (rx < 0) { src_x_offset = -rx; rw += rx; rx = 0; }
            if (ry < 0) { src_y_offset = -ry; rh += ry; ry = 0; }
            if (rx + rw > win->w) rw = win->w - rx;
            if (ry + rh > (win->h - 20)) rh = (win->h - 20) - ry;

            if (rw > 0 && rh > 0) {
                for (int y = 0; y < rh; y++) {
                    uint32_t *dest = &win->pixels[(ry + y) * win->w + rx];
                    uint32_t *src = &image_data[(src_y_offset + y) * src_w + src_x_offset];
                    for (int x = 0; x < rw; x++) {
                        uint32_t s = src[x];
                        uint8_t alpha = (s >> 24) & 0xFF;
                        if (alpha == 0xFF) {
                            dest[x] = s;
                        } else if (alpha > 0) {
                            uint32_t d = dest[x];
                            uint32_t rb = ((s & 0xFF00FF) * alpha + (d & 0xFF00FF) * (255 - alpha)) >> 8;
                            uint32_t g = ((s & 0x00FF00) * alpha + (d & 0x00FF00) * (255 - alpha)) >> 8;
                            dest[x] = (rb & 0xFF00FF) | (g & 0x00FF00) | 0xFF000000;
                        }
                    }
                }
            }
        }
        
        spinlock_release_irqrestore(&win->lock, rflags);
    }
    return 0;
}

static uint64_t gui_cmd_mark_dirty(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    uint64_t *u_params = (uint64_t *)args->arg3;
    process_t *proc = process_get_current();

    if (win && u_params && proc && proc->ui_window == win) {
        uint64_t params[4];
        for (int i = 0; i < 4; i++) params[i] = u_params[i];

        // Dual-buffer commit: copy pixels to comp_pixels
        if (win->pixels && win->comp_pixels) {
            uint64_t win_rflags = spinlock_acquire_irqsave(&win->lock);
            extern void mem_memcpy(void *dest, const void *src, size_t len);
            mem_memcpy(win->comp_pixels, win->pixels, (size_t)win->w * (win->h - 20) * 4);
            spinlock_release_irqrestore(&win->lock, win_rflags);
        }

        uint64_t rflags = wm_lock_acquire();
        wm_mark_dirty(win->x + (int)params[0], win->y + (int)params[1], (int)params[2], (int)params[3]);
        wm_lock_release(rflags);
    }
    return 0;
}

static uint64_t gui_cmd_get_event(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    gui_event_t *ev_out = (gui_event_t *)args->arg3;
    if (!ev_out) return 0;
    if (proc->gui_event_head != proc->gui_event_tail) {
        *ev_out = proc->gui_events[proc->gui_event_head];
        proc->gui_event_head = (proc->gui_event_head + 1) % MAX_GUI_EVENTS;
        return 1;
    }
    return 0;
}

static uint64_t gui_cmd_get_string_width(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *user_str = (const char *)args->arg2;
    if (!user_str) return 0;
    
    char kernel_str[256];
    int i = 0;
    while (i < 255 && user_str[i]) {
        kernel_str[i] = user_str[i];
        i++;
    }
    kernel_str[i] = 0;
    
    ttf_font_t *font = (proc->ui_window && ((Window*)proc->ui_window)->font) ? (ttf_font_t*)((Window*)proc->ui_window)->font : graphics_get_current_ttf();
    if (font) {
        return (uint64_t)font_manager_get_string_width_scaled(font, kernel_str, font->pixel_height);
    } else {
        return (uint64_t)i * 8; // Fallback bitmap width
    }
}

static uint64_t gui_cmd_get_font_height(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    ttf_font_t *font = (proc->ui_window && ((Window*)proc->ui_window)->font) ? (ttf_font_t*)((Window*)proc->ui_window)->font : graphics_get_current_ttf();
    if (font) {
        return (uint64_t)font_manager_get_font_height_scaled(font, font->pixel_height);
    }
    return 10;
}

static uint64_t gui_cmd_get_string_width_scaled(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *user_str = (const char *)args->arg2;
    uint32_t scale_bits = (uint32_t)args->arg3;
    float scale = *(float*)&scale_bits;

    if (!user_str) return 0;
    
    char kernel_str[256];
    int i = 0;
    while (i < 255 && user_str[i]) {
        kernel_str[i] = user_str[i];
        i++;
    }
    kernel_str[i] = 0;
    
    extern int graphics_get_string_width_scaled(const char *s, float scale);
    ttf_font_t *font = (proc->ui_window && ((Window*)proc->ui_window)->font) ? (ttf_font_t*)((Window*)proc->ui_window)->font : graphics_get_current_ttf();
    if (font) {
        return (uint64_t)font_manager_get_string_width_scaled(font, kernel_str, scale);
    } else {
        return (uint64_t)i * 8; // Fallback
    }
}

static uint64_t gui_cmd_get_font_height_scaled(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    uint32_t scale_bits = (uint32_t)args->arg2;
    float scale = *(float*)&scale_bits;
    ttf_font_t *font = (proc->ui_window && ((Window*)proc->ui_window)->font) ? (ttf_font_t*)((Window*)proc->ui_window)->font : graphics_get_current_ttf();
    if (font) {
        return (uint64_t)font_manager_get_font_height_scaled(font, scale);
    }
    return 10;
}

static uint64_t gui_cmd_window_set_resizable(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    process_t *proc = process_get_current();
    if (win && proc && proc->ui_window == win) {
        uint64_t flags = spinlock_acquire_irqsave(&win->lock);
        win->resizable = (args->arg3 != 0);
        spinlock_release_irqrestore(&win->lock, flags);
        
        extern void serial_write(const char *str);
        serial_write("[WM] Resizable: ");
        serial_write(args->arg3 ? "true\n" : "false\n");
    }
    return 0;
}

static uint64_t gui_cmd_window_set_title(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    const char *user_title = (const char *)args->arg3;
    process_t *proc = process_get_current();

    if (win && user_title && proc && proc->ui_window == win) {
        int title_len = 0;
        while (user_title[title_len] && title_len < 255) title_len++;
        
        char *kernel_title = kmalloc(title_len + 1);
        if (kernel_title) {
            for (int i = 0; i < title_len; i++) {
                kernel_title[i] = user_title[i];
            }
            kernel_title[title_len] = '\0';
            
            uint64_t flags = spinlock_acquire_irqsave(&win->lock);
            char *old_title = win->title;
            win->title = kernel_title;
            spinlock_release_irqrestore(&win->lock, flags);

            if (old_title && old_title != (char*)"Unknown") {
                kfree(old_title);
            }
            
            wm_mark_dirty(win->x, win->y - 20, win->w, 20); // Mark title bar dirty
            wm_refresh();
        }
    }
    return 0;
}

static uint64_t gui_cmd_set_font(const syscall_args_t *args) {
    Window *win = (Window *)args->arg2;
    const char *user_path = (const char *)args->arg3;
    if (win && user_path) {
        char kernel_path[256];
        int i = 0;
        while (i < 255 && user_path[i]) {
            kernel_path[i] = user_path[i];
            i++;
        }
        kernel_path[i] = 0;
        
        ttf_font_t *new_font = font_manager_load(kernel_path, 15.0f);
        if (new_font) {
            win->font = new_font;
        }
    }
    return 0;
}

static uint64_t gui_cmd_get_screen_size(const syscall_args_t *args) {
    uint64_t *out_w = (uint64_t *)args->arg2;
    uint64_t *out_h = (uint64_t *)args->arg3;
    if (out_w && out_h) {
        extern int get_screen_width(void);
        extern int get_screen_height(void);
        *out_w = (uint64_t)get_screen_width();
        *out_h = (uint64_t)get_screen_height();
    }
    return 0;
}

static uint64_t gui_cmd_get_screenbuffer(const syscall_args_t *args) {
    uint32_t *dest = (uint32_t *)args->arg2;
    if (dest) {
        extern void graphics_copy_screenbuffer(uint32_t *dest);
        graphics_copy_screenbuffer(dest);
    }
    return 0;
}

static uint64_t gui_cmd_show_notification(const syscall_args_t *args) {
    const char *user_msg = (const char *)args->arg2;
    if (user_msg) {
        char kernel_msg[256];
        int i = 0;
        while (i < 255 && user_msg[i]) {
            kernel_msg[i] = user_msg[i];
            i++;
        }
        kernel_msg[i] = 0;
        extern void wm_show_notification(const char *msg);
        wm_show_notification(kernel_msg);
    }
    return 0;
}

static uint64_t gui_cmd_get_datetime(const syscall_args_t *args) {
    uint64_t *out_arr = (uint64_t *)args->arg2;
    if (out_arr) {
        extern void rtc_get_datetime(int *year, int *month, int *day, int *hour, int *minute, int *second);
        int y, m, d, h, min, s;
        rtc_get_datetime(&y, &m, &d, &h, &min, &s);
        out_arr[0] = y;
        out_arr[1] = m;
        out_arr[2] = d;
        out_arr[3] = h;
        out_arr[4] = min;
        out_arr[5] = s;
    }
    return 0;
}

#define GUI_CMD_TABLE_SIZE 54
static const syscall_handler_fn gui_cmd_table[GUI_CMD_TABLE_SIZE] = {
    [GUI_CMD_WINDOW_CREATE]          = gui_cmd_window_create,
    [GUI_CMD_DRAW_RECT]              = gui_cmd_draw_rect,
    [GUI_CMD_DRAW_STRING]            = gui_cmd_draw_string,
    [GUI_CMD_MARK_DIRTY]             = gui_cmd_mark_dirty,
    [GUI_CMD_GET_EVENT]              = gui_cmd_get_event,
    [GUI_CMD_DRAW_ROUNDED_RECT_FILLED] = gui_cmd_draw_rounded_rect_filled,
    [GUI_CMD_DRAW_IMAGE]             = gui_cmd_draw_image,
    [GUI_CMD_GET_STRING_WIDTH]       = gui_cmd_get_string_width,
    [GUI_CMD_GET_FONT_HEIGHT]        = gui_cmd_get_font_height,
    [10]                             = gui_cmd_draw_string_bitmap,
    [11]                             = gui_cmd_draw_string_scaled,
    [12]                             = gui_cmd_get_string_width_scaled,
    [13]                             = gui_cmd_get_font_height_scaled,
    [GUI_CMD_WINDOW_SET_RESIZABLE]   = gui_cmd_window_set_resizable,
    [15]                             = gui_cmd_window_set_title,
    [16]                             = gui_cmd_set_font,
    [18]                             = gui_cmd_draw_string_scaled_sloped,
    [GUI_CMD_GET_SCREEN_SIZE]        = gui_cmd_get_screen_size,
    [GUI_CMD_GET_SCREENBUFFER]       = gui_cmd_get_screenbuffer,
    [GUI_CMD_SHOW_NOTIFICATION]      = gui_cmd_show_notification,
    [GUI_CMD_GET_DATETIME]           = gui_cmd_get_datetime,
};

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800
#define F_GETFL 3
#define F_SETFL 4

static int fs_alloc_fd_slot(process_t *proc, int start) {
    for (int i = start; i < MAX_PROCESS_FDS; i++) {
        if (!proc->fds[i]) return i;
    }
    return -1;
}

static int fs_mode_to_flags(const char *mode) {
    if (!mode || !mode[0]) return O_RDONLY;
    if (mode[0] == 'r') {
        return (mode[1] == '+') ? O_RDWR : O_RDONLY;
    }
    if (mode[0] == 'a') {
        return (mode[1] == '+') ? (O_RDWR | O_APPEND) : (O_WRONLY | O_APPEND);
    }
    if (mode[0] == 'w') {
        return (mode[1] == '+') ? O_RDWR : O_WRONLY;
    }
    return O_RDONLY;
}

static uint64_t fs_cmd_open(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *path = (const char *)args->arg2;
    const char *mode = (const char *)args->arg3;
    if (!path || !mode) return -1;
    
    // vfs_open now handles normalization internally with process_get_current()
    // but let's be explicit if we can.
    vfs_file_t *vf = vfs_open(path, mode);
    if (!vf) return -1;

    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)kmalloc(sizeof(process_fd_file_ref_t));
    if (!ref) {
        vfs_close(vf);
        return -1;
    }
    ref->file = vf;
    ref->refs = 1;

    for (int i = 0; i < MAX_PROCESS_FDS; i++) {
        if (proc->fds[i] == NULL) {
            proc->fds[i] = ref;
            proc->fd_kind[i] = PROC_FD_KIND_FILE;
            proc->fd_flags[i] = fs_mode_to_flags(mode);
            return (uint64_t)i;
        }
    }

    kfree(ref);
    vfs_close(vf);
    return -1;
}

static uint64_t fs_cmd_read(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    void *buf = (void *)args->arg3;
    uint32_t len = (uint32_t)args->arg4;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;

    if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
        if (!ref || !ref->file) return -1;
        return (uint64_t)vfs_read(ref->file, buf, (int)len);
    }

    if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        if (!pipe || !buf) return -1;
        uint8_t *out = (uint8_t *)buf;
        uint32_t n = 0;
        while (n < len) {
            if (pipe->count == 0) {
                if (pipe->writers == 0) break;
                if (proc->fd_flags[fd] & O_NONBLOCK) {
                    if (n == 0) return (uint64_t)-1;
                    break;
                }
                break;
            }
            out[n++] = pipe->data[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % sizeof(pipe->data);
            pipe->count--;
        }
        return n;
    }

    return -1;
}

static uint64_t fs_cmd_write(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    const void *buf = (const void *)args->arg3;
    uint32_t len = (uint32_t)args->arg4;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;

    if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
        if (!ref || !ref->file) return -1;
        return (uint64_t)vfs_write(ref->file, buf, (int)len);
    }

    if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        if (!pipe || !buf) return -1;
        if (pipe->readers <= 0) return (uint64_t)-1;
        const uint8_t *in = (const uint8_t *)buf;
        uint32_t n = 0;
        while (n < len) {
            if (pipe->count == sizeof(pipe->data)) {
                if (proc->fd_flags[fd] & O_NONBLOCK) {
                    if (n == 0) return (uint64_t)-1;
                    break;
                }
                break;
            }
            pipe->data[pipe->write_pos] = in[n++];
            pipe->write_pos = (pipe->write_pos + 1) % sizeof(pipe->data);
            pipe->count++;
        }
        return n;
    }

    return -1;
}

static uint64_t fs_cmd_close(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;

    if (proc->fd_kind[fd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
        if (ref) {
            ref->refs--;
            if (ref->refs <= 0) {
                if (ref->file) vfs_close(ref->file);
                kfree(ref);
            }
        }
    } else if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ || proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        if (pipe) {
            if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ) pipe->readers--;
            else pipe->writers--;
            if (pipe->readers <= 0 && pipe->writers <= 0) {
                kfree(pipe);
            }
        }
    }

    proc->fds[fd] = NULL;
    proc->fd_kind[fd] = PROC_FD_KIND_NONE;
    proc->fd_flags[fd] = 0;
    return 0;
}

static uint64_t fs_cmd_seek(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    int offset = (int)args->arg3;
    int whence = (int)args->arg4; // 0=SET, 1=CUR, 2=END
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
    if (proc->fd_kind[fd] != PROC_FD_KIND_FILE) return -1;
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (!ref || !ref->file) return -1;
    return (uint64_t)vfs_seek(ref->file, offset, whence);
}

static uint64_t fs_cmd_tell(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
    if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ || proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        return pipe ? pipe->count : 0;
    }
    if (proc->fd_kind[fd] != PROC_FD_KIND_FILE) return -1;
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (!ref || !ref->file) return -1;
    return (uint64_t)vfs_file_position(ref->file);
}

static uint64_t fs_cmd_size(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;
    if (proc->fd_kind[fd] == PROC_FD_KIND_PIPE_READ || proc->fd_kind[fd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[fd];
        return pipe ? pipe->count : 0;
    }
    if (proc->fd_kind[fd] != PROC_FD_KIND_FILE) return -1;
    process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
    if (!ref || !ref->file) return -1;
    return (uint64_t)vfs_file_size(ref->file);
}

static uint64_t fs_cmd_dup(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int oldfd = (int)args->arg2;
    if (oldfd < 0 || oldfd >= MAX_PROCESS_FDS || !proc->fds[oldfd]) return -1;

    int newfd = fs_alloc_fd_slot(proc, 0);
    if (newfd < 0) return -1;

    proc->fds[newfd] = proc->fds[oldfd];
    proc->fd_kind[newfd] = proc->fd_kind[oldfd];
    proc->fd_flags[newfd] = proc->fd_flags[oldfd];

    if (proc->fd_kind[oldfd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[oldfd];
        if (ref) ref->refs++;
    } else if (proc->fd_kind[oldfd] == PROC_FD_KIND_PIPE_READ) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[oldfd];
        if (pipe) pipe->readers++;
    } else if (proc->fd_kind[oldfd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[oldfd];
        if (pipe) pipe->writers++;
    }

    return (uint64_t)newfd;
}

static uint64_t fs_cmd_dup2(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int oldfd = (int)args->arg2;
    int newfd = (int)args->arg3;
    if (oldfd < 0 || oldfd >= MAX_PROCESS_FDS || !proc->fds[oldfd]) return -1;
    if (newfd < 0 || newfd >= MAX_PROCESS_FDS) return -1;
    if (oldfd == newfd) return (uint64_t)newfd;

    if (proc->fds[newfd]) {
        syscall_args_t close_args = *args;
        close_args.arg2 = (uint64_t)newfd;
        if (fs_cmd_close(&close_args) != 0) return -1;
    }

    proc->fds[newfd] = proc->fds[oldfd];
    proc->fd_kind[newfd] = proc->fd_kind[oldfd];
    proc->fd_flags[newfd] = proc->fd_flags[oldfd];

    if (proc->fd_kind[oldfd] == PROC_FD_KIND_FILE) {
        process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[oldfd];
        if (ref) ref->refs++;
    } else if (proc->fd_kind[oldfd] == PROC_FD_KIND_PIPE_READ) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[oldfd];
        if (pipe) pipe->readers++;
    } else if (proc->fd_kind[oldfd] == PROC_FD_KIND_PIPE_WRITE) {
        process_fd_pipe_t *pipe = (process_fd_pipe_t *)proc->fds[oldfd];
        if (pipe) pipe->writers++;
    }

    return (uint64_t)newfd;
}

static uint64_t fs_cmd_pipe(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int *pipefd = (int *)args->arg2;
    if (!pipefd) return -1;

    int rfd = fs_alloc_fd_slot(proc, 0);
    if (rfd < 0) return -1;
    int wfd = fs_alloc_fd_slot(proc, rfd + 1);
    if (wfd < 0) return -1;

    process_fd_pipe_t *pipe = (process_fd_pipe_t *)kmalloc(sizeof(process_fd_pipe_t));
    if (!pipe) return -1;
    mem_memset(pipe, 0, sizeof(*pipe));
    pipe->readers = 1;
    pipe->writers = 1;

    proc->fds[rfd] = pipe;
    proc->fd_kind[rfd] = PROC_FD_KIND_PIPE_READ;
    proc->fd_flags[rfd] = O_RDONLY;

    proc->fds[wfd] = pipe;
    proc->fd_kind[wfd] = PROC_FD_KIND_PIPE_WRITE;
    proc->fd_flags[wfd] = O_WRONLY;

    pipefd[0] = rfd;
    pipefd[1] = wfd;
    return 0;
}

static uint64_t fs_cmd_fcntl(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int fd = (int)args->arg2;
    int cmd = (int)args->arg3;
    int val = (int)args->arg4;
    if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd]) return -1;

    if (cmd == F_GETFL) {
        return (uint64_t)proc->fd_flags[fd];
    }
    if (cmd == F_SETFL) {
        proc->fd_flags[fd] = (proc->fd_flags[fd] & ~(O_APPEND | O_NONBLOCK)) | (val & (O_APPEND | O_NONBLOCK));
        return 0;
    }
    return -1;
}

static uint64_t fs_cmd_list(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *path = (const char *)args->arg2;
    FAT32_FileInfo *u_entries = (FAT32_FileInfo *)args->arg3;
    int max_entries = (int)args->arg4;
    if (!path || !u_entries) return -1;
    
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(proc->cwd, path, normalized);
    
    // Safety cap for kernel allocation
    if (max_entries > 256) max_entries = 256;
    if (max_entries <= 0) return 0;
    
    vfs_dirent_t *v_entries = (vfs_dirent_t *)kmalloc(sizeof(vfs_dirent_t) * max_entries);
    if (!v_entries) return -1;
    
    int count = vfs_list_directory(normalized, v_entries, max_entries);
    if (count > 0) {
        for (int i = 0; i < count; i++) {
            // Direct copy as layouts are now aligned
            strcpy(u_entries[i].name, v_entries[i].name);
            u_entries[i].size = v_entries[i].size;
            u_entries[i].is_directory = v_entries[i].is_directory;
            u_entries[i].start_cluster = v_entries[i].start_cluster;
            u_entries[i].write_date = v_entries[i].write_date;
            u_entries[i].write_time = v_entries[i].write_time;
        }
    }
    kfree(v_entries);
    return (uint64_t)count;
}

static uint64_t fs_cmd_delete(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *path = (const char *)args->arg2;
    if (!path) return -1;
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(proc->cwd, path, normalized);
    return vfs_delete(normalized) ? 0 : -1;
}

static uint64_t fs_cmd_get_info(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *path = (const char *)args->arg2;
    FAT32_FileInfo *u_info = (FAT32_FileInfo *)args->arg3;
    if (!path || !u_info) return -1;
    
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(proc->cwd, path, normalized);
    
    vfs_dirent_t v_info;
    int res = vfs_get_info(normalized, &v_info);
    if (res == 0) {
        strcpy(u_info->name, v_info.name);
        u_info->size = v_info.size;
        u_info->is_directory = v_info.is_directory;
        u_info->start_cluster = v_info.start_cluster;
        u_info->write_date = v_info.write_date;
        u_info->write_time = v_info.write_time;
    }
    return (uint64_t)res;
}

static uint64_t fs_cmd_mkdir(const syscall_args_t *args) {
    const char *path = (const char *)args->arg2;
    if (!path) return -1;
    return vfs_mkdir(path) ? 0 : -1;
}

static uint64_t fs_cmd_exists(const syscall_args_t *args) {
    const char *path = (const char *)args->arg2;
    if (!path) return 0;
    return vfs_exists(path) ? 1 : 0;
}

static uint64_t fs_cmd_getcwd(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    char *buf = (char *)args->arg2;
    int size = (int)args->arg3;
    if (!buf || size <= 0) return -1;
    int len = (int)strlen(proc->cwd);
    if (len >= size) return -1;
    strcpy(buf, proc->cwd);
    return (uint64_t)len;
}

static uint64_t fs_cmd_chdir(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *path = (const char *)args->arg2;
    if (!path) return -1;
    char normalized[VFS_MAX_PATH];
    vfs_normalize_path(proc->cwd, path, normalized);
    if (vfs_is_directory(normalized)) {
        strcpy(proc->cwd, normalized);
        return 0;
    }
    return -1;
}
#define FS_CMD_TABLE_SIZE 19
static const syscall_handler_fn fs_cmd_table[FS_CMD_TABLE_SIZE] = {
    [FS_CMD_OPEN]     = fs_cmd_open,      // 1
    [FS_CMD_READ]     = fs_cmd_read,      // 2
    [FS_CMD_WRITE]    = fs_cmd_write,     // 3
    [FS_CMD_CLOSE]    = fs_cmd_close,     // 4
    [FS_CMD_SEEK]     = fs_cmd_seek,      // 5
    [FS_CMD_TELL]     = fs_cmd_tell,      // 6
    [FS_CMD_LIST]     = fs_cmd_list,      // 7
    [FS_CMD_DELETE]   = fs_cmd_delete,    // 8
    [FS_CMD_SIZE]     = fs_cmd_size,      // 9
    [FS_CMD_MKDIR]    = fs_cmd_mkdir,     // 10
    [FS_CMD_EXISTS]   = fs_cmd_exists,    // 11
    [FS_CMD_GETCWD]   = fs_cmd_getcwd,    // 12
    [FS_CMD_CHDIR]    = fs_cmd_chdir,     // 13
    [FS_CMD_GET_INFO] = fs_cmd_get_info,  // 14
    [FS_CMD_DUP]      = fs_cmd_dup,       // 15
    [FS_CMD_DUP2]     = fs_cmd_dup2,      // 16
    [FS_CMD_PIPE]     = fs_cmd_pipe,      // 17
    [FS_CMD_FCNTL]    = fs_cmd_fcntl,     // 18
};

static uint64_t sys_cmd_set_bg_color(const syscall_args_t *args) {
    uint32_t color = (uint32_t)args->arg2;
    extern void graphics_set_bg_color(uint32_t color);
    graphics_set_bg_color(color);
    return 0;
}

static uint64_t sys_cmd_set_bg_pattern(const syscall_args_t *args) {
    uint32_t *user_pat = (uint32_t *)args->arg2;
    if (!user_pat) {
        graphics_set_bg_pattern(NULL);
    } else {
        static uint32_t global_bg_pattern[128*128];
        for (int i=0; i<128*128; i++) {
            global_bg_pattern[i] = user_pat[i];
        }
        graphics_set_bg_pattern(global_bg_pattern);
    }
    extern void wm_refresh(void);
    wm_refresh();
    return 0;
}

static uint64_t sys_cmd_set_wallpaper(const syscall_args_t *args) {
    (void)args;
    return -1;
}

static uint64_t sys_cmd_set_desktop_prop(const syscall_args_t *args) {
    int prop = (int)args->arg2;
    int val = (int)args->arg3;
    extern _Bool desktop_snap_to_grid;
    extern _Bool desktop_auto_align;
    extern int desktop_max_rows_per_col;
    extern int desktop_max_cols;
    if (prop == 1) desktop_snap_to_grid = val;
    if (prop == 2) desktop_auto_align = val;
    if (prop == 3) desktop_max_rows_per_col = val;
    if (prop == 4) desktop_max_cols = val;
    extern void wm_refresh_desktop(void);
    wm_refresh_desktop();
    return 0;
}

static uint64_t sys_cmd_set_mouse_speed(const syscall_args_t *args) {
    extern int mouse_speed;
    mouse_speed = (int)args->arg2;
    return 0;
}

static uint64_t sys_cmd_set_mouse_cursor_scale(const syscall_args_t *args) {
    wm_set_cursor_scale_tenths((int)args->arg2);
    return 0;
}

static uint64_t sys_cmd_network_init(const syscall_args_t *args) {
    (void)args;
    extern int network_init(void);
    return network_init();
}

static uint64_t sys_cmd_get_desktop_prop(const syscall_args_t *args) {
    int prop = (int)args->arg2;
    extern _Bool desktop_snap_to_grid;
    extern _Bool desktop_auto_align;
    extern int desktop_max_rows_per_col;
    extern int desktop_max_cols;
    if (prop == 1) return desktop_snap_to_grid;
    if (prop == 2) return desktop_auto_align;
    if (prop == 3) return desktop_max_rows_per_col;
    if (prop == 4) return desktop_max_cols;
    return 0;
}

static uint64_t sys_cmd_get_mouse_speed(const syscall_args_t *args) {
    (void)args;
    extern int mouse_speed;
    return mouse_speed;
}

static uint64_t sys_cmd_get_mouse_cursor_scale(const syscall_args_t *args) {
    (void)args;
    return (uint64_t)wm_get_cursor_scale_tenths();
}

static uint64_t sys_cmd_get_wallpaper_thumb(const syscall_args_t *args) {
    (void)args;
    return -1;
}

static uint64_t sys_cmd_clear_screen(const syscall_args_t *args) {
    (void)args;
    extern void cmd_screen_clear(void);
    cmd_screen_clear();
    return 0;
}

static uint64_t sys_cmd_rtc_get(const syscall_args_t *args) {
    int *dt = (int *)args->arg2;
    if (!dt) return -1;
    extern void rtc_get_datetime(int *y, int *m, int *d, int *h, int *min, int *s);
    rtc_get_datetime(&dt[0], &dt[1], &dt[2], &dt[3], &dt[4], &dt[5]);
    return 0;
}

static uint64_t sys_cmd_reboot(const syscall_args_t *args) {
    (void)args;
    k_reboot();
    return 0;
}

static uint64_t sys_cmd_shutdown(const syscall_args_t *args) {
    (void)args;
    k_shutdown();
    return 0;
}

static uint64_t sys_cmd_beep(const syscall_args_t *args) {
    int freq = (int)args->arg2;
    int ms = (int)args->arg3;
    extern void k_beep(int freq, int ms);
    k_beep(freq, ms);
    return 0;
}

static uint64_t sys_cmd_get_mem_info(const syscall_args_t *args) {
    uint64_t *out = (uint64_t *)args->arg2;
    if (!out) return -1;
    MemStats stats = memory_get_stats();
    out[0] = (uint64_t)stats.total_memory;
    out[1] = (uint64_t)stats.used_memory;
    return 0;
}

static uint64_t sys_cmd_get_ticks(const syscall_args_t *args) {
    (void)args;
    extern uint32_t wm_get_ticks(void);
    return (uint64_t)wm_get_ticks();
}

static uint64_t sys_cmd_pci_list(const syscall_args_t *args) {
    typedef struct {
        uint16_t vendor;
        uint16_t device;
        uint8_t class_code;
        uint8_t subclass;
    } pci_info_t;
    pci_info_t *info = (pci_info_t *)args->arg2;
    int idx = (int)args->arg3;
    if (!info) {
        pci_device_t pci_devs[128];
        return pci_enumerate_devices(pci_devs, 128);
    }
    pci_device_t pci_devs[128];
    int count = pci_enumerate_devices(pci_devs, 128);
    if (idx >= 0 && idx < count) {
        info->vendor = pci_devs[idx].vendor_id;
        info->device = pci_devs[idx].device_id;
        info->class_code = pci_devs[idx].class_code;
        info->subclass = pci_devs[idx].subclass;
        return 0;
    }
    return -1;
}

static uint64_t sys_cmd_network_dhcp(const syscall_args_t *args) {
    (void)args;
    return network_dhcp_acquire();
}

static uint64_t sys_cmd_network_get_mac(const syscall_args_t *args) {
    mac_address_t *mac = (mac_address_t *)args->arg2;
    if (!mac) return -1;
    return network_get_mac_address(mac);
}

static uint64_t sys_cmd_network_get_ip(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    if (!ip) return -1;
    return network_get_ipv4_address(ip);
}

static uint64_t sys_cmd_network_set_ip(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    if (!ip) return -1;
    return network_set_ipv4_address(ip);
}

static uint64_t sys_cmd_udp_send(const syscall_args_t *args) {
    ipv4_address_t *dest_ip = (ipv4_address_t *)args->arg2;
    uint32_t ports = (uint32_t)args->arg3;  
    uint16_t dest_port = ports & 0xFFFF;
    uint16_t src_port = (ports >> 16) & 0xFFFF;
    const void *data = (const void *)args->arg4;
    size_t data_len = (size_t)args->arg5;
    if (!dest_ip || !data) return -1;
    return udp_send_packet(dest_ip, dest_port, src_port, data, data_len);
}

static uint64_t sys_cmd_network_get_stats(const syscall_args_t *args) {
    int stat_type = (int)args->arg2;
    switch (stat_type) {
        case 0: return network_get_frames_received();
        case 1: return network_get_udp_packets_received();
        case 2: return network_get_frames_sent();
        case 3: return network_get_e1000_receive_calls();
        case 4: return network_get_e1000_receive_empty();
        case 5: return network_get_process_calls();
        default: return -1;
    }
}

static uint64_t sys_cmd_network_get_gateway(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    if (!ip) return -1;
    return network_get_gateway_ip(ip);
}

static uint64_t sys_cmd_network_get_dns(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    if (!ip) return -1;
    return network_get_dns_ip(ip);
}

static uint64_t sys_cmd_icmp_ping(const syscall_args_t *args) {
    ipv4_address_t *dest_ip = (ipv4_address_t *)args->arg2;
    if (!dest_ip) return -1;
    extern int network_icmp_single_ping(ipv4_address_t *dest);
    return (uint64_t)network_icmp_single_ping(dest_ip);
}

static uint64_t sys_cmd_network_is_init(const syscall_args_t *args) {
    (void)args;
    return network_is_initialized() ? 1 : 0;
}

static uint64_t sys_cmd_get_shell_config(const syscall_args_t *args) {
    const char *key = (const char *)args->arg2;
    if (!key) return -1;
    return cmd_get_config_value(key);
}

static uint64_t sys_cmd_set_text_color(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    uint32_t color = (uint32_t)args->arg2;
    if (proc->is_terminal_proc && proc->tty_id >= 0) {
        char seq[32];
        int pos = 0;
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;

        seq[pos++] = 0x1B;
        seq[pos++] = '[';
        seq[pos++] = '3';
        seq[pos++] = '8';
        seq[pos++] = ';';
        seq[pos++] = '2';
        seq[pos++] = ';';

        char num[8];
        itoa(r, num);
        for (int i = 0; num[i] && pos < (int)sizeof(seq) - 1; i++) seq[pos++] = num[i];
        seq[pos++] = ';';
        itoa(g, num);
        for (int i = 0; num[i] && pos < (int)sizeof(seq) - 1; i++) seq[pos++] = num[i];
        seq[pos++] = ';';
        itoa(b, num);
        for (int i = 0; num[i] && pos < (int)sizeof(seq) - 1; i++) seq[pos++] = num[i];
        seq[pos++] = 'm';

        tty_write_output(proc->tty_id, seq, (size_t)pos);
        return 0;
    }
    cmd_set_current_color(color);
    return 0;
}

static uint64_t sys_cmd_network_has_ip(const syscall_args_t *args) {
    (void)args;
    return network_has_ip() ? 1 : 0;
}

static uint64_t sys_cmd_set_wallpaper_path(const syscall_args_t *args) {
    const char *user_path = (const char *)args->arg2;
    if (!user_path) return -1;
    
    // Copy path safely to kernel buffer
    char kernel_path[256];
    int i = 0;
    while (i < 255 && user_path[i]) {
        kernel_path[i] = user_path[i];
        i++;
    }
    kernel_path[i] = 0;
    
    extern void wallpaper_request_set_from_file(const char *path);
    wallpaper_request_set_from_file(kernel_path);
    return 0;
}

static uint64_t sys_cmd_rtc_set(const syscall_args_t *args) {
    int *dt = (int *)args->arg2;
    if (!dt) return -1;
    extern void rtc_set_datetime(int y, int m, int d, int h, int min, int s);
    rtc_set_datetime(dt[0], dt[1], dt[2], dt[3], dt[4], dt[5]);
    return 0;
}

static uint64_t sys_cmd_tcp_connect(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    uint16_t port = (uint16_t)args->arg3;
    extern int network_tcp_connect(const ipv4_address_t *ip, uint16_t port);
    return (uint64_t)network_tcp_connect(ip, port);
}

static uint64_t sys_cmd_tcp_send(const syscall_args_t *args) {
    const void *data = (const void *)args->arg2;
    size_t len = (size_t)args->arg3;
    extern int network_tcp_send(const void *data, size_t len);
    return (uint64_t)network_tcp_send(data, len);
}

static uint64_t sys_cmd_tcp_recv(const syscall_args_t *args) {
    void *buf = (void *)args->arg2;
    size_t max_len = (size_t)args->arg3;
    extern int network_tcp_recv(void *buf, size_t max_len);
    return (uint64_t)network_tcp_recv(buf, max_len);
}

static uint64_t sys_cmd_tcp_close(const syscall_args_t *args) {
    (void)args;
    extern int network_tcp_close(void);
    return (uint64_t)network_tcp_close();
}

static uint64_t sys_cmd_dns_lookup(const syscall_args_t *args) {
    const char *user_name = (const char *)args->arg2;
    ipv4_address_t *out_ip = (ipv4_address_t *)args->arg3;
    char name_buf[256];
    int i = 0;
    while (i < 255 && user_name[i]) { name_buf[i] = user_name[i]; i++; }
    name_buf[i] = 0;
    extern int network_dns_lookup(const char *name, ipv4_address_t *out_ip);
    return (uint64_t)network_dns_lookup(name_buf, out_ip);
}

static uint64_t sys_cmd_set_dns(const syscall_args_t *args) {
    ipv4_address_t *ip = (ipv4_address_t *)args->arg2;
    extern int network_set_dns_server(const ipv4_address_t *ip);
    return (uint64_t)network_set_dns_server(ip);
}

static uint64_t sys_cmd_net_unlock(const syscall_args_t *args) {
    (void)args;
    extern void network_force_unlock(void);
    network_force_unlock();
    return 0;
}

static uint64_t sys_cmd_set_font(const syscall_args_t *args) {
    const char *user_path = (const char *)args->arg2;
    if (!user_path) return -1;
    // Copy font path from userland
    char path[128];
    int i;
    for (i = 0; i < 127 && user_path[i]; i++) {
        path[i] = user_path[i];
    }
    path[i] = 0;
    graphics_set_font(path);
    return 0;
}

static uint64_t sys_cmd_set_raw_mode(const syscall_args_t *args) {
    extern void cmd_set_raw_mode(bool enabled);
    cmd_set_raw_mode((bool)args->arg2);
    return 0;
}

static uint64_t sys_cmd_tcp_recv_nb(const syscall_args_t *args) {
    void *buf = (void *)args->arg2;
    size_t max_len = (size_t)args->arg3;
    extern int network_tcp_recv_nb(void *buf, size_t max_len);
    return (uint64_t)network_tcp_recv_nb(buf, max_len);
}

static uint64_t sys_cmd_set_resolution(const syscall_args_t *args) {
    uint16_t req_w = (uint16_t)args->arg2;
    uint16_t req_h = (uint16_t)args->arg3;
    uint16_t req_bpp = (uint16_t)args->arg4;
    int req_color_mode = (int)args->arg5;
    
    extern bool vga_set_mode(uint16_t width, uint16_t height, uint16_t bpp, void **out_framebuffer);
    extern void graphics_update_resolution(int width, int height, int bpp, void* fb_addr, int color_mode);
    extern void wm_refresh(void);
    extern void vga_set_palette_grayscale(void);
    extern void vga_set_palette_standard(void);
    
    void *new_fb = NULL;
    if (vga_set_mode(req_w, req_h, req_bpp, &new_fb)) {
        if (req_color_mode == 1 || req_color_mode == 2) {
            vga_set_palette_grayscale();
        } else if (req_bpp <= 8) {
            vga_set_palette_standard();
        }
        graphics_update_resolution(req_w, req_h, req_bpp, new_fb, req_color_mode);
        wm_refresh();
        return 0;
    }
    return -1;
}

static uint64_t sys_cmd_network_get_nic_name(const syscall_args_t *args) {
    char *user_buf = (char *)args->arg2;
    if (!user_buf) return -1;
    char name_buf[64];
    extern int network_get_nic_name(char *name_out);
    if (network_get_nic_name(name_buf) == 0) {
        extern void mem_memcpy(void *dest, const void *src, size_t len);
        size_t len = 0;
        while (name_buf[len] && len < 63) len++;
        name_buf[len] = 0;
        mem_memcpy(user_buf, name_buf, len + 1);
        return 0;
    }
    return -1;
}

static uint64_t sys_cmd_parallel_run(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    void (*user_fn)(void*) = (void (*)(void*))args->arg2;
    void **user_args = (void **)args->arg3;
    int count = (int)args->arg4;

    if (count <= 0) return 0;
    if (count > 64) count = 64; 

    volatile int completion_counter = count;
    uint64_t current_pml4 = proc->pml4_phys;

    smp_user_task_t tasks[64];
    
    for (int i = 0; i < count; i++) {
        tasks[i].fn = user_fn;
        tasks[i].arg = user_args[i];
        tasks[i].pml4_phys = current_pml4;
        tasks[i].completion_counter = &completion_counter;
        
        extern void work_queue_submit(void (*fn)(void*), void *arg);
        work_queue_submit(smp_user_wrapper, &tasks[i]);
    }

    extern bool work_queue_drain_one(void);
    while (completion_counter > 0) {
        if (!work_queue_drain_one()) {
            asm volatile("pause");
        }
    }
    return 0;
}

static uint64_t sys_cmd_tty_create(const syscall_args_t *args) {
    (void)args;
    return tty_create();
}

static uint64_t sys_cmd_tty_read_out(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    char *buf = (char *)args->arg3;
    size_t len = (size_t)args->arg4;
    if (!buf || len == 0) return 0;
    return tty_read_output(tty_id, buf, len);
}

static uint64_t sys_cmd_tty_write_in(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    const char *buf = (const char *)args->arg3;
    size_t len = (size_t)args->arg4;
    if (!buf || len == 0) return 0;
    return tty_write_input(tty_id, buf, len);
}

static uint64_t sys_cmd_tty_read_in(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    char *buf = (char *)args->arg2;
    size_t len = (size_t)args->arg3;
    if (!buf || len == 0) return 0;
    if (proc->tty_id < 0) return 0;
    return tty_read_input(proc->tty_id, buf, len);
}

static uint64_t sys_cmd_spawn_process(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    const char *user_path = (const char *)args->arg2;
    const char *user_args = (const char *)args->arg3;
    uint64_t flags = args->arg4;
    int tty_id = (int)args->arg5;

    if (!user_path) return -1;

    char path_buf[256];
    int pi = 0;
    while (pi < 255 && user_path[pi]) {
        path_buf[pi] = user_path[pi];
        pi++;
    }
    path_buf[pi] = 0;

    char args_buf[512];
    const char *args_ptr = NULL;
    if (user_args) {
        int ai = 0;
        while (ai < 511 && user_args[ai]) {
            args_buf[ai] = user_args[ai];
            ai++;
        }
        args_buf[ai] = 0;
        args_ptr = args_buf;
    }

    bool terminal_proc = (flags & SPAWN_FLAG_TERMINAL) != 0;
    int effective_tty = -1;
    if (flags & SPAWN_FLAG_TTY_ID) effective_tty = tty_id;
    else if (flags & SPAWN_FLAG_INHERIT_TTY) effective_tty = proc ? proc->tty_id : -1;

    process_t *child = process_create_elf(path_buf, args_ptr, terminal_proc, effective_tty);
    if (!child) return -1;
    return (uint64_t)child->pid;
}

typedef struct {
    uint64_t sa_handler;
    uint64_t sa_mask;
    int sa_flags;
} k_sigaction_t;

#define SA_RESETHAND 0x80000000
#define SIGKILL_NUM 9

static uint64_t sys_cmd_exec_process(const syscall_args_t *args) {
    const char *user_path = (const char *)args->arg2;
    const char *user_args = (const char *)args->arg3;
    if (!user_path) return -1;

    char path_buf[256];
    int pi = 0;
    while (pi < 255 && user_path[pi]) {
        path_buf[pi] = user_path[pi];
        pi++;
    }
    path_buf[pi] = 0;

    char args_buf[512];
    const char *args_ptr = NULL;
    if (user_args) {
        int ai = 0;
        while (ai < 511 && user_args[ai]) {
            args_buf[ai] = user_args[ai];
            ai++;
        }
        args_buf[ai] = 0;
        args_ptr = args_buf;
    }

    return process_exec_replace_current(args->regs, path_buf, args_ptr);
}

static uint64_t sys_cmd_waitpid(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int pid = (int)args->arg2;
    int *status = (int *)args->arg3;
    int options = (int)args->arg4;
    if (!proc) return -1;

    int st = 0;
    int res = process_waitpid(proc->pid, pid, options, &st);
    if (res == -2) {
        if (options & 1) return 0; // WNOHANG
        return (uint64_t)-2;
    }
    if (res < 0) return (uint64_t)-1;
    if (status) *status = st;
    return (uint64_t)res;
}

static uint64_t sys_cmd_kill_signal(const syscall_args_t *args) {
    int pid = (int)args->arg2;
    int sig = (int)args->arg3;
    process_t *target;
    if (pid == -1) {
        target = process_get_current();
    } else {
        target = process_get_by_pid((uint32_t)pid);
    }
    if (!target) return -1;
    if (sig == 0) return 0;
    if (sig <= 0 || sig >= MAX_SIGNALS) return -1;

    if (sig == 9) {
        process_terminate_with_status(target, 128 + sig);
        return 0;
    }

    target->signal_pending |= (1ULL << (uint32_t)sig);
    return 0;
}

static uint64_t sys_cmd_sigaction(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int sig = (int)args->arg2;
    const k_sigaction_t *act = (const k_sigaction_t *)args->arg3;
    k_sigaction_t *oldact = (k_sigaction_t *)args->arg4;
    if (!proc || sig <= 0 || sig >= MAX_SIGNALS) return -1;

    if (oldact) {
        oldact->sa_handler = proc->signal_handlers[sig];
        oldact->sa_mask = proc->signal_action_mask[sig];
        oldact->sa_flags = proc->signal_action_flags[sig];
    }
    if (act) {
        if (sig == SIGKILL_NUM && act->sa_handler != 0) {
            return -1;
        }
        proc->signal_handlers[sig] = act->sa_handler;
        proc->signal_action_mask[sig] = act->sa_mask;
        proc->signal_action_flags[sig] = act->sa_flags;
    }
    return 0;
}

static uint64_t sys_cmd_sigprocmask(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    int how = (int)args->arg2;
    const uint64_t *set = (const uint64_t *)args->arg3;
    uint64_t *oldset = (uint64_t *)args->arg4;
    if (!proc) return -1;

    if (oldset) {
        *oldset = proc->signal_mask;
    }
    if (!set) return 0;

    if (how == 0) {
        proc->signal_mask |= *set;
    } else if (how == 1) {
        proc->signal_mask &= ~(*set);
    } else if (how == 2) {
        proc->signal_mask = *set;
    } else {
        return -1;
    }
    proc->signal_mask &= ~(1ULL << SIGKILL_NUM);

    return 0;
}

static uint64_t sys_cmd_sigpending(const syscall_args_t *args) {
    process_t *proc = process_get_current();
    uint64_t *set = (uint64_t *)args->arg2;
    if (!proc || !set) return -1;
    *set = proc->signal_pending;
    return 0;
}

static uint64_t sys_cmd_tty_set_fg(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    int pid = (int)args->arg3;
    return tty_set_foreground(tty_id, pid);
}

static uint64_t sys_cmd_tty_get_fg(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    return tty_get_foreground(tty_id);
}

static uint64_t sys_cmd_tty_kill_fg(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    int pid = tty_get_foreground(tty_id);
    if (pid <= 0) return 0;
    process_t *target = process_get_by_pid((uint32_t)pid);
    if (target) process_terminate(target);
    tty_set_foreground(tty_id, 0);
    return 0;
}

static uint64_t sys_cmd_tty_kill_all(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    process_kill_by_tty(tty_id);
    tty_set_foreground(tty_id, 0);
    return 0;
}

static uint64_t sys_cmd_tty_destroy(const syscall_args_t *args) {
    int tty_id = (int)args->arg2;
    return tty_destroy(tty_id);
}

static uint64_t sys_cmd_get_elf_metadata(const syscall_args_t *args) {
    const char *path = (const char *)args->arg2;
    boredos_app_metadata_t *out = (boredos_app_metadata_t *)args->arg3;
    if (!path || !out) return 0;
    return app_metadata_read(path, out) ? 1 : 0;
}
static uint64_t sys_cmd_get_elf_primary_image(const syscall_args_t *args) {
    const char *path     = (const char *)args->arg2;
    char       *out_path = (char *)args->arg3;
    size_t      out_size = (size_t)args->arg4;
    if (!path || !out_path || !out_size) return 0;
    return app_metadata_get_primary_image(path, out_path, out_size) ? 1 : 0;
}

static uint64_t sys_cmd_set_keyboard_layout(const syscall_args_t *args) {
    keymap_set_current((keymap_id_t)args->arg2);
    return 0;
}

static uint64_t sys_cmd_get_keyboard_layout(const syscall_args_t *args) {
    (void)args;
    return (uint64_t)keymap_get_current();
}

typedef struct {
    char     devname[16];
    char     label[32];
    uint32_t type;
    uint32_t total_sectors;
    bool     is_partition;
    bool     is_fat32;
    bool     is_esp;
    uint32_t lba_offset;
} k_disk_info_t;

typedef struct {
    uint32_t lba_start;
    uint32_t sector_count;
    uint8_t  part_type;
    uint8_t  flags;
    char     label[36];
} k_partition_spec_t;

static void disk_k_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int disk_k_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint64_t sys_cmd_disk_get_count(const syscall_args_t *args) {
    (void)args;
    return (uint64_t)disk_get_count();
}

static uint64_t sys_cmd_disk_get_info(const syscall_args_t *args) {
    int index = (int)args->arg2;
    k_disk_info_t *out = (k_disk_info_t *)args->arg3;
    if (!out) return (uint64_t)-1;
    Disk *d = disk_get_by_index(index);
    if (!d) return (uint64_t)-1;
    disk_k_strcpy(out->devname, d->devname, 16);
    disk_k_strcpy(out->label, d->label, 32);
    out->type = (uint32_t)d->type;
    out->total_sectors = d->total_sectors;
    out->is_partition = d->is_partition;
    out->is_fat32 = d->is_fat32;
    out->is_esp = d->is_esp;
    out->lba_offset = d->partition_lba_offset;
    return 0;
}

static uint64_t sys_cmd_disk_write_gpt(const syscall_args_t *args) {
    const char *devname = (const char *)args->arg2;
    k_partition_spec_t *parts = (k_partition_spec_t *)args->arg3;
    int count = (int)args->arg4;
    if (!devname || !parts) return (uint64_t)-1;
    Disk *d = disk_get_by_name(devname);
    if (!d) return (uint64_t)-1;
    return (uint64_t)disk_write_gpt(d, (disk_partition_spec_t *)parts, count);
}

static uint64_t sys_cmd_disk_write_mbr(const syscall_args_t *args) {
    const char *devname = (const char *)args->arg2;
    k_partition_spec_t *parts = (k_partition_spec_t *)args->arg3;
    int count = (int)args->arg4;
    if (!devname || !parts) return (uint64_t)-1;
    Disk *d = disk_get_by_name(devname);
    if (!d) return (uint64_t)-1;
    return (uint64_t)disk_write_mbr(d, (disk_partition_spec_t *)parts, count);
}

static uint64_t sys_cmd_disk_mkfs_fat32(const syscall_args_t *args) {
    extern int mkfs_fat32_format(Disk *disk, uint32_t sector_count, const char *label);
    const char *devname = (const char *)args->arg2;
    const char *label   = (const char *)args->arg3;
    if (!devname) return (uint64_t)-1;
    Disk *d = disk_get_by_name(devname);
    if (!d) return (uint64_t)-1;
    int ret = mkfs_fat32_format(d, d->total_sectors, label);
    if (ret == 0) d->is_fat32 = true;
    return (uint64_t)ret;
}

static uint64_t sys_cmd_disk_mount(const syscall_args_t *args) {
    const char *devname    = (const char *)args->arg2;
    const char *mountpoint = (const char *)args->arg3;
    if (!devname || !mountpoint) return (uint64_t)-1;
    Disk *d = disk_get_by_name(devname);
    if (!d || !d->is_fat32) return (uint64_t)-1;
    void *vol = fat32_mount_volume(d);
    if (!vol) return (uint64_t)-1;
    if (!vfs_mount(mountpoint, devname, "fat32", fat32_get_realfs_ops(), vol)) return (uint64_t)-1;
    wm_notify_fs_change();
    return 0;
}

static uint64_t sys_cmd_disk_umount(const syscall_args_t *args) {
    const char *mountpoint = (const char *)args->arg2;
    if (!mountpoint) return (uint64_t)-1;
    return vfs_umount(mountpoint) ? 0 : (uint64_t)-1;
}

static uint64_t sys_cmd_disk_rescan(const syscall_args_t *args) {
    const char *devname = (const char *)args->arg2;
    if (!devname) return (uint64_t)-1;
    Disk *d = disk_get_by_name(devname);
    if (!d) return (uint64_t)-1;
    return (uint64_t)disk_rescan(d);
}

static uint64_t sys_cmd_disk_sync(const syscall_args_t *args) {
    const char *mountpoint = (const char *)args->arg2;
    if (!mountpoint) return (uint64_t)-1;
    int mc = vfs_get_mount_count();
    for (int i = 0; i < mc; i++) {
        vfs_mount_t *m = vfs_get_mount(i);
        if (m && m->active && disk_k_strcmp(m->path, mountpoint) == 0) {
            Disk *d = disk_get_by_name(m->device);
            if (d) return (uint64_t)disk_sync(d);
        }
    }
    return (uint64_t)-1;
}

static uint64_t sys_cmd_disk_replace_kernel(const syscall_args_t *args) {
    extern void serial_write(const char *str);
    const char *src_path       = (const char *)args->arg2;
    const char *esp_mountpoint = (const char *)args->arg3;
    if (!src_path || !esp_mountpoint) return (uint64_t)-1;

    char dest_path[256];
    int mi = 0;
    while (mi < 255 && esp_mountpoint[mi]) { dest_path[mi] = esp_mountpoint[mi]; mi++; }
    const char *suffix = "/boredos.elf";
    for (int i = 0; suffix[i] && mi < 255; i++) dest_path[mi++] = suffix[i];
    dest_path[mi] = 0;

    if (disk_k_strcmp(src_path, dest_path) == 0) {
        serial_write("[KUP] Error: source and destination are the same file\n");
        return (uint64_t)-1;
    }

    vfs_file_t *src = vfs_open(src_path, "r");
    if (!src) { serial_write("[KUP] Error: source not found\n"); return (uint64_t)-1; }

    uint32_t src_size = vfs_file_size(src);
    if (src_size > 100 * 1024 * 1024) {
        serial_write("[KUP] Error: source > 100 MB\n");
        vfs_close(src); return (uint64_t)-1;
    }

    uint8_t magic[4];
    vfs_read(src, magic, 4);
    if (magic[0] != 0x7F || magic[1] != 'E' || magic[2] != 'L' || magic[3] != 'F') {
        serial_write("[KUP] Error: not an ELF file\n");
        vfs_close(src); return (uint64_t)-1;
    }
    vfs_seek(src, 0, 0);

    char bak_path[256];
    mi = 0;
    while (mi < 255 && esp_mountpoint[mi]) { bak_path[mi] = esp_mountpoint[mi]; mi++; }
    const char *bak_suffix = "/boredos.elf.bak";
    for (int i = 0; bak_suffix[i] && mi < 255; i++) bak_path[mi++] = bak_suffix[i];
    bak_path[mi] = 0;

    vfs_file_t *existing = vfs_open(dest_path, "r");
    if (existing) {
        vfs_file_t *bakf = vfs_open(bak_path, "w");
        if (bakf) {
            uint8_t *cbuf = (uint8_t *)kmalloc(4096);
            if (cbuf) {
                int n;
                while ((n = vfs_read(existing, cbuf, 4096)) > 0)
                    vfs_write(bakf, cbuf, n);
                kfree(cbuf);
            }
            vfs_close(bakf);
        } else {
            serial_write("[KUP] Warning: could not create backup\n");
        }
        vfs_close(existing);
    }

    vfs_file_t *dst = vfs_open(dest_path, "w");
    if (!dst) {
        serial_write("[KUP] Error: could not open destination for write\n");
        vfs_close(src); return (uint64_t)-1;
    }

    uint8_t *buf = (uint8_t *)kmalloc(4096);
    if (!buf) { vfs_close(src); vfs_close(dst); return (uint64_t)-1; }

    uint32_t bytes_written = 0;
    int n;
    while ((n = vfs_read(src, buf, 4096)) > 0) {
        int written = vfs_write(dst, buf, n);
        if (written != n) {
            serial_write("[KUP] Error: write failed mid-copy\n");
            kfree(buf); vfs_close(src); vfs_close(dst); return (uint64_t)-1;
        }
        bytes_written += (uint32_t)written;
    }

    kfree(buf);
    vfs_close(src);
    vfs_close(dst);

    if (bytes_written != src_size) {
        serial_write("[KUP] Error: incomplete write (size mismatch)\n");
        return (uint64_t)-1;
    }

    serial_write("[KUP] Kernel replaced (");
    {
        char numstr[16]; int ni = 15;
        numstr[ni] = 0;
        uint32_t v = bytes_written;
        if (v == 0) { numstr[--ni] = '0'; } else {
            while (v > 0) { numstr[--ni] = '0' + (v % 10); v /= 10; }
        }
        serial_write(numstr + ni);
    }
    serial_write(" bytes). Backup at boredos.elf.bak. Reboot required.\n");
    return 0;
}

#define SYS_CMD_TABLE_SIZE 110
static const syscall_handler_fn sys_cmd_table[SYS_CMD_TABLE_SIZE] = {
    [SYSTEM_CMD_SET_BG_COLOR]        = sys_cmd_set_bg_color,
    [SYSTEM_CMD_SET_BG_PATTERN]      = sys_cmd_set_bg_pattern,
    [SYSTEM_CMD_SET_WALLPAPER]       = sys_cmd_set_wallpaper,
    [SYSTEM_CMD_SET_DESKTOP_PROP]    = sys_cmd_set_desktop_prop,
    [SYSTEM_CMD_SET_MOUSE_SPEED]     = sys_cmd_set_mouse_speed,
    [SYSTEM_CMD_NETWORK_INIT]        = sys_cmd_network_init,
    [SYSTEM_CMD_GET_DESKTOP_PROP]    = sys_cmd_get_desktop_prop,
    [SYSTEM_CMD_GET_MOUSE_SPEED]     = sys_cmd_get_mouse_speed,
    [SYSTEM_CMD_GET_WALLPAPER_THUMB] = sys_cmd_get_wallpaper_thumb,
    [SYSTEM_CMD_CLEAR_SCREEN]        = sys_cmd_clear_screen,
    [SYSTEM_CMD_RTC_GET]             = sys_cmd_rtc_get,
    [SYSTEM_CMD_REBOOT]              = sys_cmd_reboot,
    [SYSTEM_CMD_SHUTDOWN]            = sys_cmd_shutdown,
    [SYSTEM_CMD_BEEP]                = sys_cmd_beep,
    [SYSTEM_CMD_GET_MEM_INFO]        = sys_cmd_get_mem_info,
    [SYSTEM_CMD_GET_TICKS]           = sys_cmd_get_ticks,
    [SYSTEM_CMD_PCI_LIST]            = sys_cmd_pci_list,
    [SYSTEM_CMD_NETWORK_DHCP]        = sys_cmd_network_dhcp,
    [SYSTEM_CMD_NETWORK_GET_MAC]     = sys_cmd_network_get_mac,
    [SYSTEM_CMD_NETWORK_GET_IP]      = sys_cmd_network_get_ip,
    [SYSTEM_CMD_NETWORK_SET_IP]      = sys_cmd_network_set_ip,
    [SYSTEM_CMD_UDP_SEND]            = sys_cmd_udp_send,
    [SYSTEM_CMD_NETWORK_GET_STATS]   = sys_cmd_network_get_stats,
    [SYSTEM_CMD_NETWORK_GET_GATEWAY] = sys_cmd_network_get_gateway,
    [SYSTEM_CMD_NETWORK_GET_DNS]     = sys_cmd_network_get_dns,
    [SYSTEM_CMD_ICMP_PING]           = sys_cmd_icmp_ping,
    [SYSTEM_CMD_NETWORK_IS_INIT]     = sys_cmd_network_is_init,
    [SYSTEM_CMD_GET_SHELL_CONFIG]    = sys_cmd_get_shell_config,
    [SYSTEM_CMD_SET_TEXT_COLOR]      = sys_cmd_set_text_color,
    [SYSTEM_CMD_NETWORK_HAS_IP]      = sys_cmd_network_has_ip,
    [SYSTEM_CMD_SET_WALLPAPER_PATH]  = sys_cmd_set_wallpaper_path,
    [SYSTEM_CMD_RTC_SET]             = sys_cmd_rtc_set,
    [SYSTEM_CMD_TCP_CONNECT]         = sys_cmd_tcp_connect,
    [SYSTEM_CMD_TCP_SEND]            = sys_cmd_tcp_send,
    [SYSTEM_CMD_TCP_RECV]            = sys_cmd_tcp_recv,
    [SYSTEM_CMD_TCP_CLOSE]           = sys_cmd_tcp_close,
    [SYSTEM_CMD_DNS_LOOKUP]          = sys_cmd_dns_lookup,
    [SYSTEM_CMD_SET_DNS]             = sys_cmd_set_dns,
    [SYSTEM_CMD_NET_UNLOCK]          = sys_cmd_net_unlock,
    [SYSTEM_CMD_SET_FONT]            = sys_cmd_set_font,
    [SYSTEM_CMD_SET_RAW_MODE]        = sys_cmd_set_raw_mode,
    [SYSTEM_CMD_TCP_RECV_NB]         = sys_cmd_tcp_recv_nb,
    [SYSTEM_CMD_SET_RESOLUTION]      = sys_cmd_set_resolution,
    [SYSTEM_CMD_NETWORK_GET_NIC_NAME] = sys_cmd_network_get_nic_name,
    [SYSTEM_CMD_PARALLEL_RUN]        = sys_cmd_parallel_run,
    [SYSTEM_CMD_SET_KEYBOARD_LAYOUT] = sys_cmd_set_keyboard_layout,
    [SYSTEM_CMD_GET_KEYBOARD_LAYOUT] = sys_cmd_get_keyboard_layout,
    [SYSTEM_CMD_SET_MOUSE_CURSOR_SCALE] = sys_cmd_set_mouse_cursor_scale,
    [SYSTEM_CMD_GET_MOUSE_CURSOR_SCALE] = sys_cmd_get_mouse_cursor_scale,
    [SYSTEM_CMD_TTY_CREATE]          = sys_cmd_tty_create,
    [SYSTEM_CMD_TTY_READ_OUT]        = sys_cmd_tty_read_out,
    [SYSTEM_CMD_TTY_WRITE_IN]        = sys_cmd_tty_write_in,
    [SYSTEM_CMD_TTY_READ_IN]         = sys_cmd_tty_read_in,
    [SYSTEM_CMD_SPAWN]               = sys_cmd_spawn_process,
    [SYSTEM_CMD_TTY_SET_FG]          = sys_cmd_tty_set_fg,
    [SYSTEM_CMD_TTY_GET_FG]          = sys_cmd_tty_get_fg,
    [SYSTEM_CMD_TTY_KILL_FG]         = sys_cmd_tty_kill_fg,
    [SYSTEM_CMD_TTY_KILL_ALL]        = sys_cmd_tty_kill_all,
    [SYSTEM_CMD_TTY_DESTROY]         = sys_cmd_tty_destroy,
    [SYSTEM_CMD_EXEC]                = sys_cmd_exec_process,
    [SYSTEM_CMD_WAITPID]             = sys_cmd_waitpid,
    [SYSTEM_CMD_KILL_SIGNAL]         = sys_cmd_kill_signal,
    [SYSTEM_CMD_SIGACTION]           = sys_cmd_sigaction,
    [SYSTEM_CMD_SIGPROCMASK]         = sys_cmd_sigprocmask,
    [SYSTEM_CMD_SIGPENDING]          = sys_cmd_sigpending,
    [SYSTEM_CMD_GET_ELF_METADATA]    = sys_cmd_get_elf_metadata,
    [SYSTEM_CMD_GET_ELF_PRIMARY_IMAGE] = sys_cmd_get_elf_primary_image,
    [SYSTEM_CMD_DISK_GET_COUNT]      = sys_cmd_disk_get_count,
    [SYSTEM_CMD_DISK_GET_INFO]       = sys_cmd_disk_get_info,
    [SYSTEM_CMD_DISK_WRITE_GPT]      = sys_cmd_disk_write_gpt,
    [SYSTEM_CMD_DISK_WRITE_MBR]      = sys_cmd_disk_write_mbr,
    [SYSTEM_CMD_DISK_MKFS_FAT32]     = sys_cmd_disk_mkfs_fat32,
    [SYSTEM_CMD_DISK_MOUNT]          = sys_cmd_disk_mount,
    [SYSTEM_CMD_DISK_UMOUNT]         = sys_cmd_disk_umount,
    [SYSTEM_CMD_DISK_RESCAN]         = sys_cmd_disk_rescan,
    [SYSTEM_CMD_DISK_REPLACE_KERNEL] = sys_cmd_disk_replace_kernel,
    [SYSTEM_CMD_DISK_SYNC]           = sys_cmd_disk_sync,
};

static uint64_t handle_sys_write(const syscall_args_t *args) {
    extern void cmd_write_len(const char *str, size_t len);
    process_t *proc = process_get_current();
    const char *buf = (const char*)args->arg2;
    size_t len = (size_t)args->arg3;
    if (!proc || !proc->is_user) {
        cmd_write_len(buf, len);
        return len;
    }
    if (proc->is_terminal_proc) {
        if (proc->tty_id >= 0) {
            tty_write_output(proc->tty_id, buf, len);
            return len;
        }
        cmd_write_len(buf, len);
        return len;
    }
    return len;
}

static uint64_t handle_sys_gui(const syscall_args_t *args) {
    int cmd = (int)args->arg1;
    if (cmd >= 0 && cmd < GUI_CMD_TABLE_SIZE && gui_cmd_table[cmd]) {
        return gui_cmd_table[cmd](args);
    }
    return 0;
}

static uint64_t handle_sys_fs(const syscall_args_t *args) {
    int cmd = (int)args->arg1;
    if (cmd >= 0 && cmd < FS_CMD_TABLE_SIZE && fs_cmd_table[cmd]) {
        return fs_cmd_table[cmd](args);
    }
    return 0;
}

static uint64_t handle_sys_system(const syscall_args_t *args) {
    int cmd = (int)args->arg1;
    if (cmd >= 0 && cmd < SYS_CMD_TABLE_SIZE && sys_cmd_table[cmd]) {
        return sys_cmd_table[cmd](args);
    }
    return -1;
}

static uint64_t handle_debug_serial_write(const syscall_args_t *args) {
    extern void serial_write(const char *str);
    serial_write((const char *)args->arg2);
    return 0;
}

static uint64_t handle_sys_sbrk(const syscall_args_t *args) {
    int incr = (int)args->arg1;
    process_t *proc = process_get_current();
    if (!proc || !proc->is_user) return (uint64_t)-1;
    
    uint64_t old_end = proc->heap_end;
    if (incr == 0) return old_end;
    
    uint64_t new_end = old_end + incr;
    
    if (incr > 0) {
        uint64_t start_page = (old_end + 0xFFF) & ~0xFFF;
        uint64_t end_page = (new_end + 0xFFF) & ~0xFFF;
        
        if (end_page > start_page) {
            uint64_t total_size = end_page - start_page;
            void *phys_block = kmalloc_aligned(total_size, 4096);
            if (!phys_block) return (uint64_t)-1; // Out of memory
            
            extern void mem_memset(void *dest, int val, size_t len);
            mem_memset(phys_block, 0, total_size);
            
            uint64_t phys_addr = (uint64_t)phys_block;
            for (uint64_t page = start_page; page < end_page; page += 4096) {
                paging_map_page(proc->pml4_phys, page, v2p(phys_addr), 0x07); // PT_PRESENT | PT_RW | PT_USER
                phys_addr += 4096;
            }
            proc->used_memory += (end_page - start_page);
        }
    }
    
    proc->heap_end = new_end;
    return old_end;
}

static uint64_t handle_sys_kill(const syscall_args_t *args) {
    (void)args;
    return 0;
}

#define SYSCALL_TABLE_SIZE 11
static const syscall_handler_fn syscall_table[SYSCALL_TABLE_SIZE] = {
    [SYS_WRITE] = handle_sys_write,          
    [SYS_GUI]   = handle_sys_gui,            
    [SYS_FS]    = handle_sys_fs,             
    [5]         = handle_sys_system,    
    [8]         = handle_debug_serial_write, 
    [9]         = handle_sys_sbrk,            
    [10]        = handle_sys_kill,            
};

static uint64_t syscall_handler_inner(registers_t *regs) {
    uint64_t syscall_num = regs->rax;

    syscall_args_t args = {
        .regs = regs,
        .arg1 = regs->rdi,
        .arg2 = regs->rsi,
        .arg3 = regs->rdx,
        .arg4 = regs->r10,
        .arg5 = regs->r8,
    };

    if (syscall_num < SYSCALL_TABLE_SIZE && syscall_table[syscall_num]) {
        return syscall_table[syscall_num](&args);
    }

    return 0;
}

static uint64_t syscall_maybe_deliver_signal(registers_t *regs) {
    process_t *proc = process_get_current();
    if (!proc || !proc->is_user) return (uint64_t)regs;

    uint64_t pending = proc->signal_pending & ~proc->signal_mask;
    if (!pending) return (uint64_t)regs;

    int sig = -1;
    for (int i = 1; i < MAX_SIGNALS; i++) {
        if (pending & (1ULL << (uint32_t)i)) {
            sig = i;
            break;
        }
    }
    if (sig < 0) return (uint64_t)regs;

    proc->signal_pending &= ~(1ULL << (uint32_t)sig);
    uint64_t handler = proc->signal_handlers[sig];
    int flags = proc->signal_action_flags[sig];

    if (handler == 1) {
        return (uint64_t)regs;
    }

    if (handler == 0 || sig == 9) {
        process_terminate_with_status(proc, 128 + sig);
        return process_schedule((uint64_t)regs);
    }

    if (flags & SA_RESETHAND) {
        proc->signal_handlers[sig] = 0;
        proc->signal_action_mask[sig] = 0;
        proc->signal_action_flags[sig] = 0;
    }

    uint64_t new_rsp = regs->rsp - sizeof(uint64_t);
    *((uint64_t *)new_rsp) = regs->rip;
    regs->rsp = new_rsp;
    regs->rip = handler;
    regs->rdi = (uint64_t)sig;
    return (uint64_t)regs;
}

uint64_t syscall_handler_c(registers_t *regs) {
    uint64_t syscall_num = regs->rax;
    
    // Check for context-switching syscalls
    if (syscall_num == 0 || syscall_num == 60) { // EXIT
        return process_terminate_current();
    }
    
    if (syscall_num == 10) { // KILL
        uint32_t target_pid = (uint32_t)regs->rdi;
        process_t *current = process_get_current();
        if (target_pid == 0) {
            // Protect kernel process
            regs->rax = -1;
            return (uint64_t)regs;
        }
        if (target_pid == 0xFFFFFFFF || target_pid == current->pid) {
            return process_terminate_current();
        } else {
            process_t *target = process_get_by_pid(target_pid);
            if (target) {
                process_terminate(target);
            }
            regs->rax = 0;
            return (uint64_t)regs;
        }
    }
    
    if (syscall_num == SYS_SYSTEM && regs->rdi == SYSTEM_CMD_YIELD) {
        extern uint64_t process_schedule(uint64_t current_rsp);
        regs->rax = 0;
        return process_schedule((uint64_t)regs);
    }
    
    if (syscall_num == SYS_SYSTEM && regs->rdi == SYSTEM_CMD_SLEEP) {
        uint32_t ms = (uint32_t)regs->rsi;
        process_t *proc = process_get_current();
        extern uint32_t wm_get_ticks(void);
        uint32_t ticks = ms / 16;
        if (ticks == 0 && ms > 0) ticks = 1;
        proc->sleep_until = wm_get_ticks() + ticks;
        regs->rax = 0;
        return process_schedule((uint64_t)regs);
    }
    
    // Normal syscalls
    regs->rax = syscall_handler_inner(regs);

    if (syscall_num == SYS_SYSTEM && regs->rdi == SYSTEM_CMD_WAITPID && regs->rax == (uint64_t)-2) {
        regs->rax = 0;
        return process_schedule((uint64_t)regs);
    }

    return syscall_maybe_deliver_signal(regs);
}

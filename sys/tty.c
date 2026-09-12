// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "tty.h"
#include "pty.h"
#include "spinlock.h"
#include "wait_queue.h"
#include "graphics.h"
#include "kutils.h"
#include "process.h"
#include "syscall.h"
#include "errno.h"
#include <stdbool.h>
#include <stdint.h>

static tty_t g_ttys[TTY_COUNT];
static int g_active_tty = 0;
static spinlock_t g_tty_global_lock = SPINLOCK_INIT;

extern bool g_headless_mode;

#define CONSOLE_BOOT_LOG_SIZE 65536
static char g_console_boot_log[CONSOLE_BOOT_LOG_SIZE];
static size_t g_console_boot_log_len = 0;
static bool g_console_boot_log_active = true;

struct vt_bootlog {
    char *buf;
    size_t size;
    size_t written;
};

#define VTERM_EVENT_QUEUE_SIZE 64
static vterm_event_t g_vterm_events[VTERM_EVENT_QUEUE_SIZE];
static uint32_t g_vterm_event_head = 0;
static uint32_t g_vterm_event_tail = 0;
static wait_queue_head_t g_vterm_event_wait;
static spinlock_t g_vterm_event_lock = SPINLOCK_INIT;

static void tty_queue_init(tty_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    wait_queue_init(&q->wait_queue);
    memset(q->buffer, 0, TTY_IN_QUEUE_SIZE);
}

static void tty_queue_push(tty_queue_t *q, uint8_t val) {
    uint32_t next = (q->head + 1) % TTY_IN_QUEUE_SIZE;
    if (next != q->tail) {
        q->buffer[q->head] = val;
        q->head = next;
        wait_queue_wake_all(&q->wait_queue);
    }
}

static int tty_queue_pop(tty_queue_t *q, uint8_t *buf, size_t len) {
    size_t count = 0;
    while (q->head != q->tail && count < len) {
        buf[count++] = q->buffer[q->tail];
        q->tail = (q->tail + 1) % TTY_IN_QUEUE_SIZE;
    }
    return (int)count;
}

static void tty_out_queue_init(tty_out_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    wait_queue_init(&q->wait_queue);
    memset(q->buffer, 0, TTY_OUT_QUEUE_SIZE);
}

static void tty_out_queue_push(tty_out_queue_t *q, uint8_t val) {
    uint32_t next = (q->head + 1) % TTY_OUT_QUEUE_SIZE;
    if (next != q->tail) {
        q->buffer[q->head] = val;
        q->head = next;
        wait_queue_wake_all(&q->wait_queue);
    }
}

static int tty_out_queue_pop(tty_out_queue_t *q, uint8_t *buf, size_t len) {
    size_t count = 0;
    while (q->head != q->tail && count < len) {
        buf[count++] = q->buffer[q->tail];
        q->tail = (q->tail + 1) % TTY_OUT_QUEUE_SIZE;
    }
    return (int)count;
}

void vterm_event_push(uint32_t type, int vt_id, int val) {
    uint64_t flags = spinlock_acquire_irqsave(&g_vterm_event_lock);
    uint32_t next = (g_vterm_event_head + 1) % VTERM_EVENT_QUEUE_SIZE;
    if (next != g_vterm_event_tail) {
        g_vterm_events[g_vterm_event_head].type = type;
        g_vterm_events[g_vterm_event_head].vt_id = vt_id;
        g_vterm_events[g_vterm_event_head].val = val;
        g_vterm_event_head = next;
        wait_queue_wake_all(&g_vterm_event_wait);
    }
    spinlock_release_irqrestore(&g_vterm_event_lock, flags);
}

int vterm_event_read(void *buf, size_t len) {
    if (!buf || len < sizeof(vterm_event_t)) return -EINVAL;
    uint64_t flags = spinlock_acquire_irqsave(&g_vterm_event_lock);
    if (g_vterm_event_head == g_vterm_event_tail) {
        spinlock_release_irqrestore(&g_vterm_event_lock, flags);
        return -EAGAIN;
    }
    vterm_event_t *ev = (vterm_event_t *)buf;
    *ev = g_vterm_events[g_vterm_event_tail];
    g_vterm_event_tail = (g_vterm_event_tail + 1) % VTERM_EVENT_QUEUE_SIZE;
    spinlock_release_irqrestore(&g_vterm_event_lock, flags);
    return sizeof(vterm_event_t);
}

int vterm_event_poll(struct poll_table *pt) {
    if (pt && pt->qproc) {
        pt->qproc(&g_vterm_event_wait, pt);
    }
    int mask = 0;
    uint64_t flags = spinlock_acquire_irqsave(&g_vterm_event_lock);
    if (g_vterm_event_head != g_vterm_event_tail) {
        mask |= 0x0001; // POLLIN
    }
    spinlock_release_irqrestore(&g_vterm_event_lock, flags);
    return mask;
}

void tty_init(void) {
    wait_queue_init(&g_vterm_event_wait);

    int w = get_screen_width();
    int h = get_screen_height();
    int cols = w > 0 ? (w / 8) : 80;
    int rows = h > 0 ? (h / 16 > 0 ? h / 16 : h / 8) : 25;

    for (int i = 0; i < TTY_COUNT; i++) {
        g_ttys[i].id = i;
        g_ttys[i].used = true;
        g_ttys[i].opened = (i == 0); // tty1 opened by default at boot
        if (i >= GRAPHICAL_TTY_COUNT) {
            int dev_id = i - GRAPHICAL_TTY_COUNT;
            serial_device_t *dev = serial_get_device(dev_id);
            g_ttys[i].is_serial = true;
            g_ttys[i].serial_dev = dev;
            g_ttys[i].serial_port = dev ? dev->io_port : 0;
        } else {
            g_ttys[i].is_serial = false;
            g_ttys[i].serial_dev = NULL;
            g_ttys[i].serial_port = 0;
        }
        g_ttys[i].kd_mode = KD_TEXT;
        g_ttys[i].blit_enabled = !g_ttys[i].is_serial;
        g_ttys[i].fg_pid = -1;
        g_ttys[i].last_char_was_cr = false;
        g_ttys[i].lock = SPINLOCK_INIT;

        g_ttys[i].ws.ws_row = (unsigned short)rows;
        g_ttys[i].ws.ws_col = (unsigned short)cols;
        g_ttys[i].ws.ws_xpixel = (unsigned short)(w > 0 ? w : cols * 8);
        g_ttys[i].ws.ws_ypixel = (unsigned short)(h > 0 ? h : rows * 16);

        tty_queue_init(&g_ttys[i].key_queue);
        tty_queue_init(&g_ttys[i].mouse_queue);
        tty_out_queue_init(&g_ttys[i].out_queue);
        tty_queue_init(&g_ttys[i].char_queue);
    }

    g_active_tty = 0;
}

tty_t* tty_get(int id) {
    if (id < 0 || id >= TTY_COUNT) return NULL;
    return &g_ttys[id];
}

void tty_switch(int id) {
    if (id < 0 || id >= TTY_COUNT) return;
    uint64_t flags = spinlock_acquire_irqsave(&g_tty_global_lock);
    g_active_tty = id;
    bool newly_opened = !g_ttys[id].opened;
    g_ttys[id].opened = true;
    int fg_pid = g_ttys[id].fg_pid;
    spinlock_release_irqrestore(&g_tty_global_lock, flags);

    vterm_event_push(VTERM_EVENT_SWITCH, id, 0);

    if (newly_opened || fg_pid <= 0) {
        extern int signal_send_to_pid(int pid, int sig);
        signal_send_to_pid(1, 28 /* SIGWINCH */);
    }
}

int tty_get_active_id(void) {
    return g_active_tty;
}

void tty_write(int id, const char *data, size_t len) {
    if ((id == 0 || (g_headless_mode && id == 10)) && g_console_boot_log_active && data && len > 0) {
        uint64_t bflags = spinlock_acquire_irqsave(&g_tty_global_lock);
        size_t avail = CONSOLE_BOOT_LOG_SIZE - g_console_boot_log_len;
        size_t to_copy = len < avail ? len : avail;
        if (to_copy > 0) {
            memcpy(g_console_boot_log + g_console_boot_log_len, data, to_copy);
            g_console_boot_log_len += to_copy;
        }
        spinlock_release_irqrestore(&g_tty_global_lock, bflags);
    }

    if (pty_is_pty_id(id)) {
        pty_write_output(id, data, len);
        return;
    }
    tty_t *t = tty_get(id);
    if (!t) return;
    uint64_t flags = spinlock_acquire_irqsave(&t->lock);
    if (t->is_serial) {
        if (t->serial_dev) {
            for (size_t i = 0; i < len; i++) {
                char c = data[i];
                if (c == '\n') {
                    serial_device_write_char(t->serial_dev, '\r');
                }
                serial_device_write_char(t->serial_dev, c);
            }
        } else if (t->serial_port > 0) {
            for (size_t i = 0; i < len; i++) {
                char c = data[i];
                if (c == '\n') {
                    serial_write_char(t->serial_port, '\r');
                }
                serial_write_char(t->serial_port, c);
            }
        }
        spinlock_release_irqrestore(&t->lock, flags);
        return;
    }

    for (size_t i = 0; i < len; i++) {
        tty_out_queue_push(&t->out_queue, (uint8_t)data[i]);
    }

    spinlock_release_irqrestore(&t->lock, flags);
}

void tty_push_key(int id, uint8_t scancode) {
    tty_t *t = tty_get(id);
    if (!t) return;
    tty_queue_push(&t->key_queue, scancode);
}

void tty_push_mouse(int id, uint8_t *packet, size_t len) {
    tty_t *t = tty_get(id);
    if (!t) return;
    for (size_t i = 0; i < len; i++) {
        tty_queue_push(&t->mouse_queue, packet[i]);
    }
}

int tty_read_key(int id, uint8_t *buf, size_t len) {
    tty_t *t = tty_get(id);
    if (!t) return 0;
    return tty_queue_pop(&t->key_queue, buf, len);
}

int tty_read_mouse(int id, uint8_t *buf, size_t len) {
    tty_t *t = tty_get(id);
    if (!t) return 0;
    return tty_queue_pop(&t->mouse_queue, buf, len);
}

void tty_push_char(int id, uint8_t c) {
    tty_t *t = tty_get(id);
    if (!t) return;

    if (c == CTRL_C_CHAR) { // Ctrl+C (ETX / SIGINT)
        int fg = t->fg_pid;
        process_t *target = NULL;
        if (fg > 0) {
            target = process_get_by_pid((uint32_t)fg);
        }
        if (!target) {
            target = process_find_child_on_tty(id);
        }
        if (target && target->pid > 1) {
            tty_write_output(id, "^C\n", 3);
            target->signal_pending |= SIGINT;
            if (target->state == PROC_STATE_BLOCKED) {
                target->state = PROC_STATE_RUNNING;
                target->sleep_until = 0;
            }
            t->fg_pid = -1;
            process_put(target);
            return;
        }
        if (target) {
            process_put(target);
        }
    }

    tty_queue_push(&t->char_queue, c);
}

void tty_push_serial_char(int id, uint8_t ch) {
    tty_t *t = tty_get(id);
    if (!t || !t->is_serial) return;

    if (ch == CTRL_C_CHAR) { // Ctrl+C (ETX / SIGINT)
        int fg = t->fg_pid;
        process_t *target = NULL;
        if (fg > 0) {
            target = process_get_by_pid((uint32_t)fg);
        }
        if (!target) {
            target = process_find_child_on_tty(id);
        }
        if (target && target->pid > 1) {
            if (t->serial_dev) {
                serial_device_write_str(t->serial_dev, "^C\r\n");
            } else if (t->serial_port > 0) {
                serial_write_str(t->serial_port, "^C\r\n");
            }
            target->signal_pending |= SIGINT;
            if (target->state == PROC_STATE_BLOCKED) {
                target->state = PROC_STATE_RUNNING;
                target->sleep_until = 0;
            }
            t->fg_pid = -1;
            return;
        }
    }

    if (ch == '\r') {
        t->last_char_was_cr = true;
        tty_queue_push(&t->char_queue, '\n');
    } else if (ch == '\n') {
        if (t->last_char_was_cr) {
            t->last_char_was_cr = false;
            return;
        }
        tty_queue_push(&t->char_queue, '\n');
    } else {
        t->last_char_was_cr = false;
        if (ch == 0x7F || ch == '\b') {
            tty_queue_push(&t->char_queue, '\b');
        } else {
            tty_queue_push(&t->char_queue, ch);
        }
    }
}

bool tty_is_serial(int id) {
    tty_t *t = tty_get(id);
    if (!t) return false;
    return t->is_serial;
}

int tty_read_input(int id, char *buf, size_t len) {
    if (pty_is_pty_id(id)) return pty_read_input(id, buf, len);
    tty_t *t = tty_get(id);
    if (!t) return 0;
    return tty_queue_pop(&t->char_queue, (uint8_t*)buf, len);
}

int tty_read_master(int id, char *buf, size_t len) {
    if (id < 0 || id >= GRAPHICAL_TTY_COUNT) return -1;
    tty_t *t = tty_get(id);
    if (!t) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&t->lock);
    int ret = tty_out_queue_pop(&t->out_queue, (uint8_t*)buf, len);
    spinlock_release_irqrestore(&t->lock, flags);
    return ret;
}

int tty_write_master(int id, const char *buf, size_t len) {
    if (id < 0 || id >= GRAPHICAL_TTY_COUNT) return -1;
    tty_t *t = tty_get(id);
    if (!t) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&t->lock);
    for (size_t i = 0; i < len; i++) {
        tty_queue_push(&t->char_queue, (uint8_t)buf[i]);
    }
    spinlock_release_irqrestore(&t->lock, flags);
    return (int)len;
}

int tty_poll_master(int id, struct poll_table *pt) {
    if (id < 0 || id >= GRAPHICAL_TTY_COUNT) return POLLNVAL;
    tty_t *t = tty_get(id);
    if (!t) return POLLNVAL;

    if (pt && pt->qproc) {
        pt->qproc(&t->out_queue.wait_queue, pt);
    }

    int mask = 0;
    uint64_t flags = spinlock_acquire_irqsave(&t->lock);
    if (t->out_queue.head != t->out_queue.tail) {
        mask |= 0x0001; // POLLIN
    }
    mask |= 0x0004; // POLLOUT
    spinlock_release_irqrestore(&t->lock, flags);
    return mask;
}

int tty_create(void) {
    uint64_t flags = spinlock_acquire_irqsave(&g_tty_global_lock);
    for (int i = 0; i < TTY_COUNT; i++) {
        if (!g_ttys[i].used) {
            g_ttys[i].used = true;
            spinlock_release_irqrestore(&g_tty_global_lock, flags);
            return i;
        }
    }
    spinlock_release_irqrestore(&g_tty_global_lock, flags);
    return -1;
}

#define TIOCSCTTY   0x540E
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCSWINSZ  0x5414

int tty_ioctl(int id, uint64_t request, void *arg) {
    tty_t *t = tty_get(id);
    if (!t) return -1;
    
    if (request == TIOCGWINSZ) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(struct winsize))) return -EFAULT;
        struct winsize *ws = (struct winsize *)arg;
        *ws = t->ws;
        return 0;
    } else if (request == TIOCSWINSZ) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(struct winsize))) return -EFAULT;
        struct winsize *ws = (struct winsize *)arg;
        t->ws = *ws;
        if (t->fg_pid > 0) {
            signal_send_to_pgrp(t->fg_pid, 28 /* SIGWINCH */);
        }
        return 0;
    } else if (request == TIOCSCTTY) {
        process_t *proc = process_get_current();
        if (proc) {
            proc->tty_id = id;
            t->fg_pid = proc->pid;
        }
        return 0;
    } else if (request == TIOCGPGRP) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        *(int *)arg = t->fg_pid;
        return 0;
    } else if (request == TIOCSPGRP) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        t->fg_pid = *(int *)arg;
        return 0;
    } else if (request == KDSETMODE) {
        uint64_t mode = (uint64_t)arg;
        if (mode == KD_GRAPHICS) {
            t->kd_mode = KD_GRAPHICS;
            t->blit_enabled = false;
        } else if (mode == KD_TEXT) {
            t->kd_mode = KD_TEXT;
            t->blit_enabled = true;
        }
        vterm_event_push(VTERM_EVENT_KDMODE, id, (int)mode);
        return 0;
    } else if (request == KDGETMODE) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        *(int *)arg = t->kd_mode;
        return 0;
    } else if (request == 0x5606 /* VT_ACTIVATE */) {
        int target = id;
        if (arg) {
            uintptr_t val = (uintptr_t)arg;
            if (val < TTY_COUNT) {
                target = (int)val;
            } else if (is_valid_user_ptr(arg, sizeof(int))) {
                target = *(int *)arg;
            }
            if (target > 0 && target <= GRAPHICAL_TTY_COUNT) {
                target = target - 1;
            }
        }
        tty_switch(target);
        return 0;
    } else if (request == 0x5607) { // VT_GETACTIVE
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        *(int *)arg = tty_get_active_id();
        return 0;
    } else if (request == 0x5603 /* VT_GETSTATE */) {
        struct vt_stat_k {
            uint16_t v_active;
            uint16_t v_signal;
            uint16_t v_state;
        };
        if (!arg || !is_valid_user_ptr(arg, sizeof(struct vt_stat_k))) return -EFAULT;
        uint16_t state = 0;
        for (int i = 0; i < TTY_COUNT; i++) {
            if (g_ttys[i].opened || i == g_active_tty) {
                state |= (1U << i);
            }
        }
        struct vt_stat_k *vs = (struct vt_stat_k *)arg;
        vs->v_active = (uint16_t)(g_active_tty + 1);
        vs->v_signal = 0;
        vs->v_state = state;
        return 0;
    } else if (request == 0x5420 /* TIOCISOPEN */) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        *(int *)arg = (t->opened || id == g_active_tty) ? 1 : 0;
        return 0;
    } else if (request == 0x541F /* TIOCISSERIAL */) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(int))) return -EFAULT;
        *(int *)arg = t->is_serial ? 1 : 0;
        return 0;
    } else if (request == 0x5608 /* VT_GETBOOTLOG */) {
        if (!arg || !is_valid_user_ptr(arg, sizeof(struct vt_bootlog))) return -EFAULT;
        struct vt_bootlog *bl = (struct vt_bootlog *)arg;
        if (!bl->buf || bl->size == 0 || !is_valid_user_ptr(bl->buf, bl->size)) return -EFAULT;
        uint64_t bflags = spinlock_acquire_irqsave(&g_tty_global_lock);
        size_t to_copy = g_console_boot_log_len < bl->size ? g_console_boot_log_len : bl->size;
        memcpy(bl->buf, g_console_boot_log, to_copy);
        bl->written = to_copy;
        spinlock_release_irqrestore(&g_tty_global_lock, bflags);
        return 0;
    } else if (request == 0x5609 /* VT_STOPBOOTLOG */) {
        uint64_t bflags = spinlock_acquire_irqsave(&g_tty_global_lock);
        g_console_boot_log_active = false;
        spinlock_release_irqrestore(&g_tty_global_lock, bflags);
        return 0;
    }
    
    return -1;
}

size_t tty_copy_boot_log(char *dst, size_t max_len) {
    if (!dst || max_len == 0) return 0;
    uint64_t bflags = spinlock_acquire_irqsave(&g_tty_global_lock);
    size_t to_copy = g_console_boot_log_len < max_len ? g_console_boot_log_len : max_len;
    memcpy(dst, g_console_boot_log, to_copy);
    spinlock_release_irqrestore(&g_tty_global_lock, bflags);
    return to_copy;
}

int tty_destroy(int id) {
    tty_t *t = tty_get(id);
    if (!t) return -1;
    uint64_t flags = spinlock_acquire_irqsave(&g_tty_global_lock);
    t->used = false;
    spinlock_release_irqrestore(&g_tty_global_lock, flags);
    return 0;
}

void tty_write_output(int id, const char *data, size_t len) {
    if (!data || len == 0) return;
    if (pty_is_pty_id(id)) { pty_write_output(id, data, len); return; }
    tty_t *t = tty_get(id);
    if (!t) return;
    for (size_t i = 0; i < len; i++) {
        tty_out_queue_push(&t->out_queue, (uint8_t)data[i]);
    }
}

int tty_read_output(int id, char *buf, size_t len) {
    if (!buf || len == 0) return 0;
    if (pty_is_pty_id(id)) return pty_read_output(id, buf, len);
    tty_t *t = tty_get(id);
    if (!t) return 0;
    return tty_out_queue_pop(&t->out_queue, (uint8_t*)buf, len);
}

int tty_write_input(int id, const char *buf, size_t len) {
    if (!buf || len == 0) return 0;
    if (pty_is_pty_id(id)) return pty_write_input(id, buf, len);
    tty_t *t = tty_get(id);
    if (!t) return 0;
    for (size_t i = 0; i < len; i++) {
        tty_queue_push(&t->char_queue, (uint8_t)buf[i]);
    }
    return (int)len;
}

int tty_set_foreground(int id, int pid) {
    if (pty_is_pty_id(id)) return pty_set_foreground(id, pid);
    tty_t *t = tty_get(id);
    if (!t) return -1;
    t->fg_pid = pid;
    return 0;
}

int tty_get_foreground(int id) {
    if (pty_is_pty_id(id)) return pty_get_foreground(id);
    tty_t *t = tty_get(id);
    if (!t) return -1;
    return t->fg_pid;
}

void tty_set_blit_enabled_for_id(int id, bool enabled) {
    if (pty_is_pty_id(id)) return;
    tty_t *t = tty_get(id);
    if (t) {
        if (enabled && t->kd_mode == KD_GRAPHICS) return;
        t->blit_enabled = enabled;
    }
}

void tty_set_blit_enabled(bool enabled) {
    tty_set_blit_enabled_for_id(g_active_tty, enabled);
}

bool tty_get_blit_enabled(void) {
    tty_t *t = tty_get(g_active_tty);
    if (!t) return true;
    return (t->kd_mode != KD_GRAPHICS && t->blit_enabled);
}

int tty_poll(int id, struct poll_table *pt) {
    if (pty_is_pty_id(id)) return pty_poll(id, pt);
    tty_t *t = tty_get(id);
    if (!t) return 0;
    
    int mask = 0;
    if (pt && pt->qproc) {
        pt->qproc(&t->char_queue.wait_queue, pt);
    }
    
    if (t->char_queue.head != t->char_queue.tail) {
        mask |= 0x0001; // POLLIN
    }
    
    mask |= 0x0004; // POLLOUT (always writable)
    
    return mask;
}

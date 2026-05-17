// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "tty.h"
#include "spinlock.h"
#include "wait_queue.h"
#include "../fs/vfs.h"
#include <stdbool.h>
#include <stdint.h>

#define TTY_MAX 8
#define TTY_OUT_SIZE 16384
#define TTY_IN_SIZE 4096

typedef struct {
    bool used;
    int id;
    char out_buf[TTY_OUT_SIZE];
    uint32_t out_head;
    uint32_t out_tail;
    char in_buf[TTY_IN_SIZE];
    uint32_t in_head;
    uint32_t in_tail;
    int fg_pid;
    spinlock_t lock;
    wait_queue_head_t wait_queue;
} tty_t;

static tty_t ttys[TTY_MAX] = {0};

extern void mem_memset(void *dest, int val, size_t len);

static tty_t *tty_get(int tty_id) {
    if (tty_id < 0 || tty_id >= TTY_MAX) return NULL;
    if (!ttys[tty_id].used) return NULL;
    return &ttys[tty_id];
}

int tty_create(void) {
    for (int i = 0; i < TTY_MAX; i++) {
        if (!ttys[i].used) {
            ttys[i].used = true;
            ttys[i].id = i;
            ttys[i].out_head = 0;
            ttys[i].out_tail = 0;
            ttys[i].in_head = 0;
            ttys[i].in_tail = 0;
            ttys[i].fg_pid = -1;
            ttys[i].lock = SPINLOCK_INIT;
            wait_queue_init(&ttys[i].wait_queue);
            mem_memset(ttys[i].out_buf, 0, sizeof(ttys[i].out_buf));
            mem_memset(ttys[i].in_buf, 0, sizeof(ttys[i].in_buf));
            return i;
        }
    }
    return -1;
}

int tty_destroy(int tty_id) {
    if (tty_id < 0 || tty_id >= TTY_MAX) return -1;
    tty_t *tty = &ttys[tty_id];

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    if (!tty->used) {
        spinlock_release_irqrestore(&tty->lock, rflags);
        return -1;
    }

    tty->used = false;
    tty->id = -1;
    tty->out_head = 0;
    tty->out_tail = 0;
    tty->in_head = 0;
    tty->in_tail = 0;
    tty->fg_pid = -1;
    mem_memset(tty->out_buf, 0, sizeof(tty->out_buf));
    mem_memset(tty->in_buf, 0, sizeof(tty->in_buf));

    spinlock_release_irqrestore(&tty->lock, rflags);
    return 0;
}

static int tty_write_ring(char *buf, uint32_t size, uint32_t *head, uint32_t *tail, const char *data, size_t len) {
    int written = 0;
    for (size_t i = 0; i < len; i++) {
        uint32_t next = (*head + 1) % size;
        if (next == *tail) break;
        buf[*head] = data[i];
        *head = next;
        written++;
    }
    return written;
}

static int tty_read_ring(char *buf, uint32_t size, uint32_t *head, uint32_t *tail, char *out, size_t max_len) {
    int read = 0;
    while (*tail != *head && (size_t)read < max_len) {
        out[read++] = buf[*tail];
        *tail = (*tail + 1) % size;
    }
    return read;
}

int tty_write_output(int tty_id, const char *data, size_t len) {
    tty_t *tty = tty_get(tty_id);
    if (!tty || !data || len == 0) return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    int written = tty_write_ring(tty->out_buf, TTY_OUT_SIZE, &tty->out_head, &tty->out_tail, data, len);
    spinlock_release_irqrestore(&tty->lock, rflags);
    return written;
}

int tty_read_output(int tty_id, char *buf, size_t max_len) {
    tty_t *tty = tty_get(tty_id);
    if (!tty || !buf || max_len == 0) return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    int read = tty_read_ring(tty->out_buf, TTY_OUT_SIZE, &tty->out_head, &tty->out_tail, buf, max_len);
    spinlock_release_irqrestore(&tty->lock, rflags);
    return read;
}

int tty_write_input(int tty_id, const char *data, size_t len) {
    tty_t *tty = tty_get(tty_id);
    if (!tty || !data || len == 0) return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    int written = tty_write_ring(tty->in_buf, TTY_IN_SIZE, &tty->in_head, &tty->in_tail, data, len);
    spinlock_release_irqrestore(&tty->lock, rflags);
    if (written > 0) {
        wait_queue_wake_all(&tty->wait_queue);
    }
    return written;
}

int tty_read_input(int tty_id, char *buf, size_t max_len) {
    tty_t *tty = tty_get(tty_id);
    if (!tty || !buf || max_len == 0) return 0;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    int read = tty_read_ring(tty->in_buf, TTY_IN_SIZE, &tty->in_head, &tty->in_tail, buf, max_len);
    spinlock_release_irqrestore(&tty->lock, rflags);
    return read;
}

int tty_set_foreground(int tty_id, int pid) {
    tty_t *tty = tty_get(tty_id);
    if (!tty) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    tty->fg_pid = pid;
    spinlock_release_irqrestore(&tty->lock, rflags);
    return 0;
}

int tty_get_foreground(int tty_id) {
    tty_t *tty = tty_get(tty_id);
    if (!tty) return -1;

    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    int pid = tty->fg_pid;
    spinlock_release_irqrestore(&tty->lock, rflags);
    return pid;
}

int tty_poll(int tty_id, struct poll_table *pt) {
    tty_t *tty = tty_get(tty_id);
    if (!tty) return POLLNVAL;

    if (pt && pt->qproc) {
        pt->qproc(&tty->wait_queue, pt);
    }

    int mask = 0;
    uint64_t rflags = spinlock_acquire_irqsave(&tty->lock);
    if (tty->in_head != tty->in_tail) {
        mask |= POLLIN;
    }
    mask |= POLLOUT;
    spinlock_release_irqrestore(&tty->lock, rflags);

    return mask;
}

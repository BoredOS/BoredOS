// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "sockbuf.h"
#include "slab.h"
#include "kutils.h"

void sockbuf_init(sockbuf_t *sb, uint32_t hiwat) {
    if (!sb) return;
    sb->sb_cc = 0;
    sb->sb_hiwat = hiwat ? hiwat : (512 * 1024); // Default 512KB
    sb->sb_lowat = 1;

    sb->sb_flags = 0;
    sb->lock = SPINLOCK_INIT;
    wait_queue_init(&sb->waitq);
    sb->head = NULL;
    sb->tail = NULL;
}

void sockbuf_destroy(sockbuf_t *sb) {
    if (!sb) return;
    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
    sockbuf_entry_t *curr = sb->head;
    sb->head = NULL;
    sb->tail = NULL;
    sb->sb_cc = 0;
    spinlock_release_irqrestore(&sb->lock, flags);

    while (curr) {
        sockbuf_entry_t *next = curr->next;
        if (curr->p) {
            pbuf_free(curr->p);
            curr->p = NULL;
        }
        kfree_null(curr);
        curr = next;
    }
}


int sockbuf_append(sockbuf_t *sb, struct pbuf *p, const ip_addr_t *src_ip, uint16_t src_port) {
    if (!sb || !p) return -1;

    sockbuf_entry_t *entry = (sockbuf_entry_t *)kmalloc(sizeof(sockbuf_entry_t));
    if (!entry) return -1;

    entry->p = p;
    if (src_ip) entry->src_ip = *src_ip;
    else ip_addr_set_zero(&entry->src_ip);
    entry->src_port = src_port;
    entry->next = NULL;

    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);

    if (sb->sb_cc + p->tot_len > sb->sb_hiwat) {
        spinlock_release_irqrestore(&sb->lock, flags);
        kfree_null(entry);
        return -1;
    }

    if (!sb->tail) {
        sb->head = entry;
        sb->tail = entry;
    } else {
        sb->tail->next = entry;
        sb->tail = entry;
    }

    sb->sb_cc += p->tot_len;
    spinlock_release_irqrestore(&sb->lock, flags);

    wait_queue_wake_all(&sb->waitq);
    return 0;
}

int sockbuf_read(sockbuf_t *sb, void *buf, size_t max_len, ip_addr_t *out_ip, uint16_t *out_port, int peek) {
    if (!sb || !buf || max_len == 0) return 0;

    if (peek) {
        uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
        if (!sb->head) {
            spinlock_release_irqrestore(&sb->lock, flags);
            return 0;
        }
        sockbuf_entry_t *entry = sb->head;
        size_t avail = entry->p->tot_len;
        size_t to_copy = (max_len < avail) ? max_len : avail;
        if (to_copy > 0xFFFF) to_copy = 0xFFFF;
        if (out_ip) *out_ip = entry->src_ip;
        if (out_port) *out_port = entry->src_port;

        uint8_t kbuf[512];
        size_t kcopy = (to_copy < sizeof(kbuf)) ? to_copy : sizeof(kbuf);
        pbuf_copy_partial(entry->p, kbuf, (u16_t)kcopy, 0);
        spinlock_release_irqrestore(&sb->lock, flags);

        memcpy(buf, kbuf, kcopy);
        return (int)kcopy;
    }

    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);

    if (!sb->head) {
        spinlock_release_irqrestore(&sb->lock, flags);
        return 0;
    }

    sockbuf_entry_t *consumed_head = NULL;
    sockbuf_entry_t *consumed_tail = NULL;
    size_t total_dequeued = 0;

    uint8_t partial_buf[4096];
    size_t partial_len = 0;
    ip_addr_t first_ip;
    uint16_t first_port = 0;
    bool has_first_meta = false;

    while (sb->head && total_dequeued < max_len) {
        sockbuf_entry_t *entry = sb->head;
        size_t avail = entry->p->tot_len;
        size_t wanted = max_len - total_dequeued;

        if (!has_first_meta) {
            first_ip = entry->src_ip;
            first_port = entry->src_port;
            has_first_meta = true;
        }

        if (wanted >= avail) {
            sb->head = entry->next;
            if (!sb->head) sb->tail = NULL;
            entry->next = NULL;
            if (sb->sb_cc >= (uint32_t)avail) {
                sb->sb_cc -= (uint32_t)avail;
            } else {
                sb->sb_cc = 0;
            }
            total_dequeued += avail;

            if (!consumed_head) {
                consumed_head = entry;
                consumed_tail = entry;
            } else {
                consumed_tail->next = entry;
                consumed_tail = entry;
            }
        } else {
            size_t to_copy = (wanted < sizeof(partial_buf)) ? wanted : sizeof(partial_buf);
            pbuf_copy_partial(entry->p, partial_buf, (u16_t)to_copy, 0);
            partial_len = to_copy;
            total_dequeued += to_copy;

            entry->p = pbuf_free_header(entry->p, (u16_t)to_copy);
            if (sb->sb_cc >= (uint32_t)to_copy) {
                sb->sb_cc -= (uint32_t)to_copy;
            } else {
                sb->sb_cc = 0;
            }
            break;
        }
    }

    spinlock_release_irqrestore(&sb->lock, flags);

    if (out_ip && has_first_meta) *out_ip = first_ip;
    if (out_port && has_first_meta) *out_port = first_port;

    uint8_t *dest = (uint8_t *)buf;
    size_t copied = 0;

    sockbuf_entry_t *curr = consumed_head;
    while (curr) {
        sockbuf_entry_t *next = curr->next;
        if (curr->p) {
            u16_t len = curr->p->tot_len;
            pbuf_copy_partial(curr->p, dest + copied, len, 0);
            copied += len;
            pbuf_free(curr->p);
            curr->p = NULL;
        }
        kfree_null(curr);
        curr = next;
    }

    if (partial_len > 0) {
        memcpy(dest + copied, partial_buf, partial_len);
        copied += partial_len;
    }

    if (copied > 0) {
        wait_queue_wake_all(&sb->waitq);
    }
    return (int)copied;
}

int sockbuf_is_empty(sockbuf_t *sb) {
    if (!sb) return 1;
    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
    int empty = (sb->head == NULL);
    spinlock_release_irqrestore(&sb->lock, flags);
    return empty;
}

int sockbuf_readable(sockbuf_t *sb) {
    if (!sb) return 0;
    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
    uint32_t threshold = sb->sb_lowat > 0 ? sb->sb_lowat : 1;
    int readable = (sb->sb_cc >= threshold);
    spinlock_release_irqrestore(&sb->lock, flags);
    return readable;
}

int sockbuf_writable(sockbuf_t *sb) {
    if (!sb) return 0;
    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
    int writable = (sb->sb_cc < sb->sb_hiwat);
    spinlock_release_irqrestore(&sb->lock, flags);
    return writable;
}

uint32_t sockbuf_get_cc(sockbuf_t *sb) {
    if (!sb) return 0;
    uint64_t flags = spinlock_acquire_irqsave(&sb->lock);
    uint32_t cc = sb->sb_cc;
    spinlock_release_irqrestore(&sb->lock, flags);
    return cc;
}

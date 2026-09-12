// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef TTY_H
#define TTY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "spinlock.h"
#include "wait_queue.h"

#define TIOCGWINSZ 0x5413
#define KDSETMODE   0x4B3A
#define KDGETMODE   0x4B3B
#define KD_TEXT     0x00
#define KD_GRAPHICS 0x01

#define CTRL_C_CHAR 0x03

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#include "serial.h"

#define GRAPHICAL_TTY_COUNT 10
#define SERIAL_TTY_COUNT 4
#define TTY_COUNT (GRAPHICAL_TTY_COUNT + SERIAL_TTY_COUNT)
#define TTY_IN_QUEUE_SIZE 4096
#define TTY_OUT_QUEUE_SIZE 8192

typedef struct {
    uint8_t buffer[TTY_IN_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    wait_queue_head_t wait_queue;
} tty_queue_t;

typedef struct {
    uint8_t buffer[TTY_OUT_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    wait_queue_head_t wait_queue;
} tty_out_queue_t;

#define VTERM_EVENT_SWITCH   1
#define VTERM_EVENT_KDMODE   2
#define VTERM_EVENT_RESIZE   3

typedef struct {
    uint32_t type;
    int vt_id;
    int val;
} vterm_event_t;

typedef struct {
    int id;
    bool used;
    bool opened;
    bool is_serial;
    uint16_t serial_port;
    serial_device_t *serial_dev;
    int kd_mode;
    bool blit_enabled;
    struct winsize ws;

    tty_queue_t key_queue;
    tty_queue_t mouse_queue;
    tty_out_queue_t out_queue;
    tty_queue_t char_queue;

    int fg_pid;
    bool last_char_was_cr;
    spinlock_t lock;
} tty_t;

void tty_init(void);
tty_t* tty_get(int id);
void tty_switch(int id);
int tty_get_active_id(void);

int tty_create(void);
int tty_destroy(int id);

void tty_write(int id, const char *data, size_t len);
void tty_write_output(int id, const char *data, size_t len);
int tty_read_output(int id, char *buf, size_t len);
int tty_write_input(int id, const char *buf, size_t len);

void tty_push_key(int id, uint8_t scancode);
void tty_push_mouse(int id, uint8_t *packet, size_t len);
int tty_read_key(int id, uint8_t *buf, size_t len);
int tty_read_mouse(int id, uint8_t *buf, size_t len);
void tty_push_char(int id, uint8_t c);
void tty_push_serial_char(int id, uint8_t ch);
bool tty_is_serial(int id);
int tty_read_input(int id, char *buf, size_t len);

int tty_read_master(int id, char *buf, size_t len);
int tty_write_master(int id, const char *buf, size_t len);
int tty_poll_master(int id, struct poll_table *pt);

void vterm_event_push(uint32_t type, int vt_id, int val);
int vterm_event_read(void *buf, size_t len);
int vterm_event_poll(struct poll_table *pt);

int tty_set_foreground(int id, int pid);
int tty_get_foreground(int id);

void tty_blit_active(void);
void tty_set_blit_enabled(bool enabled);
bool tty_get_blit_enabled(void);
struct poll_table;
int tty_poll(int id, struct poll_table *pt);
int tty_ioctl(int id, uint64_t request, void *arg);
size_t tty_copy_boot_log(char *dst, size_t max_len);

#endif



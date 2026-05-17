// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef TTY_H
#define TTY_H

#include <stddef.h>

int tty_create(void);
int tty_destroy(int tty_id);
int tty_write_output(int tty_id, const char *data, size_t len);
int tty_read_output(int tty_id, char *buf, size_t max_len);
int tty_write_input(int tty_id, const char *data, size_t len);
int tty_read_input(int tty_id, char *buf, size_t max_len);
int tty_set_foreground(int tty_id, int pid);
int tty_get_foreground(int tty_id);

struct poll_table;
int tty_poll(int tty_id, struct poll_table *pt);

#endif

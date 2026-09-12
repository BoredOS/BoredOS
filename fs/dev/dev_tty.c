// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"
#include "../../sys/errno.h"

extern bool g_headless_mode;
extern int pty_create(void);
extern void* pty_get(int pty_id);
extern int pty_destroy(int pty_id);
extern int pty_read_input(int pty_id, char *buf, size_t len);
extern int pty_read_output(int pty_id, char *buf, size_t len);
extern void pty_write_output(int pty_id, const char *data, size_t len);
extern int pty_write_input(int pty_id, const char *buf, size_t len);
extern int pty_ioctl(int pty_id, uint64_t request, void *arg);
extern int pty_poll(int pty_id, struct poll_table *pt);
extern int pty_poll_master(int pty_id, struct poll_table *pt);
extern int tty_read_master(int id, char *buf, size_t len);
extern int tty_write_master(int id, const char *buf, size_t len);
extern int tty_poll_master(int id, struct poll_table *pt);
extern int vterm_event_read(void *buf, size_t len);
extern int vterm_event_poll(struct poll_table *pt);

vfs_file_t* dev_tty_open(const char *devname, const char *mode) {
    (void)mode;
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    // Serial TTY devices: /dev/ttyS0..ttyS3
    if (str_starts_with(devname, "ttyS")) {
        int s_id = atoi(devname + 4);
        if (s_id >= 0 && s_id < SERIAL_TTY_COUNT) {
            tty_t *t = tty_get(GRAPHICAL_TTY_COUNT + s_id);
            if (t && !t->opened) {
                t->opened = true;
                extern int signal_send_to_pid(int pid, int sig);
                signal_send_to_pid(1, 1 /* SIGHUP */);
            }
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = &mounts[0];
                vf->fs_handle = (void*)(uintptr_t)(GRAPHICAL_TTY_COUNT + s_id);
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_TTY;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // System console: /dev/console
    if (strcmp(devname, "console") == 0) {
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = &mounts[0];
            int console_id = g_headless_mode ? 10 : 0;
            vf->fs_handle = (void*)(uintptr_t)console_id;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_TTY;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    if (str_starts_with(devname, "tty")) {
        if (strcmp(devname, "tty0") == 0) {
            if (g_headless_mode) {
                spinlock_release_irqrestore(&vfs_lock, flags);
                return NULL;
            }
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = NULL;
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_TTY_ACTIVE;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
        int id = atoi(devname + 3);
        if (id >= 1 && id <= TTY_COUNT) {
            int target_tty = id - 1;
            if (g_headless_mode && target_tty < GRAPHICAL_TTY_COUNT) {
                spinlock_release_irqrestore(&vfs_lock, flags);
                return NULL;
            }
            tty_t *t = tty_get(target_tty);
            if (t && !t->opened) {
                t->opened = true;
                extern int signal_send_to_pid(int pid, int sig);
                signal_send_to_pid(1, 1 /* SIGHUP */);
            }
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)target_tty;
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_TTY;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // PTY master: /dev/ptmx
    if (strcmp(devname, "ptmx") == 0) {
        int pty_id = pty_create();
        if (pty_id >= 0) {
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)pty_id;
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_PTY_MASTER;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // PTY slave: /dev/pts/X
    if (str_starts_with(devname, "pts/")) {
        int idx = atoi(devname + 4);
        int pty_id = 1024 + idx;
        void *p = pty_get(pty_id);
        if (p) {
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)pty_id;
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_PTY_SLAVE;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // Keyboard devices: /dev/keyboard or /dev/keyboardX
    if (str_starts_with(devname, "keyboard")) {
        int id = 0;
        if (strcmp(devname, "keyboard") == 0) {
            id = tty_get_active_id() + 1;
        } else {
            id = atoi(devname + 8);
        }

        if (id >= 1 && id <= TTY_COUNT) {
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)(id - 1);
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_KEYBOARD;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // Mouse devices: /dev/mouse or /dev/mouseX
    if (str_starts_with(devname, "mouse")) {
        int id = 0;
        if (strcmp(devname, "mouse") == 0) {
            id = tty_get_active_id() + 1;
        } else {
            id = atoi(devname + 5);
        }

        if (id >= 1 && id <= TTY_COUNT) {
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)(id - 1);
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_MOUSE;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    // VTerm control device: /dev/vterm_ctl
    if (strcmp(devname, "vterm_ctl") == 0) {
        if (g_headless_mode) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            return NULL;
        }
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = NULL;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_VTERM_CTL;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    // VTerm master endpoints: /dev/vterm1..vterm10
    if (str_starts_with(devname, "vterm")) {
        int id = atoi(devname + 5);
        if (id >= 1 && id <= GRAPHICAL_TTY_COUNT) {
            if (g_headless_mode) {
                spinlock_release_irqrestore(&vfs_lock, flags);
                return NULL;
            }
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = (void*)(uintptr_t)(id - 1);
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_VTERM_MASTER;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);
    return NULL;
}

void dev_tty_close(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return;
    if (file->device_type == DEVICE_TYPE_PTY_MASTER) {
        pty_destroy((int)(uintptr_t)file->fs_handle);
    }
}

int dev_tty_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_TTY_ACTIVE) {
        return tty_read_input(tty_get_active_id(), (char*)buf, size);
    } else if (file->device_type == DEVICE_TYPE_TTY) {
        return tty_read_input((int)(uintptr_t)file->fs_handle, (char*)buf, (size_t)size);
    } else if (file->device_type == DEVICE_TYPE_VTERM_MASTER) {
        return tty_read_master((int)(uintptr_t)file->fs_handle, (char*)buf, size);
    } else if (file->device_type == DEVICE_TYPE_VTERM_CTL) {
        return vterm_event_read(buf, size);
    } else if (file->device_type == DEVICE_TYPE_PTY_MASTER) {
        return pty_read_output((int)(uintptr_t)file->fs_handle, (char*)buf, (size_t)size);
    } else if (file->device_type == DEVICE_TYPE_PTY_SLAVE) {
        return pty_read_input((int)(uintptr_t)file->fs_handle, (char*)buf, (size_t)size);
    } else if (file->device_type == DEVICE_TYPE_KEYBOARD) {
        return tty_read_key((int)(uintptr_t)file->fs_handle, (uint8_t*)buf, size);
    } else if (file->device_type == DEVICE_TYPE_MOUSE) {
        return tty_read_mouse((int)(uintptr_t)file->fs_handle, (uint8_t*)buf, size);
    }
    return -1;
}

int dev_tty_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_TTY_ACTIVE) {
        tty_write(tty_get_active_id(), (const char*)buf, size);
        return size;
    } else if (file->device_type == DEVICE_TYPE_TTY) {
        tty_write((int)(uintptr_t)file->fs_handle, (const char*)buf, size);
        return size;
    } else if (file->device_type == DEVICE_TYPE_VTERM_MASTER) {
        return tty_write_master((int)(uintptr_t)file->fs_handle, (const char*)buf, size);
    } else if (file->device_type == DEVICE_TYPE_VTERM_CTL) {
        return -EBADF;
    } else if (file->device_type == DEVICE_TYPE_PTY_MASTER) {
        return pty_write_input((int)(uintptr_t)file->fs_handle, (const char*)buf, size);
    } else if (file->device_type == DEVICE_TYPE_PTY_SLAVE) {
        pty_write_output((int)(uintptr_t)file->fs_handle, (const char*)buf, size);
        return size;
    }
    return -1;
}

int dev_tty_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_TTY_ACTIVE) {
        extern int tty_ioctl(int id, uint64_t request, void *arg);
        return tty_ioctl(tty_get_active_id(), request, arg);
    } else if (file->device_type == DEVICE_TYPE_TTY || file->device_type == DEVICE_TYPE_VTERM_MASTER) {
        extern int tty_ioctl(int id, uint64_t request, void *arg);
        return tty_ioctl((int)(uintptr_t)file->fs_handle, request, arg);
    } else if (file->device_type == DEVICE_TYPE_PTY_MASTER || file->device_type == DEVICE_TYPE_PTY_SLAVE) {
        return pty_ioctl((int)(uintptr_t)file->fs_handle, request, arg);
    }
    return -1;
}

int dev_tty_poll(vfs_file_t *file, struct poll_table *pt) {
    if (!file || !file->valid || !file->is_device) return POLLNVAL;

    if (file->device_type == DEVICE_TYPE_TTY_ACTIVE) {
        int handle_id = tty_get_active_id();
        tty_t *t = tty_get(handle_id);
        if (!t) return POLLNVAL;
        if (pt && pt->qproc) pt->qproc(&t->char_queue.wait_queue, pt);
        int mask = 0;
        uint64_t flags = spinlock_acquire_irqsave(&t->lock);
        if (t->char_queue.head != t->char_queue.tail) mask |= POLLIN;
        mask |= POLLOUT;
        spinlock_release_irqrestore(&t->lock, flags);
        return mask;
    }
    if (file->device_type == DEVICE_TYPE_VTERM_MASTER) {
        return tty_poll_master((int)(uintptr_t)file->fs_handle, pt);
    }
    if (file->device_type == DEVICE_TYPE_VTERM_CTL) {
        return vterm_event_poll(pt);
    }
    if (file->device_type == DEVICE_TYPE_PTY_MASTER) {
        return pty_poll_master((int)(uintptr_t)file->fs_handle, pt);
    }
    if (file->device_type == DEVICE_TYPE_PTY_SLAVE) {
        return pty_poll((int)(uintptr_t)file->fs_handle, pt);
    }
    if (file->device_type == DEVICE_TYPE_TTY || file->device_type == DEVICE_TYPE_KEYBOARD || file->device_type == DEVICE_TYPE_MOUSE) {
        int handle_id = (int)(uintptr_t)file->fs_handle;
        tty_t *t = tty_get(handle_id);
        if (!t) return POLLNVAL;

        tty_queue_t *q = NULL;
        if (file->device_type == DEVICE_TYPE_TTY) q = &t->char_queue;
        else if (file->device_type == DEVICE_TYPE_KEYBOARD) q = &t->key_queue;
        else q = &t->mouse_queue;

        if (pt && pt->qproc) {
            pt->qproc(&q->wait_queue, pt);
        }

        int mask = 0;
        uint64_t flags = spinlock_acquire_irqsave(&t->lock);
        if (q->head != q->tail) mask |= POLLIN;
        mask |= POLLOUT;
        spinlock_release_irqrestore(&t->lock, flags);
        return mask;
    }
    return POLLIN | POLLOUT;
}

int dev_tty_list_entries(vfs_dirent_t *entries, int max, int count) {
    if (!g_headless_mode) {
        if (count < max) {
            strcpy(entries[count].name, "tty0");
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }
        for (int i = 0; i < GRAPHICAL_TTY_COUNT && count < max; i++) {
            char name[16];
            strcpy(name, "tty");
            itoa(i + 1, name + 3);
            strcpy(entries[count].name, name);
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }

        if (count < max) {
            strcpy(entries[count].name, "vterm_ctl");
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }
        for (int i = 0; i < GRAPHICAL_TTY_COUNT && count < max; i++) {
            char name[16];
            strcpy(name, "vterm");
            itoa(i + 1, name + 5);
            strcpy(entries[count].name, name);
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }
    }

    for (int i = 0; i < SERIAL_TTY_COUNT && count < max; i++) {
        char name[16];
        strcpy(name, "ttyS");
        itoa(i, name + 4);
        strcpy(entries[count].name, name);
        entries[count].size = 0;
        entries[count].is_directory = 0;
        count++;
    }

    if (count < max) {
        strcpy(entries[count].name, "ptmx");
        entries[count].size = 0;
        entries[count].is_directory = 0;
        count++;
    }
    if (count < max) {
        strcpy(entries[count].name, "pts");
        entries[count].size = 0;
        entries[count].is_directory = 1;
        count++;
    }

    if (count < max) {
        strcpy(entries[count].name, "keyboard");
        entries[count].size = 0;
        entries[count].is_directory = 0;
        count++;
    }
    if (count < max) {
        strcpy(entries[count].name, "mouse");
        entries[count].size = 0;
        entries[count].is_directory = 0;
        count++;
    }

    return count;
}

bool dev_tty_exists(const char *dev) {
    if (strcmp(dev, "ptmx") == 0) return true;
    if (strcmp(dev, "pts") == 0) return true;
    if (str_starts_with(dev, "pts/")) return true;
    if (strcmp(dev, "keyboard") == 0 || str_starts_with(dev, "keyboard")) return true;
    if (strcmp(dev, "mouse") == 0 || str_starts_with(dev, "mouse")) return true;
    if (strcmp(dev, "vterm_ctl") == 0) return !g_headless_mode;
    if (str_starts_with(dev, "vterm")) return !g_headless_mode;
    if (strcmp(dev, "tty0") == 0) return !g_headless_mode;
    if (str_starts_with(dev, "tty")) return true;
    if (strcmp(dev, "console") == 0) return true;
    return false;
}

int dev_tty_get_info(const char *dev, vfs_dirent_t *info) {
    if (strcmp(dev, "ptmx") == 0) {
        strcpy(info->name, "ptmx");
        info->size = 0;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    if (strcmp(dev, "pts") == 0) {
        strcpy(info->name, "pts");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    if (str_starts_with(dev, "pts/")) {
        const char *pts_name = dev + 4;
        strcpy(info->name, pts_name);
        info->size = 0;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    if (strcmp(dev, "vterm_ctl") == 0 || str_starts_with(dev, "vterm") ||
        strcmp(dev, "keyboard") == 0 || str_starts_with(dev, "keyboard") ||
        strcmp(dev, "mouse") == 0 || str_starts_with(dev, "mouse") ||
        str_starts_with(dev, "tty") || strcmp(dev, "console") == 0) {
        strcpy(info->name, dev);
        info->size = 0;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    return -1;
}

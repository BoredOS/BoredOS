// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"

vfs_file_t* vfs_dev_open(const char *devname, const char *mode) {
    if (!devname) return NULL;

    vfs_file_t *f = NULL;

    if ((f = dev_tty_open(devname, mode)) != NULL) return f;
    if ((f = dev_fb_open(devname, mode)) != NULL) return f;
    if ((f = dev_audio_open(devname, mode)) != NULL) return f;
    if ((f = dev_net_open(devname, mode)) != NULL) return f;
    if ((f = dev_shm_open(devname, mode)) != NULL) return f;
    if ((f = dev_disk_open(devname, mode)) != NULL) return f;
    if ((f = dev_random_open(devname, mode)) != NULL) return f;

    return NULL;
}

void vfs_dev_close(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return;

    switch (file->device_type) {
        case DEVICE_TYPE_PTY_MASTER:
            dev_tty_close(file);
            break;
        case DEVICE_TYPE_AUDIO:
            dev_audio_close(file);
            break;
        case DEVICE_TYPE_TUN:
            dev_net_close(file);
            break;
        case DEVICE_TYPE_SHM:
            dev_shm_close(file);
            break;
        case DEVICE_TYPE_RANDOM:
        case DEVICE_TYPE_NULL:
        case DEVICE_TYPE_ZERO:
            break;
        default:
            break;
    }
}

int vfs_dev_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    switch (file->device_type) {
        case DEVICE_TYPE_TTY:
        case DEVICE_TYPE_PTY_MASTER:
        case DEVICE_TYPE_PTY_SLAVE:
        case DEVICE_TYPE_KEYBOARD:
        case DEVICE_TYPE_MOUSE:
            return dev_tty_read(file, buf, size);

        case DEVICE_TYPE_FRAMEBUFFER:
            return dev_fb_read(file, buf, size);

        case DEVICE_TYPE_AUDIO:
        case DEVICE_TYPE_RTC:
            return dev_audio_read(file, buf, size);

        case DEVICE_TYPE_TUN:
            return dev_net_read(file, buf, size);

        case DEVICE_TYPE_SHM:
            return dev_shm_read(file, buf, size);

        case DEVICE_TYPE_BLOCK:
            return dev_disk_read(file, buf, size);

        case DEVICE_TYPE_RANDOM:
            return dev_random_read(file, buf, size);

        case DEVICE_TYPE_NULL:
            return 0; // EOF

        case DEVICE_TYPE_ZERO:
            if (buf && size > 0) memset(buf, 0, size);
            return (int)size;

        default:
            return -1;
    }
}

int vfs_dev_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    switch (file->device_type) {
        case DEVICE_TYPE_TTY:
        case DEVICE_TYPE_PTY_MASTER:
        case DEVICE_TYPE_PTY_SLAVE:
            return dev_tty_write(file, buf, size);

        case DEVICE_TYPE_FRAMEBUFFER:
            return dev_fb_write(file, buf, size);

        case DEVICE_TYPE_AUDIO:
        case DEVICE_TYPE_RTC:
            return dev_audio_write(file, buf, size);

        case DEVICE_TYPE_TUN:
            return dev_net_write(file, buf, size);

        case DEVICE_TYPE_SHM:
            return dev_shm_write(file, buf, size);

        case DEVICE_TYPE_BLOCK:
            return dev_disk_write(file, buf, size);

        case DEVICE_TYPE_RANDOM:
            return dev_random_write(file, buf, size);

        case DEVICE_TYPE_NULL:
        case DEVICE_TYPE_ZERO:
            return (int)size; // Discard data successfully

        default:
            return -1;
    }
}

int vfs_dev_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;

    switch (file->device_type) {
        case DEVICE_TYPE_TTY:
        case DEVICE_TYPE_PTY_MASTER:
        case DEVICE_TYPE_PTY_SLAVE:
            return dev_tty_ioctl(file, request, arg);

        case DEVICE_TYPE_FRAMEBUFFER:
            return dev_fb_ioctl(file, request, arg);

        case DEVICE_TYPE_PCSPKR:
        case DEVICE_TYPE_AUDIO:
        case DEVICE_TYPE_MIXER:
            return dev_audio_ioctl(file, request, arg);

        case DEVICE_TYPE_TUN:
            return dev_net_ioctl(file, request, arg);

        case DEVICE_TYPE_BLOCK:
            return dev_disk_ioctl(file, request, arg);

        default:
            return -1;
    }
}

int vfs_dev_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->valid || !file->is_device) return -1;

    switch (file->device_type) {
        case DEVICE_TYPE_FRAMEBUFFER:
            return dev_fb_seek(file, offset, whence);

        case DEVICE_TYPE_SHM:
            return dev_shm_seek(file, offset, whence);

        case DEVICE_TYPE_BLOCK:
            return dev_disk_seek(file, offset, whence);

        case DEVICE_TYPE_NULL:
        case DEVICE_TYPE_ZERO:
            return 0;

        default:
            return -29; // -ESPIPE
    }
}

int vfs_dev_poll(vfs_file_t *file, struct poll_table *pt) {
    if (!file || !file->valid || !file->is_device) return POLLNVAL;

    switch (file->device_type) {
        case DEVICE_TYPE_TTY:
        case DEVICE_TYPE_PTY_MASTER:
        case DEVICE_TYPE_PTY_SLAVE:
        case DEVICE_TYPE_KEYBOARD:
        case DEVICE_TYPE_MOUSE:
            return dev_tty_poll(file, pt);

        default:
            return POLLIN | POLLOUT;
    }
}

uint64_t vfs_dev_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return 0;

    switch (file->device_type) {
        case DEVICE_TYPE_FRAMEBUFFER:
            return dev_fb_file_size(file);

        case DEVICE_TYPE_SHM:
            return dev_shm_file_size(file);

        case DEVICE_TYPE_BLOCK:
            return dev_disk_file_size(file);

        default:
            return 0;
    }
}

int vfs_dev_list_entries(vfs_dirent_t *entries, int max, int count) {
    count = dev_tty_list_entries(entries, max, count);
    count = dev_audio_list_entries(entries, max, count);
    count = dev_fb_list_entries(entries, max, count);
    count = dev_shm_list_entries(entries, max, count);
    count = dev_disk_list_entries(entries, max, count);
    count = dev_random_list_entries(entries, max, count);
    return count;
}

bool vfs_dev_exists(const char *dev) {
    if (dev_fb_exists(dev)) return true;
    if (dev_tty_exists(dev)) return true;
    if (dev_net_exists(dev)) return true;
    if (dev_audio_exists(dev)) return true;
    if (dev_shm_exists(dev)) return true;
    if (dev_disk_exists(dev)) return true;
    if (dev_random_exists(dev)) return true;
    return false;
}

bool vfs_dev_is_directory(const char *dev) {
    (void)dev;
    return false;
}

bool vfs_dev_delete(const char *dev) {
    return dev_shm_delete(dev);
}

int vfs_dev_get_info(const char *dev, vfs_dirent_t *info) {
    if (!dev || !info) return -1;

    int ret;
    if ((ret = dev_tty_get_info(dev, info)) == 0) return 0;
    if ((ret = dev_fb_get_info(dev, info)) == 0) return 0;
    if ((ret = dev_net_get_info(dev, info)) == 0) return 0;
    if ((ret = dev_disk_get_info(dev, info)) == 0) return 0;
    if ((ret = dev_random_get_info(dev, info)) == 0) return 0;

    return -1;
}

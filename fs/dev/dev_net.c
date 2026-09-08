// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"

extern void* tun_open(void);
extern void  tun_close(void *handle);
extern int   tun_read(void *handle, void *buf, size_t count);
extern int   tun_write(void *handle, const void *buf, size_t count);
extern int   tun_ioctl(void *handle, unsigned long request, void *arg);

vfs_file_t* dev_net_open(const char *devname, const char *mode) {
    (void)mode;
    if (strcmp(devname, "net/tun") == 0 || strcmp(devname, "tun") == 0 || strcmp(devname, "tun0") == 0) {
        void *th = tun_open();
        if (th) {
            uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = NULL;
                vf->fs_handle = th;
                vf->is_device = true;
                vf->device_type = DEVICE_TYPE_TUN;
                vf->position = 0;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            } else {
                tun_close(th);
                spinlock_release_irqrestore(&vfs_lock, flags);
                return NULL;
            }
        }
    }
    return NULL;
}

void dev_net_close(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return;
    if (file->device_type == DEVICE_TYPE_TUN) {
        tun_close(file->fs_handle);
    }
}

int dev_net_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type == DEVICE_TYPE_TUN) {
        return tun_read(file->fs_handle, buf, size);
    }
    return -1;
}

int dev_net_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type == DEVICE_TYPE_TUN) {
        return tun_write(file->fs_handle, buf, size);
    }
    return -1;
}

int dev_net_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type == DEVICE_TYPE_TUN) {
        return tun_ioctl(file->fs_handle, (unsigned long)request, arg);
    }
    return -1;
}

bool dev_net_exists(const char *dev) {
    if (strcmp(dev, "net") == 0 ||
        strcmp(dev, "net/tun") == 0 ||
        strcmp(dev, "tun") == 0 ||
        strcmp(dev, "tun0") == 0) {
        return true;
    }
    return false;
}

int dev_net_get_info(const char *dev, vfs_dirent_t *info) {
    if (strcmp(dev, "net") == 0) {
        strcpy(info->name, "net");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    if (strcmp(dev, "net/tun") == 0 || strcmp(dev, "tun") == 0 || strcmp(dev, "tun0") == 0) {
        strcpy(info->name, "tun");
        info->size = 0;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    return -1;
}

// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"
#include "../../sys/syscall.h"
#include "../../sys/errno.h"

vfs_file_t* dev_disk_open(const char *devname, const char *mode) {
    (void)mode;
    Disk *d = disk_get_by_name(devname);
    if (d) {
        uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = (void*)d;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_BLOCK;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
        spinlock_release_irqrestore(&vfs_lock, flags);
    }
    return NULL;
}

int dev_disk_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_BLOCK) return -1;

    Disk *d = (Disk*)file->fs_handle;
    if (!d) return -1;

    uint32_t total_read = 0;
    uint32_t sector = (uint32_t)(file->position / 512);
    uint32_t offset = (uint32_t)(file->position % 512);
    uint8_t sector_buf[512] __attribute__((aligned(512)));

    while (total_read < (uint32_t)size) {
        if (sector >= d->total_sectors) break;

        if (offset == 0 && (size - total_read) >= 512 && d->read_sectors && (((uintptr_t)buf + total_read) & 511) == 0) {
            uint32_t remaining_sectors = (uint32_t)(size - total_read) / 512;
            if (sector + remaining_sectors > d->total_sectors) {
                remaining_sectors = d->total_sectors - sector;
            }
            if (remaining_sectors > 0) {
                uint32_t chunk = (remaining_sectors > 128) ? 128 : remaining_sectors;
                if (d->read_sectors(d, sector, chunk, (uint8_t*)buf + total_read) != 0) {
                    break;
                }
                uint32_t bytes = chunk * 512;
                total_read += bytes;
                file->position += bytes;
                sector += chunk;
                continue;
            }
        }

        if (d->read_sector(d, sector, sector_buf) != 0) break;

        uint32_t to_copy = 512 - offset;
        if (to_copy > (uint32_t)size - total_read) to_copy = (uint32_t)size - total_read;

        memcpy((uint8_t*)buf + total_read, sector_buf + offset, to_copy);

        total_read += to_copy;
        file->position += to_copy;
        sector++;
        offset = 0;
    }
    return (int)total_read;
}

int dev_disk_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_BLOCK) return -1;

    Disk *d = (Disk*)file->fs_handle;
    if (!d) return -1;

    uint32_t total_written = 0;
    uint32_t sector = (uint32_t)(file->position / 512);
    uint32_t offset = (uint32_t)(file->position % 512);
    uint8_t sector_buf[512] __attribute__((aligned(512)));

    while (total_written < (uint32_t)size) {
        if (sector >= d->total_sectors) break;

        if (offset == 0 && (size - total_written) >= 512 && d->write_sectors && (((uintptr_t)buf + total_written) & 511) == 0) {
            uint32_t remaining_sectors = (uint32_t)(size - total_written) / 512;
            if (sector + remaining_sectors > d->total_sectors) {
                remaining_sectors = d->total_sectors - sector;
            }
            if (remaining_sectors > 0) {
                uint32_t chunk = (remaining_sectors > 128) ? 128 : remaining_sectors;
                if (d->write_sectors(d, sector, chunk, (const uint8_t*)buf + total_written) != 0) {
                    break;
                }
                uint32_t bytes = chunk * 512;
                total_written += bytes;
                file->position += bytes;
                sector += chunk;
                continue;
            }
        }

        uint32_t to_copy = 512 - offset;
        if (to_copy > (uint32_t)size - total_written) to_copy = (uint32_t)size - total_written;

        if (offset != 0 || to_copy < 512) {
            if (d->read_sector(d, sector, sector_buf) != 0) break;
        }
        memcpy(sector_buf + offset, (const uint8_t*)buf + total_written, to_copy);
        if (d->write_sector(d, sector, sector_buf) != 0) break;

        total_written += to_copy;
        file->position += to_copy;
        sector++;
        offset = 0;
    }
    return (int)total_written;
}

int dev_disk_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_BLOCK) return -1;

    Disk *d = (Disk*)file->fs_handle;
    if (!d) return -1;
    uint64_t dev_size = (uint64_t)d->total_sectors * 512;
    uint64_t new_pos = file->position;

    if (whence == 0) {
        if (offset < 0) return -1;
        new_pos = (uint64_t)offset;
    } else if (whence == 1) {
        if ((int64_t)new_pos + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)new_pos + offset);
    } else if (whence == 2) {
        if ((int64_t)dev_size + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)dev_size + offset);
    } else return -1;

    if (new_pos > dev_size) new_pos = dev_size;
    file->position = new_pos;
    return 0;
}

uint64_t dev_disk_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return 0;
    if (file->device_type != DEVICE_TYPE_BLOCK) return 0;
    Disk *d = (Disk*)file->fs_handle;
    return d ? ((uint64_t)d->total_sectors * 512) : 0;
}

int dev_disk_list_entries(vfs_dirent_t *entries, int max, int count) {
    int dcount = disk_get_count();
    for (int i = 0; i < dcount && count < max; i++) {
        Disk *d = disk_get_by_index(i);
        if (d && d->registered) {
            bool found = false;
            for (int k = 0; k < count; k++) {
                if (strcmp(entries[k].name, d->devname) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                strcpy(entries[count].name, d->devname);
                entries[count].size = (uint64_t)d->total_sectors * 512;
                entries[count].is_directory = 0;
                entries[count].start_cluster = 0;
                entries[count].write_date = 0;
                entries[count].write_time = 0;
                count++;
            }
        }
    }
    return count;
}

bool dev_disk_exists(const char *dev) {
    return disk_get_by_name(dev) != NULL;
}

int dev_disk_get_info(const char *dev, vfs_dirent_t *info) {
    Disk *d = disk_get_by_name(dev);
    if (d) {
        strcpy(info->name, d->devname);
        info->size = (uint64_t)d->total_sectors * 512;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    return -1;
}

static inline bool is_valid_ioctl_ptr(const void *ptr, size_t size) {
    if (!ptr) return false;
    if ((uintptr_t)ptr >= 0xFFFF800000000000ULL) return true;
    return is_valid_user_ptr(ptr, size);
}

int dev_disk_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;
    Disk *d = (Disk*)file->fs_handle;
    if (!d) return -1;

    switch (request) {
        case 0x125F: // BLKRRPART
            return disk_rescan(d);
        case 0x80081272: // BLKGETSIZE64
        case 0x1272:
            if (!is_valid_ioctl_ptr(arg, sizeof(uint64_t))) return -EFAULT;
            *(uint64_t*)arg = (uint64_t)d->total_sectors * 512;
            return 0;
        case 0x1260: // BLKGETSIZE
            if (!is_valid_ioctl_ptr(arg, sizeof(unsigned long))) return -EFAULT;
            *(unsigned long*)arg = d->total_sectors;
            return 0;
        case 0x1268: // BLKSSZGET
            if (!is_valid_ioctl_ptr(arg, sizeof(int))) return -EFAULT;
            *(int*)arg = 512;
            return 0;
        default:
            return -1;
    }
}

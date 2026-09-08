// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"
#include "../shm.h"

extern void shm_unref(shm_segment_t *seg);
extern shm_segment_t* shm_get_or_create(const char *name);
extern void shm_unlink(const char *name);
extern bool shm_exists(const char *name);
extern int  shm_allocate(shm_segment_t *seg, size_t size);
extern uint64_t p2v(uint64_t phys);

vfs_file_t* dev_shm_open(const char *devname, const char *mode) {
    (void)mode;
    if (str_starts_with(devname, "shm/")) {
        const char *shm_name = devname + 4;
        if (shm_name[0] != '\0') {
            shm_segment_t *seg = shm_get_or_create(shm_name);
            if (seg) {
                uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
                vfs_file_t *vf = vfs_alloc_file();
                if (vf) {
                    vf->mount = NULL;
                    vf->fs_handle = (void*)seg;
                    vf->is_device = true;
                    vf->device_type = DEVICE_TYPE_SHM;
                    vf->position = 0;
                    spinlock_release_irqrestore(&vfs_lock, flags);
                    return vf;
                } else {
                    shm_unref(seg);
                    spinlock_release_irqrestore(&vfs_lock, flags);
                }
            }
        }
    }
    return NULL;
}

void dev_shm_close(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return;
    if (file->device_type == DEVICE_TYPE_SHM) {
        shm_unref((shm_segment_t *)file->fs_handle);
    }
}

int dev_shm_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_SHM) return -1;

    shm_segment_t *seg = (shm_segment_t *)file->fs_handle;
    if (!seg || seg->page_count == 0) return -1;
    if (file->position >= seg->size) return 0;

    uint64_t to_read = seg->size - file->position;
    if ((uint64_t)size < to_read) to_read = size;

    uint64_t read_accum = 0;
    while (read_accum < to_read) {
        uint64_t current_pos = file->position + read_accum;
        uint32_t page_idx = (uint32_t)(current_pos / 4096);
        uint32_t page_off = (uint32_t)(current_pos % 4096);

        uint64_t chunk = 4096 - page_off;
        if (chunk > to_read - read_accum) chunk = to_read - read_accum;

        void *page_vaddr = (void *)p2v(seg->phys_pages[page_idx]);
        memcpy((uint8_t*)buf + read_accum, (uint8_t*)page_vaddr + page_off, chunk);

        read_accum += chunk;
    }
    file->position += to_read;
    return (int)to_read;
}

int dev_shm_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_SHM) return -1;

    shm_segment_t *seg = (shm_segment_t *)file->fs_handle;
    if (!seg) return -1;

    // Grow the segment if this write extends beyond current size
    size_t end_pos = (size_t)file->position + (size_t)size;
    if (end_pos > seg->size) {
        if (shm_allocate(seg, end_pos) < 0) return -1;
    }
    if (file->position >= seg->size) return -1;

    uint64_t to_write = seg->size - file->position;
    if ((uint64_t)size < to_write) to_write = size;

    uint64_t write_accum = 0;
    while (write_accum < to_write) {
        uint64_t current_pos = file->position + write_accum;
        uint32_t page_idx = (uint32_t)(current_pos / 4096);
        uint32_t page_off = (uint32_t)(current_pos % 4096);

        uint64_t chunk = 4096 - page_off;
        if (chunk > to_write - write_accum) chunk = to_write - write_accum;

        void *page_vaddr = (void *)p2v(seg->phys_pages[page_idx]);
        memcpy((uint8_t*)page_vaddr + page_off, (const uint8_t*)buf + write_accum, chunk);

        write_accum += chunk;
    }
    file->position += to_write;
    return (int)to_write;
}

int dev_shm_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (file->device_type != DEVICE_TYPE_SHM) return -1;

    shm_segment_t *seg = (shm_segment_t *)file->fs_handle;
    if (!seg) return -1;
    uint64_t new_pos = file->position;
    if (whence == 0) {
        if (offset < 0) return -1;
        new_pos = (uint64_t)offset;
    } else if (whence == 1) {
        if ((int64_t)new_pos + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)new_pos + offset);
    } else if (whence == 2) {
        if ((int64_t)seg->size + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)seg->size + offset);
    } else return -1;

    if (new_pos > seg->size) new_pos = seg->size;
    file->position = new_pos;
    return 0;
}

uint64_t dev_shm_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return 0;
    shm_segment_t *seg = (shm_segment_t *)file->fs_handle;
    return seg ? (uint64_t)seg->size : 0;
}

bool dev_shm_delete(const char *dev) {
    if (str_starts_with(dev, "shm/")) {
        const char *shm_name = dev + 4;
        if (shm_name[0] != '\0') {
            shm_unlink(shm_name);
            return true;
        }
    }
    return false;
}

bool dev_shm_exists(const char *dev) {
    if (strcmp(dev, "shm") == 0) return true;
    if (str_starts_with(dev, "shm/")) {
        return shm_exists(dev + 4);
    }
    return false;
}

int dev_shm_list_entries(vfs_dirent_t *entries, int max, int count) {
    if (count < max) {
        strcpy(entries[count].name, "shm");
        entries[count].size = 0;
        entries[count].is_directory = 1;
        count++;
    }
    return count;
}

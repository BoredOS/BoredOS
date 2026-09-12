// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "vfs_internal.h"

vfs_mount_t mounts[VFS_MAX_MOUNTS];
int mount_count = 0;
vfs_file_t *open_files_head = NULL;
spinlock_t vfs_lock = SPINLOCK_INIT;

extern void serial_write(const char *str);

vfs_file_t* vfs_alloc_file(void) {
    vfs_file_t *vf = (vfs_file_t *)kmalloc(sizeof(vfs_file_t));
    if (!vf) {
        serial_write("[VFS] Out of memory allocating file handle\n");
        return NULL;
    }
    memset(vf, 0, sizeof(vfs_file_t));
    vf->valid = true;
    vf->next = open_files_head;
    vf->prev = NULL;
    if (open_files_head) {
        open_files_head->prev = vf;
    }
    open_files_head = vf;
    return vf;
}

void vfs_free_file(vfs_file_t *f) {
    if (!f) return;
    if (f->prev) {
        f->prev->next = f->next;
    } else if (open_files_head == f) {
        open_files_head = f->next;
    }
    if (f->next) {
        f->next->prev = f->prev;
    }
    f->valid = false;
    kfree(f);
}

void vfs_init(void) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = false;
        mounts[i].ops = NULL;
        mounts[i].fs_private = NULL;
    }
    mount_count = 0;
    open_files_head = NULL;
    spinlock_release_irqrestore(&vfs_lock, flags);

    serial_write("[VFS] Initialized\n");
}

vfs_file_t* vfs_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    // Check device nodes (/dev/...)
    if (str_starts_with(normalized, "/dev/")) {
        const char *devname = normalized + 5;
        vfs_file_t *dev_file = vfs_dev_open(devname, mode);
        if (dev_file) {
            strncpy(dev_file->path, normalized, VFS_MAX_PATH - 1);
            dev_file->path[VFS_MAX_PATH - 1] = '\0';
            return dev_file;
        }
    }

    if (vfs_is_directory(normalized)) {
        const char *rel_path = NULL;
        vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

        uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
        vfs_file_t *vf = vfs_alloc_file();
        if (!vf) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            return NULL;
        }
        vf->mount = mount;
        strncpy(vf->path, normalized, VFS_MAX_PATH - 1);
        vf->path[VFS_MAX_PATH - 1] = '\0';
        vf->position = 0;
        spinlock_release_irqrestore(&vfs_lock, flags);

        if (mount && mount->ops && mount->ops->open) {
            vf->fs_handle = mount->ops->open(mount->fs_private, (rel_path && rel_path[0]) ? rel_path : "/", mode);
        }
        return vf;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (!mount || !mount->ops->open) {
        return NULL;
    }

    if (!rel_path || rel_path[0] == '\0') {
        rel_path = "/";
    }

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    vfs_file_t *vf = vfs_alloc_file();
    if (!vf) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: No free file handles\n");
        return NULL;
    }

    vf->mount = mount;
    strncpy(vf->path, normalized, VFS_MAX_PATH - 1);
    vf->path[VFS_MAX_PATH - 1] = '\0';
    spinlock_release_irqrestore(&vfs_lock, flags);

    void *fs_handle = mount->ops->open(mount->fs_private, rel_path, mode);
    if (!fs_handle) {
        flags = spinlock_acquire_irqsave(&vfs_lock);
        vfs_free_file(vf);
        spinlock_release_irqrestore(&vfs_lock, flags);
        return NULL;
    }

    vf->fs_handle = fs_handle;
    return vf;
}

void vfs_close(vfs_file_t *file) {
    if (!file || !file->valid) return;

    if (file->is_device) {
        vfs_dev_close(file);
    } else {
        vfs_mount_t *mount = file->mount;
        if (mount && mount->ops->close && file->fs_handle) {
            mount->ops->close(mount->fs_private, file->fs_handle);
        }
    }

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    vfs_free_file(file);
    spinlock_release_irqrestore(&vfs_lock, flags);
}

int vfs_read(vfs_file_t *file, void *buf, size_t size) {
    if (VFS_FILE_INVALID(file)) return -1;

    if (file->is_device) {
        return vfs_dev_read(file, buf, size);
    }

    if (!file->mount->ops->read) return -1;
    int ret = file->mount->ops->read(file->mount->fs_private, file->fs_handle, buf, size);
    if (ret > 0) file->position += ret;
    return ret;
}

int vfs_write(vfs_file_t *file, const void *buf, size_t size) {
    if (VFS_FILE_INVALID(file)) return -1;

    if (file->is_device) {
        return vfs_dev_write(file, buf, size);
    }

    if (!file->mount->ops->write) return -1;
    return file->mount->ops->write(file->mount->fs_private, file->fs_handle, buf, size);
}

int vfs_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (VFS_FILE_INVALID(file)) return -1;

    if (file->is_device) {
        return vfs_dev_ioctl(file, request, arg);
    }

    if (file->mount->ops->ioctl) {
        return file->mount->ops->ioctl(file->mount->fs_private, file->fs_handle, request, arg);
    }

    return -1;
}

int vfs_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (VFS_FILE_INVALID(file)) return -1;

    if (file->is_device) {
        return vfs_dev_seek(file, offset, whence);
    }

    if (!file->mount || !file->mount->ops || !file->mount->ops->seek) return -1;
    int ret = file->mount->ops->seek(file->mount->fs_private, file->fs_handle, (int)offset, whence);
    if (ret >= 0) {
        // Sync position back from driver if possible
        if (file->mount->ops->get_position) {
            file->position = file->mount->ops->get_position(file->fs_handle);
        } else {
            // Manual sync if driver doesn't support get_position but seek succeeded
            if (whence == 0) file->position = offset;
            else if (whence == 1) file->position += offset;
        }
    }
    return ret;
}

int vfs_poll(vfs_file_t *file, struct poll_table *pt) {
    if (VFS_FILE_INVALID(file)) return POLLNVAL;

    if (file->is_device) {
        return vfs_dev_poll(file, pt);
    }

    if (!file->mount->ops->poll) {
        return POLLIN | POLLOUT;
    }
    return file->mount->ops->poll(file->mount->fs_private, file->fs_handle, pt);
}

uint64_t vfs_file_position(vfs_file_t *file) {
    if (VFS_FILE_INVALID(file)) return 0;
    if (file->is_device) return file->position;
    if (!file->mount->ops->get_position) return 0;
    return (uint64_t)file->mount->ops->get_position(file->fs_handle);
}

uint64_t vfs_file_size(vfs_file_t *file) {
    if (VFS_FILE_INVALID(file)) return 0;
    if (file->is_device) {
        return vfs_dev_file_size(file);
    }
    if (!file->mount->ops->get_size) return 0;
    return (uint64_t)file->mount->ops->get_size(file->fs_handle);
}

int vfs_list_directory(const char *path, vfs_dirent_t *entries, int max, int offset) {
    if (!path || !entries) return -1;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (strcmp(normalized, "/dev") == 0) {
        vfs_dirent_t all_devs[256];
        int total = vfs_dev_list_entries(all_devs, 256, 0);
        int dev_count = 0;
        for (int i = offset; i < total && dev_count < max; i++) {
            entries[dev_count++] = all_devs[i];
        }
        return dev_count;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    int count = 0;
    if (mount && mount->ops->readdir) {
        if (!rel_path || rel_path[0] == '\0') rel_path = "/";
        count = mount->ops->readdir(mount->fs_private, rel_path, entries, max, offset);
        if (count < 0) count = 0;
    }

    if (offset == 0) {
        uint64_t v_flags = spinlock_acquire_irqsave(&vfs_lock);
        for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
            if (!mounts[i].active) continue;
            if (strcmp(mounts[i].path, normalized) == 0) continue;

            if (vfs_path_is_parent(normalized, mounts[i].path)) {
                const char *sub = mounts[i].path + strlen(normalized);
                if (*sub == '/') sub++;

                if (*sub != '\0') {
                    char comp[VFS_MAX_NAME];
                    int j = 0;
                    while (sub[j] && sub[j] != '/' && j < VFS_MAX_NAME - 1) {
                        comp[j] = sub[j];
                        j++;
                    }
                    comp[j] = 0;

                    bool found = false;
                    for (int k = 0; k < count; k++) {
                        if (strcmp(entries[k].name, comp) == 0) {
                            found = true;
                            break;
                        }
                    }

                    if (!found && count < max) {
                        strcpy(entries[count].name, comp);
                        entries[count].is_directory = 1;
                        entries[count].size = 0;
                        entries[count].start_cluster = 0;
                        count++;
                    }
                }
            }
        }
        spinlock_release_irqrestore(&vfs_lock, v_flags);

        // Special case: Ensure "dev", "sys", "proc" are visible in "/"
        if (strcmp(normalized, "/") == 0) {
            const char *virtual_dirs[] = {"dev", "sys", "proc"};
            for (int v = 0; v < 3; v++) {
                bool found = false;
                for (int i = 0; i < count; i++) {
                    if (strcmp(entries[i].name, virtual_dirs[v]) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found && count < max) {
                    strcpy(entries[count].name, virtual_dirs[v]);
                    entries[count].is_directory = 1;
                    entries[count].size = 0;
                    entries[count].start_cluster = 0;
                    count++;
                }
            }
        }
    }

    return count;
}

bool vfs_mkdir(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (str_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false;
        }
    }

    if (!mount || !mount->ops->mkdir) return false;
    return mount->ops->mkdir(mount->fs_private, rel_path);
}

bool vfs_rmdir(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (strcmp(normalized, "/dev") == 0) return false;

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (str_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false;
        }
    }

    if (!mount || !mount->ops->rmdir) return false;
    return mount->ops->rmdir(mount->fs_private, rel_path);
}

bool vfs_delete(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (strcmp(normalized, "/dev") == 0) return false;

    if (str_starts_with(normalized, "/dev/")) {
        const char *devname = normalized + 5;
        if (vfs_dev_delete(devname)) return true;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (str_starts_with(normalized, "/dev/")) {
        if (!mount || !rel_path || rel_path[0] == '\0') {
            return false;
        }
    }

    if (!mount || !mount->ops->unlink) return false;
    return mount->ops->unlink(mount->fs_private, rel_path);
}

bool vfs_rename(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return false;

    char norm_old[VFS_MAX_PATH], norm_new[VFS_MAX_PATH];
    vfs_normalize_process_path(old_path, norm_old);
    vfs_normalize_process_path(new_path, norm_new);

    const char *rel_old = NULL, *rel_new = NULL;
    vfs_mount_t *mount_old = vfs_resolve_mount(norm_old, &rel_old);
    vfs_mount_t *mount_new = vfs_resolve_mount(norm_new, &rel_new);

    if (!mount_old || mount_old != mount_new) return false;
    if (!mount_old->ops->rename) return false;

    if (!rel_old || rel_old[0] == '\0') return false;
    if (!rel_new || rel_new[0] == '\0') return false;

    return mount_old->ops->rename(mount_old->fs_private, rel_old, rel_new);
}

bool vfs_exists(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && str_starts_with(mounts[i].path, normalized)) {
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    if (strcmp(normalized, "/dev") == 0 ||
        strcmp(normalized, "/sys") == 0 ||
        strcmp(normalized, "/proc") == 0) return true;

    if (str_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        if (vfs_dev_exists(dev)) return true;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->exists) return false;

    if (!rel_path || rel_path[0] == '\0') return true;

    return mount->ops->exists(mount->fs_private, rel_path);
}

bool vfs_is_directory(const char *path) {
    if (!path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && vfs_path_is_parent(normalized, mounts[i].path)) {
            if (strcmp(mounts[i].path, normalized) == 0) {
                spinlock_release_irqrestore(&vfs_lock, flags_vfs);
                return true;
            }
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    if (strcmp(normalized, "/dev") == 0 ||
        strcmp(normalized, "/dev/net") == 0 ||
        strcmp(normalized, "/dev/shm") == 0 ||
        strcmp(normalized, "/dev/pts") == 0 ||
        strcmp(normalized, "/sys") == 0 ||
        strcmp(normalized, "/proc") == 0) return true;

    if (str_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        if (!vfs_dev_is_directory(dev)) return false;
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount) return false;

    if (!rel_path || rel_path[0] == '\0') return true;

    if (!mount->ops->is_dir) return false;
    return mount->ops->is_dir(mount->fs_private, rel_path);
}

int vfs_statfs(const char *path, vfs_statfs_t *stat) {
    if (!path || !stat) return -1;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount) return -1;

    if (mount->ops->statfs) {
        return mount->ops->statfs(mount->fs_private, stat);
    }

    stat->total_blocks = 0;
    stat->free_blocks = 0;
    stat->block_size = 512;
    return 0;
}

int vfs_get_info(const char *path, vfs_dirent_t *info) {
    if (!path || !info) return -1;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_process_path(path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') {
        strcpy(info->name, "/");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    if (strcmp(normalized, "/dev") == 0 ||
        strcmp(normalized, "/sys") == 0 ||
        strcmp(normalized, "/proc") == 0) {
        const char *name = normalized + 1;
        strcpy(info->name, name);
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && vfs_path_is_parent(normalized, mounts[i].path)) {
            if (strcmp(mounts[i].path, normalized) != 0) {
                const char *p = normalized + strlen(normalized);
                while (p > normalized && *(p-1) != '/') p--;
                strcpy(info->name, p);
                info->size = 0;
                info->is_directory = 1;
                info->start_cluster = 0;
                info->write_date = 0;
                info->write_time = 0;
                spinlock_release_irqrestore(&vfs_lock, flags_vfs);
                return 0;
            }
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    // Device check
    if (str_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        if (vfs_dev_get_info(dev, info) == 0) {
            return 0;
        }
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->get_info) return -1;

    if (!rel_path || rel_path[0] == '\0') {
        // Info about mount root
        strcpy(info->name, mount->device);
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    return mount->ops->get_info(mount->fs_private, rel_path, info);
}

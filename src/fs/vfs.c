// Copyright (c) 2023-2026 Chris (boreddevnl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "vfs.h"
#include "memory_manager.h"
#include "spinlock.h"
#include <stddef.h>
#include "disk.h"
#include "process.h"


static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static int mount_count = 0;
static vfs_file_t open_files[VFS_MAX_OPEN_FILES];
static spinlock_t vfs_lock = SPINLOCK_INIT;

extern void serial_write(const char *str);
extern void serial_write_num(uint64_t num);

static int vfs_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void vfs_strcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

static int vfs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int vfs_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static bool vfs_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return false;
    }
    return true;
}

static bool vfs_path_is_parent(const char *parent, const char *child) {
    int plen = vfs_strlen(parent);
    if (vfs_strncmp(parent, child, plen) != 0) return false;
    if (child[plen] == '\0') return true;
    if (child[plen] == '/') return true;
    if (plen == 1 && parent[0] == '/') return true;
    return false;
}

void vfs_normalize_path(const char *cwd, const char *path, char *normalized) {
    char parts[32][64]; // Reduced size to save stack, 64 is enough for most names
    int depth = 0;
    int i = 0;

    // Handle relative path by starting with CWD
    if (path[0] != '/' && cwd) {
        int ci = 0;
        if (cwd[0] == '/') ci = 1;
        while (cwd[ci]) {
            if (cwd[ci] == '/') { ci++; continue; }
            int j = 0;
            while (cwd[ci] && cwd[ci] != '/' && j < 63) {
                parts[depth][j++] = cwd[ci++];
            }
            parts[depth][j] = 0;
            if (j > 0) depth++;
            if (depth >= 32) break;
            if (cwd[ci] == '/') ci++;
        }
    }

    if (path[0] == '/') i = 1;

    while (path[i]) {
        if (path[i] == '/') { i++; continue; }

        int j = 0;
        while (path[i] && path[i] != '/' && j < 63) {
            parts[depth][j++] = path[i++];
        }
        parts[depth][j] = 0;

        if (parts[depth][0] == '.' && parts[depth][1] == 0) {
            // "." skip
        } else if (parts[depth][0] == '.' && parts[depth][1] == '.' && parts[depth][2] == 0) {
            // ".." pop
            if (depth > 0) depth--;
        } else {
            if (j > 0) {
                depth++;
                if (depth >= 32) break;
            }
        }

        if (path[i] == '/') i++;
    }

    normalized[0] = '/';
    int pos = 1;
    for (int k = 0; k < depth; k++) {
        int l = 0;
        while (parts[k][l] && pos < VFS_MAX_PATH - 2) {
            normalized[pos++] = parts[k][l++];
        }
        if (k < depth - 1) normalized[pos++] = '/';
    }
    normalized[pos] = 0;

    if (pos == 1 && normalized[0] == '/') {
        normalized[1] = 0;
    }
}

static vfs_mount_t* vfs_resolve_mount(const char *path, const char **rel_path_out) {
    vfs_mount_t *best = NULL;
    int best_len = -1;

    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;

        int mlen = mounts[i].path_len;

        if (mlen == 1 && mounts[i].path[0] == '/') {
            if (best_len < 1) {
                best = &mounts[i];
                best_len = 1;
            }
            continue;
        }

        if (vfs_strncmp(path, mounts[i].path, mlen) == 0) {
            if (path[mlen] == '/' || path[mlen] == '\0') {
                if (mlen > best_len) {
                    best = &mounts[i];
                    best_len = mlen;
                }
            }
        }
    }

    if (best && rel_path_out) {
        const char *rel = path + best_len;
        while (*rel == '/') rel++;
        *rel_path_out = rel;
    }

    return best;
}

static vfs_file_t* vfs_alloc_file(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].valid) {
            open_files[i].valid = true;
            open_files[i].fs_handle = NULL;
            open_files[i].mount = NULL;
            open_files[i].position = 0;
            open_files[i].is_device = false;
            return &open_files[i];
        }
    }
    return NULL;
}

static void vfs_free_file(vfs_file_t *f) {
    if (f) {
        f->valid = false;
        f->fs_handle = NULL;
        f->mount = NULL;
        f->position = 0;
        f->is_device = false;
    }
}

void vfs_init(void) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        mounts[i].active = false;
    }
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        open_files[i].valid = false;
    }
    mount_count = 0;

    serial_write("[VFS] Ready\n");
}

// ===============
// Mount / Unmount
// ===============

bool vfs_mount(const char *mount_path, const char *device, const char *fs_type,
               vfs_fs_ops_t *ops, void *fs_private) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    if (mount_count >= VFS_MAX_MOUNTS) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: Mount table full\n");
        return false;
    }

    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_strcmp(mounts[i].path, mount_path) == 0) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            serial_write("[VFS] ERROR: Mount point already in use: ");
            serial_write(mount_path);
            serial_write("\n");
            return false;
        }
    }

    vfs_mount_t *m = &mounts[mount_count];
    vfs_strcpy(m->path, mount_path);
    m->path_len = vfs_strlen(mount_path);
    m->ops = ops;
    m->fs_private = fs_private;
    vfs_strcpy(m->device, device ? device : "none");
    vfs_strcpy(m->fs_type, fs_type ? fs_type : "unknown");
    m->active = true;
    mount_count++;

    spinlock_release_irqrestore(&vfs_lock, flags);

    serial_write("[VFS] Mounted ");
    serial_write(fs_type);
    serial_write(" (");
    serial_write(device ? device : "none");
    serial_write(") at ");
    serial_write(mount_path);
    serial_write("\n");

    return true;
}

bool vfs_umount(const char *mount_path) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_strcmp(mounts[i].path, mount_path) == 0) {
            for (int j = 0; j < VFS_MAX_OPEN_FILES; j++) {
                if (open_files[j].valid && open_files[j].mount == &mounts[i]) {
                    if (mounts[i].ops->close) {
                        mounts[i].ops->close(mounts[i].fs_private, open_files[j].fs_handle);
                    }
                    vfs_free_file(&open_files[j]);
                }
            }

            serial_write("[VFS] Unmounted ");
            serial_write(mounts[i].path);
            serial_write("\n");

            mounts[i].active = false;

            // Compact array
            for (int k = i; k < mount_count - 1; k++) {
                mounts[k] = mounts[k + 1];
            }
            mount_count--;

            spinlock_release_irqrestore(&vfs_lock, flags);
            return true;
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);
    return false;
}

// ==============
// File Operations
// ==============

vfs_file_t* vfs_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    char normalized[VFS_MAX_PATH];
    process_t *proc = process_get_current();
    vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    
    // Fallback for block devices (/dev/sda etc)
    if (vfs_starts_with(normalized, "/dev/")) {
        const char *devname = normalized + 5;
        Disk *d = disk_get_by_name(devname);
        if (d && (!mount || mount->path_len == 1)) {
            vfs_file_t *vf = vfs_alloc_file();
            if (vf) {
                vf->mount = &mounts[0];
                vf->fs_handle = (void*)d; 
                vf->is_device = true;
                vf->position = 0;
                spinlock_release_irqrestore(&vfs_lock, flags);
                return vf;
            }
        }
    }
    
    if (!mount || !mount->ops->open) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        return NULL;
    }

    if (!rel_path || rel_path[0] == '\0') {
        rel_path = "/";
    }

    vfs_file_t *vf = vfs_alloc_file();
    if (!vf) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: No free file handles\n");
        return NULL;
    }

    vf->mount = mount;

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

    vfs_mount_t *mount = file->mount;
    if (mount && mount->ops->close) {
        mount->ops->close(mount->fs_private, file->fs_handle);
    }

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    vfs_free_file(file);
    spinlock_release_irqrestore(&vfs_lock, flags);
}

int vfs_read(vfs_file_t *file, void *buf, int size) {
    if (!file || !file->valid || !file->mount) return -1;
    
    if (file->is_device) {
        Disk *d = (Disk*)file->fs_handle;
        if (!d) return -1;
        
        uint32_t total_read = 0;
        uint32_t sector = (uint32_t)(file->position / 512);
        uint32_t offset = (uint32_t)(file->position % 512);
        uint8_t sector_buf[512];
        
        while (total_read < (uint32_t)size) {
            if (sector >= d->total_sectors) break;
            if (d->read_sector(d, sector, sector_buf) != 0) break;
            
            uint32_t to_copy = 512 - offset;
            if (to_copy > (uint32_t)size - total_read) to_copy = (uint32_t)size - total_read;
            
            extern void mem_memcpy(void *dest, const void *src, size_t len);
            mem_memcpy((uint8_t*)buf + total_read, sector_buf + offset, to_copy);
            
            total_read += to_copy;
            file->position += to_copy;
            sector++;
            offset = 0;
        }
        return (int)total_read;
    }

    if (!file->mount->ops->read) return -1;
    int ret = file->mount->ops->read(file->mount->fs_private, file->fs_handle, buf, size);
    if (ret > 0) file->position += ret;
    return ret;
}

int vfs_write(vfs_file_t *file, const void *buf, int size) {
    if (!file || !file->valid || !file->mount) return -1;
    if (!file->mount->ops->write) return -1;

    return file->mount->ops->write(file->mount->fs_private, file->fs_handle, buf, size);
}

int vfs_seek(vfs_file_t *file, int offset, int whence) {
    if (!file || !file->valid || !file->mount) return -1;
    
    if (file->is_device) {
        Disk *d = (Disk*)file->fs_handle;
        if (!d) return -1;
        uint64_t new_pos = file->position;
        if (whence == 0) new_pos = (uint64_t)offset; // SET
        else if (whence == 1) new_pos += (uint64_t)offset; // CUR
        else if (whence == 2) new_pos = (uint64_t)(d->total_sectors * 512 + offset); // END
        
        if (new_pos > (uint64_t)d->total_sectors * 512) new_pos = (uint64_t)d->total_sectors * 512;
        file->position = new_pos;
        return 0;
    }

    if (!file->mount->ops->seek) return -1;
    int ret = file->mount->ops->seek(file->mount->fs_private, file->fs_handle, offset, whence);
    if (ret == 0) {
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

uint32_t vfs_file_position(vfs_file_t *file) {
    if (!file || !file->valid || !file->mount) return 0;
    if (file->is_device) return (uint32_t)file->position;
    if (!file->mount->ops->get_position) return 0;
    return file->mount->ops->get_position(file->fs_handle);
}

uint32_t vfs_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->mount) return 0;
    if (file->is_device) {
        Disk *d = (Disk*)file->fs_handle;
        return d ? d->total_sectors * 512 : 0;
    }
    if (!file->mount->ops->get_size) return 0;
    return file->mount->ops->get_size(file->fs_handle);
}



int vfs_list_directory(const char *path, vfs_dirent_t *entries, int max) {
    if (!path || !entries) return -1;
    

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path("/", path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    
    int count = 0;
    if (mount && mount->ops->readdir) {
        if (!rel_path || rel_path[0] == '\0') rel_path = "/";
        count = mount->ops->readdir(mount->fs_private, rel_path, entries, max);
        if (count < 0) count = 0; 
    }

    uint64_t v_flags = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        if (vfs_strcmp(mounts[i].path, normalized) == 0) continue; 

        if (vfs_path_is_parent(normalized, mounts[i].path)) {
            const char *sub = mounts[i].path + vfs_strlen(normalized);
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
                    if (vfs_strcmp(entries[k].name, comp) == 0) {
                        found = true;
                        break;
                    }
                }

                if (!found && count < max) {
                    vfs_strcpy(entries[count].name, comp);
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
    if (vfs_strcmp(normalized, "/") == 0) {
        const char *virtual_dirs[] = {"dev", "sys", "proc"};
        for (int v = 0; v < 3; v++) {
            bool found = false;
            for (int i = 0; i < count; i++) {
                if (vfs_strcmp(entries[i].name, virtual_dirs[v]) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found && count < max) {
                vfs_strcpy(entries[count].name, virtual_dirs[v]);
                entries[count].is_directory = 1;
                entries[count].size = 0;
                entries[count].start_cluster = 0;
                count++;
            }
        }
    }

    // Special case: /dev listing for block devices
    if (vfs_strcmp(normalized, "/dev") == 0) {
        int dcount = disk_get_count();
        for (int i = 0; i < dcount && count < max; i++) {
            Disk *d = disk_get_by_index(i);
            if (d) {
                bool found = false;
                for (int k = 0; k < count; k++) {
                    if (vfs_strcmp(entries[k].name, d->devname) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    vfs_strcpy(entries[count].name, d->devname);
                    entries[count].size = d->total_sectors * 512;
                    entries[count].is_directory = 0;
                    entries[count].start_cluster = 0;
                    entries[count].write_date = 0;
                    entries[count].write_time = 0;
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
    vfs_normalize_path("/", path, normalized);

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (vfs_starts_with(normalized, "/dev/")) {
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
    vfs_normalize_path("/", path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (vfs_strcmp(normalized, "/dev") == 0) return false;

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (vfs_starts_with(normalized, "/dev/")) {
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
    vfs_normalize_path("/", path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return false;
    if (vfs_strcmp(normalized, "/dev") == 0) return false;

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);

    if (vfs_starts_with(normalized, "/dev/")) {
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
    vfs_normalize_path("/", old_path, norm_old);
    vfs_normalize_path("/", new_path, norm_new);

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
    vfs_normalize_path("/", path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_starts_with(mounts[i].path, normalized)) {
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    if (vfs_strcmp(normalized, "/dev") == 0 || 
        vfs_strcmp(normalized, "/sys") == 0 || 
        vfs_strcmp(normalized, "/proc") == 0) return true;

    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        if (disk_get_by_name(dev)) return true;
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
    vfs_normalize_path("/", path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') return true;

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_path_is_parent(normalized, mounts[i].path)) {
            if (vfs_strcmp(mounts[i].path, normalized) == 0) {
                spinlock_release_irqrestore(&vfs_lock, flags_vfs);
                return true;
            }
            // If normalized is a parent of a mount, it's a virtual directory
            spinlock_release_irqrestore(&vfs_lock, flags_vfs);
            return true;
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags_vfs);

    if (vfs_strcmp(normalized, "/dev") == 0 || 
        vfs_strcmp(normalized, "/sys") == 0 || 
        vfs_strcmp(normalized, "/proc") == 0) return true;

    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        Disk *d = disk_get_by_name(dev);
        if (d) return false;
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
    vfs_normalize_path("/", path, normalized);
    
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
    vfs_normalize_path("/", path, normalized);

    if (normalized[0] == '/' && normalized[1] == '\0') {
        vfs_strcpy(info->name, "/");
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    if (vfs_strcmp(normalized, "/dev") == 0 || 
        vfs_strcmp(normalized, "/sys") == 0 || 
        vfs_strcmp(normalized, "/proc") == 0) {
        const char *name = normalized + 1;
        vfs_strcpy(info->name, name);
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    uint64_t flags_vfs = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active && vfs_path_is_parent(normalized, mounts[i].path)) {
            if (vfs_strcmp(mounts[i].path, normalized) != 0) {
                const char *p = normalized + vfs_strlen(normalized);
                while (p > normalized && *(p-1) != '/') p--;
                vfs_strcpy(info->name, p);
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
    if (vfs_starts_with(normalized, "/dev/")) {
        const char *dev = normalized + 5;
        Disk *d = disk_get_by_name(dev);
        if (d) {
            vfs_strcpy(info->name, d->devname);
            info->size = d->total_sectors * 512;
            info->is_directory = 0;
            info->start_cluster = 0;
            info->write_date = 0;
            info->write_time = 0;
            return 0;
        }
    }

    const char *rel_path = NULL;
    vfs_mount_t *mount = vfs_resolve_mount(normalized, &rel_path);
    if (!mount || !mount->ops->get_info) return -1;

    if (!rel_path || rel_path[0] == '\0') {
        // Info about mount root
        vfs_strcpy(info->name, mount->device);
        info->size = 0;
        info->is_directory = 1;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }

    return mount->ops->get_info(mount->fs_private, rel_path, info);
}

int vfs_get_mount_count(void) {
    return mount_count;
}

vfs_mount_t* vfs_get_mount(int index) {
    if (index < 0 || index >= mount_count) return NULL;
    if (!mounts[index].active) return NULL;
    return &mounts[index];
}

void vfs_automount_partition(const char *devname) {
    char mount_path[64] = "/mnt/";
    int i = 5;
    const char *d = devname;
    while (*d && i < 62) mount_path[i++] = *d++;
    mount_path[i] = 0;

    serial_write("[VFS] Auto-mount requested for ");
    serial_write(devname);
    serial_write(" at ");
    serial_write(mount_path);
    serial_write("\n");
}

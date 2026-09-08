// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "vfs_internal.h"

extern void serial_write(const char *str);

bool vfs_mount(const char *mount_path, const char *device, const char *fs_type,
               vfs_fs_ops_t *ops, void *fs_private) {
    if (!mount_path || !ops) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path("/", mount_path, normalized);

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && strcmp(mounts[i].path, normalized) == 0) {
            mounts[i].ops = ops;
            mounts[i].fs_private = fs_private;
            if (device) strncpy(mounts[i].device, device, 31);
            if (fs_type) strncpy(mounts[i].fs_type, fs_type, 15);
            mounts[i].path_len = strlen(normalized);
            spinlock_release_irqrestore(&vfs_lock, flags);

            serial_write("[VFS] Updated mount: ");
            serial_write(normalized);
            serial_write(" (");
            serial_write(fs_type ? fs_type : "unknown");
            serial_write(")\n");
            return true;
        }
    }

    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        spinlock_release_irqrestore(&vfs_lock, flags);
        serial_write("[VFS] ERROR: Maximum mounts reached\n");
        return false;
    }

    strncpy(mounts[slot].path, normalized, 255);
    mounts[slot].path_len = strlen(normalized);
    mounts[slot].ops = ops;
    mounts[slot].fs_private = fs_private;
    mounts[slot].active = true;

    if (device) strncpy(mounts[slot].device, device, 31);
    else mounts[slot].device[0] = '\0';

    if (fs_type) strncpy(mounts[slot].fs_type, fs_type, 15);
    else mounts[slot].fs_type[0] = '\0';

    mount_count++;
    spinlock_release_irqrestore(&vfs_lock, flags);

    serial_write("[VFS] Mounted ");
    serial_write(device ? device : "none");
    serial_write(" at ");
    serial_write(normalized);
    serial_write(" (type: ");
    serial_write(fs_type ? fs_type : "unknown");
    serial_write(")\n");

    return true;
}

bool vfs_umount(const char *mount_path) {
    if (!mount_path) return false;

    char normalized[VFS_MAX_PATH];
    vfs_normalize_path("/", mount_path, normalized);

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && strcmp(mounts[i].path, normalized) == 0) {
            for (int f = 0; f < VFS_MAX_OPEN_FILES; f++) {
                if (open_files[f].valid && open_files[f].mount == &mounts[i]) {
                    spinlock_release_irqrestore(&vfs_lock, flags);
                    serial_write("[VFS] Cannot unmount: files still open\n");
                    return false;
                }
            }

            if (mounts[i].ops && mounts[i].ops->unmount) {
                mounts[i].ops->unmount(mounts[i].fs_private);
            }

            mounts[i].active = false;
            mounts[i].ops = NULL;
            mounts[i].fs_private = NULL;
            mount_count--;

            spinlock_release_irqrestore(&vfs_lock, flags);

            serial_write("[VFS] Unmounted: ");
            serial_write(normalized);
            serial_write("\n");
            return true;
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);
    return false;
}

int vfs_get_mount_count(void) {
    return VFS_MAX_MOUNTS;
}

vfs_mount_t* vfs_get_mount(int index) {
    if (index < 0 || index >= VFS_MAX_MOUNTS) return NULL;
    if (!mounts[index].active) return NULL;
    return &mounts[index];
}

int vfs_sync_all(void) {
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].active && mounts[i].ops && mounts[i].ops->sync_fs) {
            mounts[i].ops->sync_fs(mounts[i].fs_private);
        }
    }
    spinlock_release_irqrestore(&vfs_lock, flags);
    return 0;
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

// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef VFS_INTERNAL_H
#define VFS_INTERNAL_H

#include "vfs.h"
#include "shm.h"
#include "slab.h"
#include "spinlock.h"
#include <stddef.h>
#include <string.h>
#include "disk.h"
#include "process.h"
#include "tty.h"
#include "kutils.h"
#include "graphics.h"
#include "pcsk.h"
#include "ac97.h"

#define VFS_FILE_INVALID(f) (!(f) || !(f)->valid || (!(f)->is_device && (!(f)->mount || !(f)->mount->active)))

extern vfs_mount_t mounts[VFS_MAX_MOUNTS];
extern int mount_count;
extern vfs_file_t *open_files_head;
extern spinlock_t vfs_lock;

vfs_file_t* vfs_alloc_file(void);
void vfs_free_file(vfs_file_t *f);

bool vfs_path_is_parent(const char *parent, const char *child);
void vfs_normalize_process_path(const char *path, char *normalized);
vfs_mount_t* vfs_resolve_mount(const char *path, const char **rel_path_out);

vfs_file_t* vfs_dev_open(const char *devname, const char *mode);
void vfs_dev_close(vfs_file_t *file);
int vfs_dev_read(vfs_file_t *file, void *buf, size_t size);
int vfs_dev_write(vfs_file_t *file, const void *buf, size_t size);
int vfs_dev_ioctl(vfs_file_t *file, uint64_t request, void *arg);
int vfs_dev_seek(vfs_file_t *file, int64_t offset, int whence);
int vfs_dev_poll(vfs_file_t *file, struct poll_table *pt);
uint64_t vfs_dev_file_size(vfs_file_t *file);
int vfs_dev_list_entries(vfs_dirent_t *entries, int max, int count);
bool vfs_dev_exists(const char *devname);
bool vfs_dev_is_directory(const char *devname);
bool vfs_dev_delete(const char *devname);
int vfs_dev_get_info(const char *devname, vfs_dirent_t *info);

#endif // VFS_INTERNAL_H

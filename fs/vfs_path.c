// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "vfs_internal.h"

bool vfs_path_is_parent(const char *parent, const char *child) {
    size_t plen = strlen(parent);
    if (strncmp(parent, child, plen) != 0) return false;
    if (child[plen] == '\0') return true;
    if (child[plen] == '/') return true;
    if (plen == 1 && parent[0] == '/') return true;
    return false;
}

void vfs_normalize_path(const char *cwd, const char *path, char *normalized) {
    if (!normalized) return;
    if (!path || path[0] == '\0') {
        if (cwd && cwd[0] != '\0') {
            vfs_normalize_path(NULL, cwd, normalized);
        } else {
            strcpy(normalized, "/");
        }
        return;
    }

    int out = 0;

    if (path[0] == '/') {
        normalized[out++] = '/';
    } else {
        if (cwd && cwd[0] == '/') {
            size_t cwd_len = strlen(cwd);
            if (cwd_len >= VFS_MAX_PATH) cwd_len = VFS_MAX_PATH - 1;
            memcpy(normalized, cwd, cwd_len);
            out = (int)cwd_len;
            if (out > 0 && normalized[out - 1] != '/') {
                if (out < VFS_MAX_PATH - 1) normalized[out++] = '/';
            }
        } else {
            normalized[out++] = '/';
        }
    }

    const char *p = (path[0] == '/') ? (path + 1) : path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        const char *comp_start = p;
        while (*p && *p != '/') p++;
        size_t comp_len = p - comp_start;

        if (comp_len == 1 && comp_start[0] == '.') {
            continue;
        } else if (comp_len == 2 && comp_start[0] == '.' && comp_start[1] == '.') {
            if (out > 1) {
                if (normalized[out - 1] == '/') out--;
                while (out > 1 && normalized[out - 1] != '/') {
                    out--;
                }
            }
        } else {
            if (out > 0 && normalized[out - 1] != '/') {
                if (out < VFS_MAX_PATH - 1) normalized[out++] = '/';
            }
            size_t to_copy = comp_len;
            if (out + (int)to_copy >= VFS_MAX_PATH) {
                to_copy = VFS_MAX_PATH - 1 - out;
            }
            if (to_copy > 0) {
                memcpy(&normalized[out], comp_start, to_copy);
                out += to_copy;
            }
        }
    }

    if (out > 1 && normalized[out - 1] == '/') {
        out--;
    }

    if (out == 0) {
        normalized[out++] = '/';
    }
    normalized[out] = '\0';
}

void vfs_normalize_process_path(const char *path, char *normalized) {
    process_t *proc = process_get_current();
    vfs_normalize_path(proc ? proc->cwd : "/", path, normalized);
}

vfs_mount_t* vfs_resolve_mount(const char *path, const char **rel_path_out) {
    if (!path) return NULL;

    vfs_mount_t *best_mount = NULL;
    int best_len = -1;

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;

        int len = mounts[i].path_len;
        if (strncmp(path, mounts[i].path, len) == 0) {
            if (path[len] == '/' || path[len] == '\0' || len == 1) {
                if (len > best_len) {
                    best_len = len;
                    best_mount = &mounts[i];
                }
            }
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);

    if (best_mount && rel_path_out) {
        if (best_len == 1) {
            *rel_path_out = path; 
        } else {
            *rel_path_out = path + best_len;
            if (**rel_path_out == '\0') {
                *rel_path_out = "/";
            }
        }
    }

    return best_mount;
}

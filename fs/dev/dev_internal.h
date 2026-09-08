// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#ifndef FS_DEV_INTERNAL_H
#define FS_DEV_INTERNAL_H

#include "../vfs_internal.h"
#include "../../graphics/graphics.h"
#include "../../dev/disk.h"
#include "../../dev/ac97.h"
#include "../../core/platform.h"

typedef framebuffer_info_t vfs_framebuffer_info_t;

// --- TTY & Input Sub-Driver (fs/dev/dev_tty.c) ---
vfs_file_t* dev_tty_open(const char *devname, const char *mode);
void        dev_tty_close(vfs_file_t *file);
int         dev_tty_read(vfs_file_t *file, void *buf, size_t size);
int         dev_tty_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_tty_ioctl(vfs_file_t *file, uint64_t request, void *arg);
int         dev_tty_poll(vfs_file_t *file, struct poll_table *pt);
int         dev_tty_list_entries(vfs_dirent_t *entries, int max, int count);
bool        dev_tty_exists(const char *dev);
int         dev_tty_get_info(const char *dev, vfs_dirent_t *info);

// --- Framebuffer Sub-Driver (fs/dev/dev_fb.c) ---
vfs_file_t* dev_fb_open(const char *devname, const char *mode);
int         dev_fb_read(vfs_file_t *file, void *buf, size_t size);
int         dev_fb_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_fb_seek(vfs_file_t *file, int64_t offset, int whence);
int         dev_fb_ioctl(vfs_file_t *file, uint64_t request, void *arg);
uint64_t    dev_fb_file_size(vfs_file_t *file);
int         dev_fb_list_entries(vfs_dirent_t *entries, int max, int count);
bool        dev_fb_exists(const char *dev);
int         dev_fb_get_info(const char *dev, vfs_dirent_t *info);

// --- Audio & RTC Sub-Driver (fs/dev/dev_audio.c) ---
vfs_file_t* dev_audio_open(const char *devname, const char *mode);
void        dev_audio_close(vfs_file_t *file);
int         dev_audio_read(vfs_file_t *file, void *buf, size_t size);
int         dev_audio_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_audio_ioctl(vfs_file_t *file, uint64_t request, void *arg);
int         dev_audio_list_entries(vfs_dirent_t *entries, int max, int count);
bool        dev_audio_exists(const char *dev);

// --- Network/TUN Sub-Driver (fs/dev/dev_net.c) ---
vfs_file_t* dev_net_open(const char *devname, const char *mode);
void        dev_net_close(vfs_file_t *file);
int         dev_net_read(vfs_file_t *file, void *buf, size_t size);
int         dev_net_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_net_ioctl(vfs_file_t *file, uint64_t request, void *arg);
bool        dev_net_exists(const char *dev);
int         dev_net_get_info(const char *dev, vfs_dirent_t *info);

// --- Shared Memory Sub-Driver (fs/dev/dev_shm.c) ---
vfs_file_t* dev_shm_open(const char *devname, const char *mode);
void        dev_shm_close(vfs_file_t *file);
int         dev_shm_read(vfs_file_t *file, void *buf, size_t size);
int         dev_shm_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_shm_seek(vfs_file_t *file, int64_t offset, int whence);
uint64_t    dev_shm_file_size(vfs_file_t *file);
bool        dev_shm_delete(const char *dev);
bool        dev_shm_exists(const char *dev);
int         dev_shm_list_entries(vfs_dirent_t *entries, int max, int count);

// --- Block / Disk Sub-Driver (fs/dev/dev_disk.c) ---
vfs_file_t* dev_disk_open(const char *devname, const char *mode);
int         dev_disk_read(vfs_file_t *file, void *buf, size_t size);
int         dev_disk_write(vfs_file_t *file, const void *buf, size_t size);
int         dev_disk_seek(vfs_file_t *file, int64_t offset, int whence);
uint64_t    dev_disk_file_size(vfs_file_t *file);
int         dev_disk_list_entries(vfs_dirent_t *entries, int max, int count);
bool        dev_disk_exists(const char *dev);
int         dev_disk_get_info(const char *dev, vfs_dirent_t *info);

// --- Random Sub-Driver (fs/dev/dev_random.c) ---
vfs_file_t* dev_random_open(const char *devname, const char *mode);
int         dev_random_read(vfs_file_t *file, void *buf, size_t size);
int         dev_random_write(vfs_file_t *file, const void *buf, size_t size);
bool        dev_random_exists(const char *dev);
int         dev_random_get_info(const char *dev, vfs_dirent_t *info);
int         dev_random_list_entries(vfs_dirent_t *entries, int max, int count);

#endif // FS_DEV_INTERNAL_H

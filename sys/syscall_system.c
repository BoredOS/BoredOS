// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See
// LICENSE file for details. This header needs to maintain in any file it is
// present in, as per the GPL license terms.
#include "syscall_internal.h"
#include "fat32.h"
#undef RB_BLACK
#undef RB_RED
#undef RB_ROOT
#include "ext4fs.h"

typedef struct {
  char devname[16];
  char label[32];
  uint32_t type;
  uint32_t total_sectors;
  bool is_partition;
  bool is_fat32;
  bool is_esp;
  uint32_t lba_offset;
} k_disk_info_t;

typedef struct {
  uint32_t lba_start;
  uint32_t sector_count;
  uint8_t part_type;
  uint8_t flags;
  char label[36];
} k_partition_spec_t;

static void disk_k_strcpy(char *dst, const char *src, int max) {
  int i = 0;
  while (i < max - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

static uint64_t sys_cmd_reboot(const syscall_args_t *args) {
  (void)args;
  k_reboot();
  return 0;
}

static uint64_t sys_cmd_shutdown(const syscall_args_t *args) {
  (void)args;
  k_shutdown();
  return 0;
}

uint64_t sys_cmd_tty_create(const syscall_args_t *args) {
  (void)args;
  return tty_create();
}

uint64_t sys_cmd_tty_get_id(const syscall_args_t *args) {
  (void)args;
  process_t *proc = process_get_current();
  if (!proc)
    return (uint64_t)-1;
  return (uint64_t)proc->tty_id;
}

uint64_t sys_cmd_tty_read_out(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  char *buf = (char *)args->arg3;
  size_t len = (size_t)args->arg4;
  if (!buf || len == 0)
    return 0;
  return tty_read_output(tty_id, buf, len);
}

uint64_t sys_cmd_tty_write_in(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  const char *buf = (const char *)args->arg3;
  size_t len = (size_t)args->arg4;
  if (!buf || len == 0)
    return 0;
  return tty_write_input(tty_id, buf, len);
}

uint64_t sys_cmd_tty_read_in(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  char *buf = (char *)args->arg2;
  size_t len = (size_t)args->arg3;
  if (!buf || len == 0)
    return 0;
  if (proc->tty_id < 0)
    return 0;
  return tty_read_input(proc->tty_id, buf, len);
}

uint64_t sys_cmd_tty_set_fg(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  int pid = (int)args->arg3;
  return tty_set_foreground(tty_id, pid);
}

uint64_t sys_cmd_tty_get_fg(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  return tty_get_foreground(tty_id);
}

uint64_t sys_cmd_tty_kill_fg(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  int pid = tty_get_foreground(tty_id);
  if (pid <= 0)
    return 0;
  process_t *target = process_get_by_pid((uint32_t)pid);
  if (target) {
    process_terminate(target);
    process_put(target);
  }
  tty_set_foreground(tty_id, 0);
  return 0;
}

uint64_t sys_cmd_tty_kill_all(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  process_kill_by_tty(tty_id);
  tty_set_foreground(tty_id, 0);
  return 0;
}

uint64_t sys_cmd_tty_destroy(const syscall_args_t *args) {
  int tty_id = (int)args->arg2;
  return tty_destroy(tty_id);
}

uint64_t sys_cmd_pty_create(const syscall_args_t *args) {
  (void)args;
  return (uint64_t)pty_create();
}

uint64_t sys_cmd_pty_destroy(const syscall_args_t *args) {
  int pty_id = (int)args->arg2;
  return (uint64_t)pty_destroy(pty_id);
}

static uint64_t sys_cmd_disk_get_count(const syscall_args_t *args) {
  (void)args;
  return (uint64_t)disk_get_count();
}

static uint64_t sys_cmd_disk_get_info(const syscall_args_t *args) {
  int index = (int)args->arg2;
  k_disk_info_t *out = (k_disk_info_t *)args->arg3;
  if (!out)
    return (uint64_t)-1;
  Disk *d = disk_get_by_index(index);
  if (!d)
    return (uint64_t)-1;
  disk_k_strcpy(out->devname, d->devname, 16);
  disk_k_strcpy(out->label, d->label, 32);
  out->type = (uint32_t)d->type;
  out->total_sectors = d->total_sectors;
  out->is_partition = d->is_partition;
  out->is_fat32 = d->is_fat32;
  out->is_esp = d->is_esp;
  out->lba_offset = d->partition_lba_offset;
  return 0;
}

static uint64_t sys_cmd_disk_mount(const syscall_args_t *args) {
  const char *devname = (const char *)args->arg2;
  const char *mountpoint = (const char *)args->arg3;
  if (!devname || !mountpoint)
    return (uint64_t)-1;
  Disk *d = disk_get_by_name(devname);
  if (!d)
    return (uint64_t)-1;

  if (d->is_fat32) {
    void *vol = fat32_mount_volume(d);
    if (vol) {
      if (vfs_mount(mountpoint, devname, "fat32", fat32_get_realfs_ops(), vol))
        return 0;
    }
  }

  uint8_t sb_buf[512];
  if (d->read_sector(d, 2, sb_buf) == 0) {
    uint16_t magic = *(uint16_t *)(sb_buf + 56);
    if (magic == 0xEF53) {
      void *vol = ext4fs_mount_volume(d);
      if (vol) {
        if (vfs_mount(mountpoint, devname, "ext4", ext4fs_get_ops(), vol)) {
          d->is_fat32 = false;
          return 0;
        }
      }
    }
  }

  void *vol = ext4fs_mount_volume(d);
  if (vol) {
    if (vfs_mount(mountpoint, devname, "ext4", ext4fs_get_ops(), vol)) {
      d->is_fat32 = false;
      return 0;
    }
  }

  vol = fat32_mount_volume(d);
  if (vol) {
    if (vfs_mount(mountpoint, devname, "fat32", fat32_get_realfs_ops(), vol)) {
      d->is_fat32 = true;
      return 0;
    }
  }

  return (uint64_t)-1;
}

static uint64_t sys_cmd_disk_umount(const syscall_args_t *args) {
  const char *mountpoint = (const char *)args->arg2;
  if (!mountpoint)
    return (uint64_t)-1;
  return vfs_umount(mountpoint) ? 0 : (uint64_t)-1;
}

static uint64_t sys_cmd_disk_rescan(const syscall_args_t *args) {
  const char *devname = (const char *)args->arg2;
  if (!devname)
    return (uint64_t)-1;
  Disk *d = disk_get_by_name(devname);
  if (!d)
    return (uint64_t)-1;
  return (uint64_t)disk_rescan(d);
}

static uint64_t sys_cmd_disk_sync(const syscall_args_t *args) {
  const char *mountpoint = (const char *)args->arg2;
  if (!mountpoint)
    return (uint64_t)-1;
  int mc = vfs_get_mount_count();
  for (int i = 0; i < mc; i++) {
    vfs_mount_t *m = vfs_get_mount(i);
    if (m && m->active && strcmp(m->path, mountpoint) == 0) {
      Disk *d = disk_get_by_name(m->device);
      if (d)
        return (uint64_t)disk_sync(d);
    }
  }
  return (uint64_t)-1;
}

uint64_t handle_sys_reboot(const syscall_args_t *args) {
  int cmd = (int)args->arg3;
  if (cmd == LINUX_REBOOT_CMD_POWER_OFF || cmd == LINUX_REBOOT_CMD_HALT || (args->arg1 == 1 && args->arg2 == 0)) {
    k_shutdown();
  } else {
    k_reboot();
  }
  return 0;
}

uint64_t handle_sys_sysctl(const syscall_args_t *args) {
    extern void kernel_get_hostname(char *buf, size_t max_len);
    extern int kernel_set_hostname(const char *name, size_t len);

    const int *name = (const int *)args->arg1;
    unsigned int nlen = (unsigned int)args->arg2;
    void *oldp = (void *)args->arg3;
    size_t *oldlenp = (size_t *)args->arg4;
    const void *newp = (const void *)args->arg5;
    size_t newlen = (size_t)args->arg6;

    if (!name || nlen < 2) return (uint64_t)-1;

    // CTL_KERN = 1, KERN_HOSTNAME = 10
    if (name[0] == 1 && name[1] == 10) {
        if (oldp && oldlenp && *oldlenp > 0) {
            kernel_get_hostname((char *)oldp, *oldlenp);
            *oldlenp = strlen((char *)oldp);
        }
        if (newp && newlen > 0) {
            return (uint64_t)kernel_set_hostname((const char *)newp, newlen);
        }
        return 0;
    }

    return (uint64_t)-1;
}

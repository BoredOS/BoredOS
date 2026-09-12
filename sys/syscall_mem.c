// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "syscall_internal.h"

typedef struct vfs_page_cache_node {
    void *mount_or_fs;
    void *fs_handle;
    uint64_t offset;
    uintptr_t phys_addr;
    struct vfs_page_cache_node *next;
} vfs_page_cache_node_t;

#define VFS_PAGE_CACHE_BUCKETS 512
static vfs_page_cache_node_t *vfs_page_cache_table[VFS_PAGE_CACHE_BUCKETS] = {NULL};
static spinlock_t vfs_page_cache_lock = SPINLOCK_INIT;

static uintptr_t vfs_get_cached_page(vfs_file_t *file, uint64_t offset, uint64_t file_size) {
    if (!file || !file->fs_handle) return 0;
    uint32_t bucket = (((uintptr_t)file->fs_handle >> 4) ^ (offset >> 12)) % VFS_PAGE_CACHE_BUCKETS;

    uint64_t flags = spinlock_acquire_irqsave(&vfs_page_cache_lock);
    vfs_page_cache_node_t *node = vfs_page_cache_table[bucket];
    while (node) {
        if (node->mount_or_fs == file->mount && node->fs_handle == file->fs_handle && node->offset == offset) {
            uintptr_t paddr = node->phys_addr;
            spinlock_release_irqrestore(&vfs_page_cache_lock, flags);
            return paddr;
        }
        node = node->next;
    }
    spinlock_release_irqrestore(&vfs_page_cache_lock, flags);

    page_t *pg = pmm_alloc_page(PAGE_FLAG_ZERO);
    if (!pg) return 0;
    uintptr_t phys = pmm_page_to_paddr(pg);
    extern uint64_t p2v(uint64_t phys);
    void *kptr = (void *)p2v(phys);
    memset(kptr, 0, 4096);

    if (offset < file_size) {
        size_t to_read = (file_size - offset > 4096) ? 4096 : (size_t)(file_size - offset);
        vfs_seek(file, (int64_t)offset, 0);
        vfs_read(file, kptr, to_read);
    }

    flags = spinlock_acquire_irqsave(&vfs_page_cache_lock);
    node = vfs_page_cache_table[bucket];
    while (node) {
        if (node->mount_or_fs == file->mount && node->fs_handle == file->fs_handle && node->offset == offset) {
            uintptr_t paddr = node->phys_addr;
            spinlock_release_irqrestore(&vfs_page_cache_lock, flags);
            pmm_free_page(pg);
            return paddr;
        }
        node = node->next;
    }

    vfs_page_cache_node_t *new_node = (vfs_page_cache_node_t *)kmalloc(sizeof(vfs_page_cache_node_t));
    if (new_node) {
        new_node->mount_or_fs = file->mount;
        new_node->fs_handle = file->fs_handle;
        new_node->offset = offset;
        new_node->phys_addr = phys;
        new_node->next = vfs_page_cache_table[bucket];
        vfs_page_cache_table[bucket] = new_node;
    }
    spinlock_release_irqrestore(&vfs_page_cache_lock, flags);

    return phys;
}

uint64_t handle_sys_brk(const syscall_args_t *args) {
  uint64_t val = args->arg1;
  process_t *proc = process_get_current();
  if (!proc || !proc->is_user)
    return (uint64_t)-1;

  if (val == 0)
    return proc->heap_end;

  if (proc->vmm_space) {
    uintptr_t res = vmm_brk(proc->vmm_space, val);
    proc->heap_end = res;
    return res;
  }

  return proc->heap_end;
}

uint64_t handle_sys_mmap(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc || !proc->is_user)
    return (uint64_t)MAP_FAILED;

  uint64_t addr = args->arg1;
  uint64_t length = args->arg2;
  int prot = (int)args->arg3;
  int flags = (int)args->arg4;
  int fd = (int)args->arg5;
  uint64_t offset = args->arg6;

  if (length == 0)
    return (uint64_t)MAP_FAILED;
  uint64_t aligned_len = (length + 4095) & ~4095ULL;

  if (flags & MAP_FIXED) {
    if (addr < 0x10000 || addr + aligned_len > 0x00007FFFFFF00000ULL)
      return (uint64_t)MAP_FAILED;
    if (proc->vmm_space) {
      vmm_unmap(proc->vmm_space, addr, aligned_len);
    }
  }

  if (flags & MAP_ANONYMOUS) {
    if (proc->vmm_space) {
      uint32_t mmu_prot = MMU_PROT_USER;
      if (prot & PROT_READ)  mmu_prot |= MMU_PROT_READ;
      if (prot & PROT_WRITE) mmu_prot |= MMU_PROT_WRITE;
      if (prot & PROT_EXEC)  mmu_prot |= MMU_PROT_EXEC;
      void *res = vmm_map(proc->vmm_space, addr, aligned_len, mmu_prot, 0, NULL, 0);
      if (!res) return (uint64_t)MAP_FAILED;
      uint64_t virt_res = (uint64_t)res;
      if (flags & 0x08000 /* MAP_POPULATE */) {
        for (uint64_t off = 0; off < aligned_len; off += 4096) {
          page_t *p = pmm_alloc_page(PAGE_FLAG_ZERO);
          if (p) {
            mmu_map_page(proc->vmm_space->mmu_ctx, virt_res + off, pmm_page_to_paddr(p), mmu_prot);
          } else {
            vmm_unmap(proc->vmm_space, virt_res, aligned_len);
            return (uint64_t)MAP_FAILED;
          }
        }
      }
      return virt_res;
    }
  }

  uint64_t virt_addr = addr;
  if (virt_addr == 0) {
    virt_addr = proc->mmap_current;
    proc->mmap_current += aligned_len;
  }

  uint32_t map_prot = MMU_PROT_READ | MMU_PROT_USER;
  if (prot & PROT_WRITE)
    map_prot |= MMU_PROT_WRITE;
  if (prot & 0x4)
    map_prot |= MMU_PROT_EXEC;

  mmu_context_t fallback_ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
  mmu_context_t *proc_ctx = (proc->vmm_space && proc->vmm_space->mmu_ctx) ? proc->vmm_space->mmu_ctx : &fallback_ctx;

  if (flags & MAP_ANONYMOUS) {
    for (uint64_t off = 0; off < aligned_len; off += 4096) {
      void *phys_page = kmalloc_aligned(4096, 4096);
      if (!phys_page)
        return (uint64_t)MAP_FAILED;
      memset(phys_page, 0, 4096);

      if (mmu_map_page(proc_ctx, virt_addr + off, v2p((uint64_t)phys_page), map_prot) != 0) {
        kfree_null(phys_page);
        return (uint64_t)MAP_FAILED;
      }
    }
    return virt_addr;
  }

  // File-backed mapping
  if (fd < 0 || fd >= MAX_PROCESS_FDS || !proc->fds[fd])
    return (uint64_t)MAP_FAILED;
  if (proc->fd_kind[fd] != PROC_FD_KIND_FILE)
    return (uint64_t)MAP_FAILED;

  process_fd_file_ref_t *ref = (process_fd_file_ref_t *)proc->fds[fd];
  if (!ref || !ref->file)
    return (uint64_t)MAP_FAILED;
  vfs_file_t *file = ref->file;

  if (file->is_device && file->device_type == DEVICE_TYPE_FRAMEBUFFER) {
    framebuffer_info_t fb = graphics_get_fb_params();
    if (!fb.address)
      return (uint64_t)MAP_FAILED;

    uint64_t phys_addr = v2p((uint64_t)fb.address);
    if (proc->vmm_space) {
      uint32_t mmu_prot = MMU_PROT_READ | MMU_PROT_USER;
      if (prot & PROT_WRITE) mmu_prot |= MMU_PROT_WRITE;
      void *res = vmm_map(proc->vmm_space, addr, aligned_len, mmu_prot, VMA_FLAG_SHARED, (void *)1, 0);
      if (!res) return (uint64_t)MAP_FAILED;
      virt_addr = (uint64_t)res;
      for (uint64_t off = 0; off < aligned_len; off += 4096) {
        mmu_map_page(proc->vmm_space->mmu_ctx, virt_addr + off, phys_addr + off, mmu_prot | MMU_FLAG_WC);
      }
      return virt_addr;
    }

    for (uint64_t off = 0; off < aligned_len; off += 4096) {
      if (mmu_map_page(proc_ctx, virt_addr + off, phys_addr + off, map_prot | MMU_FLAG_WC) != 0)
        return (uint64_t)MAP_FAILED;
    }
    return virt_addr;
  }

  if (file->is_device && file->device_type == DEVICE_TYPE_SHM) {
    typedef struct shm_segment shm_segment_t;
    extern int shm_allocate(shm_segment_t *seg, size_t size);
    extern void shm_ref(shm_segment_t *seg);
    extern void shm_unref(shm_segment_t *seg);
    shm_segment_t *seg = (shm_segment_t *)file->fs_handle;
    if (!seg)
      return (uint64_t)MAP_FAILED;

    // Ensure segment has enough pages for the requested mapping size
    if ((uint64_t)seg->page_count * 4096 < aligned_len) {
      if (shm_allocate(seg, aligned_len) < 0)
        return (uint64_t)MAP_FAILED;
    }

    // Keep the segment alive after the file descriptor is closed.
    shm_ref(seg);

    if (proc->vmm_space) {
      uint32_t mmu_prot = MMU_PROT_READ | MMU_PROT_USER;
      if (prot & PROT_WRITE) mmu_prot |= MMU_PROT_WRITE;
      if (prot & 0x4)        mmu_prot |= MMU_PROT_EXEC;
      void *res = vmm_map(proc->vmm_space, addr, aligned_len, mmu_prot, VMA_FLAG_SHARED, (void *)seg, 0);
      if (!res) {
        shm_unref(seg);
        return (uint64_t)MAP_FAILED;
      }
      virt_addr = (uint64_t)res;

      if (proc->shm_mapping_count < 64) {
        proc->shm_mappings[proc->shm_mapping_count].addr = virt_addr;
        proc->shm_mappings[proc->shm_mapping_count].length = aligned_len;
        proc->shm_mappings[proc->shm_mapping_count].seg = (void*)seg;
        proc->shm_mapping_count++;
      }

      uint32_t pages_to_map = aligned_len / 4096;
      for (uint32_t i = 0; i < pages_to_map; i++) {
        mmu_map_page(proc->vmm_space->mmu_ctx, virt_addr + i * 4096, seg->phys_pages[i], mmu_prot);
      }
      return virt_addr;
    }

    // Track the SHM mapping in proc
    if (proc->shm_mapping_count >= 64) {
      shm_unref(seg);
      return (uint64_t)MAP_FAILED;
    }
    proc->shm_mappings[proc->shm_mapping_count].addr = virt_addr;
    proc->shm_mappings[proc->shm_mapping_count].length = aligned_len;
    proc->shm_mappings[proc->shm_mapping_count].seg = (void*)seg;
    proc->shm_mapping_count++;

    // Map pages covering the requested length
    uint32_t pages_to_map = aligned_len / 4096;
    for (uint32_t i = 0; i < pages_to_map; i++) {
      if (mmu_map_page(proc_ctx, virt_addr + i * 4096, seg->phys_pages[i], map_prot) != 0)
        return (uint64_t)MAP_FAILED;
    }
    return virt_addr;
  }

  if (!file->is_device && file->fs_handle) {
    uint64_t file_size = 0;
    if (file->mount && file->mount->ops && file->mount->ops->get_size) {
      file_size = file->mount->ops->get_size(file->fs_handle);
    }

    if (proc->vmm_space) {
      uint32_t mmu_prot = MMU_PROT_READ | MMU_PROT_USER;
      if (prot & PROT_WRITE) mmu_prot |= MMU_PROT_WRITE;
      if (prot & 0x4)        mmu_prot |= MMU_PROT_EXEC;

      uint32_t vma_flags = VMA_FLAG_READ;
      if (prot & PROT_WRITE) vma_flags |= VMA_FLAG_WRITE;
      if (prot & 0x4)        vma_flags |= VMA_FLAG_EXEC;
      if (flags & MAP_SHARED) vma_flags |= VMA_FLAG_SHARED;
      else if (!(prot & PROT_WRITE)) vma_flags |= VMA_FLAG_SHARED;

      void *res = vmm_map(proc->vmm_space, addr, aligned_len, mmu_prot, vma_flags, (void *)1, 0);
      if (!res) return (uint64_t)MAP_FAILED;
      virt_addr = (uint64_t)res;

      uint32_t pages_to_map = aligned_len / 4096;
      for (uint32_t i = 0; i < pages_to_map; i++) {
        uint64_t curr_off = offset + (uint64_t)i * 4096;
        uintptr_t phys_page = vfs_get_cached_page(file, curr_off, file_size);
        if (!phys_page) {
          page_t *p = pmm_alloc_page(PAGE_FLAG_ZERO);
          if (p) phys_page = pmm_page_to_paddr(p);
        }
        if (phys_page) {
          mmu_map_page(proc->vmm_space->mmu_ctx, virt_addr + (uint64_t)i * 4096, phys_page, mmu_prot);
        }
      }
      return virt_addr;
    }

    uint32_t pages_to_map = aligned_len / 4096;
    for (uint32_t i = 0; i < pages_to_map; i++) {
      uint64_t curr_off = offset + (uint64_t)i * 4096;
      uintptr_t phys_page = vfs_get_cached_page(file, curr_off, file_size);
      if (!phys_page) {
        page_t *p = pmm_alloc_page(PAGE_FLAG_ZERO);
        if (p) phys_page = pmm_page_to_paddr(p);
      }
      if (phys_page) {
        mmu_map_page(proc_ctx, virt_addr + (uint64_t)i * 4096, phys_page, map_prot);
      }
    }
    return virt_addr;
  }

  return (uint64_t)MAP_FAILED;
}

uint64_t handle_sys_munmap(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc || !proc->is_user)
    return (uint64_t)-1;

  uint64_t addr = args->arg1;
  uint64_t length = args->arg2;

  if (length == 0)
    return 0;
  uint64_t aligned_len = (length + 4095) & ~4095ULL;

  if (proc->vmm_space) {
    for (uint32_t i = 0; i < proc->shm_mapping_count; i++) {
      if (proc->shm_mappings[i].addr == addr) {
        if (proc->shm_mappings[i].seg) {
          shm_unref((shm_segment_t *)proc->shm_mappings[i].seg);
          proc->shm_mappings[i].seg = NULL;
        }
        for (uint32_t j = i; j < proc->shm_mapping_count - 1; j++) {
          proc->shm_mappings[j] = proc->shm_mappings[j + 1];
        }
        proc->shm_mapping_count--;
        break;
      }
    }
    return (uint64_t)vmm_unmap(proc->vmm_space, addr, aligned_len);
  }

  mmu_context_t fallback_ctx = { .pml4_phys = proc->pml4_phys, .lock = SPINLOCK_INIT };
  for (uint64_t off = 0; off < aligned_len; off += 4096) {
    uint64_t vaddr = addr + off;
    uint64_t phys = mmu_virt_to_phys(&fallback_ctx, vaddr);
    if (phys) {
      bool is_shm = false;
      for (uint32_t i = 0; i < proc->shm_mapping_count; i++) {
        if (vaddr >= proc->shm_mappings[i].addr &&
            vaddr < proc->shm_mappings[i].addr + proc->shm_mappings[i].length) {
          is_shm = true;
          break;
        }
      }
      if (!is_shm) {
        page_t *p = pmm_paddr_to_page(phys);
        if (p && !(p->flags & (PAGE_FLAG_SLAB | PAGE_FLAG_KMALLOC_LARGE))) {
          pmm_free_page(p);
        } else {
          kfree((void *)p2v(phys));
        }
      }
    }
    mmu_unmap_page(&fallback_ctx, vaddr);
  }

  // Find and release the SHM mapping
  for (uint32_t i = 0; i < proc->shm_mapping_count; i++) {
    if (proc->shm_mappings[i].addr == addr) {
      if (proc->shm_mappings[i].seg) {
        shm_unref((shm_segment_t *)proc->shm_mappings[i].seg);
      }
      // Remove from list by shifting remaining
      for (uint32_t j = i; j < proc->shm_mapping_count - 1; j++) {
        proc->shm_mappings[j] = proc->shm_mappings[j + 1];
      }
      proc->shm_mapping_count--;
      break;
    }
  }

  return 0;
}

uint64_t handle_sys_mprotect(const syscall_args_t *args) {
  process_t *proc = process_get_current();
  if (!proc || !proc->is_user)
    return (uint64_t)-1;

  uintptr_t addr = args->arg1;
  size_t length = args->arg2;
  int prot = (int)args->arg3;

  if (proc->vmm_space) {
    uint32_t mmu_prot = MMU_PROT_USER;
    if (prot & PROT_READ)  mmu_prot |= MMU_PROT_READ;
    if (prot & PROT_WRITE) mmu_prot |= MMU_PROT_WRITE;
    if (prot & PROT_EXEC)  mmu_prot |= MMU_PROT_EXEC;
    return (uint64_t)vmm_protect(proc->vmm_space, addr, length, mmu_prot);
  }
  return 0;
}

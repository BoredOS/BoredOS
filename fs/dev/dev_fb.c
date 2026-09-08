// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"

extern bool g_headless_mode;

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOPUT_VSCREENINFO 0x4601
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPAN_DISPLAY     0x4604
#define FBIOSET_DIRTY       0x4606

typedef struct {
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct { uint32_t offset; uint32_t length; } red, green, blue, transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
    uint32_t reserved[4];
} fb_var_screeninfo_t;

typedef struct {
    char id[16];
    uint64_t smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    uint64_t mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t reserved[3];
} fb_fix_screeninfo_t;

vfs_file_t* dev_fb_open(const char *devname, const char *mode) {
    (void)mode;
    if (g_headless_mode) return NULL;

    int id = 0;
    if (strcmp(devname, "fb0") == 0 || strcmp(devname, "fb") == 0) {
        id = 0;
    } else if (devname[2] >= '0' && devname[2] <= '9') {
        id = atoi(devname + 2);
    } else {
        id = -1;
    }

    if (id == 0) { // Currently only support /dev/fb0
        uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = (void*)(uintptr_t)id;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_FRAMEBUFFER;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
        spinlock_release_irqrestore(&vfs_lock, flags);
    }
    return NULL;
}

int dev_fb_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    vfs_framebuffer_info_t fb = graphics_get_fb_params();
    if (!fb.address || fb.width == 0 || fb.height == 0) return -1;

    uint64_t fb_size = (uint64_t)fb.width * fb.height * (fb.bpp / 8);
    if (file->position >= fb_size) return 0;

    uint64_t to_read = fb_size - file->position;
    if ((uint64_t)size < to_read) to_read = size;

    memcpy(buf, (uint8_t*)fb.address + file->position, to_read);
    file->position += to_read;
    return (int)to_read;
}

int dev_fb_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    vfs_framebuffer_info_t fb = graphics_get_fb_params();
    if (!fb.address || fb.width == 0 || fb.height == 0) return -1;

    uint64_t fb_size = (uint64_t)fb.width * fb.height * (fb.bpp / 8);
    if (file->position >= fb_size) return -1;

    uint64_t to_write = fb_size - file->position;
    if ((uint64_t)size < to_write) to_write = size;

    memcpy((uint8_t*)fb.address + file->position, buf, to_write);
    file->position += to_write;
    return (int)to_write;
}

int dev_fb_seek(vfs_file_t *file, int64_t offset, int whence) {
    if (!file || !file->valid || !file->is_device) return -1;

    vfs_framebuffer_info_t fb = graphics_get_fb_params();
    if (!fb.address || fb.width == 0 || fb.height == 0) return -1;
    uint64_t fb_size = (uint64_t)fb.width * fb.height * (fb.bpp / 8);
    uint64_t new_pos = file->position;

    if (whence == 0) {
        if (offset < 0) return -1;
        new_pos = (uint64_t)offset;
    } else if (whence == 1) {
        if ((int64_t)new_pos + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)new_pos + offset);
    } else if (whence == 2) {
        if ((int64_t)fb_size + offset < 0) return -1;
        new_pos = (uint64_t)((int64_t)fb_size + offset);
    } else return -1;

    if (new_pos > fb_size) new_pos = fb_size;
    file->position = new_pos;
    return 0;
}

int dev_fb_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;

    vfs_framebuffer_info_t fb = graphics_get_fb_params();
    if (!fb.address || fb.width == 0 || fb.height == 0 || fb.bpp == 0) {
        return -1;
    }

    if (request == FBIOGET_VSCREENINFO) {
        if (!arg) return -1;
        fb_var_screeninfo_t *vinfo = (fb_var_screeninfo_t *)arg;
        vinfo->xres = fb.width;
        vinfo->yres = fb.height;
        vinfo->xres_virtual = fb.width;
        vinfo->yres_virtual = fb.height;
        vinfo->xoffset = 0;
        vinfo->yoffset = 0;
        vinfo->bits_per_pixel = fb.bpp;
        vinfo->grayscale = 0;
        vinfo->red.offset = fb.red_mask_shift;
        vinfo->red.length = fb.red_mask_size;
        vinfo->green.offset = fb.green_mask_shift;
        vinfo->green.length = fb.green_mask_size;
        vinfo->blue.offset = fb.blue_mask_shift;
        vinfo->blue.length = fb.blue_mask_size;
        vinfo->transp.offset = 0;
        vinfo->transp.length = 0;
        vinfo->nonstd = 0;
        vinfo->activate = 0;
        vinfo->height = 0;
        vinfo->width = 0;
        vinfo->accel_flags = 0;
        vinfo->pixclock = 0;
        vinfo->left_margin = 0;
        vinfo->right_margin = 0;
        vinfo->upper_margin = 0;
        vinfo->lower_margin = 0;
        vinfo->hsync_len = 0;
        vinfo->vsync_len = 0;
        vinfo->sync = 0;
        vinfo->vmode = 0;
        vinfo->rotate = 0;
        vinfo->colorspace = 0;
        return 0;
    } else if (request == FBIOGET_FSCREENINFO) {
        if (!arg) return -1;
        fb_fix_screeninfo_t *finfo = (fb_fix_screeninfo_t *)arg;
        strcpy(finfo->id, "BoredOS FB");
        finfo->smem_start = v2p((uint64_t)fb.address);
        finfo->smem_len = (uint32_t)(fb.width * fb.height * (fb.bpp / 8));
        finfo->type = 0; // FB_TYPE_PACKED_PIXELS
        finfo->type_aux = 0;
        finfo->visual = 2; // FB_VISUAL_TRUECOLOR
        finfo->xpanstep = 0;
        finfo->ypanstep = 0;
        finfo->ywrapstep = 0;
        finfo->line_length = (uint32_t)fb.pitch;
        finfo->mmio_start = 0;
        finfo->mmio_len = 0;
        finfo->accel = 0;
        return 0;
    } else if (request == FBIOPAN_DISPLAY) {
        __builtin_ia32_sfence();
        return 0;
    } else if (request == FBIOSET_DIRTY) {
        __builtin_ia32_sfence();
        return 0;
    } else if (request == FBIOPUT_VSCREENINFO) {
        return 0;
    }
    return -1;
}

uint64_t dev_fb_file_size(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return 0;
    vfs_framebuffer_info_t fb = graphics_get_fb_params();
    return (uint64_t)fb.width * fb.height * (fb.bpp / 8);
}

int dev_fb_list_entries(vfs_dirent_t *entries, int max, int count) {
    if (!g_headless_mode && count < max) {
        strcpy(entries[count].name, "fb0");
        vfs_framebuffer_info_t fb = graphics_get_fb_params();
        entries[count].size = (uint64_t)fb.width * fb.height * (fb.bpp / 8);
        entries[count].is_directory = 0;
        count++;
    }
    return count;
}

bool dev_fb_exists(const char *dev) {
    if (strcmp(dev, "fb0") == 0 || strcmp(dev, "fb") == 0) {
        vfs_framebuffer_info_t fb = graphics_get_fb_params();
        return fb.address != NULL && fb.width > 0 && fb.height > 0;
    }
    return false;
}

int dev_fb_get_info(const char *dev, vfs_dirent_t *info) {
    if (!dev || !info) return -1;
    if (strcmp(dev, "fb0") == 0 || strcmp(dev, "fb") == 0) {
        vfs_framebuffer_info_t fb = graphics_get_fb_params();
        strcpy(info->name, dev);
        info->size = (uint64_t)fb.width * fb.height * (fb.bpp / 8);
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    return -1;
}

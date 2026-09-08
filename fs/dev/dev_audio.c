// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"

extern int pcsk_ioctl(void *file_handle, uint64_t request, void *arg);
extern int rtc_dev_read(void *buf, size_t size, uint64_t *position);
extern int rtc_dev_write(const void *buf, size_t size, uint64_t *position);

vfs_file_t* dev_audio_open(const char *devname, const char *mode) {
    (void)mode;
    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);

    // Handle PC speaker: /dev/pcsk
    if (strcmp(devname, "pcsk") == 0) {
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = (void*)0;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_PCSPKR;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    // Handle DSP (audio stream): /dev/dsp
    if (strcmp(devname, "dsp") == 0) {
        if (!ac97_present()) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            return NULL;
        }
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = ac97_open_client();
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_AUDIO;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    // Handle Mixer: /dev/mixer
    if (strcmp(devname, "mixer") == 0) {
        if (!ac97_present()) {
            spinlock_release_irqrestore(&vfs_lock, flags);
            return NULL;
        }
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = (void*)0;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_MIXER;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    // Handle RTC: /dev/rtc
    if (strcmp(devname, "rtc") == 0) {
        vfs_file_t *vf = vfs_alloc_file();
        if (vf) {
            vf->mount = NULL;
            vf->fs_handle = (void*)0;
            vf->is_device = true;
            vf->device_type = DEVICE_TYPE_RTC;
            vf->position = 0;
            spinlock_release_irqrestore(&vfs_lock, flags);
            return vf;
        }
    }

    spinlock_release_irqrestore(&vfs_lock, flags);
    return NULL;
}

void dev_audio_close(vfs_file_t *file) {
    if (!file || !file->valid || !file->is_device) return;
    if (file->device_type == DEVICE_TYPE_AUDIO) {
        ac97_close_client(file->fs_handle);
    }
}

int dev_audio_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_AUDIO) {
        return ac97_read(file->fs_handle, buf, size);
    } else if (file->device_type == DEVICE_TYPE_RTC) {
        return rtc_dev_read(buf, size, &file->position);
    }
    return -1;
}

int dev_audio_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_AUDIO) {
        return ac97_write(file->fs_handle, buf, size);
    } else if (file->device_type == DEVICE_TYPE_RTC) {
        return rtc_dev_write(buf, size, &file->position);
    }
    return -1;
}

int dev_audio_ioctl(vfs_file_t *file, uint64_t request, void *arg) {
    if (!file || !file->valid || !file->is_device) return -1;

    if (file->device_type == DEVICE_TYPE_PCSPKR) {
        return pcsk_ioctl(file->fs_handle, request, arg);
    } else if (file->device_type == DEVICE_TYPE_AUDIO) {
        return ac97_dsp_ioctl(file->fs_handle, request, arg);
    } else if (file->device_type == DEVICE_TYPE_MIXER) {
        return ac97_mixer_ioctl(request, arg);
    }
    return -1;
}

int dev_audio_list_entries(vfs_dirent_t *entries, int max, int count) {
    if (count < max) {
        strcpy(entries[count].name, "pcsk");
        entries[count].size = 0;
        entries[count].is_directory = 0;
        count++;
    }

    if (count < max) {
        if (ac97_present()) {
            strcpy(entries[count].name, "dsp");
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }
    }

    if (count < max) {
        if (ac97_present()) {
            strcpy(entries[count].name, "mixer");
            entries[count].size = 0;
            entries[count].is_directory = 0;
            count++;
        }
    }

    return count;
}

bool dev_audio_exists(const char *dev) {
    if (strcmp(dev, "pcsk") == 0) return true;
    if (strcmp(dev, "dsp") == 0) return ac97_present();
    if (strcmp(dev, "mixer") == 0) return ac97_present();
    if (strcmp(dev, "rtc") == 0) return true;
    return false;
}

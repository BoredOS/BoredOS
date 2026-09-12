// Copyright (c) 2023-2026 Christiaan (chris@boreddev.nl)
// This software is released under the GNU General Public License v3.0. See LICENSE file for details.
// This header needs to maintain in any file it is present in, as per the GPL license terms.
#include "dev_internal.h"

extern volatile uint64_t kernel_ticks;

static spinlock_t rng_lock = SPINLOCK_INIT;
static uint32_t rng_state[16];
static bool rng_initialized = false;

static inline void cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}

static bool has_rdrand(void) {
    static int cached = -1;
    if (cached != -1) return (bool)cached;
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    cached = (ecx & (1 << 30)) ? 1 : 0;
    return (bool)cached;
}

static bool has_rdseed(void) {
    static int cached = -1;
    if (cached != -1) return (bool)cached;
    uint32_t eax, ebx, ecx, edx;
    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) {
        cached = 0;
        return false;
    }
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    cached = (ebx & (1 << 18)) ? 1 : 0;
    return (bool)cached;
}

static inline bool get_rdrand64(uint64_t *val) {
    if (!has_rdrand()) return false;
    unsigned char ok = 0;
    int retry = 10;
    while (retry--) {
        __asm__ volatile("rdrand %0; setc %1" : "=r"(*val), "=q"(ok) : : "cc");
        if (ok) return true;
    }
    return false;
}

static inline bool get_rdseed64(uint64_t *val) {
    if (!has_rdseed()) return false;
    unsigned char ok = 0;
    int retry = 10;
    while (retry--) {
        __asm__ volatile("rdseed %0; setc %1" : "=r"(*val), "=q"(ok) : : "cc");
        if (ok) return true;
    }
    return false;
}

static inline uint64_t read_tsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static inline uint32_t rotl32(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

#define CHACHA_QR(a, b, c, d) \
    a += b; d ^= a; d = rotl32(d, 16); \
    c += d; b ^= c; b = rotl32(b, 12); \
    a += b; d ^= a; d = rotl32(d, 8);  \
    c += d; b ^= c; b = rotl32(b, 7);

static void chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    for (int i = 0; i < 16; i++) out[i] = in[i];
    for (int i = 0; i < 10; i++) {
        CHACHA_QR(out[0], out[4], out[8],  out[12]);
        CHACHA_QR(out[1], out[5], out[9],  out[13]);
        CHACHA_QR(out[2], out[6], out[10], out[14]);
        CHACHA_QR(out[3], out[7], out[11], out[15]);
        CHACHA_QR(out[0], out[5], out[10], out[15]);
        CHACHA_QR(out[1], out[6], out[11], out[12]);
        CHACHA_QR(out[2], out[7], out[8],  out[13]);
        CHACHA_QR(out[3], out[4], out[9],  out[14]);
    }
    for (int i = 0; i < 16; i++) out[i] += in[i];
}

static void mix_entropy_locked(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t*)data;
    uint8_t *key_bytes = (uint8_t*)&rng_state[4];
    for (size_t i = 0; i < len; i++) {
        key_bytes[i % 32] ^= p[i];
    }
    uint64_t tsc = read_tsc();
    rng_state[4] ^= (uint32_t)tsc;
    rng_state[5] ^= (uint32_t)(tsc >> 32);
    rng_state[12]++;
}

static void rng_init_locked(void) {
    if (rng_initialized) return;

    rng_state[0] = 0x61707865;
    rng_state[1] = 0x3320646e;
    rng_state[2] = 0x79622d32;
    rng_state[3] = 0x6b206574;

    uint64_t tsc = read_tsc();
    uint64_t ticks = kernel_ticks;
    uintptr_t stack_sample = (uintptr_t)&tsc;

    rng_state[4] = (uint32_t)tsc;
    rng_state[5] = (uint32_t)(tsc >> 32);
    rng_state[6] = (uint32_t)ticks;
    rng_state[7] = (uint32_t)(ticks >> 32);
    rng_state[8] = (uint32_t)stack_sample;
    rng_state[9] = (uint32_t)(stack_sample >> 32);
    rng_state[10] = 0x12345678;
    rng_state[11] = 0x9abcdef0;

    rng_state[12] = 0;
    rng_state[13] = 0;
    rng_state[14] = (uint32_t)tsc; // nonce
    rng_state[15] = (uint32_t)(ticks ^ 0xa5a5a5a5);

    if (has_rdseed() || has_rdrand()) {
        uint64_t hw = 0;
        for (int i = 4; i <= 11; i += 2) {
            if ((has_rdseed() && get_rdseed64(&hw)) || (has_rdrand() && get_rdrand64(&hw))) {
                rng_state[i] ^= (uint32_t)hw;
                rng_state[i + 1] ^= (uint32_t)(hw >> 32);
            }
        }
    }

    uint32_t block[16];
    for (int r = 0; r < 4; r++) {
        rng_state[12]++;
        chacha20_block(block, rng_state);
        memcpy(&rng_state[4], block, 32);
    }

    rng_initialized = true;
}

vfs_file_t* dev_random_open(const char *devname, const char *mode) {
    (void)mode;
    if (!devname) return NULL;
    int type = -1;
    if (strcmp(devname, "random") == 0 || strcmp(devname, "urandom") == 0) {
        type = DEVICE_TYPE_RANDOM;
    } else if (strcmp(devname, "null") == 0) {
        type = DEVICE_TYPE_NULL;
    } else if (strcmp(devname, "zero") == 0) {
        type = DEVICE_TYPE_ZERO;
    } else {
        return NULL;
    }

    uint64_t flags = spinlock_acquire_irqsave(&vfs_lock);
    vfs_file_t *vf = vfs_alloc_file();
    if (vf) {
        vf->mount = NULL;
        vf->fs_handle = NULL;
        vf->is_device = true;
        vf->device_type = type;
        vf->position = 0;
        spinlock_release_irqrestore(&vfs_lock, flags);
        return vf;
    }
    spinlock_release_irqrestore(&vfs_lock, flags);
    return NULL;
}

int dev_random_read(vfs_file_t *file, void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (!buf || size == 0) return 0;

    uint8_t *out = (uint8_t*)buf;
    size_t remaining = size;

    uint64_t flags = spinlock_acquire_irqsave(&rng_lock);
    if (!rng_initialized) {
        rng_init_locked();
    }

    uint32_t block[16];

    while (remaining > 0) {
        rng_state[12]++;
        if (rng_state[12] == 0) {
            rng_state[13]++;
        }

        chacha20_block(block, rng_state);

        size_t take = (remaining < 64) ? remaining : 64;
        memcpy(out, block, take);
        out += take;
        remaining -= take;

        memcpy(&rng_state[4], block, 32);

        uint64_t tsc = read_tsc();
        rng_state[4] ^= (uint32_t)tsc;
        rng_state[5] ^= (uint32_t)(tsc >> 32);

        static uint32_t blocks_since_hw_reseed = 0;
        if (++blocks_since_hw_reseed >= 4096) {
            blocks_since_hw_reseed = 0;
            uint64_t hw = 0;
            if (get_rdseed64(&hw) || get_rdrand64(&hw)) {
                rng_state[4] ^= (uint32_t)hw;
                rng_state[5] ^= (uint32_t)(hw >> 32);
            }
        }
    }

    spinlock_release_irqrestore(&rng_lock, flags);
    return (int)size;
}

int dev_random_write(vfs_file_t *file, const void *buf, size_t size) {
    if (!file || !file->valid || !file->is_device) return -1;
    if (!buf || size == 0) return 0;

    uint64_t flags = spinlock_acquire_irqsave(&rng_lock);
    if (!rng_initialized) {
        rng_init_locked();
    }
    mix_entropy_locked(buf, size);
    spinlock_release_irqrestore(&rng_lock, flags);
    return (int)size;
}

bool dev_random_exists(const char *dev) {
    if (!dev) return false;
    return (strcmp(dev, "random") == 0 || strcmp(dev, "urandom") == 0 ||
            strcmp(dev, "null") == 0 || strcmp(dev, "zero") == 0);
}

int dev_random_get_info(const char *dev, vfs_dirent_t *info) {
    if (!dev || !info) return -1;
    if (strcmp(dev, "random") == 0 || strcmp(dev, "urandom") == 0 ||
        strcmp(dev, "null") == 0 || strcmp(dev, "zero") == 0) {
        strcpy(info->name, dev);
        info->size = 0;
        info->is_directory = 0;
        info->start_cluster = 0;
        info->write_date = 0;
        info->write_time = 0;
        return 0;
    }
    return -1;
}

int dev_random_list_entries(vfs_dirent_t *entries, int max, int count) {
    if (!entries) return count;

    const char *names[] = {"random", "urandom", "null", "zero"};
    for (int i = 0; i < 4; i++) {
        if (count < max) {
            strcpy(entries[count].name, names[i]);
            entries[count].size = 0;
            entries[count].is_directory = 0;
            entries[count].start_cluster = 0;
            entries[count].write_date = 0;
            entries[count].write_time = 0;
            count++;
        }
    }

    return count;
}

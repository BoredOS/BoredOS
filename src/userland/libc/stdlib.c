#include "stdlib.h"
#include "syscall.h"

// Block allocator over sys_sbrk with coalescing and splitting
typedef struct BlockMeta {
    size_t size;
    int free;
    struct BlockMeta *next;
    struct BlockMeta *prev;
} BlockMeta;

#define META_SIZE sizeof(BlockMeta)
#define MIN_SPLIT_SIZE 64
#define ALIGN8(x) (((x) + 7) & ~(size_t)7)

static BlockMeta *heap_head = NULL;
static BlockMeta *heap_tail = NULL;

static BlockMeta *request_space(size_t size) {
    BlockMeta *block = (BlockMeta *)sys_sbrk(0);
    void *request = sys_sbrk(size + META_SIZE);
    if (request == (void*)-1) return NULL;

    block->size = size;
    block->free = 0;
    block->next = NULL;
    block->prev = heap_tail;

    if (heap_tail) heap_tail->next = block;
    else heap_head = block;
    heap_tail = block;

    return block;
}

// Split a block if remainder is large enough
static void split_block(BlockMeta *block, size_t size) {
    size_t remain = block->size - size - META_SIZE;
    if (block->size < size + META_SIZE + MIN_SPLIT_SIZE) return;

    BlockMeta *new_block = (BlockMeta *)((char *)(block + 1) + size);
    new_block->size = remain;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next) block->next->prev = new_block;
    else heap_tail = new_block;
    block->next = new_block;
    block->size = size;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;

    size = ALIGN8(size);

    // First-fit search (faster than best-fit for large heaps)
    BlockMeta *current = heap_head;
    while (current) {
        if (current->free && current->size >= size) {
            split_block(current, size);
            current->free = 0;
            return (current + 1);
        }
        current = current->next;
    }

    // No suitable block found, request more space
    BlockMeta *block = request_space(size);
    if (!block) return NULL;
    return (block + 1);
}

void free(void *ptr) {
    if (!ptr) return;

    BlockMeta *block = (BlockMeta *)ptr - 1;
    block->free = 1;

    // Coalesce with next block
    if (block->next && block->next->free) {
        block->size += META_SIZE + block->next->size;
        block->next = block->next->next;
        if (block->next) block->next->prev = block;
        else heap_tail = block;
    }

    // Coalesce with previous block
    if (block->prev && block->prev->free) {
        block->prev->size += META_SIZE + block->size;
        block->prev->next = block->next;
        if (block->next) block->next->prev = block->prev;
        else heap_tail = block->prev;
    }
}

void *calloc(size_t nelem, size_t elsize) {
    size_t size = nelem * elsize;
    void *ptr = malloc(size);
    if (ptr) {
        char *p = ptr;
        for (size_t i = 0; i < size; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) {
        return malloc(size);
    }
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    BlockMeta *block = (BlockMeta*)ptr - 1;
    if (block->size >= size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) {
        return NULL;
    }

    char *src = ptr;
    char *dst = new_ptr;
    for (size_t i = 0; i < block->size; i++) {
        dst[i] = src[i];
    }
    free(ptr);
    return new_ptr;
}

void *memset(void *s, int c, size_t n) {
    uint64_t *d64 = (uint64_t *)s;
    uint64_t val8 = (unsigned char)c;
    uint64_t val64 = val8 | (val8 << 8) | (val8 << 16) | (val8 << 24) |
                     (val8 << 32) | (val8 << 40) | (val8 << 48) | (val8 << 56);
                     
    if (((uintptr_t)s & 7) == 0) {
        size_t words = n / 8;
        for (size_t i = 0; i < words; i++) {
            d64[i] = val64;
        }
        
        unsigned char *d8 = (unsigned char *)(d64 + words);
        size_t rem = n % 8;
        for (size_t i = 0; i < rem; i++) {
            d8[i] = (unsigned char)c;
        }
        return s;
    }
    
    unsigned char *p = (unsigned char *)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    uint64_t *d64 = (uint64_t *)dest;
    const uint64_t *s64 = (const uint64_t *)src;
    
    if (((uintptr_t)dest & 7) == 0 && ((uintptr_t)src & 7) == 0) {
        size_t words = n / 8;
        for (size_t i = 0; i < words; i++) {
            d64[i] = s64[i];
        }
        
        unsigned char *d8 = (unsigned char *)(d64 + words);
        const unsigned char *s8 = (const unsigned char *)(s64 + words);
        size_t rem = n % 8;
        for (size_t i = 0; i < rem; i++) {
            d8[i] = s8[i];
        }
        return dest;
    }
    
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// String functions
size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

char* strcpy(char *dest, const char *src) {
    char *ret = dest;
    while (*src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

char* strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) dest++;
    while (*src) *dest++ = *src++;
    *dest = 0;
    return ret;
}

char *strchr(const char *s, int c) {
    while (*s != (char)c) {
        if (!*s++) return NULL;
    }
    return (char *)s;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (!needle_len) return (char *)haystack;
    while (*haystack) {
        if (memcmp(haystack, needle, needle_len) == 0) return (char *)haystack;
        haystack++;
    }
    return NULL;
}

int atoi(const char *nptr) {
    int res = 0;
    int sign = 1;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') nptr++;
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        res = res * 10 + (*nptr - '0');
        nptr++;
    }
    return sign * res;
}

long strtol(const char *nptr, char **endptr, int base) {
    long res = 0;
    int sign = 1;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') nptr++;
    if (*nptr == '-') { sign = -1; nptr++; }
    else if (*nptr == '+') nptr++;

    if (base == 0) {
        if (*nptr == '0') {
            if (*(nptr+1) == 'x' || *(nptr+1) == 'X') { base = 16; nptr += 2; }
            else base = 8;
        } else base = 10;
    } else if (base == 16) {
        if (*nptr == '0' && (*(nptr+1) == 'x' || *(nptr+1) == 'X')) nptr += 2;
    }

    while (1) {
        int val = -1;
        if (*nptr >= '0' && *nptr <= '9') val = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'f') val = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'F') val = *nptr - 'A' + 10;
        
        if (val == -1 || val >= base) break;
        res = res * base + val;
        nptr++;
    }
    if (endptr) *endptr = (char *)nptr;
    return sign * res;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    return (unsigned long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtol(nptr, endptr, base);
}

static void swap(void *a, void *b, size_t size) {
    char *ca = a, *cb = b;
    while (size--) { char t = *ca; *ca++ = *cb; *cb++ = t; }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb < 2) return;
    char *pivot = (char *)base + (nmemb / 2) * size;
    char *left = base;
    char *right = (char *)base + (nmemb - 1) * size;

    while (left <= right) {
        while (compar(left, pivot) < 0) left += size;
        while (compar(right, pivot) > 0) right -= size;
        if (left <= right) {
            swap(left, right, size);
            if (pivot == left) pivot = right;
            else if (pivot == right) pivot = left;
            left += size;
            right -= size;
        }
    }
    if (base < (void*)right) qsort(base, (right - (char *)base) / size + 1, size, compar);
    if ((void*)left < (char *)base + nmemb * size) qsort(left, ((char *)base + nmemb * size - left) / size, size, compar);
}

void itoa(int n, char *buf) {
    if (n == 0) {
        buf[0] = '0'; buf[1] = 0; return;
    }
    int i = 0;
    int sign = n < 0;
    if (sign) n = -n;
    while (n > 0) {
        buf[i++] = (n % 10) + '0';
        n /= 10;
    }
    if (sign) buf[i++] = '-';
    buf[i] = 0;
    // Reverse
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = t;
    }
}

// System/Process functions
int chdir(const char *path) {
    return sys_chdir(path);
}

char* getcwd(char *buf, int size) {
    if (sys_getcwd(buf, size) >= 0) return buf;
    return NULL;
}

char *realpath(const char *path, char *resolved_path) {
    if (!resolved_path) resolved_path = malloc(1024);
    if (!resolved_path) return NULL;
    strcpy(resolved_path, path);
    return resolved_path;
}

void sleep(int ms) {
    sys_system(SYSTEM_CMD_SLEEP, ms, 0, 0, 0);
}

void exit(int status) {
    sys_exit(status);
}

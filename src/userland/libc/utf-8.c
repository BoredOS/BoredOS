#include "utf-8.h"

static int utf8_write_replacement(char *out) {
    out[0] = (char)0xEF;
    out[1] = (char)0xBF;
    out[2] = (char)0xBD;
    return 3;
}

uint32_t text_decode_utf8(const char *s, int *advance) {
    const unsigned char *u = (const unsigned char *)s;

    if (!u || u[0] == 0) {
        if (advance) *advance = 0;
        return 0;
    }

    if ((u[0] & 0x80) == 0) {
        if (advance) *advance = 1;
        return u[0];
    }

    if ((u[0] & 0xE0) == 0xC0 &&
        (u[1] & 0xC0) == 0x80) {
        if (advance) *advance = 2;
        return ((u[0] & 0x1F) << 6) |
               (u[1] & 0x3F);
    }

    if ((u[0] & 0xF0) == 0xE0 &&
        (u[1] & 0xC0) == 0x80 &&
        (u[2] & 0xC0) == 0x80) {
        if (advance) *advance = 3;
        return ((u[0] & 0x0F) << 12) |
               ((u[1] & 0x3F) << 6) |
               (u[2] & 0x3F);
    }

    if ((u[0] & 0xF8) == 0xF0 &&
        (u[1] & 0xC0) == 0x80 &&
        (u[2] & 0xC0) == 0x80 &&
        (u[3] & 0xC0) == 0x80) {
        if (advance) *advance = 4;
        return ((u[0] & 0x07) << 18) |
               ((u[1] & 0x3F) << 12) |
               ((u[2] & 0x3F) << 6) |
               (u[3] & 0x3F);
    }

    if (advance) *advance = 1;
    return 0xFFFD;
}

int text_encode_utf8(uint32_t cp, char *out) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    }

    if (cp <= 0x7FF) {
        out[0] = 0xC0 | (cp >> 6);
        out[1] = 0x80 | (cp & 0x3F);
        return 2;
    }

    if (cp <= 0xFFFF) {
        out[0] = 0xE0 | (cp >> 12);
        out[1] = 0x80 | ((cp >> 6) & 0x3F);
        out[2] = 0x80 | (cp & 0x3F);
        return 3;
    }

    if (cp <= 0x10FFFF) {
        out[0] = 0xF0 | (cp >> 18);
        out[1] = 0x80 | ((cp >> 12) & 0x3F);
        out[2] = 0x80 | ((cp >> 6) & 0x3F);
        out[3] = 0x80 | (cp & 0x3F);
        return 4;
    }

    return utf8_write_replacement(out);
}

const char* text_next_utf8(const char *s) {
    if (!s || *s == 0) return s;

    int adv;
    text_decode_utf8(s, &adv);
    return s + adv;
}

const char* text_prev_utf8(const char *start, const char *s) {
    if (!s || s <= start) return start;

    s--;
    while (s > start && ((*s & 0xC0) == 0x80)) {
        s--;
    }
    return s;
}

int text_strlen_utf8(const char *s) {
    if (!s) return 0;

    int count = 0;
    int adv;

    while (*s) {
        text_decode_utf8(s, &adv);
        s += adv;
        count++;
    }

    return count;
}
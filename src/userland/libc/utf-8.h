#ifndef UTF_8_H
#define UTF_8_H

#include <stdint.h>

// Decode one UTF-8 codepoint
// s: input string
// advance: number of bytes consumed
uint32_t text_decode_utf8(const char *s, int *advance);

// Encode one codepoint into UTF-8
// out must be at least 4 bytes
// return: number of bytes written
int text_encode_utf8(uint32_t cp, char *out);

// Move to next UTF-8 character
const char* text_next_utf8(const char *s);

// Move to previous UTF-8 character
const char* text_prev_utf8(const char *start, const char *s);

// Count characters (not bytes)
int text_strlen_utf8(const char *s);

#endif
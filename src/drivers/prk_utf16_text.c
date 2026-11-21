/**
 * @file prk_utf16_text.c
 * @brief UTF-16LE to UTF-8 conversion for tool output (#74): `cmd.exe /u` writes its own output, such as the `set`
 *        listing after vcvarsall.bat, as UTF-16LE. Portable C, so the parser is tested on every platform.
 */
#include <stdlib.h>

#include "prk_c_driver_internal.h"

/** @brief Reads the UTF-16LE unit at @p index. */
static unsigned read_unit(const unsigned char* bytes, size_t index) {
    return (unsigned)bytes[2 * index] | ((unsigned)bytes[2 * index + 1] << 8);
}

/** @brief Writes @p code_point as UTF-8 at @p out_text. @return Bytes written (1 to 4). */
static size_t write_utf8(unsigned long code_point, char* out_text) {
    if (code_point < 0x80ul) {
        out_text[0] = (char)code_point;
        return 1;
    }
    if (code_point < 0x800ul) {
        out_text[0] = (char)(0xC0u | (code_point >> 6));
        out_text[1] = (char)(0x80u | (code_point & 0x3Fu));
        return 2;
    }
    if (code_point < 0x10000ul) {
        out_text[0] = (char)(0xE0u | (code_point >> 12));
        out_text[1] = (char)(0x80u | ((code_point >> 6) & 0x3Fu));
        out_text[2] = (char)(0x80u | (code_point & 0x3Fu));
        return 3;
    }
    out_text[0] = (char)(0xF0u | (code_point >> 18));
    out_text[1] = (char)(0x80u | ((code_point >> 12) & 0x3Fu));
    out_text[2] = (char)(0x80u | ((code_point >> 6) & 0x3Fu));
    out_text[3] = (char)(0x80u | (code_point & 0x3Fu));
    return 4;
}

PrkStatus prk_decode_utf16le_if_wide(const char* text, size_t length, char** out_text, size_t* out_length) {
    *out_text = NULL;
    *out_length = 0;
    const unsigned char* const bytes = (const unsigned char*)text;
    if (text == NULL || length < 2 || bytes[1] != 0) {
        return PRK_STATUS_OK; /* Not UTF-16LE (cmd output always starts with an ASCII character). */
    }
    const size_t unit_count = length / 2;
    char* const decoded = (char*)malloc(unit_count * 3 + 1);
    if (decoded == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    size_t written = 0;
    for (size_t index = 0; index < unit_count; ++index) {
        unsigned long code_point = read_unit(bytes, index);
        const int is_high = code_point >= 0xD800ul && code_point <= 0xDBFFul;
        const unsigned next = index + 1 < unit_count ? read_unit(bytes, index + 1) : 0u;
        if (is_high && next >= 0xDC00u && next <= 0xDFFFu) {
            code_point = 0x10000ul + ((code_point - 0xD800ul) << 10) + (next - 0xDC00u);
            ++index;
        } else if (code_point >= 0xD800ul && code_point <= 0xDFFFul) {
            code_point = 0xFFFDul; /* A lone surrogate. */
        }
        written += write_utf8(code_point, decoded + written);
    }
    decoded[written] = '\0';
    *out_text = decoded;
    *out_length = written;
    return PRK_STATUS_OK;
}

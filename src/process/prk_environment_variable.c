/**
 * @file prk_environment_variable.c
 * @brief Reads one environment variable of this process into a caller buffer (#74): _dupenv_s on MSVC, which
 *        has no C4996 warning and needs no _CRT_SECURE_NO_WARNINGS, and getenv elsewhere.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proofrunkit/prk_process.h"

int prk_read_environment_variable(const char* name, char* out_value, size_t capacity) {
    if (out_value == NULL || capacity == 0) {
        return 0;
    }
    out_value[0] = '\0';
    if (name == NULL || name[0] == '\0') {
        return 0;
    }
#if defined(_MSC_VER)
    char* value = NULL;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == NULL) {
        free(value);
        return 0;
    }
    const size_t value_length = strlen(value);
    const int is_fitting = value_length < capacity;
    if (is_fitting) {
        memcpy(out_value, value, value_length + 1);
    }
    free(value);
    return is_fitting;
#else
    const char* const value = getenv(name);
    if (value == NULL || strlen(value) >= capacity) {
        return 0;
    }
    memcpy(out_value, value, strlen(value) + 1);
    return 1;
#endif
}

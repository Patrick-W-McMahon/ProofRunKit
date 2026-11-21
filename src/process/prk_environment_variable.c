/**
 * @file prk_environment_variable.c
 * @brief Reads one environment variable of this process (#74, #78): _dupenv_s on MSVC, which has no C4996
 *        warning and needs no _CRT_SECURE_NO_WARNINGS, and getenv elsewhere. Values can be long (PATH inside a
 *        Visual Studio developer environment is often over 4096 bytes), so prk_duplicate_environment_variable()
 *        returns a heap copy of any size; prk_read_environment_variable() is for short values in a fixed buffer.
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

int prk_duplicate_environment_variable(const char* name, char** out_value) {
    if (out_value == NULL) {
        return 0;
    }
    *out_value = NULL;
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
    *out_value = value;
    return 1;
#else
    const char* const value = getenv(name);
    if (value == NULL) {
        return 0;
    }
    const size_t size = strlen(value) + 1;
    char* const copy = (char*)malloc(size);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, value, size);
    *out_value = copy;
    return 1;
#endif
}

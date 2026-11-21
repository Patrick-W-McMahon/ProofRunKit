/**
 * @file prk_registry.c
 * @brief Test-case registry: an owned, growable list of named test bodies.
 */
#include <stdlib.h>
#include <string.h>

#include "prk_internal.h"

enum { PRK_INITIAL_REGISTRY_CAPACITY = 16 };

/** @brief Returns a heap copy of @p text, or NULL on allocation failure. */
static char* duplicate_text(const char* text) {
    const size_t length = strlen(text);
    char* const copy = (char*)malloc(length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length + 1);
    }
    return copy;
}

/** @brief Reports whether @p name is already registered. */
static int is_test_case_name_registered(const PrkRegistry* registry, const char* name) {
    for (size_t index = 0; index < registry->count; ++index) {
        if (strcmp(registry->entries[index].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

/** @brief Ensures room for one more entry. Returns PRK_STATUS_OK or PRK_STATUS_OUT_OF_MEMORY. */
static PrkStatus reserve_registry_slot(PrkRegistry* registry) {
    if (registry->count < registry->capacity) {
        return PRK_STATUS_OK;
    }
    const size_t new_capacity = registry->capacity == 0 ? PRK_INITIAL_REGISTRY_CAPACITY : registry->capacity * 2;
    PrkRegisteredTestCase* const grown =
        (PrkRegisteredTestCase*)realloc(registry->entries, new_capacity * sizeof(PrkRegisteredTestCase));
    if (grown == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    registry->entries = grown;
    registry->capacity = new_capacity;
    return PRK_STATUS_OK;
}

PrkRegistry* prk_create_registry(void) {
    return (PrkRegistry*)calloc(1, sizeof(PrkRegistry));
}

void prk_destroy_registry(PrkRegistry* registry) {
    if (registry == NULL) {
        return;
    }
    for (size_t index = 0; index < registry->count; ++index) {
        free(registry->entries[index].name);
    }
    free(registry->entries);
    free(registry);
}

PrkStatus prk_register_test_case(PrkRegistry* registry, const PrkTestCase* test_case) {
    if (registry == NULL || test_case == NULL || test_case->name == NULL || test_case->name[0] == '\0' ||
        test_case->body == NULL) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    if (is_test_case_name_registered(registry, test_case->name)) {
        return PRK_STATUS_DUPLICATE_NAME;
    }
    const PrkStatus reserve_status = reserve_registry_slot(registry);
    if (reserve_status != PRK_STATUS_OK) {
        return reserve_status;
    }
    char* const name_copy = duplicate_text(test_case->name);
    if (name_copy == NULL) {
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    const PrkRegisteredTestCase entry = {name_copy, test_case->body, test_case->user_state};
    registry->entries[registry->count] = entry;
    registry->count += 1;
    return PRK_STATUS_OK;
}

size_t prk_count_test_cases(const PrkRegistry* registry) {
    return registry == NULL ? 0 : registry->count;
}

const PrkRegisteredTestCase* prk_find_registered_test_case(const PrkRegistry* registry, size_t index) {
    if (registry == NULL || index >= registry->count) {
        return NULL;
    }
    return &registry->entries[index];
}

const char* prk_get_test_case_name(const PrkRegistry* registry, size_t index) {
    const PrkRegisteredTestCase* const entry = prk_find_registered_test_case(registry, index);
    return entry == NULL ? NULL : entry->name;
}

/**
 * @file prk_internal.h
 * @brief Private layout of the registry and test context, shared by the core translation units only.
 */
#ifndef PROOFRUNKIT_PRK_INTERNAL_H
#define PROOFRUNKIT_PRK_INTERNAL_H

#include "proofrunkit/prk.h"

/** @brief One stored test case; the registry owns @p name. */
typedef struct PrkRegisteredTestCase {
    char* name;
    PrkTestBody body;
    void* user_state;
} PrkRegisteredTestCase;

/** @brief Growable array of registered test cases. */
struct PrkRegistry {
    PrkRegisteredTestCase* entries;
    size_t count;
    size_t capacity;
};

/** @brief Outcome channel for one running test body. Written only through prk_fail_test/prk_skip_test. */
struct PrkTestContext {
    PrkOutcome outcome;
    char message[PRK_MESSAGE_CAPACITY];
};

/**
 * @brief Looks up a registered test case by position.
 * @return The entry, or NULL when @p index is out of range.
 */
const PrkRegisteredTestCase* prk_find_registered_test_case(const PrkRegistry* registry, size_t index);

#endif /* PROOFRUNKIT_PRK_INTERNAL_H */

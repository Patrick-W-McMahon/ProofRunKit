/**
 * @file prk_summary.c
 * @brief Pure helpers for result totals and printable names of outcomes and statuses.
 */
#include "proofrunkit/prk.h"

PrkRunSummary prk_add_outcome_to_summary(PrkRunSummary summary, PrkOutcome outcome) {
    PrkRunSummary updated = summary;
    updated.total += 1;
    switch (outcome) {
        case PRK_OUTCOME_PASSED:
            updated.passed += 1;
            break;
        case PRK_OUTCOME_FAILED:
            updated.failed += 1;
            break;
        case PRK_OUTCOME_SKIPPED:
            updated.skipped += 1;
            break;
        default:
            updated.failed += 1; /* An unknown outcome is never counted as a pass. */
            break;
    }
    return updated;
}

const char* prk_get_outcome_name(PrkOutcome outcome) {
    switch (outcome) {
        case PRK_OUTCOME_PASSED:
            return "passed";
        case PRK_OUTCOME_FAILED:
            return "failed";
        case PRK_OUTCOME_SKIPPED:
            return "skipped";
        default:
            return "unknown";
    }
}

const char* prk_get_status_name(PrkStatus status) {
    switch (status) {
        case PRK_STATUS_OK:
            return "ok";
        case PRK_STATUS_INVALID_ARGUMENT:
            return "invalid argument";
        case PRK_STATUS_OUT_OF_MEMORY:
            return "out of memory";
        case PRK_STATUS_DUPLICATE_NAME:
            return "duplicate name";
        case PRK_STATUS_SCRIPT_ERROR:
            return "script error";
        default:
            return "unknown status";
    }
}

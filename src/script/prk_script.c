/**
 * @file prk_script.c
 * @brief Helpers for the engine-neutral script ABI in include/proofrunkit/prk_script.h (#12).
 */
#include "proofrunkit/prk_script.h"

const char* prk_get_file_access_status_name(PrkFileAccessStatus status) {
    switch (status) {
        case PRK_FILE_ACCESS_OK:
            return "ok";
        case PRK_FILE_ACCESS_NOT_FOUND:
            return "not found";
        case PRK_FILE_ACCESS_DENIED:
            return "access denied";
        case PRK_FILE_ACCESS_READ_ERROR:
            return "read error";
    }
    return "unknown file access status";
}

uint64_t prk_cap_script_process_timeout_ms(uint64_t requested_ms, uint64_t deadline_ms, uint64_t now_ms) {
    const uint64_t effective = requested_ms != 0 ? requested_ms : PRK_PROCESS_DEFAULT_TIMEOUT_MS;
    if (deadline_ms == 0) {
        return effective;
    }
    const uint64_t remaining = deadline_ms > now_ms ? deadline_ms - now_ms : 1;
    return remaining < effective ? remaining : effective;
}

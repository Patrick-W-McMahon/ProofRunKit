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

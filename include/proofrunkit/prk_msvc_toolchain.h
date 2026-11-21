/**
 * @file prk_msvc_toolchain.h
 * @brief MSVC toolchain discovery for the C/C++ driver (#14): finds Visual Studio with vswhere, runs
 *        vcvarsall.bat for an architecture, and captures the environment and the paths of cl.exe and lib.exe,
 *        so cl can be used without a Developer Command Prompt.
 *
 * Both steps run cmd.exe through an injected PrkProcessRunner (#13):
 *   cmd.exe /d /c call "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products *
 *       -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -utf8 -nologo
 *   cmd.exe /d /c chcp 65001 >nul && call "<installation>\VC\Auxiliary\Build\vcvarsall.bat" <arch> >nul
 *       && echo ::prk-where:: && where cl.exe && echo ::prk-environment:: && set
 * Nothing reaches cmd.exe from a script except an architecture from a fixed list; an installation path with
 * '"', '%', or a line break is rejected. Discovery takes a few seconds, so callers keep one PrkMsvcToolchain
 * as a cache (the script host does).
 */
#ifndef PROOFRUNKIT_PRK_MSVC_TOOLCHAIN_H
#define PROOFRUNKIT_PRK_MSVC_TOOLCHAIN_H

#include <stddef.h>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_process.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Timeout of each discovery step (vcvarsall.bat is slow on a cold disk). */
#define PRK_MSVC_DISCOVERY_TIMEOUT_MS 120000u

/** @brief A discovered MSVC toolchain, or why none was found. Release with prk_release_msvc_toolchain(). */
typedef struct PrkMsvcToolchain {
    int is_attempted;             /**< 1 once discovery ran (found or not), so a cache does not retry. */
    int is_found;                 /**< 1 when cl.exe was found and the fields below are set. */
    char architecture[16];        /**< vcvarsall architecture used, e.g. "x64". */
    char installation_path[1024]; /**< Visual Studio installation folder from vswhere. */
    char cl_path[1024];           /**< Full path of cl.exe. */
    char lib_path[1024];          /**< Full path of lib.exe (next to cl.exe). */
    char** environment;           /**< "NAME=VALUE" entries from vcvarsall (malloc'd); the whole child environment. */
    size_t environment_count;     /**< Entries in @p environment. */
    char error_message[256];      /**< Why discovery failed, when it did. */
} PrkMsvcToolchain;

/**
 * @brief Returns the vcvarsall architecture matching this build of ProofRunKit: "x64", "x86", or "arm64".
 * @return A static string. Pure function.
 */
const char* prk_get_native_msvc_architecture(void);

/**
 * @brief Reports whether @p architecture is one vcvarsall accepts (x64, x86, arm64, x64_x86, x64_arm64,
 *        x86_x64, x86_arm64, amd64, amd64_x86, amd64_arm64). @param architecture Name to check; may be NULL.
 * @return 1 when allowed, else 0. Pure function.
 */
int prk_is_msvc_architecture_allowed(const char* architecture);

/**
 * @brief Finds MSVC with vswhere and vcvarsall.bat.
 * @param runner Process runner. @param architecture vcvarsall architecture; NULL selects the native one.
 * @param out_toolchain Receives the toolchain (a documented mutator); is_found = 0 with error_message set when
 *        Visual Studio, the C++ workload, or cl.exe is missing (a result, not an error status).
 * @return PRK_STATUS_OK, PRK_STATUS_INVALID_ARGUMENT (bad runner or architecture), or PRK_STATUS_OUT_OF_MEMORY.
 *         Side effects: runs cmd.exe twice.
 */
PrkStatus prk_discover_msvc_toolchain(const PrkProcessRunner* runner, const char* architecture,
                                      PrkMsvcToolchain* out_toolchain);

/** @brief Frees a toolchain and resets it. @param toolchain Toolchain to release (a documented mutator); may be NULL.
 */
void prk_release_msvc_toolchain(PrkMsvcToolchain* toolchain);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PROOFRUNKIT_PRK_MSVC_TOOLCHAIN_H */

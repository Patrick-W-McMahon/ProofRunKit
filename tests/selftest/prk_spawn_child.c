/**
 * @file prk_spawn_child.c
 * @brief A tiny, predictable child program for the process-spawn self-tests (#13). Not installed.
 *
 * Usage: prk_spawn_child <mode> [values...]
 *   echo ARG...   prints each argument as "[ARG]" on its own line (checks argument quoting)
 *   exit N        prints "out" to stdout and "err" to stderr, then exits with N
 *   env NAME      prints the variable's value, or "(unset)"
 *   cwd           prints the current directory
 *   cat           copies stdin to stdout
 *   sleep MS      sleeps, then exits 0
 *   flood N       writes N bytes to stdout and N bytes to stderr
 */
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "proofrunkit/prk_process.h"

static void sleep_for_milliseconds(long milliseconds) {
#if defined(_WIN32)
    Sleep((DWORD)milliseconds);
#else
    const struct timespec pause = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    (void)nanosleep(&pause, NULL);
#endif
}

static int print_current_directory(void) {
    char directory[4096];
#if defined(_WIN32)
    if (_getcwd(directory, (int)sizeof directory) == NULL) {
#else
    if (getcwd(directory, sizeof directory) == NULL) {
#endif
        return 2;
    }
    (void)printf("%s\n", directory);
    return 0;
}

static int copy_input_to_output(void) {
    char chunk[4096];
    size_t count;
    while ((count = fread(chunk, 1, sizeof chunk, stdin)) > 0) {
        (void)fwrite(chunk, 1, count, stdout);
    }
    return 0;
}

static int flood_both_streams(long byte_count) {
    for (long index = 0; index < byte_count; ++index) {
        (void)fputc('o', stdout);
        (void)fputc('e', stderr);
    }
    return 0;
}

int main(int argc, char** argv) {
#if defined(_WIN32)
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    (void)_setmode(_fileno(stderr), _O_BINARY);
#endif
    const char* const mode = argc > 1 ? argv[1] : "";
    if (strcmp(mode, "echo") == 0) {
        for (int index = 2; index < argc; ++index) {
            (void)printf("[%s]\n", argv[index]);
        }
        return 0;
    }
    if (strcmp(mode, "exit") == 0 && argc > 2) {
        (void)fputs("out\n", stdout);
        (void)fputs("err\n", stderr);
        return atoi(argv[2]);
    }
    if (strcmp(mode, "env") == 0 && argc > 2) {
        static char value[32768];
        const int is_set = prk_read_environment_variable(argv[2], value, sizeof value);
        (void)printf("%s\n", is_set ? value : "(unset)");
        return 0;
    }
    if (strcmp(mode, "cwd") == 0) {
        return print_current_directory();
    }
    if (strcmp(mode, "cat") == 0) {
        return copy_input_to_output();
    }
    if (strcmp(mode, "sleep") == 0 && argc > 2) {
        sleep_for_milliseconds(atol(argv[2]));
        return 0;
    }
    if (strcmp(mode, "flood") == 0 && argc > 2) {
        return flood_both_streams(atol(argv[2]));
    }
    (void)fputs("usage: prk_spawn_child echo|exit|env|cwd|cat|sleep|flood ...\n", stderr);
    return 64;
}

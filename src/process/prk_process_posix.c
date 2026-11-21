/**
 * @file prk_process_posix.c
 * @brief POSIX adapter of the process-spawn layer (#13): fork/exec in a new process group, pipes for
 *        stdin/stdout/stderr, poll() with a monotonic deadline, and SIGKILL of the whole group on timeout.
 *
 * This is an IO edge: everything decidable without the operating system lives in prk_process.c. It never
 * uses a shell. Linux/macOS are not yet supported spawn hosts (#34, #35); this adapter lets the box
 * builds and sanitizers exercise the layer.
 */
#if !defined(_WIN32)

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "prk_process_internal.h"

extern char** environ;

/** @brief Why the child could not exec, sent to the parent over a close-on-exec pipe. */
typedef struct ChildStartError {
    int is_working_directory_error; /**< 1: chdir failed; 0: exec failed. */
    int error_number;               /**< errno of the failed call. */
} ChildStartError;

/** @brief The pipes and buffers of one spawn. */
typedef struct PosixSpawn {
    int input_fd;  /**< Parent's write end of the child's stdin, or -1. */
    int output_fd; /**< Parent's read end of the child's stdout, or -1. */
    int error_fd;  /**< Parent's read end of the child's stderr, or -1. */
    size_t input_written;
    PrkOutputBuffer output;
    PrkOutputBuffer error;
} PosixSpawn;

static uint64_t read_monotonic_ms(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void close_fd(int* fd) {
    if (*fd >= 0) {
        (void)close(*fd);
        *fd = -1;
    }
}

static int set_close_on_exec(int fd) {
    const int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

/** @brief Creates a pipe whose ends are both close-on-exec (the child's end is dup2'd, which clears it). */
static int open_pipe(int out_fds[2]) {
    if (pipe(out_fds) != 0) {
        return 0;
    }
    if (!set_close_on_exec(out_fds[0]) || !set_close_on_exec(out_fds[1])) {
        (void)close(out_fds[0]);
        (void)close(out_fds[1]);
        return 0;
    }
    return 1;
}

/** @brief Finds a bare program name on PATH. @return malloc'd path, or NULL when not found. */
static char* find_program_on_path(const char* name) {
    if (strchr(name, '/') != NULL) {
        return strdup(name);
    }
    const char* const path = getenv("PATH");
    const char* cursor = path != NULL ? path : "/usr/bin:/bin";
    for (;;) {
        const char* const end = strchr(cursor, ':');
        const size_t directory_length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        char* const candidate = (char*)malloc(directory_length + strlen(name) + 3);
        if (candidate == NULL) {
            return NULL;
        }
        (void)snprintf(candidate, directory_length + strlen(name) + 3, "%.*s/%s", (int)directory_length,
                       directory_length == 0 ? "." : cursor, name);
        struct stat status;
        if (stat(candidate, &status) == 0 && S_ISREG(status.st_mode) && access(candidate, X_OK) == 0) {
            return candidate;
        }
        free(candidate);
        if (end == NULL) {
            return NULL;
        }
        cursor = end + 1;
    }
}

/** @brief Runs in the forked child: wires the pipes, changes directory, and execs. Never returns. */
static void exec_child(const char* program_path, char* const* argv, char* const* envp, const char* working_directory,
                       const int input_pipe[2], const int output_pipe[2], const int error_pipe[2], int status_fd) {
    ChildStartError failure = {0, 0};
    (void)setpgid(0, 0);
    (void)signal(SIGPIPE, SIG_DFL);
    if (dup2(input_pipe[0], STDIN_FILENO) < 0 || dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
        dup2(error_pipe[1], STDERR_FILENO) < 0) {
        failure.error_number = errno;
    } else if (working_directory != NULL && working_directory[0] != '\0' && chdir(working_directory) != 0) {
        failure.is_working_directory_error = 1;
        failure.error_number = errno;
    } else {
        (void)execve(program_path, argv, envp);
        failure.error_number = errno;
    }
    (void)!write(status_fd, &failure, sizeof failure);
    _exit(127);
}

/** @brief Formats the start failure the child reported. */
static void describe_start_error(const ChildStartError* failure, const PrkProcessRequest* request, char* out_text,
                                 size_t capacity) {
    if (failure->is_working_directory_error) {
        (void)snprintf(out_text, capacity, "cannot change to working directory '%s': %s", request->working_directory,
                       strerror(failure->error_number));
    } else if (failure->error_number == ENOENT) {
        (void)snprintf(out_text, capacity, "executable not found: %s", request->executable);
    } else {
        (void)snprintf(out_text, capacity, "cannot start '%s': %s", request->executable,
                       strerror(failure->error_number));
    }
}

/** @brief Reads what is available on @p fd into @p buffer; closes the fd at end of file. */
static void drain_pipe(int* fd, PrkOutputBuffer* buffer) {
    char chunk[8192];
    const ssize_t count = read(*fd, chunk, sizeof chunk);
    if (count > 0) {
        prk_append_process_output(buffer, chunk, (size_t)count);
    } else if (count == 0 || (errno != EINTR && errno != EAGAIN)) {
        close_fd(fd);
    }
}

/** @brief Writes more of the request's stdin; closes the pipe when done or when the child stops reading. */
static void feed_input(PosixSpawn* spawn, const PrkProcessRequest* request) {
    const size_t remaining = request->standard_input_length - spawn->input_written;
    const ssize_t count = remaining == 0 ? 0
                                         : write(spawn->input_fd, request->standard_input + spawn->input_written,
                                                 remaining < 65536 ? remaining : 65536);
    if (count > 0) {
        spawn->input_written += (size_t)count;
    }
    if (spawn->input_written == request->standard_input_length || (count < 0 && errno != EINTR && errno != EAGAIN)) {
        close_fd(&spawn->input_fd);
    }
}

/** @brief Pumps the pipes until both outputs close or the deadline passes. @return 1 when the deadline passed. */
static int pump_pipes(PosixSpawn* spawn, const PrkProcessRequest* request, uint64_t deadline_ms) {
    while (spawn->output_fd >= 0 || spawn->error_fd >= 0) {
        const uint64_t now = read_monotonic_ms();
        if (now >= deadline_ms) {
            return 1;
        }
        struct pollfd fds[3];
        nfds_t count = 0;
        const int fd_list[3] = {spawn->output_fd, spawn->error_fd, spawn->input_fd};
        for (int index = 0; index < 3; ++index) {
            if (fd_list[index] >= 0) {
                fds[count].fd = fd_list[index];
                fds[count].events = index == 2 ? POLLOUT : POLLIN;
                fds[count].revents = 0;
                ++count;
            }
        }
        const uint64_t wait_ms = deadline_ms - now;
        if (poll(fds, count, wait_ms > 1000 ? 1000 : (int)wait_ms) <= 0) {
            continue;
        }
        for (nfds_t index = 0; index < count; ++index) {
            if (fds[index].revents == 0) {
                continue;
            }
            if (fds[index].fd == spawn->output_fd) {
                drain_pipe(&spawn->output_fd, &spawn->output);
            } else if (fds[index].fd == spawn->error_fd) {
                drain_pipe(&spawn->error_fd, &spawn->error);
            } else if ((fds[index].revents & (POLLERR | POLLHUP)) != 0) {
                close_fd(&spawn->input_fd);
            } else {
                feed_input(spawn, request);
            }
        }
    }
    return 0;
}

/** @brief Waits for the child to exit, killing its process group at the deadline. @return the wait status. */
static int reap_child(pid_t pid, uint64_t deadline_ms, int* out_is_killed) {
    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid || (done < 0 && errno != EINTR)) {
            return status;
        }
        if (read_monotonic_ms() >= deadline_ms && !*out_is_killed) {
            (void)kill(-pid, SIGKILL);
            (void)kill(pid, SIGKILL);
            *out_is_killed = 1;
        }
        const struct timespec pause = {0, 2000000};
        (void)nanosleep(&pause, NULL);
    }
}

/** @brief Fills the result after the child was started. */
static PrkStatus finish_result(PosixSpawn* spawn, int status, int is_killed, uint64_t started_ms,
                               PrkProcessResult* out_result) {
    memset(out_result, 0, sizeof *out_result);
    if (is_killed) {
        out_result->outcome = PRK_PROCESS_TIMED_OUT;
    } else if (WIFSIGNALED(status)) {
        out_result->outcome = PRK_PROCESS_SIGNALED;
        out_result->termination_signal = WTERMSIG(status);
    } else {
        out_result->outcome = PRK_PROCESS_EXITED;
        out_result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    out_result->elapsed_ms = read_monotonic_ms() - started_ms;
    if (prk_take_process_output(&spawn->output, &out_result->standard_output, &out_result->standard_output_length,
                                &out_result->is_standard_output_truncated) != PRK_STATUS_OK) {
        prk_free_process_output(&spawn->error);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    if (prk_take_process_output(&spawn->error, &out_result->standard_error, &out_result->standard_error_length,
                                &out_result->is_standard_error_truncated) != PRK_STATUS_OK) {
        prk_release_process_result(out_result);
        return PRK_STATUS_OUT_OF_MEMORY;
    }
    return PRK_STATUS_OK;
}

/** @brief Builds argv: the program name as given, then the arguments, then NULL. @return malloc'd, or NULL. */
static char** build_argument_vector(const PrkProcessRequest* request) {
    char** const argv = (char**)malloc((request->argument_count + 2) * sizeof *argv);
    if (argv != NULL) {
        /* execve takes char* const[] for historical reasons; it does not modify the strings. */
        argv[0] = (char*)request->executable;
        for (size_t index = 0; index < request->argument_count; ++index) {
            argv[index + 1] = (char*)request->arguments[index];
        }
        argv[request->argument_count + 1] = NULL;
    }
    return argv;
}

/** @brief Waits until exec succeeds (EOF) or the child reports why it failed. @return 1 when it failed. */
static int read_child_start_error(int* status_fd, ChildStartError* out_failure) {
    ssize_t count;
    do {
        count = read(*status_fd, out_failure, sizeof *out_failure);
    } while (count < 0 && errno == EINTR);
    close_fd(status_fd);
    return count == (ssize_t)sizeof *out_failure;
}

/** @brief Forks and execs; then pumps, reaps, and fills the result. Pipes are open on entry. */
static PrkStatus spawn_with_pipes(const PrkProcessRequest* request, const char* program_path, char** argv,
                                  const char** envp, int pipes[4][2], PrkProcessResult* out_result) {
    const uint64_t started_ms = read_monotonic_ms();
    const uint64_t deadline_ms = started_ms + prk_get_effective_process_timeout_ms(request);
    const pid_t pid = fork();
    if (pid < 0) {
        return prk_set_process_start_failure("cannot start a process: fork failed", out_result);
    }
    if (pid == 0) {
        (void)close(pipes[3][0]);
        exec_child(program_path, argv, (char* const*)envp, request->working_directory, pipes[0], pipes[1], pipes[2],
                   pipes[3][1]);
    }
    (void)setpgid(pid, pid); /* Also in the child: whichever runs first wins the race. */
    close_fd(&pipes[0][0]);
    close_fd(&pipes[1][1]);
    close_fd(&pipes[2][1]);
    close_fd(&pipes[3][1]);
    ChildStartError failure = {0, 0};
    const int has_start_failed = read_child_start_error(&pipes[3][0], &failure);
    PosixSpawn spawn = {pipes[0][1], pipes[1][0], pipes[2][0], 0, {0}, {0}};
    pipes[0][1] = pipes[1][0] = pipes[2][0] = -1;
    const size_t limit =
        request->output_limit_bytes != 0 ? request->output_limit_bytes : PRK_PROCESS_DEFAULT_OUTPUT_LIMIT_BYTES;
    spawn.output.limit = spawn.error.limit = limit;
    if (has_start_failed) {
        close_fd(&spawn.input_fd);
        close_fd(&spawn.output_fd);
        close_fd(&spawn.error_fd);
        int ignored = 0;
        (void)reap_child(pid, deadline_ms, &ignored);
        char message[PRK_PROCESS_ERROR_CAPACITY];
        describe_start_error(&failure, request, message, sizeof message);
        return prk_set_process_start_failure(message, out_result);
    }
    (void)fcntl(spawn.input_fd, F_SETFL, fcntl(spawn.input_fd, F_GETFL) | O_NONBLOCK);
    if (request->standard_input_length == 0) {
        close_fd(&spawn.input_fd);
    }
    int is_killed = pump_pipes(&spawn, request, deadline_ms);
    if (is_killed) {
        (void)kill(-pid, SIGKILL);
        (void)kill(pid, SIGKILL);
    }
    close_fd(&spawn.input_fd);
    close_fd(&spawn.output_fd);
    close_fd(&spawn.error_fd);
    const int status = reap_child(pid, deadline_ms, &is_killed);
    return finish_result(&spawn, status, is_killed, started_ms, out_result);
}

PrkStatus prk_spawn_system_process(void* runner_state, const PrkProcessRequest* request, PrkProcessResult* out_result) {
    (void)runner_state;
    if (out_result == NULL || prk_check_process_request(request) != PRK_STATUS_OK) {
        return PRK_STATUS_INVALID_ARGUMENT;
    }
    char* const program_path = find_program_on_path(request->executable);
    if (program_path == NULL) {
        char message[PRK_PROCESS_ERROR_CAPACITY];
        (void)snprintf(message, sizeof message, "executable not found: %s", request->executable);
        return prk_set_process_start_failure(message, out_result);
    }
    size_t parent_count = 0;
    while (environ != NULL && environ[parent_count] != NULL) {
        ++parent_count;
    }
    const char** envp = NULL;
    size_t env_count = 0;
    char** const argv = build_argument_vector(request);
    PrkStatus status = argv == NULL ? PRK_STATUS_OUT_OF_MEMORY
                                    : prk_merge_process_environment((const char* const*)environ, parent_count, request,
                                                                    0, &envp, &env_count);
    int pipes[4][2] = {{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}};
    if (status == PRK_STATUS_OK) {
        if (!open_pipe(pipes[0]) || !open_pipe(pipes[1]) || !open_pipe(pipes[2]) || !open_pipe(pipes[3])) {
            status = prk_set_process_start_failure("cannot start a process: no pipes available", out_result);
        } else {
            /* A child that exits before reading its stdin must not kill ProofRunKit with SIGPIPE. */
            struct sigaction ignore_pipe;
            struct sigaction previous;
            memset(&ignore_pipe, 0, sizeof ignore_pipe);
            ignore_pipe.sa_handler = SIG_IGN;
            (void)sigaction(SIGPIPE, &ignore_pipe, &previous);
            status = spawn_with_pipes(request, program_path, argv, envp, pipes, out_result);
            (void)sigaction(SIGPIPE, &previous, NULL);
        }
    }
    for (int index = 0; index < 4; ++index) {
        close_fd(&pipes[index][0]);
        close_fd(&pipes[index][1]);
    }
    free(envp);
    free(argv);
    free(program_path);
    return status;
}

#endif /* !_WIN32 */

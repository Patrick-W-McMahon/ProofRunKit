/**
 * @file prk_c_diagnostics.c
 * @brief Compiler diagnostics parsing for the C/C++ driver (#14): GCC/Clang
 *        "file:line:col: severity: message [-Wflag]" and MSVC "file(line,col): severity CODE: message" lines,
 *        including tool lines such as "collect2: error: ..." and "LINK : fatal error LNK1561: ...".
 *
 * Pure functions: no allocation, no I/O, no global state.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "proofrunkit/prk_c_driver.h"

/** @brief Copies at most @p length bytes of @p text into a terminated buffer. */
static void copy_text(const char* text, size_t length, char* out_text, size_t capacity) {
    const size_t copied = length < capacity - 1 ? length : capacity - 1;
    memcpy(out_text, text, copied);
    out_text[copied] = '\0';
}

/** @brief Severity tokens, searched in order of preference. */
typedef struct SeverityToken {
    const char* text;
    PrkDiagnosticSeverity severity;
} SeverityToken;

/** @brief Finds the earliest token in @p line. @return its position, or NULL. */
static const char* find_earliest_token(const char* line, const SeverityToken* tokens, size_t token_count,
                                       const SeverityToken** out_token) {
    const char* best = NULL;
    for (size_t index = 0; index < token_count; ++index) {
        const char* const found = strstr(line, tokens[index].text);
        if (found != NULL && (best == NULL || found < best)) {
            best = found;
            *out_token = &tokens[index];
        }
    }
    return best;
}

/** @brief Parses a trailing ":<digits>" ending at @p end (exclusive). @return start of ':' or NULL. */
static const char* parse_trailing_number(const char* start, const char* end, unsigned* out_number) {
    const char* cursor = end;
    while (cursor > start && isdigit((unsigned char)cursor[-1])) {
        --cursor;
    }
    if (cursor == end || cursor == start || cursor[-1] != ':') {
        return NULL;
    }
    *out_number = (unsigned)strtoul(cursor, NULL, 10);
    return cursor - 1;
}

/** @brief Moves a trailing GCC/Clang option such as " [-Wunused-variable]" from the message into the code. */
static void split_gcc_option_code(PrkCompilerDiagnostic* diagnostic) {
    const size_t length = strlen(diagnostic->message);
    char* const open = strrchr(diagnostic->message, '[');
    if (length > 3 && diagnostic->message[length - 1] == ']' && open != NULL && open[1] == '-' &&
        open > diagnostic->message && open[-1] == ' ') {
        copy_text(open + 1, (size_t)(diagnostic->message + length - 1 - (open + 1)), diagnostic->code,
                  sizeof diagnostic->code);
        open[-1] = '\0';
    }
}

/** @brief Parses "file:line:col: severity: message" (GCC and Clang). */
static int parse_gcc_diagnostic(const char* line, PrkCompilerDiagnostic* out_diagnostic) {
    static const SeverityToken tokens[] = {{": fatal error: ", PRK_DIAGNOSTIC_ERROR},
                                           {": error: ", PRK_DIAGNOSTIC_ERROR},
                                           {": warning: ", PRK_DIAGNOSTIC_WARNING},
                                           {": note: ", PRK_DIAGNOSTIC_NOTE}};
    const SeverityToken* token = NULL;
    const char* const found = find_earliest_token(line, tokens, sizeof tokens / sizeof tokens[0], &token);
    if (found == NULL || found == line) {
        return 0;
    }
    PrkCompilerDiagnostic diagnostic;
    memset(&diagnostic, 0, sizeof diagnostic);
    diagnostic.severity = token->severity;
    const char* file_end = found;
    unsigned last = 0;
    const char* const after_last = parse_trailing_number(line, file_end, &last);
    if (after_last != NULL) {
        unsigned first = 0;
        const char* const after_first = parse_trailing_number(line, after_last, &first);
        diagnostic.line = after_first != NULL ? first : last;
        diagnostic.column = after_first != NULL ? last : 0;
        file_end = after_first != NULL ? after_first : after_last;
    }
    copy_text(line, (size_t)(file_end - line), diagnostic.file, sizeof diagnostic.file);
    const char* const message = found + strlen(token->text);
    copy_text(message, strlen(message), diagnostic.message, sizeof diagnostic.message);
    split_gcc_option_code(&diagnostic);
    *out_diagnostic = diagnostic;
    return 1;
}

/** @brief Parses "file(line[,col]): severity CODE: message" and "LINK : fatal error LNK1104: ..." (MSVC). */
static int parse_msvc_diagnostic(const char* line, PrkCompilerDiagnostic* out_diagnostic) {
    static const SeverityToken tokens[] = {
        {": fatal error ", PRK_DIAGNOSTIC_ERROR}, {": command line error ", PRK_DIAGNOSTIC_ERROR},
        {": error ", PRK_DIAGNOSTIC_ERROR},       {": command line warning ", PRK_DIAGNOSTIC_WARNING},
        {": warning ", PRK_DIAGNOSTIC_WARNING},   {": note: ", PRK_DIAGNOSTIC_NOTE}};
    const SeverityToken* token = NULL;
    const char* const found = find_earliest_token(line, tokens, sizeof tokens / sizeof tokens[0], &token);
    if (found == NULL || found == line) {
        return 0;
    }
    PrkCompilerDiagnostic diagnostic;
    memset(&diagnostic, 0, sizeof diagnostic);
    diagnostic.severity = token->severity;
    const char* message = found + strlen(token->text);
    if (token->severity != PRK_DIAGNOSTIC_NOTE) {
        const char* const code_end = strstr(message, ": ");
        if (code_end == NULL || code_end == message || memchr(message, ' ', (size_t)(code_end - message)) != NULL) {
            return 0;
        }
        copy_text(message, (size_t)(code_end - message), diagnostic.code, sizeof diagnostic.code);
        message = code_end + 2;
    }
    const char* file_end = found;
    while (file_end > line && file_end[-1] == ' ') {
        --file_end;
    }
    if (file_end > line && file_end[-1] == ')') {
        const char* open = file_end - 1;
        while (open > line && *open != '(') {
            --open;
        }
        if (*open == '(' && isdigit((unsigned char)open[1])) {
            char* after_line = NULL;
            diagnostic.line = (unsigned)strtoul(open + 1, &after_line, 10);
            diagnostic.column = *after_line == ',' ? (unsigned)strtoul(after_line + 1, NULL, 10) : 0;
            file_end = open;
        }
    }
    copy_text(line, (size_t)(file_end - line), diagnostic.file, sizeof diagnostic.file);
    copy_text(message, strlen(message), diagnostic.message, sizeof diagnostic.message);
    *out_diagnostic = diagnostic;
    return 1;
}

int prk_parse_compiler_diagnostic(PrkCompilerFamily family, const char* line, PrkCompilerDiagnostic* out_diagnostic) {
    if (line == NULL || out_diagnostic == NULL) {
        return 0;
    }
    return family == PRK_COMPILER_MSVC ? parse_msvc_diagnostic(line, out_diagnostic)
                                       : parse_gcc_diagnostic(line, out_diagnostic);
}

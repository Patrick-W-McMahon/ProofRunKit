/**
 * @file script_loading.hpp
 * @brief Loads test scripts named on the command line into a registry (#9, #12, #11).
 *
 * File reading is injected (ScriptFileReader) so the loader can be tested without a disk; the
 * only real file IO is read_script_file_from_disk(), used by main().
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "proofrunkit/prk.h"
#include "proofrunkit/prk_js.h"
#include "proofrunkit/prk_lua.h"

namespace prk {

/** @brief Result of reading one script file. */
struct ScriptFileContent {
    bool was_read = false;      ///< True when @p text holds the whole file.
    std::string text;           ///< File contents (bytes, unmodified).
    std::string error_message;  ///< Why the file could not be read, when was_read is false.
};

/** @brief Reads a script file by path. Injected so tests can supply in-memory scripts. */
using ScriptFileReader = std::function<ScriptFileContent(const std::string& path)>;

/** @brief Script language chosen from the file extension. */
enum class ScriptLanguage { Lua, JavaScript, Unsupported };

/**
 * @brief Chooses the script language from the path's extension (case-insensitive). Pure function.
 * @param path Script path.
 * @return Lua for `.lua`; JavaScript for `.js` / `.mjs`; Unsupported otherwise.
 */
[[nodiscard]] ScriptLanguage detect_script_language(std::string_view path);

/**
 * @brief Reads a whole file from disk in binary mode. The IO edge used by main().
 * @param path File to read.
 * @return Content, or was_read == false with an error message such as "cannot open file".
 */
[[nodiscard]] ScriptFileContent read_script_file_from_disk(const std::string& path);

/** @brief Deleters so loaded scripts and registries are released in the right order. */
struct LuaScriptDeleter {
    void operator()(PrkLuaScript* script) const noexcept { prk_destroy_lua_script(script); }
};
struct JsScriptDeleter {
    void operator()(PrkJsScript* script) const noexcept { prk_destroy_js_script(script); }
};
struct RegistryDeleter {
    void operator()(PrkRegistry* registry) const noexcept { prk_destroy_registry(registry); }
};

/**
 * @brief Tests loaded from scripts, plus the interpreter states their bodies need.
 *
 * Member order matters: the registry is destroyed before the scripts its bodies point into.
 */
struct LoadedTestScripts {
    std::vector<std::unique_ptr<PrkLuaScript, LuaScriptDeleter>> lua_scripts;  ///< One state per Lua script.
    std::vector<std::unique_ptr<PrkJsScript, JsScriptDeleter>> js_scripts;     ///< One runtime per JS script.
    std::unique_ptr<PrkRegistry, RegistryDeleter> registry;                    ///< Every loaded test.
    std::vector<std::string> load_errors;           ///< "path: message" for each script that failed.
    std::vector<std::string> unknown_type_scripts;  ///< Scripts with an unrecognised extension.
};

/**
 * @brief Loads every script into one new registry. Scripts are isolated: each gets its own Lua state or
 *        QuickJS-ng runtime.
 * @param script_paths Paths in command-line order.
 * @param read_script_file Reader for script contents.
 * @param host Services granted to every script (print, clock, read-only files).
 * @param settings Test selection by name and tags plus the default timeout; NULL selects all tests.
 * @param limits Sandbox limits for both engines; NULL selects the defaults.
 * @return The loaded tests and any per-script problems. registry is NULL only when memory runs out.
 * Side effects: runs each script's top-level code in the sandbox.
 */
[[nodiscard]] LoadedTestScripts load_test_scripts(const std::vector<std::string>& script_paths,
                                                  const ScriptFileReader& read_script_file, const PrkScriptHost& host,
                                                  const PrkScriptRunSettings* settings, const PrkLuaLimits* limits);

}  // namespace prk

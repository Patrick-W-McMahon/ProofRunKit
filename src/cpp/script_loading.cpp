/**
 * @file script_loading.cpp
 * @brief Implementation of the test-script loader (#9). Only read_script_file_from_disk touches disk.
 */
#include "script_loading.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace prk {

namespace {

/** @brief Returns the lower-cased extension of @p path including the dot, or an empty string. */
std::string find_lowercase_extension(std::string_view path) {
    const std::size_t separator = path.find_last_of("/\\");
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (separator != std::string_view::npos && dot < separator)) {
        return {};
    }
    std::string extension(path.substr(dot));
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

/** @brief Loads one Lua script into @p loaded; records a "path: message" error on failure. */
void load_lua_test_script(const std::string& path, const ScriptFileReader& read_script_file, const PrkScriptHost& host,
                          const PrkScriptRunSettings* settings, const PrkLuaLimits* limits, LoadedTestScripts& loaded) {
    const ScriptFileContent content = read_script_file(path);
    if (!content.was_read) {
        loaded.load_errors.push_back(path + ": " + content.error_message);
        return;
    }
    std::unique_ptr<PrkLuaScript, LuaScriptDeleter> script(prk_create_lua_script(limits, &host, settings));
    if (!script) {
        loaded.load_errors.push_back(path + ": out of memory creating the Lua state");
        return;
    }
    std::array<char, PRK_LUA_ERROR_CAPACITY> error{};
    const PrkStatus status =
        prk_load_lua_test_script(script.get(), loaded.registry.get(), path.c_str(), content.text.data(),
                                 content.text.size(), error.data(), error.size());
    // Keep the state even on failure: tests registered before the error point into it.
    loaded.lua_scripts.push_back(std::move(script));
    if (status != PRK_STATUS_OK) {
        const std::string message = error[0] != '\0' ? std::string(error.data()) : prk_get_status_name(status);
        // Lua already prefixes syntax and runtime errors with "path:line:"; add the path otherwise.
        loaded.load_errors.push_back(message.starts_with(path + ":") ? message : path + ": " + message);
    }
}

}  // namespace

ScriptLanguage detect_script_language(std::string_view path) {
    const std::string extension = find_lowercase_extension(path);
    if (extension == ".lua") {
        return ScriptLanguage::Lua;
    }
    if (extension == ".js" || extension == ".mjs") {
        return ScriptLanguage::JavaScript;
    }
    return ScriptLanguage::Unsupported;
}

ScriptFileContent read_script_file_from_disk(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return ScriptFileContent{false, {}, "cannot open file"};
    }
    std::ostringstream text;
    text << file.rdbuf();
    if (file.bad()) {
        return ScriptFileContent{false, {}, "cannot read file"};
    }
    return ScriptFileContent{true, text.str(), {}};
}

LoadedTestScripts load_test_scripts(const std::vector<std::string>& script_paths,
                                    const ScriptFileReader& read_script_file, const PrkScriptHost& host,
                                    const PrkScriptRunSettings* settings, const PrkLuaLimits* limits) {
    LoadedTestScripts loaded;
    loaded.registry.reset(prk_create_registry());
    if (!loaded.registry) {
        return loaded;
    }
    for (const std::string& path : script_paths) {
        switch (detect_script_language(path)) {
            case ScriptLanguage::Lua:
                load_lua_test_script(path, read_script_file, host, settings, limits, loaded);
                break;
            case ScriptLanguage::JavaScript:
                loaded.unsupported_scripts.push_back(path);
                break;
            case ScriptLanguage::Unsupported:
                loaded.unknown_type_scripts.push_back(path);
                break;
        }
    }
    return loaded;
}

}  // namespace prk

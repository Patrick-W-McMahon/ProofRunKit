/**
 * @file script_loading.cpp
 * @brief Implementation of the test-script loader (#9, #11). Only read_script_file_from_disk touches disk.
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

/** @brief The C entry points of one script engine, so Lua and JavaScript share one loader. */
template <typename Script, typename Deleter>
struct ScriptEngineEntryPoints {
    const char* engine_name;
    Script* (*create)(const PrkScriptLimits*, const PrkScriptHost*, const PrkScriptRunSettings*);
    PrkStatus (*load)(Script*, PrkRegistry*, const char*, const char*, size_t, char*, size_t);
};

/** @brief Loads one script into @p loaded; records a "path: message" error on failure. */
template <typename Script, typename Deleter>
void load_engine_test_script(const ScriptEngineEntryPoints<Script, Deleter>& engine, const std::string& path,
                             const ScriptFileReader& read_script_file, const PrkScriptHost& host,
                             const PrkScriptRunSettings* settings, const PrkScriptLimits* limits,
                             std::vector<std::unique_ptr<Script, Deleter>>& scripts, LoadedTestScripts& loaded) {
    const ScriptFileContent content = read_script_file(path);
    if (!content.was_read) {
        loaded.load_errors.push_back(path + ": " + content.error_message);
        return;
    }
    std::unique_ptr<Script, Deleter> script(engine.create(limits, &host, settings));
    if (!script) {
        loaded.load_errors.push_back(path + ": out of memory creating the " + engine.engine_name + " state");
        return;
    }
    std::array<char, PRK_SCRIPT_ERROR_CAPACITY> error{};
    const PrkStatus status = engine.load(script.get(), loaded.registry.get(), path.c_str(), content.text.data(),
                                         content.text.size(), error.data(), error.size());
    // Keep the state even on failure: tests registered before the error point into it.
    scripts.push_back(std::move(script));
    if (status != PRK_STATUS_OK) {
        const std::string message = error[0] != '\0' ? std::string(error.data()) : prk_get_status_name(status);
        // The engines usually prefix errors with "path:line:"; add the path otherwise.
        loaded.load_errors.push_back(message.starts_with(path + ":") ? message : path + ": " + message);
    }
}

constexpr ScriptEngineEntryPoints<PrkLuaScript, LuaScriptDeleter> kLuaEngine{"Lua", &prk_create_lua_script,
                                                                             &prk_load_lua_test_script};
constexpr ScriptEngineEntryPoints<PrkJsScript, JsScriptDeleter> kJsEngine{"JavaScript", &prk_create_js_script,
                                                                          &prk_load_js_test_script};

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
                load_engine_test_script(kLuaEngine, path, read_script_file, host, settings, limits, loaded.lua_scripts,
                                        loaded);
                break;
            case ScriptLanguage::JavaScript:
                load_engine_test_script(kJsEngine, path, read_script_file, host, settings, limits, loaded.js_scripts,
                                        loaded);
                break;
            case ScriptLanguage::Unsupported:
                loaded.unknown_type_scripts.push_back(path);
                break;
        }
    }
    return loaded;
}

}  // namespace prk

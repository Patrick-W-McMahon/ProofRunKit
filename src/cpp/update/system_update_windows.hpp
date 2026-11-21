/**
 * @file system_update_windows.hpp
 * @brief Windows edges of `prk -update` (#75): one WinHTTP GET and a detached msiexec start. Windows only.
 */
#pragma once

#include <string>
#include <string_view>

#include "update_command.hpp"

namespace prk {

/** @brief Largest response kept in memory (512 MiB); larger downloads fail. */
inline constexpr std::size_t kMaximumHttpBodyBytes = std::size_t{512} * 1024 * 1024;

/**
 * @brief GETs an https URL with WinHTTP (system proxy, redirects followed, 30 s connect / 120 s receive timeouts).
 * @param url Absolute https URL. @param accept Accept header value.
 * @return The response, or one with error set. Side effects: network access.
 */
[[nodiscard]] HttpResponse get_with_winhttp(const std::string& url, std::string_view accept);

/**
 * @brief Starts "%SystemRoot%\System32\msiexec.exe /i "PATH" /passive" without waiting for it.
 * @param msi_path Installer path (a file name checked by is_safe_asset_name in the download folder).
 * @return "" or the reason it failed. Side effects: starts a process.
 */
[[nodiscard]] std::string launch_msiexec_detached(const std::string& msi_path);

}  // namespace prk

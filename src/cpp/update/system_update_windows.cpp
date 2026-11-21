/**
 * @file system_update_windows.cpp
 * @brief Windows edges of `prk -update` (#75): WinHTTP GET and a detached msiexec start. Built on Windows only.
 */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "system_update_windows.hpp"

#include <windows.h>
#include <winhttp.h>

#include <memory>
#include <string>
#include <vector>

#include "proofrunkit/prk.h"

namespace prk {

namespace {

/** @brief Closes a WinHTTP handle when it goes out of scope. */
struct InternetHandleCloser {
    void operator()(void* handle) const { (void)WinHttpCloseHandle(handle); }
};
using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

/** @brief Converts UTF-8 to UTF-16 ("" on failure). */
std::wstring convert_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0) {
        (void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    }
    return wide;
}

std::string describe_error(const char* step) {
    return std::string(step) + " failed (Windows error " + std::to_string(GetLastError()) + ")";
}

/** @brief Opens the session with the system proxy settings. */
InternetHandle open_session() {
    const std::wstring agent = convert_to_wide(std::string("ProofRunKit/") + prk_get_version_string());
#if defined(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
    HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session != nullptr) {
        return InternetHandle(session);
    }
#endif
    return InternetHandle(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                      WINHTTP_NO_PROXY_BYPASS, 0));
}

/** @brief Reads the whole body of @p request into @p out_body. @return "" or the reason. */
std::string read_body(HINTERNET request, std::string& out_body) {
    std::vector<char> chunk(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), static_cast<DWORD>(chunk.size()), &read)) {
            return describe_error("reading the response");
        }
        if (read == 0) {
            return {};
        }
        if (out_body.size() + read > kMaximumHttpBodyBytes) {
            return "the response is larger than 512 MiB";
        }
        out_body.append(chunk.data(), read);
    }
}

/** @brief Sends the request and fills @p out_response. */
void send_request(HINTERNET request, std::string_view accept, HttpResponse& out_response) {
    const std::wstring headers = convert_to_wide("Accept: " + std::string(accept) + "\r\n");
    if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        out_response.error = describe_error("the HTTPS request");
        return;
    }
    DWORD status = 0;
    DWORD size = sizeof status;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
        out_response.error = describe_error("reading the HTTP status");
        return;
    }
    out_response.status_code = static_cast<int>(status);
    out_response.error = read_body(request, out_response.body);
}

}  // namespace

HttpResponse get_with_winhttp(const std::string& url, std::string_view accept) {
    HttpResponse response;
    std::wstring wide_url = convert_to_wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (wide_url.empty() || !WinHttpCrackUrl(wide_url.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS) {
        response.error = "not an https URL: " + url;
        return response;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                              std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    const InternetHandle session = open_session();
    if (!session) {
        response.error = describe_error("WinHttpOpen");
        return response;
    }
    (void)WinHttpSetTimeouts(session.get(), 30000, 30000, 30000, 120000);
    const InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0));
    const InternetHandle request(connection ? WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                                                 WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                 WINHTTP_FLAG_SECURE)
                                            : nullptr);
    if (!request) {
        response.error = describe_error("connecting to the server");
        return response;
    }
    send_request(request.get(), accept, response);
    return response;
}

std::string launch_msiexec_detached(const std::string& msi_path) {
    std::vector<wchar_t> system_folder(MAX_PATH + 1);
    const UINT length = GetSystemDirectoryW(system_folder.data(), static_cast<UINT>(system_folder.size()));
    if (length == 0 || length >= system_folder.size()) {
        return describe_error("GetSystemDirectoryW");
    }
    const std::wstring program = std::wstring(system_folder.data(), length) + L"\\msiexec.exe";
    std::wstring command_line = L"\"" + program + L"\" /i \"" + convert_to_wide(msi_path) + L"\" /passive";
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(program.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &startup, &process)) {
        return describe_error("starting msiexec");
    }
    (void)CloseHandle(process.hThread);
    (void)CloseHandle(process.hProcess);
    return {};
}

}  // namespace prk

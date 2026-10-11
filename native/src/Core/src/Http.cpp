// OpenNOW native client — HTTP transport.
//
// Windows implementation uses WinHTTP: no third-party TLS dependency, correct
// system proxy handling (important for corporate networks the GFN client is
// often used on), and HTTP/2 where NVIDIA endpoints negotiate it.

#include "opennow/core/Http.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace opennow::core {
namespace {

bool HeaderNameEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<std::string> HttpResponse::Header(std::string_view name) const {
    for (const HttpHeader& header : headers) {
        if (HeaderNameEquals(header.name, name)) return header.value;
    }
    return std::nullopt;
}

std::string UrlEncode(std::string_view value) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (const char c : value) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                                byte == '.' || byte == '~';
        if (unreserved) {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[(byte >> 4) & 0xF]);
            out.push_back(kHex[byte & 0xF]);
        }
    }
    return out;
}

std::string FormEncode(const std::vector<std::pair<std::string, std::string>>& fields) {
    // application/x-www-form-urlencoded: identical to UrlEncode except that a
    // space becomes '+' instead of %20. NVIDIA's token endpoint is strict here.
    static const char* kHex = "0123456789ABCDEF";
    auto encode = [](std::string_view value) {
        std::string out;
        out.reserve(value.size() * 3);
        for (const char c : value) {
            const auto byte = static_cast<unsigned char>(c);
            const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                    (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                                    byte == '.' || byte == '~';
            if (unreserved) {
                out.push_back(c);
            } else if (c == ' ') {
                out.push_back('+');
            } else {
                out.push_back('%');
                out.push_back(kHex[(byte >> 4) & 0xF]);
                out.push_back(kHex[byte & 0xF]);
            }
        }
        return out;
    };

    std::string out;
    for (const auto& [name, value] : fields) {
        if (!out.empty()) out.push_back('&');
        out += encode(name);
        out.push_back('=');
        out += encode(value);
    }
    return out;
}

std::string BuildQueryString(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string out;
    for (const auto& [name, value] : fields) {
        if (!out.empty()) out.push_back('&');
        out += UrlEncode(name);
        out.push_back('=');
        out += UrlEncode(value);
    }
    return out;
}

#ifdef _WIN32

namespace {

class WinHttpClient final : public IHttpClient {
public:
    HttpResponse Send(const HttpRequest& request) override {
        HttpResponse response;

        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwSchemeLength = 1;
        components.dwHostNameLength = 1;
        components.dwUrlPathLength = 1;
        components.dwExtraInfoLength = 1;

        std::wstring wideUrl = Utf8ToWide(request.url);
        if (!WinHttpCrackUrl(wideUrl.c_str(), static_cast<DWORD>(wideUrl.size()), 0,
                             &components)) {
            response.transportError = "WinHttpCrackUrl failed (malformed URL)";
            return response;
        }

        const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
        const std::wstring host(components.lpszHostName, components.dwHostNameLength);
        const std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
        const std::wstring extra(components.lpszExtraInfo ? components.lpszExtraInfo : L"",
                                 components.lpszExtraInfo ? components.dwExtraInfoLength : 0);

        HINTERNET session = WinHttpOpen(kUserAgentW, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (session == nullptr) {
            response.transportError = "WinHttpOpen failed";
            return response;
        }

        const DWORD timeoutMs = static_cast<DWORD>(request.timeout.count());
        WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

        HINTERNET connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
        if (connection == nullptr) {
            response.transportError = "WinHttpConnect failed";
            WinHttpCloseHandle(session);
            return response;
        }

        const std::wstring target = path + extra;
        const std::wstring method = Utf8ToWide(request.method);
        DWORD flags = WINHTTP_FLAG_REFRESH;
        if (secure) flags |= WINHTTP_FLAG_SECURE;

        HINTERNET requestHandle = WinHttpOpenRequest(
            connection, method.c_str(), target.empty() ? L"/" : target.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (requestHandle == nullptr) {
            response.transportError = "WinHttpOpenRequest failed";
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        if (secure && !request.strictTls) {
            DWORD securityFlags = SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                  SECURITY_FLAG_IGNORE_UNKNOWN_CA;
            WinHttpSetOption(requestHandle, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags,
                             sizeof(securityFlags));
        }

        if (!request.followRedirects) {
            DWORD disable = WINHTTP_DISABLE_REDIRECTS;
            WinHttpSetOption(requestHandle, WINHTTP_OPTION_DISABLE_FEATURE, &disable,
                             sizeof(disable));
        }

        // Collect headers into one CRLF-separated block; WinHTTP wants them all
        // in a single call.
        std::wstring headerBlock;
        for (const HttpHeader& header : request.headers) {
            if (HeaderNameEquals(header.name, "Content-Length")) continue;  // set by WinHTTP
            headerBlock += Utf8ToWide(header.name);
            headerBlock += L": ";
            headerBlock += Utf8ToWide(header.value);
            headerBlock += L"\r\n";
        }

        const DWORD headerLength = static_cast<DWORD>(headerBlock.size());
        const bool sent = WinHttpSendRequest(
            requestHandle, headerBlock.empty() ? WINHTTP_NO_REQUEST_DATA : headerBlock.c_str(),
            headerLength, request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                               : const_cast<char*>(request.body.data()),
            static_cast<DWORD>(request.body.size()), static_cast<DWORD>(request.body.size()),
            nullptr);

        if (!sent || !WinHttpReceiveResponse(requestHandle, nullptr)) {
            response.transportError = "WinHttpSendRequest failed: " +
                                      std::to_string(GetLastError());
            WinHttpCloseHandle(requestHandle);
            WinHttpCloseHandle(connection);
            WinHttpCloseHandle(session);
            return response;
        }

        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(requestHandle,
                                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                                WINHTTP_NO_HEADER_INDEX)) {
            response.status = static_cast<int>(statusCode);
        }

        // Response headers (WINHTTP_QUERY_RAW_HEADERS_CRLF returns one block).
        DWORD headerBufferSize = 0;
        WinHttpQueryHeaders(requestHandle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER,
                            &headerBufferSize, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && headerBufferSize > 0) {
            std::wstring raw(headerBufferSize / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(requestHandle, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                    WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &headerBufferSize,
                                    WINHTTP_NO_HEADER_INDEX)) {
                response.headers = ParseHeaderBlock(WideToUtf8(raw));
            }
        }

        std::string body;
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(requestHandle, &available) && available > 0) {
            std::string chunk(static_cast<std::size_t>(available), '\0');
            DWORD read = 0;
            if (!WinHttpReadData(requestHandle, chunk.data(), available, &read)) break;
            body.append(chunk.data(), static_cast<std::size_t>(read));
        }
        response.body = std::move(body);

        WinHttpCloseHandle(requestHandle);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return response;
    }

private:
    static constexpr wchar_t kUserAgentW[] =
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/128.0.0.0 Safari/537.36 NVIDIACEFClient/HEAD/debb5919f6 GFN-PC/2.0.80.173";

    static std::wstring Utf8ToWide(std::string_view text) {
        if (text.empty()) return {};
        const int needed = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                               static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(static_cast<std::size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                            needed);
        return out;
    }

    static std::string WideToUtf8(std::wstring_view text) {
        if (text.empty()) return {};
        const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                               static_cast<int>(text.size()), nullptr, 0, nullptr,
                                               nullptr);
        std::string out(static_cast<std::size_t>(needed), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                            needed, nullptr, nullptr);
        return out;
    }

    static std::vector<HttpHeader> ParseHeaderBlock(const std::string& raw) {
        std::vector<HttpHeader> out;
        std::istringstream stream(raw);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;  // status line
            HttpHeader header;
            header.name = line.substr(0, colon);
            const std::size_t valueStart = line.find_first_not_of(' ', colon + 1);
            header.value = valueStart == std::string::npos
                               ? std::string()
                               : line.substr(valueStart);
            out.push_back(std::move(header));
        }
        return out;
    }
};

}  // namespace

HttpClientPtr CreateHttpClient() { return std::make_shared<WinHttpClient>(); }

#else

namespace {

// Non-Windows builds only exist so the portable Core unit tests can run. The
// product target is Windows; tests inject a scripted IHttpClient instead.
class UnavailableHttpClient final : public IHttpClient {
public:
    HttpResponse Send(const HttpRequest& request) override {
        HttpResponse response;
        response.status = 0;
        response.transportError =
            "HTTP transport is only implemented for Windows (WinHTTP); inject a test client on "
            "other platforms: " + request.method + " " + request.url;
        return response;
    }
};

}  // namespace

HttpClientPtr CreateHttpClient() { return std::make_shared<UnavailableHttpClient>(); }

#endif

}  // namespace opennow::core

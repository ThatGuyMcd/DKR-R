#include "update_http.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif !defined(__ANDROID__) && !defined(__APPLE__)
#include <curl/curl.h>
#endif
namespace dkr::updates {
struct HttpSink::Impl {
    HttpRequest request;
    std::stop_token stop;
    Progress progress;
    std::ofstream output;
    std::uint64_t received{}, base{};
    std::size_t header_bytes{};
    bool opened = false;
    std::chrono::steady_clock::time_point deadline;
};
HttpSink::HttpSink(const HttpRequest& r, std::stop_token s, const Progress& p) : impl(std::make_unique<Impl>()) {
    impl->request = r;
    impl->stop = s;
    impl->progress = p;
    impl->deadline = std::chrono::steady_clock::now() + std::chrono::seconds(r.timeout_seconds);
}
HttpSink::~HttpSink() = default;
bool HttpSink::cancelled() const {
    return impl->stop.stop_requested() || std::chrono::steady_clock::now() > impl->deadline;
}
bool HttpSink::header(std::string_view name, std::string_view value) {
    impl->header_bytes += name.size() + value.size();
    if (impl->header_bytes > 32768) {
        error = "Update response headers are too large.";
        return false;
    }
    std::string key(name), v(value);
    for (auto& c : key)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
        v.pop_back();
    while (!v.empty() && v.front() == ' ')
        v.erase(v.begin());
    if (key == "etag" || key == "location" || key == "content-range" || key == "retry-after" ||
        key == "x-ratelimit-reset" || key == "x-ratelimit-remaining")
        response.headers[key] = std::move(v);
    return true;
}
bool HttpSink::write(std::span<const std::uint8_t> bytes) {
    if (cancelled()) {
        error = "Update request cancelled or timed out.";
        return false;
    }
    if (response.status != 200 && response.status != 206)
        return true; // Never write error/redirect pages into a package.
    try {
        if (!impl->opened) {
            if (!impl->request.etag_file.empty()) {
                if (auto etag = response.headers.find("etag"); etag != response.headers.end())
                    durable_write(impl->request.etag_file, etag->second);
            }
            if (response.status == 206) {
                auto field = response.headers.find("content-range");
                auto prefix = "bytes " + std::to_string(impl->request.offset) + "-";
                if (!impl->request.offset || field == response.headers.end() || !field->second.starts_with(prefix))
                    throw std::runtime_error("Server returned an unexpected update byte range.");
                impl->base = impl->request.offset;
            }
            if (!impl->request.file.empty()) {
                require_private_tree(impl->request.file.parent_path());
                std::error_code ec;
                auto status = std::filesystem::symlink_status(impl->request.file, ec);
                if (status.type() != std::filesystem::file_type::not_found &&
                    (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status) ||
                     std::filesystem::hard_link_count(impl->request.file) != 1))
                    throw std::runtime_error("Update download destination is not a private ordinary file.");
                if (impl->base && std::filesystem::file_size(impl->request.file) != impl->base)
                    throw std::runtime_error("Partial update changed before resuming.");
                impl->output.open(impl->request.file,
                                  std::ios::binary | (impl->base ? std::ios::app : std::ios::trunc));
                if (!impl->output)
                    throw std::runtime_error("Could not open update download destination.");
            }
            impl->opened = true;
        }
        if (bytes.size() > impl->request.limit - std::min(impl->request.limit, impl->base + impl->received))
            throw std::runtime_error("Update response exceeded its declared size limit.");
        if (impl->request.file.empty())
            response.body.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        else {
            impl->output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (!impl->output)
                throw std::runtime_error("Update download could not be written; check free disk space.");
        }
        impl->received += bytes.size();
        if (impl->progress)
            impl->progress(impl->base + impl->received);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}
void HttpSink::finish() {
    if (impl->output.is_open()) {
        impl->output.flush();
        if (!impl->output)
            error = "Update download could not be flushed.";
        impl->output.close();
    }
    if (!error.empty())
        throw std::runtime_error(error);
    if (cancelled())
        throw std::runtime_error("Update request cancelled or timed out.");
}

#if !defined(__ANDROID__) && !defined(__APPLE__)
namespace {
#ifdef _WIN32
struct Handle {
    HINTERNET value{};
    ~Handle() {
        if (value)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const {
        return value;
    }
};
std::wstring wide(std::string_view s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid update URL encoding.");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n);
    return out;
}
HttpResponse single(const HttpRequest& r, std::stop_token stop, const Progress& progress) {
    auto url = wide(r.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = DWORD(-1);
    parts.dwUrlPathLength = DWORD(-1);
    parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw std::runtime_error("Invalid HTTPS update URL.");
    Handle session{WinHttpOpen(L"DKR-R-Updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value)
        throw std::runtime_error("Could not initialize update networking.");
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 4000);
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))
        throw std::runtime_error("Could not require modern TLS for updates.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handle connection{WinHttpConnect(session, host.c_str(), parts.nPort, 0)};
    Handle request{connection.value ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                         WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                    : nullptr};
    if (!request.value)
        throw std::runtime_error("Could not open the update connection.");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    std::wstring headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!r.etag.empty() && r.etag.find_first_of("\r\n") == r.etag.npos)
        headers += wide(std::string(r.offset ? "If-Range: " : "If-None-Match: ") + r.etag + "\r\n");
    if (r.offset)
        headers += wide("Range: bytes=" + std::to_string(r.offset) + "-\r\n");
    if (stop.stop_requested() ||
        !WinHttpSendRequest(request, headers.c_str(), DWORD(headers.size()), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        throw std::runtime_error("Could not contact GitHub securely. Check the connection and try again.");
    HttpSink sink(r, stop, progress);
    DWORD size = sizeof(DWORD);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &sink.response.status, &size, WINHTTP_NO_HEADER_INDEX);
    DWORD required = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &required,
                        WINHTTP_NO_HEADER_INDEX);
    if (required > 65536)
        throw std::runtime_error("Update response headers are too large.");
    std::wstring raw(required / sizeof(wchar_t), L'\0');
    if (required && WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                                        raw.data(), &required, WINHTTP_NO_HEADER_INDEX)) {
        std::string ascii;
        for (auto c : raw)
            if (c)
                ascii += c < 128 ? char(c) : '?';
        std::size_t start = 0;
        while (start < ascii.size()) {
            auto end = ascii.find("\r\n", start);
            auto line = ascii.substr(start, end - start);
            auto colon = line.find(':');
            if (colon != line.npos && !sink.header(line.substr(0, colon), line.substr(colon + 1)))
                throw std::runtime_error(sink.error);
            if (end == ascii.npos)
                break;
            start = end + 2;
        }
    }
    std::array<std::uint8_t, 65536> buffer{};
    DWORD read = 0;
    while (!sink.cancelled()) {
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read))
            throw std::runtime_error("Update connection interrupted. The partial download can be resumed.");
        if (!read)
            break;
        if (!sink.write(std::span(buffer.data(), read)))
            break;
    }
    sink.finish();
    return sink.response;
}
#else
HttpResponse single(const HttpRequest& r, std::stop_token stop, const Progress& progress) {
    static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init != CURLE_OK)
        throw std::runtime_error("Could not initialize update networking.");
    auto* curl = curl_easy_init();
    if (!curl)
        throw std::runtime_error("Could not open update networking.");
    struct Cleanup {
        CURL* c;
        curl_slist* h{};
        ~Cleanup() {
            curl_slist_free_all(h);
            curl_easy_cleanup(c);
        }
    } cleanup{curl};
    HttpSink sink(r, stop, progress);
    for (const auto& h :
         {std::string("Accept: application/vnd.github+json"), std::string("X-GitHub-Api-Version: 2022-11-28")})
        cleanup.h = curl_slist_append(cleanup.h, h.c_str());
    if (!r.etag.empty() && r.etag.find_first_of("\r\n") == r.etag.npos)
        cleanup.h =
            curl_slist_append(cleanup.h, (std::string(r.offset ? "If-Range: " : "If-None-Match: ") + r.etag).c_str());
    std::string range;
    if (r.offset) {
        range = std::to_string(r.offset) + "-";
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, cleanup.h);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DKR-R-Updater/1");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, long(r.timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &sink);
    curl_easy_setopt(
        curl, CURLOPT_HEADERFUNCTION,
        +[](char* bytes, std::size_t size, std::size_t count, void* context) -> std::size_t {
            auto& s = *static_cast<HttpSink*>(context);
            std::string_view line(bytes, size * count);
            if (line.starts_with("HTTP/")) {
                auto at = line.find(' ');
                unsigned code{};
                std::from_chars(line.data() + at + 1, line.data() + line.size(), code);
                s.response.status = code;
            } else {
                auto colon = line.find(':');
                if (colon != line.npos && !s.header(line.substr(0, colon), line.substr(colon + 1)))
                    return 0;
            }
            return line.size();
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION,
        +[](char* bytes, std::size_t size, std::size_t count, void* context) -> std::size_t {
            auto n = size * count;
            return static_cast<HttpSink*>(context)->write(std::span(reinterpret_cast<const std::uint8_t*>(bytes), n))
                       ? n
                       : 0;
        });
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        curl, CURLOPT_XFERINFOFUNCTION, +[](void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<HttpSink*>(context)->cancelled() ? 1 : 0;
        });
    auto code = curl_easy_perform(curl);
    sink.finish();
    if (code != CURLE_OK)
        throw std::runtime_error("Update connection interrupted or TLS verification failed. Nothing was installed.");
    return sink.response;
}
#endif
} // namespace
HttpResponse https_get(const HttpRequest& request, std::stop_token stop, const Progress& progress) {
    HttpRequest current = request;
    for (unsigned hop = 0; hop < 6; ++hop) {
        if (!allowed_https_url(current.url))
            throw std::runtime_error("Update URL is not an approved HTTPS release endpoint.");
        auto result = single(current, stop, progress);
        if (result.status != 301 && result.status != 302 && result.status != 303 && result.status != 307 &&
            result.status != 308)
            return result;
        auto location = result.headers.find("location");
        if (location == result.headers.end())
            throw std::runtime_error("Update redirect has no destination.");
        current.url = location->second;
    }
    throw std::runtime_error("Update server redirected too many times.");
}
#endif
} // namespace dkr::updates

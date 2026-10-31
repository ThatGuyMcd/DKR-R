#include "online_directory.hpp"
#include "atomic_snapshot.hpp"
#include "linux_keyring.hpp"
#include "store_vehicle_adapter.hpp"
#include "mod_store.hpp"
#include "mod_store/download_history.hpp"
#include "mod_store_install.hpp"
#include "stb/stb_image.h"
#include "updates/update_core.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <stop_token>
#include <thread>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#else
// Android links no HTTPS backend (the CURL link in CMakeLists.txt).
#if !defined(__ANDROID__)
#include <curl/curl.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dkr::store {
namespace {
constexpr const char* Api = "https://dkr-mod-library-api.ross-macgregorbars.workers.dev";
constexpr const char* Site = "https://positron-portal.com";
constexpr std::uint64_t MaxFile = 2ULL * 1024 * 1024 * 1024;
constexpr std::size_t MaxJson = 4 * 1024 * 1024, MaxImage = 8 * 1024 * 1024, Chunk = 4 * 1024 * 1024;
using Clock = std::chrono::steady_clock;
struct Error : std::runtime_error {
    unsigned status;
    Json details;
    Error(std::string message, unsigned s = 0, Json context = Json::object())
        : std::runtime_error(std::move(message)), status(s), details(std::move(context)) {}
};
std::string lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return value;
}
bool prefix(std::string_view value, std::string_view start) {
    return value.starts_with(start);
}
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 160 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool hash_string(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(),
                                             [](char c) { return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9'); });
}
std::string encode(std::string_view value) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~')
            out += char(c);
        else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}
std::string base64(std::span<const std::uint8_t> bytes) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned bits = 0, count = 0;
    for (auto b : bytes) {
        bits = (bits << 8) | b;
        count += 8;
        while (count >= 6) {
            count -= 6;
            out += t[(bits >> count) & 63];
        }
    }
    if (count)
        out += t[(bits << (6 - count)) & 63];
    return out;
}
std::string nonce() {
    std::array<std::uint8_t, 32> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), ULONG(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw Error("Secure sign-in could not start.");
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        throw Error("Secure sign-in could not start.");
    std::size_t done = 0;
    while (done < bytes.size()) {
        auto n = read(fd, bytes.data() + done, bytes.size() - done);
        if (n <= 0) {
            close(fd);
            throw Error("Secure sign-in could not start.");
        }
        done += std::size_t(n);
    }
    close(fd);
#endif
    return base64(bytes);
}
std::string challenge(const std::string& value) {
    auto digest = dkr::updates::unhex(
        dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size())), 32);
    return base64(digest);
}
void write_json(const std::filesystem::path& path, const Json& value) {
    std::filesystem::create_directories(path.parent_path());
    dkr::updates::durable_write(path, value.dump());
}
Json read_json(const std::filesystem::path& path) {
    try {
        return Json::parse(dkr::updates::read_text(path, MaxJson));
    } catch (...) {
        return Json::object();
    }
}
void save_device(const std::filesystem::path& root, const std::string& token) {
#ifdef _WIN32
    DATA_BLOB input{DWORD(token.size()), reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))}, output{};
    if (!CryptProtectData(&input, L"DKR-R Mod Store", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        throw Error("Windows could not remember this sign-in securely.");
    std::unique_ptr<BYTE, decltype(&LocalFree)> memory(output.pbData, LocalFree);
    dkr::updates::durable_write(root / "device.bin",
                                std::string_view(reinterpret_cast<const char*>(output.pbData), output.cbData));
#else
    if (!dkr::online::keyring::store(root.generic_string(), token))
        throw Error("The desktop keyring could not remember sign-in. You are signed in for this session only.");
#endif
}
std::string load_device(const std::filesystem::path& root) {
#ifdef _WIN32
    try {
        const auto bytes = dkr::updates::read_text(root / "device.bin", 16384);
        DATA_BLOB input{DWORD(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data()))}, output{};
        if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
            return {};
        std::unique_ptr<BYTE, decltype(&LocalFree)> memory(output.pbData, LocalFree);
        std::string token(reinterpret_cast<const char*>(output.pbData), output.cbData);
        return token.size() == 43 && identifier(token) ? token : std::string{};
    } catch (...) {
        return {};
    }
#else
    auto token = dkr::online::keyring::load(root.generic_string());
    return token.size() == 43 && identifier(token) ? token : std::string{};
#endif
}
struct Response {
    unsigned status = 0;
    std::string body, type, etag, range, location, url;
    std::uint64_t length = 0;
};
struct Request {
    std::string url, method = "GET", body, token, type = "application/json", etag;
    std::uint64_t offset = 0, limit = MaxJson;
    unsigned timeout = 12000;
    std::string online_instance;
};
using Sink = std::function<void(const char*, std::size_t, const Response&)>;

#ifdef _WIN32
struct Internet {
    HINTERNET h = nullptr;
    ~Internet() {
        if (h)
            WinHttpCloseHandle(h);
    }
    operator HINTERNET() const {
        return h;
    }
};
std::wstring wide(std::string_view value) {
    if (value.size() > 65535)
        throw Error("Request text is too long.");
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!n)
        throw Error("Request text has invalid encoding.");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), out.data(), n);
    return out;
}
std::string header(HINTERNET request, DWORD field) {
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (bytes > 65536)
        throw Error("Response headers are too large.");
    if (!bytes)
        return {};
    std::wstring raw(bytes / sizeof(wchar_t), 0);
    if (!WinHttpQueryHeaders(request, field, WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
        return {};
    std::string out;
    for (wchar_t c : raw)
        if (c)
            out += c < 128 ? char(c) : '?';
    return out;
}
Response http_once(const Request& r, std::stop_token stop, const Sink& sink) {
    if (!trusted_url(r.url, prefix(r.url, Api)))
        throw Error("The library returned an unapproved file address.");
    auto url = wide(r.url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS ||
        parts.nPort != 443)
        throw Error("Use the secure library address.");
    Internet session{WinHttpOpen(L"DKR-R-ModStore/1.0.7-beta8", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h)
        throw Error("The secure connection could not start.");
    WinHttpSetTimeouts(session, 4000, 4000, int(r.timeout), int(r.timeout));
    DWORD tls = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    tls |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &tls, sizeof(tls)))
        throw Error("The secure connection could not start.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Internet connection{WinHttpConnect(session, host.c_str(), 443, 0)};
    const auto method = wide(r.method);
    Internet request{connection.h
                         ? WinHttpOpenRequest(connection, method.c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                         : nullptr};
    if (!request.h)
        throw Error("The library connection could not open.");
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    DWORD disable = WINHTTP_DISABLE_COOKIES;
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
    std::string headers = "Accept: */*\r\n";
    if (prefix(r.url, Api)) {
        headers += "Origin: https://positron-portal.com\r\n";
        if (!r.online_instance.empty())
            headers += "X-DKRR-Instance: " + r.online_instance + "\r\n";
        if (!r.token.empty()) {
            if (r.token.size() != 43 || !identifier(r.token))
                throw Error("Sign in again to continue.");
            headers += "Authorization: Bearer " + r.token + "\r\n";
        }
    } else {
        if (!r.token.empty())
            throw Error("Credentials cannot be sent to a file host.");
        headers += "Referer: https://positron-portal.com/dkr-r/index.html\r\n";
    }
    if (r.method == "POST")
        headers += "Content-Type: " + r.type + "\r\n";
    if (r.offset) {
        headers += "Range: bytes=" + std::to_string(r.offset) + "-\r\n";
        if (!r.etag.empty() && r.etag.find_first_of("\r\n") == std::string::npos)
            headers += "If-Range: " + r.etag + "\r\n";
    }
    auto wh = wide(headers);
    if (stop.stop_requested() ||
        !WinHttpSendRequest(request, wh.c_str(), DWORD(wh.size()),
                            r.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(r.body.data()),
                            DWORD(r.body.size()), DWORD(r.body.size()), 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        throw Error("Could not reach the library. Check your connection and retry.");
    Response response;
    response.url = r.url;
    DWORD bytes = sizeof(response.status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &bytes, WINHTTP_NO_HEADER_INDEX))
        throw Error("Invalid library response.");
    response.type = lower(header(request, WINHTTP_QUERY_CONTENT_TYPE));
    response.etag = header(request, WINHTTP_QUERY_ETAG);
    response.range = header(request, WINHTTP_QUERY_CONTENT_RANGE);
    response.location = header(request, WINHTTP_QUERY_LOCATION);
    const auto length = header(request, WINHTTP_QUERY_CONTENT_LENGTH);
    if (!length.empty() && length.size() <= 20 &&
        std::all_of(length.begin(), length.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        try {
            response.length = std::stoull(length);
        } catch (...) {
            throw Error("Invalid download size.");
        }
    }
    std::array<char, 65536> buffer{};
    std::uint64_t total = 0;
    for (;;) {
        if (stop.stop_requested())
            throw Error("Transfer paused.");
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), DWORD(buffer.size()), &read))
            throw Error("The connection was interrupted. Retry to continue.");
        if (!read)
            break;
        if (read > r.limit - total)
            throw Error("The library response exceeds its allowed size.");
        total += read;
        if (sink && response.status >= 200 && response.status < 300)
            sink(buffer.data(), read, response);
        else if (response.body.size() + read <= std::min<std::uint64_t>(r.limit, MaxImage))
            response.body.append(buffer.data(), read);
        else
            throw Error("The library response is too large.");
    }
    return response;
}
#elif defined(__ANDROID__)
Response http_once(const Request&, std::stop_token, const Sink&) {
    throw Error("The Mod Store isn't available in this build.");
}
#else
Response http_once(const Request& r, std::stop_token stop, const Sink& sink) {
    if (!trusted_url(r.url, prefix(r.url, Api)))
        throw Error("Unapproved library address.");
    static const bool initialized = [] { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }();
    if (!initialized)
        throw Error("The secure connection could not start.");
    struct Cleanup {
        CURL* curl = curl_easy_init();
        curl_slist* headers = nullptr;
        ~Cleanup() {
            if (headers)
                curl_slist_free_all(headers);
            if (curl)
                curl_easy_cleanup(curl);
        }
    } cleanup;
    if (!cleanup.curl)
        throw Error("The connection could not start.");
    Response out;
    out.url = r.url;
    struct Transfer {
        const Request* request;
        Response* response;
        const Sink* sink;
        std::stop_token stop;
        std::uint64_t total = 0;
        std::exception_ptr error;
    };
    Transfer transfer{&r, &out, &sink, stop};
    auto add = [&](std::string value) { cleanup.headers = curl_slist_append(cleanup.headers, value.c_str()); };
    if (prefix(r.url, Api)) {
        add("Origin: https://positron-portal.com");
        if (!r.online_instance.empty())
            add("X-DKRR-Instance: " + r.online_instance);
        if (!r.token.empty() && r.token.size() == 43 && identifier(r.token))
            add("Authorization: Bearer " + r.token);
    } else {
        if (!r.token.empty())
            throw Error("Credentials cannot be sent to a file host.");
        add("Referer: https://positron-portal.com/dkr-r/index.html");
    }
    if (r.method == "POST")
        add("Content-Type: " + r.type);
    if (r.offset && !r.etag.empty() && r.etag.find_first_of("\r\n") == r.etag.npos)
        add("If-Range: " + r.etag);
    curl_easy_setopt(cleanup.curl, CURLOPT_URL, r.url.c_str());
    curl_easy_setopt(cleanup.curl, CURLOPT_USERAGENT, "DKR-R-ModStore/1.0.7-beta8");
    curl_easy_setopt(cleanup.curl, CURLOPT_HTTPHEADER, cleanup.headers);
    curl_easy_setopt(cleanup.curl, CURLOPT_CONNECTTIMEOUT_MS, 4000L);
    curl_easy_setopt(cleanup.curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(cleanup.curl, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(cleanup.curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(cleanup.curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(cleanup.curl, CURLOPT_PROTOCOLS_STR, "https");
    if (r.method == "POST") {
        curl_easy_setopt(cleanup.curl, CURLOPT_POST, 1L);
        curl_easy_setopt(cleanup.curl, CURLOPT_POSTFIELDS, r.body.data());
        curl_easy_setopt(cleanup.curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(r.body.size()));
    }
    std::string range;
    if (r.offset) {
        range = std::to_string(r.offset) + "-";
        curl_easy_setopt(cleanup.curl, CURLOPT_RANGE, range.c_str());
    }
    curl_easy_setopt(cleanup.curl, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(
        cleanup.curl, CURLOPT_HEADERFUNCTION,
        +[](char* data, std::size_t size, std::size_t n, void* context) -> std::size_t {
            auto& t = *static_cast<Transfer*>(context);
            const auto bytes = size * n;
            try {
                std::string line(data, bytes);
                if (prefix(line, "HTTP/")) {
                    *t.response = Response{};
                    t.response->url = t.request->url;
                    const auto at = line.find(' ');
                    if (at != line.npos)
                        t.response->status = unsigned(std::stoul(line.substr(at + 1)));
                }
                const auto colon = line.find(':');
                if (colon != line.npos) {
                    auto key = lower(line.substr(0, colon));
                    auto value = line.substr(colon + 1);
                    while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                        value.pop_back();
                    while (!value.empty() && value.front() == ' ')
                        value.erase(value.begin());
                    if (key == "content-type")
                        t.response->type = lower(value);
                    if (key == "etag")
                        t.response->etag = value;
                    if (key == "content-range")
                        t.response->range = value;
                    if (key == "location")
                        t.response->location = value;
                    if (key == "content-length" && !value.empty() && value.size() <= 20 &&
                        std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                        t.response->length = std::stoull(value);
                }
                return bytes;
            } catch (...) {
                t.error = std::current_exception();
                return 0;
            }
        });
    curl_easy_setopt(cleanup.curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(
        cleanup.curl, CURLOPT_WRITEFUNCTION,
        +[](char* data, std::size_t size, std::size_t n, void* context) -> std::size_t {
            auto& t = *static_cast<Transfer*>(context);
            const auto bytes = size * n;
            try {
                if (t.stop.stop_requested() || bytes > t.request->limit - t.total)
                    throw Error("Transfer stopped or exceeded its size limit.");
                t.total += bytes;
                if (*t.sink && t.response->status >= 200 && t.response->status < 300)
                    (*t.sink)(data, bytes, *t.response);
                else {
                    if (t.response->body.size() + bytes > std::min<std::uint64_t>(t.request->limit, MaxImage))
                        throw Error("Response too large.");
                    t.response->body.append(data, bytes);
                }
                return bytes;
            } catch (...) {
                t.error = std::current_exception();
                return 0;
            }
        });
    curl_easy_setopt(cleanup.curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(cleanup.curl, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(
        cleanup.curl, CURLOPT_XFERINFOFUNCTION, +[](void* p, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<Transfer*>(p)->stop.stop_requested() ? 1 : 0;
        });
    const auto code = curl_easy_perform(cleanup.curl);
    if (transfer.error)
        std::rethrow_exception(transfer.error);
    if (code != CURLE_OK)
        throw Error("The library connection was interrupted. Retry to continue.");
    return out;
}
#endif
Response http(Request request, std::stop_token stop, const Sink& sink = {}) {
    // Match browser file redirects only within the existing pinned public-file
    // roots. API credentials are never forwarded through a redirect.
    for (unsigned hops = 0;; ++hops) {
        auto response = http_once(request, stop, sink);
        response.url = request.url;
        if (response.status != 301 && response.status != 302 && response.status != 303 && response.status != 307 &&
            response.status != 308)
            return response;
        if (hops == 3 || request.method != "GET" || !request.token.empty() || prefix(request.url, Api))
            throw Error("The library returned an unsupported redirect.", response.status);
        auto next = response.location;
        if (next.starts_with('/') && !next.starts_with("//"))
            next = std::string(Site) + next;
        else if (!next.starts_with("https://") && next.find(':') == next.npos && !next.starts_with("//"))
            next = request.url.substr(0, request.url.find_last_of('/') + 1) + next;
        if (!trusted_url(next))
            throw Error("The download redirects outside the approved library.", response.status);
        request.url = std::move(next);
    }
}
bool webpage(const char* bytes, std::size_t count) {
    auto sample = lower(std::string(bytes, std::min<std::size_t>(count, 512)));
    const auto first = sample.find_first_not_of(" \r\n\t\xEF\xBB\xBF");
    if (first == sample.npos)
        return false;
    sample.erase(0, first);
    return prefix(sample, "<!doctype html") || prefix(sample, "<html") || prefix(sample, "<head") ||
           prefix(sample, "<body") || prefix(sample, "<?xml");
}
constexpr const char* MissingPackage =
    "The download server sent a webpage instead of the mod file. Nothing was installed. "
    "Open the download link in your browser to compare.";
Error download_page_error(const Response& response, const char* data, std::size_t length) {
    // File requests contain neither bearer tokens nor browser cookies. Record
    // only response metadata and a short signature, never the page itself.
    static const char hex[] = "0123456789abcdef";
    std::string signature;
    for (std::size_t i = 0; i < std::min<std::size_t>(length, 16); ++i) {
        const auto value = static_cast<unsigned char>(data[i]);
        signature += hex[value >> 4];
        signature += hex[value & 15];
    }
    auto url = response.url;
    if (const auto query = url.find_first_of("?#"); query != url.npos)
        url.resize(query);
    return Error(MissingPackage, response.status,
                 {{"httpStatus", response.status},
                  {"contentType", response.type},
                  {"contentLength", response.length},
                  {"prefixHex", signature},
                  {"servedUrl", url}});
}
Json document(const Request& request, std::stop_token stop) {
    const auto response = http(request, stop);
    if (response.type.find("application/json") == std::string::npos)
        throw Error("The library service needs its launcher update installed.", response.status);
    Json data;
    try {
        data = Json::parse(response.body);
    } catch (...) {
        throw Error("The library returned an invalid response.", response.status);
    }
    if (response.status < 200 || response.status >= 300) {
        const auto message = data.contains("error") ? text(data["error"], "message", 1000) : std::string{};
        throw Error(message.empty() ? "The library could not complete this request." : message, response.status);
    }
    if (!data.is_object())
        throw Error("The library returned invalid data.");
    return data;
}
bool package_signature(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char, 12> bytes{};
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    const auto n = input.gcount();
    const auto ext = lower(path.extension().string());
    if (ext == ".xdelta")
        return n >= 3 && bytes[0] == 0xd6 && bytes[1] == 0xc3 && bytes[2] == 0xc4;
    if (ext == ".bps")
        return n >= 4 && std::string_view(reinterpret_cast<char*>(bytes.data()), 4) == "BPS1";
    if (ext == ".ips")
        return n >= 5 && std::string_view(reinterpret_cast<char*>(bytes.data()), 5) == "PATCH";
    if (ext == ".7z")
        return n >= 6 && bytes[0] == '7' && bytes[1] == 'z' && bytes[2] == 0xbc && bytes[3] == 0xaf &&
               bytes[4] == 0x27 && bytes[5] == 0x1c;
    return n >= 4 && bytes[0] == 'P' && bytes[1] == 'K';
}
} // namespace

namespace {
std::string upload_fingerprint(const std::filesystem::path& path, std::uint64_t size) {
    const auto prefix = "dkr-upload-v1\n" + std::to_string(size) + "\n";
    std::vector<std::uint8_t> bytes(prefix.begin(), prefix.end());
    const auto count = std::size_t(std::min<std::uint64_t>(65536, size));
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw Error("The selected file could not be read.");
    std::vector<char> block(count);
    file.read(block.data(), std::streamsize(count));
    if (std::size_t(file.gcount()) != count)
        throw Error("The selected file changed.");
    bytes.insert(bytes.end(), block.begin(), block.end());
    file.clear();
    file.seekg(std::streamoff(size - count));
    file.read(block.data(), std::streamsize(count));
    if (std::size_t(file.gcount()) != count)
        throw Error("The selected file changed.");
    bytes.insert(bytes.end(), block.begin(), block.end());
    return dkr::updates::hash(bytes);
}
} // namespace
std::string text(const Json& object, const char* key, std::size_t max) {
    if (!object.is_object())
        return {};
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string())
        return {};
    std::string value = found->get<std::string>();
    if (value.size() > max)
        value.resize(max);
    value.erase(
        std::remove_if(value.begin(), value.end(), [](unsigned char c) { return c < 32 && c != '\n' && c != '\t'; }),
        value.end());
    return value;
}
bool staff(const Json& user) {
    const auto role = text(user, "role", 16);
    return text(user, "status", 16) == "active" && (role == "moderator" || role == "admin");
}
bool admin(const Json& user) {
    return staff(user) && text(user, "role", 16) == "admin";
}
bool trusted_url(const std::string& url, bool api) {
    if (url.size() > 2048 || url.find_first_of("\\\r\n\t#") != url.npos || url.find('\0') != url.npos)
        return false;
    const auto lowered = lower(url);
    if (lowered.find("%2f") != lowered.npos || lowered.find("%5c") != lowered.npos ||
        lowered.find("%00") != lowered.npos || lowered.find("%25") != lowered.npos ||
        lowered.find("%2e") != lowered.npos || lowered.find("/../") != lowered.npos ||
        lowered.find("/./") != lowered.npos)
        return false;
    return api ? prefix(url, std::string(Api) + "/")
               : prefix(url, std::string(Site) + "/dkr-r/images/") ||
                     prefix(url, std::string(Site) + "/dkr-r/downloads/") ||
                     prefix(url, std::string(Site) + "/dkr-r/assets/");
}

struct Service::Impl {
    mutable std::mutex mutex;
    std::mutex download_persist_mutex;
    std::condition_variable_any cv;
    Snapshot state;
    dkr::runtime::AtomicSnapshot<const Snapshot> published{std::make_shared<const Snapshot>()};
    std::filesystem::path root;
    std::string version, token, device, browser;
    unsigned account = 0;
    bool configured = false, names_queued = false, gameplay = false;
    void preferences() {
        int layout;
        bool remember;
        {
            std::lock_guard lock(mutex);
            layout = state.layout;
            remember = state.remember;
        }
        write_json(root / "preferences.json", {{"layout", layout}, {"remember", remember}});
    }
    using Work = std::function<void(std::stop_token)>;
    std::deque<Work> work;
    std::deque<std::string> image_queue;
    std::map<std::string, std::shared_ptr<const Image>> images;
    std::map<std::string, Json> upload_proofs;
    std::set<std::string> image_pending;
    std::map<std::string, Clock::time_point> image_retry;
    std::atomic<bool> stop_upload{false};
    std::stop_source download_stop, login_stop;
    std::map<std::string, std::stop_source> preparation_stops;
    // Workers are last: joined before the objects captured by their callbacks.
    std::jthread worker, image_worker, download_worker, auth_worker;
    void publish() {
        ++state.generation;
        published.store(std::make_shared<const Snapshot>(state));
    }
    void update(const std::function<void(Snapshot&)>& fn) {
        std::lock_guard lock(mutex);
        fn(state);
        publish();
    }
    bool queue(Work fn) {
        std::lock_guard lock(mutex);
        if (work.size() >= 32) {
            state.error = "Wait for the current library request to finish.";
            publish();
            return false;
        }
        work.push_back(std::move(fn));
        cv.notify_all();
        return true;
    }
    void fail(const std::exception& error) {
        update([&](Snapshot& s) {
            s.loading = false;
            s.error = error.what();
        });
    }
    void download_report(const std::string& release, const std::string& title, const std::exception& error,
                         std::string url = {}) noexcept {
        try {
            // Public file URLs only; omit query strings and fragments.
            if (!trusted_url(url))
                url.clear();
            if (const auto query = url.find_first_of("?#"); query != url.npos)
                url.resize(query);
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
            Json report = {
                {"schema", 1}, {"timeUnixMs", timestamp}, {"build", version}, {"release", release}, {"mod", title},
                {"url", url},  {"error", error.what()}};
            if (const auto* detail = dynamic_cast<const Error*>(&error))
                report["response"] = detail->details;
            write_json(root / "last-download-error.json", report);
        } catch (...) { /* A diagnostic must never replace the original failure. */
        }
    }
    Json get(std::string path, std::stop_token stop, bool private_request = false) {
        std::string credential;
        {
            std::lock_guard lock(mutex);
            if (private_request)
                credential = token;
        }
        try {
            return document({std::string(Api) + path, "GET", {}, credential}, stop);
        } catch (const Error& error) {
            if (error.status == 401 && !credential.empty())
                expire(credential);
            throw;
        }
    }
    Json post(std::string path, Json value, std::stop_token stop, bool private_request = true) {
        std::string credential;
        {
            std::lock_guard lock(mutex);
            if (private_request)
                credential = token;
        }
        if (private_request && credential.empty())
            throw Error("Sign in with Discord to continue.");
        try {
            return document({std::string(Api) + path, "POST", value.dump(), credential}, stop);
        } catch (const Error& error) {
            if (error.status == 401 && !credential.empty())
                expire(credential);
            throw;
        }
    }
    void expire(const std::string& credential) {
        std::lock_guard lock(mutex);
        if (credential != token)
            return;
        token.clear();
        ++account;
        state.user = nullptr;
        state.personal = Json::object();
        state.staff = Json::object();
        state.admin = Json::object();
        state.uploads = Json::array();
        state.assets = Json::array();
        state.upload = Json::object();
        state.uploading = false;
        state.loading = state.writing = false;
        upload_proofs.clear();
        state.last_action.clear();
        state.last_result = nullptr;
        stop_upload = true;
        publish();
    }
    void persist_downloads() {
        std::lock_guard persist_lock(download_persist_mutex);
        Json saved;
        {
            std::lock_guard lock(mutex);
            saved = history::save<Json>(state.downloads);
        }
        write_json(root / "downloads.json", saved);
    }
    Json paged(std::string path, const char* field, std::stop_token stop, bool private_request = false) {
        Json all = Json::array();
        std::set<std::string> cursors, ids;
        std::string cursor;
        std::size_t total_bytes = 0;
        for (unsigned page = 0; page < 100; ++page) {
            auto data = get(path + (cursor.empty() ? "" : "?cursor=" + encode(cursor)), stop, private_request);
            if (!data.contains(field) || !data[field].is_array() || data[field].size() > 1000)
                throw Error("Invalid catalogue page.");
            for (auto& item : data[field]) {
                auto id = text(item, "id", 160);
                if (!identifier(id))
                    throw Error("Invalid catalogue record.");
                if (ids.insert(id).second) {
                    total_bytes += item.dump().size();
                    if (total_bytes > MaxJson - 1024)
                        throw Error("The library response exceeds its safe cache size.");
                    all.push_back(item);
                }
            }
            cursor = text(data, "nextCursor", 2048);
            if (cursor.empty())
                return all;
            if (!cursors.insert(cursor).second || all.size() > 10000)
                throw Error("The catalogue is too large or has invalid pagination.");
        }
        throw Error("The catalogue is too large.");
    }
    Json private_pages(const std::string& path, std::initializer_list<const char*> fields,
                       const std::string& credential, unsigned epoch, std::stop_token stop) {
        Json result = Json::object();
        for (const char* field : fields)
            result[field] = Json::array();
        std::map<std::string, std::set<std::string>> ids;
        std::set<std::string> cursors;
        std::string cursor;
        std::size_t bytes = 0;
        for (unsigned page = 0; page < 100; ++page) {
            {
                std::lock_guard lock(mutex);
                if (epoch != account)
                    throw Error("The signed-in account changed.");
            }
            Json data;
            try {
                data = document({std::string(Api) + path + (cursor.empty() ? "" : "?cursor=" + encode(cursor)),
                                 "GET",
                                 {},
                                 credential},
                                stop);
            } catch (const Error& error) {
                if (error.status == 401)
                    expire(credential);
                throw;
            }
            if (data.contains("user"))
                result["user"] = data["user"];
            for (const char* field : fields) {
                if (!data.contains(field) || !data[field].is_array())
                    throw Error("Invalid account page.");
                for (const auto& row : data[field]) {
                    const auto id = text(row, "id", 160);
                    if (!identifier(id))
                        throw Error("Invalid account record.");
                    if (ids[field].insert(id).second) {
                        bytes += row.dump().size();
                        if (bytes > MaxJson)
                            throw Error("The account response exceeds its safe size.");
                        result[field].push_back(row);
                    }
                }
            }
            cursor = text(data, "nextCursor", 2048);
            if (cursor.empty())
                return result;
            if (!cursors.insert(cursor).second)
                throw Error("Invalid account pagination.");
        }
        throw Error("The account has too many pages to load safely.");
    }
    void refresh(std::stop_token stop) {
        update([](Snapshot& s) {
            s.loading = true;
            s.error.clear();
        });
        auto items = paged("/v1/catalogue", "items", stop);
        for (const auto& row : items) {
            if (!identifier(text(row, "id", 160)) || text(row, "title", 200).empty())
                throw Error("Invalid mod listing.");
        }
        write_json(root / "catalogue.json", {{"schema", 1}, {"items", items}});
        update([&](Snapshot& s) {
            s.items = std::make_shared<const Json>(std::move(items));
            s.cached = false;
            s.loading = false;
            s.status = "Library up to date.";
        });
        refresh_library_names(root.parent_path(), *published.load()->items, stop);
        private_refresh(stop);
    }
    void private_refresh(std::stop_token stop) {
        unsigned epoch;
        std::string credential;
        {
            std::lock_guard lock(mutex);
            epoch = account;
            credential = token;
        }
        if (credential.empty()) {
            restore(stop);
            return;
        }
        auto me = private_pages("/v1/me", {"requests", "reviews"}, credential, epoch, stop);
        auto user = me.value("user", Json{});
        if (text(user, "status", 16) != "active")
            throw Error("This account is unavailable.", 401);
        Json staff_data = Json::object(), admin_data = Json::object();
        if (staff(user))
            staff_data = private_pages("/v1/staff", {"requests", "reports", "reviews"}, credential, epoch, stop);
        if (admin(user))
            admin_data = private_pages("/v1/admin", {"accounts", "drafts", "audit", "items"}, credential, epoch, stop);
        else if (staff(user))
            admin_data =
                private_pages("/v1/catalogue-tools", {"accounts", "drafts", "audit", "items"}, credential, epoch, stop);
        std::lock_guard lock(mutex);
        if (epoch != account)
            return;
        if (text(state.user, "role", 16) != text(user, "role", 16)) {
            stop_upload = true;
            upload_proofs.clear();
            state.uploads = Json::array();
            state.assets = Json::array();
            state.upload = Json::object();
            state.uploading = false;
        }
        state.user = std::move(user);
        state.personal = std::move(me);
        state.staff = std::move(staff_data);
        state.admin = std::move(admin_data);
        publish();
    }
    void restore(std::stop_token stop) {
        unsigned epoch;
        std::string saved;
        {
            std::lock_guard lock(mutex);
            epoch = account;
            saved = device;
        }
        if (saved.empty())
            return;
        auto response = post("/auth/launcher/refresh", {{"device", saved}}, stop, false);
        const auto session = text(response, "token", 43);
        const auto user = response.value("user", Json{});
        if (session.size() != 43 || !identifier(session) || text(user, "status", 16) != "active")
            return;
        {
            std::lock_guard lock(mutex);
            if (epoch != account)
                return;
            token = session;
            state.user = user;
            publish();
        }
        private_refresh(stop);
    }
    void run(std::stop_token stop) {
        for (;;) {
            Work task;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, stop, [&] { return !work.empty(); });
                if (stop.stop_requested())
                    return;
                task = std::move(work.front());
                work.pop_front();
            }
            try {
                task(stop);
            } catch (const Error& error) {
                fail(error);
            } catch (const std::exception& error) {
                fail(error);
            }
        }
    }
    void load_image(const std::string& url, std::stop_token stop) {
        const auto key = dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(url.data()), url.size()));
        const auto path = root / "images" / key;
        std::string bytes;
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (!ec && size <= MaxImage)
                bytes = dkr::updates::read_text(path, MaxImage);
        }
        if (bytes.empty()) {
            auto r = http({url, "GET", {}, {}, "application/json", {}, 0, MaxImage}, stop);
            if (r.status != 200 ||
                (!prefix(r.type, "image/png") && !prefix(r.type, "image/jpeg") && !prefix(r.type, "image/webp")))
                throw Error("Cover image unavailable.");
            bytes = std::move(r.body);
            std::filesystem::create_directories(path.parent_path());
            dkr::updates::durable_write(path, bytes);
        }
        int width = 0, height = 0, channels = 0;
        if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &width, &height,
                                   &channels) ||
            width < 1 || height < 1 || width > 4096 || height > 4096 ||
            std::uint64_t(width) * height > 16 * 1024 * 1024)
            throw Error("Cover image exceeds its safe size.");
        auto* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), int(bytes.size()), &width,
                                             &height, &channels, 4);
        if (!pixels)
            throw Error("Cover image could not be decoded.");
        std::unique_ptr<stbi_uc, void (*)(void*)> memory(pixels, stbi_image_free);
        auto image = std::make_shared<Image>();
        const double scale = std::min(1.0, 512.0 / double(std::max(width, height)));
        image->width = std::max(1U, unsigned(width * scale));
        image->height = std::max(1U, unsigned(height * scale));
        image->rgba.resize(std::size_t(image->width) * image->height * 4);
        for (unsigned y = 0; y < image->height; ++y)
            for (unsigned x = 0; x < image->width; ++x)
                for (unsigned c = 0; c < 4; ++c)
                    image->rgba[(std::size_t(y) * image->width + x) * 4 + c] =
                        pixels[(std::size_t(y * unsigned(height) / image->height) * width +
                                x * unsigned(width) / image->width) *
                                   4 +
                               c];
        std::lock_guard lock(mutex);
        if (images.size() >= 32)
            images.erase(images.begin());
        images[url] = std::move(image);
    }
    void image_run(std::stop_token stop) {
        for (;;) {
            std::string url;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, stop, [&] { return !image_queue.empty(); });
                if (stop.stop_requested())
                    return;
                url = std::move(image_queue.front());
                image_queue.pop_front();
            }
            try {
                load_image(url, stop);
            } catch (...) { /* A missing cover never blocks the catalogue. */
                std::lock_guard lock(mutex);
                image_retry[url] = Clock::now() + std::chrono::minutes(5);
            }
            std::lock_guard lock(mutex);
            image_pending.erase(url);
        }
    }
    void download_run(std::stop_token stop) {
        for (;;) {
            Download job;
            {
                std::unique_lock lock(mutex);
                cv.wait(lock, stop, [&] {
                    return !gameplay && std::any_of(state.downloads.begin(), state.downloads.end(),
                                                    [](const Download& d) { return d.state == "Queued"; });
                });
                if (stop.stop_requested())
                    return;
                auto it = std::find_if(state.downloads.begin(), state.downloads.end(),
                                       [](const Download& d) { return d.state == "Queued"; });
                job = *it;
                it->state = "Downloading";
                download_stop = std::stop_source{};
                publish();
            }
            try {
                persist_downloads();
            } catch (const std::exception& error) {
                fail(error);
            }
            auto cancelled = download_stop.get_token();
            std::stop_callback retire(stop, [&] { download_stop.request_stop(); });
            auto change = [&](const std::function<void(Download&)>& fn) {
                update([&](Snapshot& s) {
                    for (auto& d : s.downloads)
                        if (d.key == job.key)
                            fn(d);
                });
            };
            std::string source_url;
            try {
                auto integrity = get("/v1/launcher/releases/" + job.release, cancelled);
                job.digest = text(integrity, "sha256", 64);
                if (!hash_string(job.digest) || !integrity.contains("bytes") ||
                    !integrity["bytes"].is_number_unsigned())
                    throw Error("This release is not ready for verified installation.");
                job.total = integrity["bytes"].get<std::uint64_t>();
                const auto url = text(integrity, "url", 2048), name = text(integrity, "fileName", 180);
                source_url = url;
                if (job.total < 1 || job.total > MaxFile || !trusted_url(url) || name.empty() ||
                    name.find_first_of("\\/:*?\"<>|") != name.npos || name == "." || name == "..")
                    throw Error("The release has invalid file details.");
                const auto extension = lower(std::filesystem::path(name).extension().string());
                const std::set<std::string> formats = {".zip", ".rtz", ".dkrmap", ".xdelta",  ".nrm",      ".dkrcode",
                                                       ".bps", ".ips", ".7z",     ".dkrkart", ".dkrhover", ".dkrplane"};
                if (!formats.contains(extension))
                    throw Error("This package format is not supported.");
                const auto folder = root / "downloads" / job.digest;
                std::filesystem::create_directories(folder);
                // Legacy and texture importers use the source basename. Give
                // them the listing title, not a transport/cache placeholder.
                job.file = folder / std::filesystem::u8path(package_filename(job.title, name));
                auto partial = folder / "download.part";
                auto marker = folder / "download.json";
                std::error_code ec;
                if (std::filesystem::is_symlink(folder) || std::filesystem::is_symlink(job.file) ||
                    std::filesystem::is_symlink(partial))
                    throw Error("The download location is unsafe.");
                if (recover_cached_package(folder, job.file, job.digest, job.total, cancelled)) {
                    if (!package_signature(job.file))
                        throw Error("The cached file is not a valid mod package.");
                    change([&](Download& d) {
                        d.file = job.file;
                        d.digest = job.digest;
                        d.total = d.received = job.total;
                        d.state = "Ready to install";
                    });
                    continue;
                }
                auto meta = read_json(marker);
                std::uint64_t offset = std::filesystem::file_size(partial, ec);
                if (ec)
                    offset = 0;
                std::string etag = text(meta, "etag", 512);
                if (text(meta, "sha256", 64) != job.digest || text(meta, "url", 2048) != url || offset > job.total) {
                    offset = 0;
                    etag.clear();
                }
                if (offset == job.total) {
                    if (dkr::updates::hash_file(partial, MaxFile) == job.digest) {
                        dkr::updates::replace_file(partial, job.file);
                        if (!package_signature(job.file))
                            throw Error("The saved file is not a valid mod package.");
                        change([&](Download& d) {
                            d.file = job.file;
                            d.digest = job.digest;
                            d.total = d.received = job.total;
                            d.state = "Ready to install";
                            d.error.clear();
                        });
                        continue;
                    }
                    offset = 0;
                    etag.clear();
                    write_json(marker, Json::object());
                }
                const auto space = std::filesystem::space(folder, ec);
                if (ec || space.available < job.total - offset + 16 * 1024 * 1024)
                    throw Error("There is not enough free space for this download.");
                auto write_progress = Clock::now();
                bool opened = false;
                std::ofstream file;
                std::uint64_t received = offset;
                auto response = http(
                    {url, "GET", {}, {}, "application/octet-stream", etag, offset, job.total, 15000}, cancelled,
                    [&](const char* bytes, std::size_t count, const Response& headers) {
                        if (!opened) {
                            // Some hosts serve patches as text/plain. Inspect the
                            // bytes, then require full size, checksum and format;
                            // a MIME label alone is not a package signature.
                            if (webpage(bytes, count))
                                throw download_page_error(headers, bytes, count);
                            if (offset) {
                                const auto expected = "bytes " + std::to_string(offset) + "-";
                                if (headers.status != 206 || !prefix(headers.range, expected) ||
                                    (!etag.empty() && headers.etag != etag)) {
                                    write_json(marker, Json::object());
                                    throw Error("This server could not resume safely. Retry to restart the download.");
                                }
                            } else if (headers.status != 200)
                                throw Error("The download server returned an invalid response.");
                            file.open(partial, std::ios::binary | (offset ? std::ios::app : std::ios::trunc));
                            if (!file)
                                throw Error("The download could not be saved.");
                            write_json(marker, {{"sha256", job.digest}, {"url", url}, {"etag", headers.etag}});
                            opened = true;
                        }
                        if (count > job.total - received)
                            throw Error("The package is larger than its published size.");
                        file.write(bytes, std::streamsize(count));
                        if (!file)
                            throw Error("The download could not be saved.");
                        received += count;
                        if (Clock::now() - write_progress > std::chrono::milliseconds(100)) {
                            change([&](Download& d) {
                                d.received = received;
                                d.total = job.total;
                            });
                            write_progress = Clock::now();
                        }
                    });
                file.close();
                if (response.status != 200 && response.status != 206)
                    throw Error("The file is unavailable on the download server.", response.status);
                if (received != job.total)
                    throw Error("The download ended early. Retry to continue.");
                change([](Download& d) { d.state = "Verifying"; });
                if (dkr::updates::hash_file(partial, MaxFile) != job.digest) {
                    write_json(marker, Json::object());
                    throw Error("The downloaded file did not match the published checksum. It was not installed.");
                }
                dkr::updates::replace_file(partial, job.file);
                if (!package_signature(job.file))
                    throw Error("The file contents do not match its package format.");
                change([&](Download& d) {
                    d.state = "Ready to install";
                    d.file = job.file;
                    d.digest = job.digest;
                    d.received = d.total = job.total;
                    d.error.clear();
                });
            } catch (const std::exception& error) {
                if (!cancelled.stop_requested())
                    download_report(job.release, job.title, error, source_url);
                change([&](Download& d) {
                    d.state = cancelled.stop_requested() ? (gameplay ? "Paused for gameplay" : "Paused") : "Failed";
                    d.error = cancelled.stop_requested() && gameplay
                                  ? "Downloads resume when you return to the launcher."
                                  : error.what();
                });
            }
        }
    }
};

Service::Service() : impl_(std::make_unique<Impl>()) {
#include "account_bind.inl"
}
Service::~Service() {
    dkr::online::shutdown_account();
    auto& p = *impl_;
    p.login_stop.request_stop();
    p.download_stop.request_stop();
    p.stop_upload = true;
    p.worker.request_stop();
    p.image_worker.request_stop();
    p.download_worker.request_stop();
    p.auth_worker.request_stop();
    p.cv.notify_all();
}
Service& service() {
    static Service value;
    return value;
}
std::string download_key(const std::string& item, const std::string& release, const std::string& instance) {
    return item + "_" + release + (instance.empty() ? "" : "_" + instance);
}
void Service::gameplay(bool active) {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        p.gameplay = active;
        if (active)
            p.download_stop.request_stop();
        else
            for (auto& d : p.state.downloads)
                if (d.state == "Paused for gameplay") {
                    d.state = "Queued";
                    d.error.clear();
                }
        p.publish();
    }
    p.cv.notify_all();
}
void Service::configure(const std::filesystem::path& config, std::string version) {
    auto& p = *impl_;
    if (p.configured)
        return;
    p.configured = true;
    p.root = config / "mod-store";
    p.version = std::move(version);
    p.worker = std::jthread([&p](std::stop_token stop) { p.run(stop); });
    p.image_worker = std::jthread([&p](std::stop_token stop) { p.image_run(stop); });
    p.download_worker = std::jthread([&p](std::stop_token stop) { p.download_run(stop); });
    p.queue([&p](std::stop_token stop) {
        const auto preferences = read_json(p.root / "preferences.json"), cached = read_json(p.root / "catalogue.json");
        const auto saved = read_json(p.root / "downloads.json");
        const auto receipts = read_json(p.root / "installations.json");
        auto restored = history::restore<Download>(saved, receipts, [&](const std::string& path) {
            return history::member_present(p.root.parent_path(), path);
        });
        p.update([&](Snapshot& s) {
            if (preferences.value("layout", 0) == 1)
                s.layout = 1;
            s.remember = preferences.contains("remember") && preferences["remember"] == true;
#ifndef _WIN32
            if (!dkr::online::keyring::available())
                s.remember = false;
#endif
            if (cached.contains("items") && cached["items"].is_array()) {
                s.items = std::make_shared<const Json>(cached["items"]);
                s.cached = true;
            }
            s.downloads = std::move(restored);
        });
        if (saved.is_object() && saved.contains("jobs") && saved["jobs"].is_array() && saved["jobs"].size() <= 64 &&
            (!saved.contains("schema") || saved["schema"] == 1 || saved["schema"] == 2)) {
            try {
                p.persist_downloads();
            } catch (const std::exception& error) {
                p.fail(error);
            }
        }
        refresh_library_names(p.root.parent_path(), *p.published.load()->items, stop);
        if (p.published.load()->remember) {
            const auto device = load_device(p.root);
            {
                std::lock_guard lock(p.mutex);
                p.device = device;
            }
            if (!device.empty())
                p.restore(stop);
        }
    });
    p.queue([&p](std::stop_token stop) { p.refresh(stop); });
}
std::shared_ptr<const Snapshot> Service::view() const {
    return impl_->published.load();
}
void Service::layout(int value) {
    auto& p = *impl_;
    value = value == 1 ? 1 : 0;
    p.update([&](Snapshot& s) { s.layout = value; });
    p.queue([&p, value](std::stop_token) { p.preferences(); });
}
void Service::remember(bool remember) {
#ifndef _WIN32
    if (!dkr::online::keyring::available())
        remember = false;
#endif
    auto& p = *impl_;
    p.update([&](Snapshot& s) { s.remember = remember; });
    p.queue([&p](std::stop_token) { p.preferences(); });
}
void Service::refresh() {
    auto& p = *impl_;
    p.queue([&p](std::stop_token stop) { p.refresh(stop); });
}
bool Service::refresh_names() {
    auto& p = *impl_;
    {
        std::lock_guard lock(p.mutex);
        if (!p.configured || p.names_queued || p.work.size() >= 32)
            return false;
        p.names_queued = true;
    }
    if (!p.queue([&p](std::stop_token stop) {
            try {
                refresh_library_names(p.root.parent_path(), *p.published.load()->items, stop);
            } catch (...) { /* Optional label repair must never block later jobs. */
            }
            std::lock_guard lock(p.mutex);
            p.names_queued = false;
        })) {
        std::lock_guard lock(p.mutex);
        p.names_queued = false;
        return false;
    }
    return true;
}
void Service::reviews(std::string item) {
    if (!identifier(item))
        return;
    auto& p = *impl_;
    p.queue([&p, item](std::stop_token stop) {
        auto rows = p.paged("/v1/mods/" + item + "/reviews", "reviews", stop);
        p.update([&](Snapshot& s) {
            if (s.reviews.size() >= 16 && !s.reviews.contains(item))
                s.reviews.erase(s.reviews.begin());
            s.reviews[item] = std::move(rows);
        });
    });
}
void Service::action(std::string name, Json input) {
    static const std::set<std::string> allowed = {
        "review.save",      "review.report",  "report.resolve",    "request.submit",
        "request.resubmit", "request.start",  "request.note",      "request.decide",
        "request.publish",  "catalogue.save", "catalogue.publish", "catalogue.archive",
        "catalogue.update", "admin.role",     "account.profile",   "account.delete"};
    if (!allowed.contains(name))
        return;
    auto& p = *impl_;
    unsigned epoch;
    std::string credential;
    {
        std::lock_guard lock(p.mutex);
        if (p.state.loading || p.state.writing) {
            p.state.error = "Wait for the current library request to finish.";
            p.publish();
            return;
        }
        epoch = p.account;
        credential = p.token;
        p.state.loading = true;
        p.state.writing = true;
        p.publish();
    }
    const bool queued = p.queue([&p, name = std::move(name), input = std::move(input), epoch,
                                 credential](std::stop_token stop) {
        struct WritingFinish {
            Impl& p;
            unsigned epoch;
            ~WritingFinish() {
                try {
                    p.update([&](Snapshot& s) {
                        if (p.account == epoch)
                            s.loading = s.writing = false;
                    });
                } catch (...) {
                }
            }
        } writing_finish{p, epoch};
        {
            std::lock_guard lock(p.mutex);
            if (epoch != p.account)
                throw Error("The signed-in account changed. Reopen this form.");
        }
        if (credential.empty())
            throw Error("Sign in with Discord to continue.");
        Json response;
        try {
            response = document(
                {std::string(Api) + "/v1/actions/" + name, "POST", Json{{"input", input}}.dump(), credential}, stop);
        } catch (const Error& error) {
            if (error.status == 401)
                p.expire(credential);
            throw;
        }
        {
            std::lock_guard lock(p.mutex);
            if (epoch != p.account)
                return;
        }
        p.update([&](Snapshot& s) {
            s.status = "Your change was saved.";
            s.error.clear();
            s.last_action = name;
            s.last_result = response.value("result", Json{});
            ++s.mutation;
        });
        if (name == "account.delete") {
            p.expire(credential);
            std::error_code ignored;
            std::filesystem::remove(p.root / "device.bin", ignored);
            std::lock_guard lock(p.mutex);
            p.device.clear();
            p.state.loading = p.state.writing = false;
            p.state.status = "Library account deleted. Installed mods are unchanged.";
            p.publish();
            return;
        }
        std::string verification_warning;
        if ((name == "catalogue.publish" || name == "request.publish") && response.contains("result") &&
            response["result"].is_object()) {
            const auto& listing = response["result"];
            if (listing.contains("releases") && listing["releases"].is_array())
                for (const auto& release : listing["releases"]) {
                    const auto url = text(release, "fileUrl", 2048), id = text(release, "id", 160);
                    Json proof;
                    {
                        std::lock_guard lock(p.mutex);
                        if (p.account != epoch)
                            return;
                        auto found = p.upload_proofs.find(url);
                        if (found != p.upload_proofs.end())
                            proof = found->second;
                    }
                    if (proof.is_object() && identifier(id)) {
                        if (text(release, "sha256", 64) == text(proof, "sha256", 64) &&
                            release.value("size", 0ULL) == proof.value("bytes", 0ULL))
                            break;
                        proof["expectedRevision"] = listing.value("revision", 0);
                        try {
                            document(
                                {std::string(Api) + "/v1/launcher/releases/" + id, "POST", proof.dump(), credential},
                                stop);
                        } catch (...) {
                            verification_warning =
                                "The listing was published, but its download verification needs to be "
                                "registered. Do not publish again; use Verify current release.";
                        }
                        break;
                    }
                }
        }
        try {
            p.refresh(stop);
            p.update([&](Snapshot& s) {
                s.writing = false;
                if (!verification_warning.empty())
                    s.error = verification_warning;
            });
        } catch (...) {
            p.update([](Snapshot& s) {
                s.loading = false;
                s.writing = false;
                s.error = "Your change was saved, but the view could not refresh. Refresh the library; do not submit "
                          "it again.";
            });
        }
    });
    if (!queued)
        p.update([](Snapshot& s) {
            s.writing = false;
            s.loading = false;
        });
}
void Service::login() {
    auto& p = *impl_;
    if (p.auth_worker.joinable()) {
        if (view()->signing_in)
            return;
        p.auth_worker.join();
    }
    p.login_stop = std::stop_source{};
    p.update([](Snapshot& s) {
        s.signing_in = true;
        s.error.clear();
        s.status = "Opening Discord in your browser...";
    });
    p.auth_worker = std::jthread([&p](std::stop_token lifetime) {
        std::stop_callback retire(lifetime, [&] { p.login_stop.request_stop(); });
        const auto stop = p.login_stop.get_token();
        try {
            const auto verifier = nonce(), state = "dkrnative_" + nonce();
            auto flow = p.post("/auth/launcher/start", {{"challenge", challenge(verifier)}, {"clientState", state}},
                               stop, false);
            const auto id = text(flow, "flow", 43), url = text(flow, "browserUrl", 2048);
            if (!identifier(id) || id.size() != 43 || !trusted_url(url, true) ||
                !prefix(url, std::string(Api) + "/auth/launcher/authorize?"))
                throw Error("Invalid Discord sign-in address.");
            {
                std::lock_guard lock(p.mutex);
                p.browser = url;
                p.state.status = "Check sign-in code " + text(flow, "verificationCode", 8) + " in your browser.";
                p.publish();
            }
            bool remember;
            {
                std::lock_guard lock(p.mutex);
                remember = p.state.remember;
            }
            Json proof = {{"flow", id}, {"verifier", verifier}, {"clientState", state}, {"remember", remember}};
            const auto end = Clock::now() + std::chrono::minutes(9);
            while (!stop.stop_requested() && Clock::now() < end) {
                std::mutex wait_mutex;
                std::condition_variable_any wait_cv;
                std::unique_lock wait_lock(wait_mutex);
                wait_cv.wait_for(wait_lock, stop, std::chrono::seconds(4), [] { return false; });
                if (stop.stop_requested())
                    break;
                auto result = p.post("/auth/launcher/poll", proof, stop, false);
                if (result.value("pending", false))
                    continue;
                const auto token = text(result, "token", 43);
                if (token.size() != 43 || !identifier(token) ||
                    text(result.value("user", Json{}), "status", 16) != "active")
                    throw Error("Discord sign-in returned an invalid session.");
                const auto device = text(result, "device", 43);
                if (stop.stop_requested()) {
                    try {
                        document({std::string(Api) + "/auth/launcher/logout", "POST", Json{{"device", device}}.dump(),
                                  token},
                                 std::stop_token{});
                    } catch (...) {
                    }
                    throw Error("Sign-in was cancelled.");
                }
                std::string remember_warning;
                if (remember && device.size() == 43 && identifier(device))
                    try {
                        save_device(p.root, device);
                    } catch (const std::exception& error) {
                        remember_warning = error.what();
                    }
                // Account cancellation remains authoritative across keyring prompts.
                if (stop.stop_requested()) {
                    try {
                        Request revoke{std::string(Api) + "/auth/launcher/logout", "POST",
                                       Json{{"device", device}}.dump(), token};
                        revoke.timeout = 5000;
                        document(revoke, std::stop_token{});
                    } catch (...) {
                    }
#ifdef _WIN32
                    std::error_code cleanup;
                    std::filesystem::remove(p.root / "device.bin", cleanup);
#else
                    dkr::online::keyring::clear(p.root.generic_string());
#endif
                    throw Error("Sign-in was cancelled.");
                }
                {
                    std::lock_guard lock(p.mutex);
                    p.token = token;
                    p.device = device;
                    ++p.account;
                    p.upload_proofs.clear();
                    p.state.personal = Json::object();
                    p.state.staff = Json::object();
                    p.state.admin = Json::object();
                    p.state.uploads = Json::array();
                    p.state.assets = Json::array();
                    p.state.upload = Json::object();
                    p.state.uploading = false;
                    p.state.user = result["user"];
                    p.state.signing_in = false;
                    p.state.status = "Signed in with Discord.";
                    p.state.error = remember_warning;
                    if (!remember_warning.empty())
                        p.state.remember = false;
                    p.publish();
                }
                p.queue([&p](std::stop_token s) { p.private_refresh(s); });
                return;
            }
            try {
                p.post("/auth/launcher/cancel", proof, std::stop_token{}, false);
            } catch (...) {
            }
            throw Error("Discord sign-in was cancelled or expired.");
        } catch (const std::exception& error) {
            p.update([&](Snapshot& s) {
                s.signing_in = false;
                s.error = error.what();
            });
        }
    });
}
void Service::cancel_login() {
    impl_->login_stop.request_stop();
}
void Service::logout() {
    auto& p = *impl_;
    p.login_stop.request_stop();
    p.stop_upload = true;
    std::string token, device;
    {
        std::lock_guard lock(p.mutex);
        token = std::move(p.token);
        device = std::move(p.device);
        ++p.account;
        p.browser.clear();
        p.state.user = nullptr;
        p.state.personal = Json::object();
        p.state.staff = Json::object();
        p.state.admin = Json::object();
        p.state.uploads = Json::array();
        p.state.assets = Json::array();
        p.upload_proofs.clear();
        p.state.upload = Json::object();
        p.state.uploading = false;
        p.state.loading = p.state.writing = false;
        p.state.status = "Signed out.";
        p.state.last_action.clear();
        p.state.last_result = nullptr;
        p.publish();
    }
    p.queue([&p, token, device](std::stop_token stop) {
        std::error_code error;
        std::filesystem::remove(p.root / "device.bin", error);
#ifndef _WIN32
        dkr::online::keyring::clear(p.root.generic_string());
#endif
        try {
            if (!token.empty() || !device.empty())
                document({std::string(Api) + "/auth/launcher/logout", "POST", Json{{"device", device}}.dump(), token},
                         stop);
        } catch (...) {
        }
    });
}
void Service::download(const Json& item, const Json& release, std::string intent, std::string instance) {
    if (!hash_string(text(release, "sha256", 64)) && staff(view()->user)) {
        verify_release(item, release, true);
        return;
    }
    auto& p = *impl_;
    Download d;
    d.item = text(item, "id", 160);
    d.release = text(release, "id", 160);
    d.title = text(item, "title", 200);
    d.category = text(item, "category", 32);
    d.version = text(release, "version", 80);
    d.intent = std::move(intent);
    d.instance = std::move(instance);
    if (!identifier(d.item) || !identifier(d.release))
        return;
    d.key = download_key(d.item, d.release, d.instance);
    p.update([&](Snapshot& s) {
        auto found =
            std::find_if(s.downloads.begin(), s.downloads.end(), [&](const auto& v) { return v.key == d.key; });
        if (found == s.downloads.end()) {
            if (s.downloads.size() >= 64)
                std::erase_if(s.downloads, [](const Download& old) { return old.state == "Installed"; });
            if (s.downloads.size() >= 64) {
                s.error = "The download queue is full.";
                return;
            }
            s.downloads.push_back(d);
        } else if (found->state == "Failed" || found->state == "Paused" || found->state == "Installed" ||
                   found->state == "Ready to install" || found->state == "Install failed") {
            found->state = "Queued";
            found->error.clear();
            found->version = d.version;
            found->intent = d.intent;
            found->instance = d.instance;
        }
    });
    p.cv.notify_all();
}
void Service::verify_release(const Json& item, const Json& release, bool download_after) {
    const auto id = text(release, "id", 160), url = text(release, "fileUrl", 2048),
               name = text(release, "fileName", 180), title = text(item, "title", 200);
    if (!identifier(id) || !trusted_url(url) || !item.contains("revision") || !item["revision"].is_number_integer())
        return;
    const auto revision = item["revision"].get<unsigned>();
    auto& p = *impl_;
    unsigned epoch;
    std::string credential;
    const auto key = text(item, "id", 160) + "_" + id;
    std::stop_token preparation;
    {
        std::lock_guard lock(p.mutex);
        if (!staff(p.state.user))
            return;
        epoch = p.account;
        credential = p.token;
        if (download_after) {
            auto found = std::find_if(p.state.downloads.begin(), p.state.downloads.end(),
                                      [&](const auto& d) { return d.key == key; });
            if (found != p.state.downloads.end() && found->state != "Failed" && found->state != "Paused")
                return;
            if (found == p.state.downloads.end()) {
                if (p.state.downloads.size() >= 64) {
                    p.state.error = "The download queue is full.";
                    p.publish();
                    return;
                }
                Download d;
                d.key = key;
                d.item = text(item, "id", 160);
                d.release = id;
                d.title = text(item, "title", 200);
                d.category = text(item, "category", 32);
                d.version = text(release, "version", 80);
                p.state.downloads.push_back(std::move(d));
                found = std::prev(p.state.downloads.end());
            }
            found->state = "Preparing download";
            found->error.clear();
            p.preparation_stops[key] = std::stop_source{};
            preparation = p.preparation_stops[key].get_token();
            p.publish();
        }
    }
    const bool queued = p.queue([&p, id, url, name, title, revision, epoch, credential, key, download_after,
                                 preparation](std::stop_token lifetime) {
        std::stop_source linked;
        std::stop_callback retired(lifetime, [&] { linked.request_stop(); });
        std::stop_callback paused(preparation, [&] { linked.request_stop(); });
        const auto stop = linked.get_token();
        auto finish = [&](const std::filesystem::path& file, const std::string& digest, std::uint64_t bytes) {
            p.update([&](Snapshot& s) {
                if (download_after)
                    for (auto& d : s.downloads)
                        if (d.key == key) {
                            d.file = file;
                            d.digest = digest;
                            d.received = d.total = bytes;
                            d.state = stop.stop_requested() ? "Paused" : "Queued";
                        }
                p.preparation_stops.erase(key);
                s.status = "Download prepared.";
            });
            p.cv.notify_all();
        };
        try {
            {
                std::lock_guard lock(p.mutex);
                if (epoch != p.account)
                    throw Error("The staff account changed.");
            }
            // A catalogue snapshot can predate another staff member's preparation.
            // Reuse its manifest/cache instead of downloading the same file again.
            if (download_after) {
                try {
                    const auto ready = p.get("/v1/launcher/releases/" + id, stop);
                    if (hash_string(text(ready, "sha256", 64))) {
                        finish({}, text(ready, "sha256", 64), ready.value("bytes", 0ULL));
                        return;
                    }
                } catch (const Error& error) {
                    if (error.status != 409)
                        throw;
                }
            }
            const auto directory = p.root / "verification" / id;
            std::filesystem::create_directories(directory);
            const auto extension = lower(std::filesystem::path(name).extension().string());
            const std::set<std::string> allowed = {".zip", ".rtz", ".dkrmap", ".xdelta",  ".nrm",      ".dkrcode",
                                                   ".bps", ".ips", ".7z",     ".dkrkart", ".dkrhover", ".dkrplane"};
            if (!allowed.contains(extension))
                throw Error("This package format cannot be verified.");
            const auto target = directory / std::filesystem::u8path(package_filename(title, name));
            if (std::filesystem::is_symlink(directory) || std::filesystem::is_symlink(target))
                throw Error("Unsafe verification directory.");
            std::ofstream output(target, std::ios::binary | std::ios::trunc);
            if (!output)
                throw Error("The verification file could not be saved.");
            std::uint64_t bytes = 0;
            std::uint64_t next_space_check = 0;
            auto last = Clock::now();
            const auto result = http({url, "GET", {}, {}, "application/octet-stream", {}, 0, MaxFile, 15000}, stop,
                                     [&](const char* data, std::size_t length, const Response& response) {
                                         if (response.status != 200)
                                             throw Error("This mod's download file is unavailable.", response.status);
                                         if (!bytes && webpage(data, length))
                                             throw download_page_error(response, data, length);
                                         if (response.length > MaxFile)
                                             throw Error("This mod package exceeds the download size limit.");
                                         if (bytes >= next_space_check) {
                                             std::error_code error;
                                             const auto space = std::filesystem::space(directory, error);
                                             if (error || space.available < 16ULL * 1024 * 1024 + length)
                                                 throw Error("Not enough free space to verify this release.");
                                             next_space_check = bytes + 8ULL * 1024 * 1024;
                                         }
                                         output.write(data, std::streamsize(length));
                                         if (!output)
                                             throw Error("The verification file could not be saved.");
                                         bytes += length;
                                         if (Clock::now() - last > std::chrono::milliseconds(250)) {
                                             p.update([&](Snapshot& s) {
                                                 for (auto& d : s.downloads)
                                                     if (download_after && d.key == key) {
                                                         d.received = bytes;
                                                         d.total = response.length;
                                                     }
                                                 s.status =
                                                     "Checking release file: " + std::to_string(bytes / 1024) + " KiB";
                                             });
                                             last = Clock::now();
                                         }
                                     });
            output.close();
            if (result.status != 200 || !bytes || !package_signature(target))
                throw Error("The hosted package could not be verified.");
            const auto digest = dkr::updates::hash_file(target, MaxFile);
            {
                std::lock_guard lock(p.mutex);
                if (epoch != p.account)
                    throw Error("The staff account changed. Verification was not published.");
            }
            document({std::string(Api) + "/v1/launcher/releases/" + id, "POST",
                      Json{{"sha256", digest}, {"bytes", bytes}, {"expectedRevision", revision}}.dump(), credential},
                     stop);
            const auto cache = p.root / "downloads" / digest;
            std::filesystem::create_directories(cache);
            const auto saved = cache / std::filesystem::u8path(package_filename(title, name));
            if (!std::filesystem::is_symlink(cache) && !std::filesystem::exists(saved))
                dkr::updates::replace_file(target, saved);
            if (download_after) {
                finish(saved, digest, bytes);
                p.queue([&p](std::stop_token refresh_stop) { p.refresh(refresh_stop); });
                return;
            }
            p.update([](Snapshot& s) {
                s.status = "Release checksum published. This release can now be installed from the store.";
            });
            p.refresh(stop);
        } catch (const std::exception& error) {
            if (!stop.stop_requested())
                p.download_report(id, title, error, url);
            p.update([&](Snapshot& s) {
                p.preparation_stops.erase(key);
                for (auto& d : s.downloads)
                    if (download_after && d.key == key) {
                        d.state = stop.stop_requested() ? "Paused" : "Failed";
                        d.error = error.what();
                    }
            });
            if (download_after)
                return; // Transfer failures belong to this mod, not every store tab.
            throw;
        }
    });
    if (!queued && download_after)
        p.update([&](Snapshot& s) {
            p.preparation_stops.erase(key);
            for (auto& d : s.downloads)
                if (d.key == key) {
                    d.state = "Failed";
                    d.error = "Retry when the library is no longer busy.";
                }
        });
}
void Service::pause_download(std::string key) {
    auto& p = *impl_;
    p.update([&](Snapshot& s) {
        for (auto& d : s.downloads)
            if (d.key == key) {
                if (d.state == "Preparing download") {
                    const auto found = p.preparation_stops.find(key);
                    if (found != p.preparation_stops.end())
                        found->second.request_stop();
                }
                if (d.state == "Downloading")
                    p.download_stop.request_stop();
                else if (d.state == "Queued")
                    d.state = "Paused";
            }
    });
}
void Service::resume_download(std::string key) {
    const auto snapshot = view();
    for (const auto& job : snapshot->downloads)
        if (job.key == key && (job.state == "Paused" || job.state == "Failed"))
            for (const auto& item : *snapshot->items)
                if (text(item, "id", 160) == job.item && item.contains("releases") && item["releases"].is_array())
                    for (const auto& release : item["releases"])
                        if (text(release, "id", 160) == job.release) {
                            download(item, release, job.intent, job.instance);
                            return;
                        }
    auto& p = *impl_;
    p.update([&](Snapshot& s) {
        for (auto& d : s.downloads)
            if (d.key == key && (d.state == "Paused" || d.state == "Failed")) {
                d.state = "Queued";
                d.error.clear();
            }
    });
    p.cv.notify_all();
}
void Service::clear_completed_history() {
    auto& p = *impl_;
    p.queue([&p](std::stop_token) {
        p.update([](Snapshot& s) {
            std::erase_if(s.downloads, [](const Download& job) { return job.state == "Installed"; });
        });
        // History only: cache files, receipts, installed packages and saves stay.
        p.persist_downloads();
    });
}
void Service::clear_messages() {
    impl_->update([](Snapshot& s) {
        s.status.clear();
        s.error.clear();
    });
}
bool Service::begin_install(std::string key) {
    auto& p = *impl_;
    bool result = false;
    p.update([&](Snapshot& s) {
        for (auto& d : s.downloads)
            if (d.key == key && (d.state == "Ready to install" || d.state == "Install failed")) {
                d.state = "Installing";
                result = true;
            }
    });
    return result;
}
void Service::installed(std::string key, bool success, std::string message) {
    impl_->update([&](Snapshot& s) {
        for (auto& d : s.downloads)
            if (d.key == key) {
                const bool cancelled = message.starts_with("Installation cancelled.");
                d.state = success ? "Installed" : cancelled ? "Ready to install" : "Install failed";
                d.error = success || cancelled ? "" : message;
            }
        s.status = std::move(message);
    });
    impl_->queue([p = impl_.get()](std::stop_token stop) {
        p->persist_downloads();
        refresh_library_names(p->root.parent_path(), *p->published.load()->items, stop);
    });
}
void Service::request_image(std::string url) {
    auto& p = *impl_;
    if (!trusted_url(url))
        return;
    std::lock_guard lock(p.mutex);
    if (p.images.contains(url) || p.image_pending.contains(url) || p.image_queue.size() >= 16 ||
        (p.image_retry.contains(url) && p.image_retry[url] > Clock::now()))
        return;
    p.image_pending.insert(url);
    p.image_queue.push_back(std::move(url));
    p.cv.notify_all();
}
std::shared_ptr<const Image> Service::image(const std::string& url) const {
    std::lock_guard lock(impl_->mutex);
    auto it = impl_->images.find(url);
    return it == impl_->images.end() ? nullptr : it->second;
}
std::string Service::take_browser_url() {
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->browser, {});
}
void Service::load_staff_files() {
    auto& p = *impl_;
    unsigned epoch;
    {
        std::lock_guard lock(p.mutex);
        epoch = p.account;
    }
    p.queue([&p, epoch](std::stop_token stop) {
        auto uploads = p.get("/v1/uploads", stop, true), assets = p.get("/v1/assets", stop, true);
        std::lock_guard lock(p.mutex);
        if (epoch != p.account)
            return;
        p.state.uploads = uploads.value("uploads", Json::array());
        p.state.assets = assets.value("files", Json::array());
        p.publish();
    });
}
void Service::pause_upload() {
    impl_->stop_upload = true;
}
void Service::cancel_upload(std::string id) {
    if (!identifier(id))
        return;
    auto& p = *impl_;
    p.stop_upload = true;
    p.queue([&p, id](std::stop_token stop) {
        p.post("/v1/uploads/" + id + "/cancel", Json::object(), stop);
        p.update([](Snapshot& s) { s.status = "Upload cancellation requested."; });
    });
}
void Service::upload(std::filesystem::path source, std::string kind, std::string category, std::string resume_id) {
    auto& p = *impl_;
    if (!resume_id.empty() && !identifier(resume_id))
        return;
    p.stop_upload = false;
    unsigned epoch;
    std::string credential;
    {
        std::lock_guard lock(p.mutex);
        if (p.state.uploading || !staff(p.state.user))
            return;
        p.state.uploading = true;
        p.publish();
        epoch = p.account;
        credential = p.token;
    }
    const bool queued = p.queue([&p, source = std::move(source), kind = std::move(kind), category = std::move(category),
                                 resume_id, epoch, credential](std::stop_token stop) mutable {
        struct Finish {
            Impl& p;
            unsigned epoch;
            ~Finish() {
                try {
                    p.update([&](Snapshot& s) {
                        if (p.account == epoch)
                            s.uploading = false;
                    });
                } catch (...) {
                }
            }
        } finish{p, epoch};
        auto current = [&] {
            std::lock_guard lock(p.mutex);
            if (epoch != p.account || !staff(p.state.user))
                throw Error("The signed-in staff account changed. Reopen the uploader.");
        };
        current();
        auto call = [&](const std::string& path, const Json* body = nullptr) {
            current();
            Json result;
            try {
                result = document(
                    {std::string(Api) + path, body ? "POST" : "GET", body ? body->dump() : "", credential}, stop);
            } catch (const Error& error) {
                if (error.status == 401)
                    p.expire(credential);
                throw;
            }
            current();
            return result;
        };
        if (kind == "download" && vehicle_store_extension(lower(source.extension().string()))) {
            if (!vehicle_store_category(category))
                throw Error("Choose Vehicles before uploading a vehicle export.");
            source = prepare_vehicle_store_upload(source, p.root / "vehicle-uploads", stop);
        }
        const auto size = std::filesystem::file_size(source);
        if (!size || size > (kind == "image" ? 20ULL * 1024 * 1024 : MaxFile))
            throw Error("This file exceeds the upload size limit.");
        const auto digest = dkr::updates::hash_file(source, MaxFile);
        const auto fingerprint = upload_fingerprint(source, size);
        auto settings = call("/v1/uploads/settings");
        if (!settings.value("enabled", false) || !settings.value("configured", false))
            throw Error("Staff uploads are not configured on the website.");
        std::string id = resume_id;
        Json job;
        if (id.empty()) {
            const auto random = nonce();
            const auto client =
                dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(random.data()), random.size()))
                    .substr(0, 32);
            const Json creation = {{"fileName", source.filename().string()},
                                   {"size", size},
                                   {"kind", kind},
                                   {"category", category},
                                   {"fingerprint", fingerprint},
                                   {"clientId", client}};
            job = call("/v1/uploads", &creation).at("upload");
            id = text(job, "id", 160);
        } else
            job = call("/v1/uploads/" + id).at("upload");
        if (!identifier(id) || job.value("size", 0ULL) != size || text(job, "fingerprint", 64) != fingerprint ||
            text(job, "kind", 32) != kind || text(job, "category", 32) != category)
            throw Error("Choose the original unchanged file to resume this upload.");
        std::ifstream input(source, std::ios::binary);
        if (!input)
            throw Error("The upload file could not open.");
        auto offset = job.value("offset", 0ULL);
        const auto chunk = job.value("chunkBytes", 0U);
        if (offset > size || !chunk || chunk > Chunk)
            throw Error("Invalid upload progress from the server.");
        std::string buffer(chunk, '\0');
        if (offset) {
            auto saved = call("/v1/uploads/" + id);
            const auto parts = saved.value("resumeChunks", Json::array());
            if (!parts.is_array() || parts.size() > 512)
                throw Error("Invalid saved upload verification.");
            std::uint64_t end = 0;
            for (const auto& part : parts) {
                const std::uint64_t at = part.value("offset", MaxFile);
                const std::uint64_t length = part.value("size", 0ULL);
                if (at != end || !length || length != std::min<std::uint64_t>(chunk, size - end) ||
                    length > offset - end)
                    throw Error("Invalid saved upload verification.");
                input.read(buffer.data(), std::streamsize(length));
                if (std::uint64_t(input.gcount()) != length ||
                    dkr::updates::hash(std::span(reinterpret_cast<const std::uint8_t*>(buffer.data()),
                                                 std::size_t(length))) != text(part, "sha256", 64))
                    throw Error("The selected file differs from the saved upload.");
                end += length;
            }
            if (end != offset)
                throw Error("The saved upload prefix could not be verified.");
        }
        while (offset < size && !stop.stop_requested() && !p.stop_upload) {
            current();
            const auto bytes = std::size_t(std::min<std::uint64_t>(chunk, size - offset));
            input.seekg(std::streamoff(offset));
            input.read(buffer.data(), std::streamsize(bytes));
            if (std::size_t(input.gcount()) != bytes)
                throw Error("The selected upload file changed.");
            auto response =
                document({std::string(Api) + "/v1/uploads/" + id + "/chunk?offset=" + std::to_string(offset),
                          "POST",
                          buffer.substr(0, bytes),
                          credential,
                          "application/octet-stream",
                          {},
                          0,
                          MaxJson,
                          120000},
                         stop);
            current();
            job = response.at("upload");
            const auto next = job.value("offset", 0ULL);
            if (next != offset + bytes)
                throw Error("The server did not confirm this upload chunk. Resume to check progress.");
            offset = next;
            p.update([&](Snapshot& s) {
                if (p.account != epoch)
                    return;
                s.upload = job;
                s.status = "Uploading " + source.filename().string();
            });
        }
        if (offset == size) {
            if (dkr::updates::hash_file(source, MaxFile) != digest)
                throw Error("The selected file changed during upload.");
            const auto empty = Json::object();
            job = call("/v1/uploads/" + id + "/complete", &empty).at("upload");
            current();
            if (!job.contains("asset") || !job["asset"].is_object() || !trusted_url(text(job["asset"], "url", 2048)) ||
                job["asset"].value("size", 0ULL) != size)
                throw Error("The completed upload has invalid file details.");
            job["fullSha256"] = digest;
            if (kind == "download") {
                const Json proof = {{"sha256", digest}, {"bytes", size}};
                call("/v1/launcher/uploads/" + id + "/integrity", &proof);
            }
            {
                std::lock_guard lock(p.mutex);
                if (p.account == epoch && kind == "download") {
                    if (p.upload_proofs.size() >= 16)
                        p.upload_proofs.erase(p.upload_proofs.begin());
                    p.upload_proofs[text(job["asset"], "url", 2048)] = {{"sha256", digest}, {"bytes", size}};
                }
            }
            p.update([&](Snapshot& s) {
                if (p.account != epoch)
                    return;
                s.upload = job;
                s.status = "Upload complete. Review the file before publishing.";
            });
        } else
            p.update([&](Snapshot& s) {
                if (p.account != epoch)
                    return;
                s.upload = job;
                s.status = "Upload paused. Select the same file to resume.";
            });
    });
    if (!queued)
        p.update([](Snapshot& s) { s.uploading = false; });
}
} // namespace dkr::store

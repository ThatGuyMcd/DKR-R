#include "reporting.hpp"
#include <algorithm>
#include <cmath>
#include <json/json.hpp>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif !defined(__ANDROID__) && !defined(__APPLE__)
#include <curl/curl.h>
#endif

namespace dkr::crash_reports {
namespace {
// Public collector only. The Discord credential exists exclusively in the
// relay's encrypted environment. No accounts, cookies or player IDs are used.
constexpr auto Origin = "https://dkr-crash-reports.ross-macgregorbars.workers.dev";
struct Reply {
    unsigned status = 0;
    unsigned retry = 0;
    std::string body;
};
unsigned retry_after(std::string_view text) {
    try {
        return unsigned(std::clamp(std::ceil(std::stod(std::string(text))), 0., 86400.));
    } catch (...) {
        return 0;
    }
}
#ifdef _WIN32
struct Handle {
    HINTERNET value = nullptr;
    ~Handle() {
        if (value)
            WinHttpCloseHandle(value);
    }
    operator HINTERNET() const {
        return value;
    }
};
Reply request(const Pending& pending, std::stop_token stop) {
    Reply reply;
    Handle session{WinHttpOpen(L"DKR-R Crash Reports/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value || stop.stop_requested())
        return reply;
    WinHttpSetTimeouts(session, 2000, 2000, 3000, 3000);
    Handle connection{
        WinHttpConnect(session, L"dkr-crash-reports.ross-macgregorbars.workers.dev", INTERNET_DEFAULT_HTTPS_PORT, 0)};
    const auto route = "/v1/crash-reports/" + pending.id;
    const std::wstring path(route.begin(), route.end());
    Handle call{WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
    if (!call.value)
        return reply;
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER, credentials = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    WinHttpSetOption(call, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    WinHttpSetOption(call, WINHTTP_OPTION_AUTOLOGON_POLICY, &credentials, sizeof(credentials));
    if (!WinHttpSendRequest(call, L"Content-Type: application/zip\r\n", DWORD(-1), nullptr, 0,
                            DWORD(pending.bytes.size()), 0))
        return reply;
    for (std::size_t at = 0; at < pending.bytes.size();) {
        if (stop.stop_requested())
            return {};
        const auto amount = DWORD(std::min<std::size_t>(65536, pending.bytes.size() - at));
        DWORD written = 0;
        if (!WinHttpWriteData(call, pending.bytes.data() + at, amount, &written) || written == 0)
            return reply;
        at += written;
    }
    if (stop.stop_requested() || !WinHttpReceiveResponse(call, nullptr))
        return {};
    DWORD size = sizeof(reply.status);
    WinHttpQueryHeaders(call, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &reply.status, &size, WINHTTP_NO_HEADER_INDEX);
    wchar_t delay[64]{};
    size = sizeof(delay);
    if (WinHttpQueryHeaders(call, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX, delay, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
        std::wstring value(delay);
        reply.retry = retry_after(std::string(value.begin(), value.end()));
    }
    char buffer[1024];
    DWORD got = 0;
    while (!stop.stop_requested() && WinHttpReadData(call, buffer, sizeof(buffer), &got) && got) {
        if (reply.body.size() + got > 4096)
            return {};
        reply.body.append(buffer, got);
    }
    return stop.stop_requested() ? Reply{} : reply;
}
#elif !defined(__ANDROID__) && !defined(__APPLE__)
Reply request(const Pending& pending, std::stop_token stop) {
    Reply reply;
    static const auto initialised = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialised != CURLE_OK)
        return reply;
    CURL* curl = curl_easy_init();
    if (!curl)
        return reply;
    const auto url = std::string(Origin) + "/v1/crash-reports/" + pending.id;
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/zip");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "DKR-R Crash Reports/1");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pending.bytes.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(pending.bytes.size()));
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NETRC, CURL_NETRC_IGNORED);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &stop);
    curl_easy_setopt(
        curl, CURLOPT_XFERINFOFUNCTION, +[](void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<std::stop_token*>(data)->stop_requested() ? 1 : 0;
        });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(
        curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t a, size_t b, void* target) -> size_t {
            auto& body = static_cast<Reply*>(target)->body;
            if (a && b > 4096 / a)
                return 0;
            const auto size = a * b;
            if (body.size() + size > 4096)
                return 0;
            body.append(data, size);
            return size;
        });
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &reply);
    curl_easy_setopt(
        curl, CURLOPT_HEADERFUNCTION, +[](char* data, size_t a, size_t b, void* target) -> size_t {
            if (a && b > 8192 / a)
                return 0;
            const std::string_view line(data, a * b);
            if (line.starts_with("Retry-After:") || line.starts_with("retry-after:"))
                static_cast<Reply*>(target)->retry = retry_after(line.substr(12));
            return a * b;
        });
    const auto result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (result == CURLE_OK && !stop.stop_requested())
        reply.status = unsigned(status);
    return reply;
}
#else
Reply request(const Pending&, std::stop_token) {
    return {503};
}
#endif
} // namespace
Delivery upload(const Pending& pending, std::stop_token stop) {
    if (pending.id.size() != 32 || pending.bytes.empty() || pending.bytes.size() > MaxUpload || stop.stop_requested())
        return {};
    const auto reply = request(pending, stop);
    bool confirmed = false;
    if (reply.status == 200) {
        const auto body = nlohmann::json::parse(reply.body, nullptr, false);
        confirmed =
            body.is_object() && body.value("delivered", false) && body.value("reportId", std::string{}) == pending.id;
    }
    return {reply.status, reply.retry, confirmed};
}
} // namespace dkr::crash_reports

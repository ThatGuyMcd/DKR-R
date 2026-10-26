#pragma once
#include "update_core.hpp"
#include <functional>
#include <map>
#include <memory>
#include <stop_token>

namespace dkr::updates {
struct HttpRequest {
    std::string url, etag;
    std::filesystem::path file;
    std::uint64_t limit = kMaxMetadataBytes, offset{};
    unsigned timeout_seconds = 12;
    std::filesystem::path etag_file;
};
struct HttpResponse {
    unsigned status{};
    std::map<std::string, std::string> headers;
    std::string body;
};
using Progress = std::function<void(std::uint64_t)>;
using Transport = std::function<HttpResponse(const HttpRequest&, std::stop_token, const Progress&)>;
HttpResponse https_get(const HttpRequest&, std::stop_token, const Progress&);
#if defined(__APPLE__) || defined(__ANDROID__)
std::string native_os_version();
#endif
// Platform implementations receive the same bounded sink and redirect policy.
class HttpSink {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    HttpResponse response;
    explicit HttpSink(const HttpRequest&, std::stop_token, const Progress&);
    ~HttpSink();
    bool header(std::string_view key, std::string_view value);
    bool write(std::span<const std::uint8_t>);
    void finish();
    bool cancelled() const;
    std::string error;
};
} // namespace dkr::updates

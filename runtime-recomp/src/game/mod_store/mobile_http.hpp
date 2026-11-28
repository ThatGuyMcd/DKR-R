#pragma once

#include <cstddef>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dkr::store {
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

struct MobileHttpReceiver {
    std::function<void(unsigned)> status;
    std::function<void(std::string_view, std::string_view)> header;
    std::function<void(const char*, std::size_t)> write;
    std::stop_token stop;
};

void mobile_http(std::string_view url, std::string_view method, std::string_view body, const HttpHeaders& headers,
                 unsigned timeout_ms, MobileHttpReceiver& receiver);
} // namespace dkr::store

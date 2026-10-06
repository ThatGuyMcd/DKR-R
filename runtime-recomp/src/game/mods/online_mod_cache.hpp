#pragma once
#include "online_mod_bundle.hpp"

namespace dkr::mods::online {
// Worker-owned disk cache. Neither the network nor UI thread performs I/O.
// Partial bytes live outside profiles and cannot be activated. A completed
// payload is published only after size/full SHA verification.
class PayloadFile {
public:
    PayloadFile(const std::filesystem::path& partial,const Payload& expected);
    ~PayloadFile();
    PayloadFile(const PayloadFile&)=delete;
    PayloadFile& operator=(const PayloadFile&)=delete;
    std::uint64_t size()const{return size_;}
    void append(View bytes);
    void publish(const std::filesystem::path& target);
private:
    void close() noexcept;
    std::filesystem::path path_;
    Payload expected_;
    std::uint64_t size_=0;
    std::intptr_t handle_=-1;
};
bool cached_payload(const std::filesystem::path& directory,const Payload& expected);
void cache_bundle(const Bundle& bundle,const std::filesystem::path& directory,std::stop_token stop={});
}

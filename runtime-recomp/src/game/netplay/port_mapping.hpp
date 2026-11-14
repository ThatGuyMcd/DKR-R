#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace dkr::runtime::netplay {

enum class PortMappingState : std::uint8_t {
    Inactive,
    Mapped,
    ManualRequired,
};

struct PortMappingView {
    PortMappingState state = PortMappingState::Inactive;
    std::string method;
    std::string external_address;
    std::uint16_t external_port = 0U;
    std::string message;
};

class PortMapping final {
  public:
    PortMapping();
    ~PortMapping();
    PortMapping(const PortMapping&) = delete;
    PortMapping& operator=(const PortMapping&) = delete;

    bool open(std::string local_address, std::uint16_t local_port, std::string& error);
    void close();
    const PortMappingView& view() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dkr::runtime::netplay

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dkr::runtime::netplay {

struct PeerAddress {
    std::array<std::uint8_t, 128> storage{};
    std::uint32_t size = 0;

    bool operator==(const PeerAddress&) const;
    explicit operator bool() const {
        return size != 0U;
    }
};

enum class DatagramSendStatus : std::uint8_t {
    Sent,
    WouldBlock,
    Error,
};

class DatagramSocket final {
  public:
    DatagramSocket();
    ~DatagramSocket();
    DatagramSocket(const DatagramSocket&) = delete;
    DatagramSocket& operator=(const DatagramSocket&) = delete;

    bool open(std::uint16_t port, std::string& error);
    void close();
    bool is_open() const;
    std::uint16_t local_port() const;
    bool send(const PeerAddress& destination, std::span<const std::uint8_t> bytes, std::string& error);
    DatagramSendStatus send_status(const PeerAddress& destination, std::span<const std::uint8_t> bytes,
                                   std::string& error);
    bool receive(PeerAddress& source, std::vector<std::uint8_t>& bytes, std::string& error);

    static bool resolve(std::string_view host, std::uint16_t port, PeerAddress& address, std::string& error);
    static std::string describe(const PeerAddress& address);
    static std::string describe_host(const PeerAddress& address);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace dkr::runtime::netplay

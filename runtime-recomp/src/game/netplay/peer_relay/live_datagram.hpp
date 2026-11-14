#pragma once
#include "core.hpp"
#include "netplay/session_transport.hpp"
#include <memory>

namespace dkr::relay {
// Owner-thread adapter for small, already sealed race packets. ICE signaling
// enters only through the existing authenticated lobby extension. Received
// bytes still pass DirectSession's AEAD, sender, room and replay checks.
class LiveDatagram final {
  public:
    LiveDatagram();
    ~LiveDatagram();
    void configure(const Context&, const std::vector<std::string>& servers);
    bool control(Id source, std::span<const std::uint8_t> bytes);
    std::vector<Control> controls();
    bool send(Id destination, std::span<const std::uint8_t> sealed, unsigned lane);
    bool receive(Id& source, Bytes& sealed, unsigned& lane);
    void discard(unsigned lane);
    void signal(std::shared_ptr<dkr::runtime::netplay::TransportReceiveSignal>);
    void reset();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dkr::relay

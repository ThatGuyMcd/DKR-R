#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <utility>

namespace dkr::online_stability {
// Borrow both lifetimes. Receive remains behind checkpoint admission; sends
// retain the route's specialized owner hint. A prepared checkpoint can service
// authenticated input/ACKs before normal Start, without stepping the game.
template <class Base, class Traffic, class Admission, class Route> class StartupTransport final : public Base {
    using Address = decltype(Admission::address(std::uint8_t{}));
    using Status = decltype(std::declval<Route&>().send_status(std::declval<const Address&>(),
                                                               std::declval<std::span<const std::uint8_t>>(),
                                                               std::declval<Traffic>(), std::declval<std::string&>()));

  public:
    StartupTransport(Admission& admission, Route& route) : admission_(admission), route_(route) {}
    bool open(std::uint16_t, std::string&) override {
        return false;
    }
    void close() override {}
    bool is_open() const override {
        return admission_.is_open();
    }
    std::uint16_t local_port() const override {
        return route_.local_port();
    }
    std::size_t maximum_plaintext_datagram_bytes() const override {
        return route_.maximum_plaintext_datagram_bytes();
    }
    bool traffic_ready(const Address& p, Traffic t) const override {
        return admission_.prepared() && route_.traffic_ready(p, t);
    }
    Status send_status(const Address& p, std::span<const std::uint8_t> b, Traffic t, std::string& e) override {
        if (!admission_.prepared()) {
            e.clear();
            return Status::WouldBlock;
        }
        return route_.send_status(p, b, t, e);
    }
    Status send_live_status(const Address& p, std::span<const std::uint8_t> b, std::uint8_t owner,
                            std::string& e) override {
        if (!admission_.prepared()) {
            e.clear();
            return Status::WouldBlock;
        }
        return route_.send_live_status(p, b, owner, e);
    }
    bool receive(Address& p, std::vector<std::uint8_t>& b, std::string& e) override {
        return admission_.receive(p, b, e);
    }
    void service() override {
        admission_.service_admission();
    }

  private:
    Admission& admission_;
    Route& route_;
};
} // namespace dkr::online_stability

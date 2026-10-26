#include "session_transport.hpp"

namespace dkr::runtime::netplay {
namespace {

class UdpSessionTransport final : public SessionTransport {
  public:
    bool open(std::uint16_t port, std::string& error) override {
        return socket_.open(port, error);
    }
    void close() override {
        socket_.close();
    }
    bool is_open() const override {
        return socket_.is_open();
    }
    std::uint16_t local_port() const override {
        return socket_.local_port();
    }
    DatagramSendStatus send_status(const PeerAddress& destination, std::span<const std::uint8_t> bytes,
                                   TransportTrafficClass, std::string& error) override {
        return socket_.send_status(destination, bytes, error);
    }
    bool receive(PeerAddress& source, std::vector<std::uint8_t>& bytes, std::string& error) override {
        return socket_.receive(source, bytes, error);
    }

  private:
    DatagramSocket socket_;
};

} // namespace

std::unique_ptr<SessionTransport> make_udp_session_transport() {
    return std::make_unique<UdpSessionTransport>();
}

} // namespace dkr::runtime::netplay

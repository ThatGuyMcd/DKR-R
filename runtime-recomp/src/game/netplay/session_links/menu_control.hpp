#pragma once
#include <type_traits>

namespace dkr::runtime::netplay::links::menu_control {
// Construction context only. Each owned world retains this agreed port; tick
// and replay never consult a live lobby or another thread's UI state.
inline thread_local unsigned construction_port = 0;
class CreationPort final {
    unsigned previous_;

  public:
    explicit CreationPort(unsigned port) : previous_(construction_port) {
        construction_port = port < 4 ? port : 0;
    }
    ~CreationPort() {
        construction_port = previous_;
    }
    CreationPort(const CreationPort&) = delete;
    CreationPort& operator=(const CreationPort&) = delete;
};
inline unsigned creating_port() {
    return construction_port;
}
template <class Inputs>
auto input(const Inputs& values, unsigned authority, unsigned guest_port, bool host_only, unsigned assigned) {
    using Value = std::remove_cvref_t<decltype(values[0])>;
    if (authority >= 4 || guest_port >= 4)
        return Value{};
    if (host_only)
        return guest_port == 0 ? values[authority] : Value{};
    return !assigned || (assigned & (1U << guest_port)) ? values[guest_port] : Value{};
}
} // namespace dkr::runtime::netplay::links::menu_control

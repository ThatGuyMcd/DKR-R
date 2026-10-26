#pragma once

#include "direct_session.hpp"
#include "gekkonet.h"

namespace dkr::runtime::netplay {

// Bridges GekkoNet's peer-addressed datagrams onto DirectSession's existing
// authenticated transport. A one-byte address is the immutable lobby slot;
// DirectSession validates and relays it through the host when necessary.
class GekkoDirectAdapter final {
  public:
    GekkoDirectAdapter() = default;
    ~GekkoDirectAdapter();
    GekkoDirectAdapter(const GekkoDirectAdapter&) = delete;
    GekkoDirectAdapter& operator=(const GekkoDirectAdapter&) = delete;

    bool activate(DirectSession& session);
    void deactivate();
    bool active() const;
    GekkoNetAdapter* adapter();
    DirectSession* session() const {
        return session_;
    }

  private:
    DirectSession* session_ = nullptr;
};

} // namespace dkr::runtime::netplay

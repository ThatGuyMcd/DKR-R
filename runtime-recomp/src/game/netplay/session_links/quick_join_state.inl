std::atomic<bool> host_{false}, preparing_host_{false};
std::set<std::string> authorized_remote_;
std::map<std::uint64_t, std::string> peer_aliases_;

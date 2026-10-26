// Inside Service's constructor; the account credential stays in Impl.
{
    auto& p = *impl_;
    dkr::online::bind_account(
        [&p] {
            std::lock_guard lock(p.mutex);
            return dkr::online::Account{!p.token.empty() && text(p.state.user, "status", 16) == "active", p.account,
                                        text(p.state.user, "displayName", 100)};
        },
        [&p](std::string method, std::string path, Json input, std::string instance, std::stop_token stop) {
            if (!path.starts_with("/v1/online/") || path.find_first_of("\r\n\\#") != std::string::npos ||
                instance.size() != 64 || !identifier(instance) || (method != "GET" && method != "POST"))
                throw Error("Invalid online account request.");
            std::string credential;
            unsigned generation;
            {
                std::lock_guard lock(p.mutex);
                credential = p.token;
                generation = p.account;
            }
            if (credential.empty())
                throw Error("Sign in with Discord to continue.", 401);
            Request request{std::string(Api) + path, method, method == "POST" ? input.dump() : std::string{},
                            credential};
            request.online_instance = std::move(instance);
            request.timeout = 5000;
            try {
                auto result = document(request, stop);
                {
                    std::lock_guard lock(p.mutex);
                    if (generation != p.account || credential != p.token)
                        throw Error("The signed-in account changed.", 401);
                }
                return result;
            } catch (const Error& error) {
                if (error.status == 401) {
                    p.expire(credential);
                    unsigned refresh_generation = 0;
                    {
                        std::lock_guard lock(p.mutex);
                        if (p.token.empty() && !p.device.empty() && p.account == generation + 1)
                            refresh_generation = p.account;
                    }
                    if (refresh_generation)
                        p.queue([&p, refresh_generation](std::stop_token stop) {
                            {
                                std::lock_guard lock(p.mutex);
                                if (p.account != refresh_generation || !p.token.empty())
                                    return;
                            }
                            try {
                                p.restore(stop);
                            } catch (const std::exception& error) {
                                p.fail(error);
                            }
                        });
                }
                throw;
            }
        });
}

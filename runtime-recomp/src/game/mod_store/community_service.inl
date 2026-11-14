// Included in the retained Service::Impl. Reuse its worker, credential and
// account epoch. One bounded discussion lives in the existing extensible JSON
// snapshot; no retained class layout or generated ABI header is changed.
std::uint64_t community_serial = 0;
unsigned community_account = 0;
struct CommunityDownload {
    std::string release, attempt;
};
std::deque<CommunityDownload> community_downloads;
std::jthread community_tracking_worker;
void community_send(const CommunityDownload& job, std::stop_token stop) noexcept {
    try {
        (void)http_once({std::string(Api) + "/v1/downloads/" + job.release + "?attempt=" + job.attempt,
                         "GET",
                         {},
                         {},
                         "application/json",
                         {},
                         0,
                         4096,
                         1500},
                        stop, {});
    } catch (...) {
    }
}
void community_tracking(std::stop_token stop) {
    for (;;) {
        CommunityDownload job;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, stop, [&] { return !gameplay && !community_downloads.empty(); });
            if (stop.stop_requested())
                return;
            job = std::move(community_downloads.front());
            community_downloads.pop_front();
        }
        community_send(job, stop);
    }
}
void community_fence() {
    if (community_account != account) {
        community_account = account;
        ++community_serial;
        state.reviews.erase(community::SnapshotKey);
    }
}
bool community_action(const std::string& name, const Json& input) {
    if (!name.starts_with("community."))
        return false;
    const auto item = text(input, "itemId", 96);
    const bool read = name == "community.load";
    if (!community::identifier(item) || (!read && name != "community.create" && name != "community.edit" &&
                                         name != "community.delete" && name != "community.report"))
        return true;
    unsigned epoch;
    std::uint64_t serial;
    std::string credential;
    {
        std::lock_guard lock(mutex);
        community_fence();
        if (read && state.writing)
            return true;
        if (!read && (state.writing || state.loading || token.empty())) {
            state.error =
                token.empty() ? "Sign in with Discord to comment." : "Wait for the current library request to finish.";
            publish();
            return true;
        }
        epoch = account;
        credential = token;
        serial = ++community_serial;
        auto& discussion = state.reviews[community::SnapshotKey];
        if (!discussion.is_object() || discussion.value("itemId", std::string{}) != item)
            discussion = {{"itemId", item}, {"comments", Json::array()}, {"error", ""}};
        discussion["loading"] = true;
        discussion["error"] = "";
        if (!read)
            state.writing = true;
        publish();
    }
    const bool queued = queue([this, name, input, item, read, epoch, serial, credential](std::stop_token stop) {
        const auto active = [&] { return community::current(account, epoch, community_serial, serial); };
        const auto finish = [&](const std::string& error) {
            update([&](Snapshot& s) {
                if (account != epoch)
                    return;
                if (!read)
                    s.writing = false;
                if (active()) {
                    auto& d = s.reviews[community::SnapshotKey];
                    d["loading"] = false;
                    d["error"] = error;
                }
            });
        };
        bool committed = false;
        try {
            {
                std::lock_guard lock(mutex);
                if (!active()) {
                    if (!read && account == epoch)
                        state.writing = false;
                    return;
                }
            }
            if (!read) {
                auto body = input;
                const auto op = text(body, "operationId", 32), comment = text(body, "commentId", 96);
                if (!community::operation(op) || (name != "community.create" && !community::identifier(comment)))
                    throw Error("Refresh the comment form before trying again.");
                std::string path;
                if (name == "community.create")
                    path = "/v1/mods/" + item + "/comments";
                else {
                    path = "/v1/comments/" + comment;
                    if (name == "community.report")
                        path += "/report";
                    else
                        body["action"] = name == "community.edit" ? "edit" : "delete";
                }
                const auto result = document({std::string(Api) + path, "POST", body.dump(), credential}, stop);
                committed = true;
                update([&](Snapshot& s) {
                    if (account != epoch)
                        return;
                    s.last_action = name;
                    s.last_result = {{"response", result},
                                     {"itemId", item},
                                     {"operationId", op},
                                     {"submittedText", text(input, "text", 8000)},
                                     {"commentId", comment}};
                    ++s.mutation;
                    s.error.clear();
                    s.status = name == "community.report" ? "Comment reported." : "Your comment change was saved.";
                });
            }
            const auto page_cursor = read ? community::cursor(input) : std::string{};
            const auto data = document({std::string(Api) + "/v1/mods/" + item + "/comments" +
                                            (page_cursor.empty() ? "" : "?cursor=" + encode(page_cursor)),
                                        "GET",
                                        {},
                                        credential},
                                       stop);
            auto next = community::page(item, data, page_cursor);
            update([&](Snapshot& s) {
                if (active()) {
                    auto items = std::make_shared<Json>(*s.items);
                    for (auto& row : *items)
                        if (text(row, "id", 96) == item)
                            row["commentCount"] = data["total"];
                    s.items = std::move(items);
                    s.reviews[community::SnapshotKey] = std::move(next);
                }
                if (account == epoch && !read)
                    s.writing = false;
            });
        } catch (const Error& error) {
            if (error.status == 401 && !credential.empty())
                expire(credential);
            finish(committed ? "Your change was saved, but the discussion could not be refreshed. Refresh to see it."
                             : error.what());
        } catch (const std::exception& error) {
            finish(committed ? "Your change was saved. Refresh the discussion to see it." : error.what());
        }
    });
    if (!queued)
        update([&](Snapshot& s) {
            if (account == epoch && !read)
                s.writing = false;
            if (community::current(account, epoch, community_serial, serial)) {
                s.reviews[community::SnapshotKey]["loading"] = false;
                s.reviews[community::SnapshotKey]["error"] = "The library is busy. Try again shortly.";
            }
        });
    return true;
}
std::string community_download(const std::string& release, const std::filesystem::path& marker) noexcept {
    try {
        auto saved = read_json(marker);
        if (!saved.is_object())
            saved = Json::object();
        auto attempt = text(saved, "communityAttempt", 32);
        if (!community::operation(attempt))
            attempt = community::operation_id();
        saved["communityAttempt"] = attempt;
        write_json(marker, saved);
        // Count the same initiated request as the website. Never follow the
        // tracking redirect or send a credential. Failure cannot stop a file.
        {
            std::lock_guard lock(mutex);
            if (community_downloads.size() < 16)
                community_downloads.push_back({release, attempt});
        }
        cv.notify_all();
        return attempt;
    } catch (...) {
        return {};
    }
}

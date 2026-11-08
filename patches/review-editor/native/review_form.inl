void StoreReviewForm(const StoreJson& row, float width, const dkr::store::Snapshot& store) {
    const auto item = StoreText(row, "id", 160), prefix = "review-" + item;
    if (store.user.is_null()) {
        StoreNote("Sign in with Discord to leave a review.", width);
        if (StoreButton("Sign in with Discord"))
            dkr::store::service().login();
        return;
    }
    if (!g_mod_store.fields.contains(prefix + "-text")) {
        if (StoreBusyAction(store) && !store.personal.contains("reviews")) {
            StoreNote("Loading your review...", width);
            return;
        }
        g_mod_store.fields[prefix + "-text"] = "";
        g_mod_store.fields[prefix + "-version"] = StoreText(row, "version", 80);
        g_mod_store.fields[prefix + "-rating"] = "5";
        for (const auto& own : store.personal.value("reviews", StoreJson::array()))
            if (StoreText(own, "itemId", 160) == item) {
                g_mod_store.fields[prefix + "-text"] = StoreText(own, "text", 4000);
                g_mod_store.fields[prefix + "-version"] = StoreText(own, "version", 80);
                g_mod_store.fields[prefix + "-rating"] = std::to_string(std::clamp(StoreInt(own, "score", 5), 1, 5));
            }
    }
    StorePanel form(std::min(width, 820.F));
    const auto inner = form.inner();
    StoreHeading("Your review", inner, 20);
    ImGui::PushID(prefix.c_str());
    ImGui::BeginDisabled(StoreBusyAction(store));
    auto& rating = g_mod_store.fields[prefix + "-rating"];
    StoreNote("Rating", inner);
    const int selected = rating.empty() ? 4 : std::clamp(rating[0] - '1', 0, 4);
    rating = std::to_string(StoreTabLine({"1 star", "2 stars", "3 stars", "4 stars", "5 stars"},
                                        selected, inner, "review-rating") + 1);
    StoreField((prefix + "-text").c_str(), "Review", inner, 4000, true, true);
    StoreField((prefix + "-version").c_str(), "Version played", std::min(inner, 340.F), 80, false, true);
    if (StoreButton("Save review", 0, true)) {
        StoreAction("review.save", {{"itemId", item},
                                    {"score", std::clamp(rating[0] - '0', 1, 5)},
                                    {"text", g_mod_store.fields[prefix + "-text"]},
                                    {"version", g_mod_store.fields[prefix + "-version"]}});
        g_mod_store.review_editing = false;
    }
    ImGui::EndDisabled();
    if (StoreButton("Keep for later"))
        g_mod_store.review_editing = false;
    ImGui::PopID();
}

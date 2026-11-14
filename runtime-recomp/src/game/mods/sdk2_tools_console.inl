void DrawSdk2Console(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft,
                     const std::string& id, const std::string& label) {
    ImGui::TextWrapped("%s", label.c_str());
    ImGui::TextWrapped(
        "Choose a registered action, or enter: action-ID [up to 12 words]. Integers use decimal or 0xHEX; floats use f:1.25. This is not a system shell.");
    auto& buffer = draft.text[id];
    ImGui::SetNextItemWidth(std::max(80.F, ImGui::GetContentRegionAvail().x - 58));
    ImGui::InputText("##command", buffer.data(), buffer.size());
    ImGui::SameLine();
    if (ImGui::Button("Keys", {50, 0}))
        RequestCustomTextKeyboard(
            "SDK command", buffer.data(), buffer.size(), [digest = panel.digest, id](const std::string& text) {
                auto& target = g_sdk2_drafts[digest].text[id];
                target.fill(0);
                std::copy_n(text.begin(), std::min(text.size(), target.size() - 1), target.begin());
            });
    const auto commands = [&] {
        std::map<unsigned, dkr::mods::code::Json> values;
        for (const auto& page : panel.schema.at("pages"))
            for (const auto& field : page.at("fields"))
                if (field.at("type") == "button")
                    values.emplace(field.at("action").get<unsigned>(), field);
        return values;
    }();
    if (ImGui::BeginCombo("Registered actions", "Choose an action")) {
        for (unsigned group = 0; group < 2; ++group)
            for (const auto& [action, field] : commands) {
                const bool pinned = draft.favourites.contains(action);
                if (pinned != (group == 0))
                    continue;
                const auto caption = (pinned ? std::string("[Pinned] ") : std::string{}) + std::to_string(action) +
                                     " - " + field.at("label").get<std::string>();
                if (ImGui::Selectable(caption.c_str())) {
                    const auto value = std::to_string(action);
                    buffer.fill(0);
                    std::copy(value.begin(), value.end(), buffer.begin());
                }
            }
        ImGui::EndCombo();
    }
    unsigned chosen = 0;
    {
        std::istringstream selected(buffer.data());
        selected >> chosen;
    }
    if (commands.contains(chosen)) {
        const auto caption = commands.at(chosen).at("label").get<std::string>();
        ImGui::TextWrapped("%s", caption.c_str());
        bool pinned = draft.favourites.contains(chosen);
        if (ImGui::Checkbox("Pin command to favourites", &pinned)) {
            if (pinned && draft.favourites.size() < 128)
                draft.favourites.insert(chosen);
            else
                draft.favourites.erase(chosen);
        }
        auto values = draft.arguments;
        std::map<unsigned, std::string> hints;
        for (const auto& page : panel.schema.at("pages")) {
            bool contains = false;
            for (const auto& field : page.at("fields"))
                if (field.at("type") == "button" && field.at("action") == chosen)
                    contains = true;
            if (!contains)
                continue;
            for (const auto& field : page.at("fields")) {
                const auto type = field.at("type").get<std::string>();
                if (type == "button" && field.at("action") == chosen)
                    break;
                if (type == "inspector") {
                    hints[0] = "Selected entity slot";
                    hints[1] = "Selected lifetime generation";
                    continue;
                }
                if (!field.contains("argument") || type == "search")
                    continue;
                const unsigned argument = field.at("argument").get<unsigned>(), count = type == "vector"   ? 3
                                                                                        : type == "colour" ? 4
                                                                                                           : 1;
                const auto key = field.at("id").get<std::string>();
                const auto found = draft.numbers.find(key);
                for (unsigned i = 0; i < count && argument + i < 12; ++i) {
                    const float value =
                        found == draft.numbers.end() ? (i ? 0.F : field.value("default", 0.F)) : found->second[i];
                    values[argument + i] = type == "integer" || type == "enum" || type == "toggle"
                                               ? std::uint32_t(std::int32_t(value))
                                               : std::bit_cast<std::uint32_t>(value);
                    hints[argument + i] = field.at("label").get<std::string>() +
                                          (count > 1 ? " component " + std::to_string(i + 1) : std::string{});
                }
            }
            break;
        }
        for (const auto& [word, label] : hints)
            ImGui::TextWrapped("Word %u: %s", word, label.c_str());
        if (ImGui::Button("Complete from current tool fields",
                          {std::min(300.F, ImGui::GetContentRegionAvail().x), 38})) {
            std::string line = std::to_string(chosen);
            for (unsigned i = 0; i < 12; ++i) {
                char word[16];
                std::snprintf(word, sizeof(word), " 0x%08X", values[i]);
                line += word;
            }
            buffer.fill(0);
            std::copy_n(line.begin(), std::min(line.size(), buffer.size() - 1), buffer.begin());
        }
    }
    const bool locked = !snapshot.development || !panel.development;
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Review / Run", {180, 40})) {
        try {
            std::istringstream stream(buffer.data());
            unsigned action = 0;
            if (!(stream >> action) || !commands.contains(action))
                throw std::runtime_error("Choose a registered action ID.");
            auto arguments = draft.arguments;
            std::string token;
            unsigned word = 0;
            while (stream >> token) {
                if (word >= 12)
                    throw std::runtime_error("A command has at most 12 argument words.");
                if (token.starts_with("f:")) {
                    char* end = nullptr;
                    const float value = std::strtof(token.c_str() + 2, &end);
                    if (end == token.c_str() + 2 || *end || !std::isfinite(value))
                        throw std::runtime_error("Enter a finite float after f:.");
                    union {
                        float real;
                        std::uint32_t bits;
                    } bits{value};
                    arguments[word++] = bits.bits;
                } else {
                    std::int64_t value = 0;
                    const bool hex = token.starts_with("0x");
                    const auto begin = token.data() + (hex ? 2 : 0);
                    const auto parsed = std::from_chars(begin, token.data() + token.size(), value, hex ? 16 : 10);
                    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || value < INT32_MIN ||
                        value > UINT32_MAX)
                        throw std::runtime_error("Enter a 32-bit integer, hex word or f:float.");
                    arguments[word++] = std::uint32_t(value);
                }
            }
            draft.arguments = arguments;
            draft.numbers[id + "-pending"][0] = float(action);
            if (commands.at(action).value("confirm", false))
                ImGui::OpenPopup("Confirm console action");
            else if (Sdk2Submit(panel, snapshot, draft, action, arguments)) {
                draft.command_history.emplace_back(buffer.data());
                if (draft.command_history.size() > 64)
                    draft.command_history.pop_front();
            }
        } catch (const std::exception& e) {
            dkr::sdk2::notice(e.what());
        }
    }
    if (ImGui::BeginPopupModal("Confirm console action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const unsigned action = unsigned(draft.numbers[id + "-pending"][0]);
        ImGui::TextWrapped("Run %s?", commands.at(action).at("label").get_ref<const std::string&>().c_str());
        if (ImGui::Button("Confirm", {140, 40})) {
            Sdk2Submit(panel, snapshot, draft, action, draft.arguments);
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::GetContentRegionAvail().x >= 288)
            ImGui::SameLine();
        if (ImGui::Button("Cancel", {140, 40}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::Button(draft.recording ? "Stop recording" : "Record command macro", {230, 40})) {
        draft.recording = !draft.recording;
        if (draft.recording) {
            draft.macro.clear();
            draft.macro_scene = panel.scene;
        }
    }
    ImGui::BeginDisabled(draft.recording || draft.macro.empty() || draft.macro_scene != panel.scene);
    if (ImGui::Button("Review macro playback", {230, 40}))
        ImGui::OpenPopup("Play recorded commands");
    if (ImGui::BeginPopupModal("Play recorded commands", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Queue %u recorded commands for this scene? Every edit still uses the normal permission and lifetime checks.",
            unsigned(draft.macro.size()));
        if (ImGui::Button("Play", {140, 40})) {
            for (const auto& [action, arguments] : draft.macro)
                if (!Sdk2Submit(panel, snapshot, draft, action, arguments))
                    break;
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::GetContentRegionAvail().x >= 288)
            ImGui::SameLine();
        if (ImGui::Button("Cancel", {140, 40}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::TextDisabled("Macro: %u commands. Entity handles are valid only in their original scene.",
                        unsigned(draft.macro.size()));
    for (const auto& line : draft.command_history)
        if (ImGui::Selectable(line.c_str())) {
            buffer.fill(0);
            std::copy_n(line.begin(), std::min(line.size(), buffer.size() - 1), buffer.begin());
        }
}

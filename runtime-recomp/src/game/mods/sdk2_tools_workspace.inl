// User-controlled local workspaces. Importing one changes UI drafts, never
// submits gameplay commands. Handles/macros retain strict session lifetimes.
using Sdk2Json = dkr::mods::code::Json;
std::map<unsigned, Sdk2Json> Sdk2Registered(const dkr::sdk2::Panel& panel) {
    std::map<unsigned, Sdk2Json> result;
    for (const auto& page : panel.schema.at("pages"))
        for (const auto& field : page.at("fields"))
            if (field.at("type") == "button")
                result.emplace(field.at("action").get<unsigned>(), field);
    return result;
}
void Sdk2PinWatch(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft,
                  const dkr::sdk2::EntityView& entity, unsigned property) {
    if (draft.watches.size() >= 32) {
        dkr::sdk2::notice("Watches reached the 32-field limit.");
        return;
    }
    Sdk2Watch watch;
    watch.object_id = entity.object_id;
    watch.behaviour = entity.behaviour;
    watch.property = property;
    watch.scene = panel.scene;
    watch.slot = entity.slot;
    watch.generation = entity.generation;
    watch.session = snapshot.local_session;
    for (const auto& value : entity.properties) {
        if (value.id >= 1 && value.id <= 3)
            watch.position[value.id - 1] = std::bit_cast<float>(value.bits);
        if (entity.racer && value.id == 113)
            watch.racer = int(value.bits);
    }
    for (const auto& previous : draft.watches)
        if (previous.slot == watch.slot && previous.generation == watch.generation && previous.property == property &&
            previous.scene == panel.scene) {
            dkr::sdk2::notice("That field is already watched.");
            return;
        }
    draft.watches.push_back(watch);
    dkr::sdk2::notice("Field added to the local workspace watch list.");
}
const dkr::sdk2::EntityView* Sdk2ResolveWatch(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot,
                                              Sdk2Watch& watch) {
    if (!panel.inspector)
        return nullptr;
    const dkr::sdk2::EntityView* result = nullptr;
    if (watch.session == snapshot.local_session && watch.scene == panel.scene && watch.slot) {
        for (const auto& entity : *panel.inspector)
            if (entity.slot == watch.slot && entity.generation == watch.generation)
                return &entity;
        // A once-resolved lifetime may not bind to a replacement in this scene.
        return nullptr;
    }
    for (const auto& entity : *panel.inspector) {
        bool match = false;
        if (watch.racer >= 0 && entity.racer)
            for (const auto& property : entity.properties)
                if (property.id == 113 && int(property.bits) == watch.racer)
                    match = true;
        if (watch.racer < 0 && !entity.racer && entity.object_id == watch.object_id &&
            entity.behaviour == watch.behaviour) {
            unsigned axes = 0;
            for (const auto& property : entity.properties)
                if (property.id >= 1 && property.id <= 3 &&
                    std::abs(std::bit_cast<float>(property.bits) - watch.position[property.id - 1]) < 0.01F)
                    ++axes;
            match = axes == 3;
        }
        if (match) {
            if (result)
                return nullptr;
            result = &entity;
        }
    }
    if (result) {
        watch.slot = result->slot;
        watch.generation = result->generation;
        watch.scene = panel.scene;
        watch.session = snapshot.local_session;
    }
    return result;
}
Sdk2Json Sdk2WorkspaceDocument(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot,
                               const Sdk2PanelDraft& draft) {
    Sdk2Json value = {{"schema", 2},
                      {"kind", "tools-workspace"},
                      {"package", panel.id},
                      {"digest", panel.digest},
                      {"scene", panel.scene},
                      {"local_session", snapshot.local_session},
                      {"page", draft.page},
                      {"favourites", draft.favourites},
                      {"numbers", draft.numbers},
                      {"text", Sdk2Json::object()},
                      {"watches", Sdk2Json::array()},
                      {"macro", Sdk2Json::array()}};
    // Only declared search fields may be exported. Exclude file locations,
    // document previews, reload paths, lobby metadata and native addresses.
    for (const auto& page : panel.schema.at("pages"))
        for (const auto& field : page.at("fields"))
            if (field.at("type") == "search") {
                const auto id = field.at("id").get<std::string>();
                if (const auto at = draft.text.find(id); at != draft.text.end())
                    value["text"][id] = std::string(at->second.data());
            }
    for (const auto& watch : draft.watches)
        value["watches"].push_back({{"racer", watch.racer},
                                    {"object_id", watch.object_id},
                                    {"behaviour", watch.behaviour},
                                    {"property", watch.property},
                                    {"position", watch.position}});
    for (const auto& [action, args] : draft.macro)
        value["macro"].push_back({{"action", action}, {"arguments", args}});
    value["macro_scene"] = draft.macro_scene;
    return value;
}
void Sdk2ApplyWorkspace(const Sdk2Json& value, const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot,
                        Sdk2PanelDraft& draft) {
    const auto reject = []() {
        throw std::runtime_error("That workspace has an invalid field, edition or portable limit.");
    };
    if (value.at("schema") != 2 || value.at("kind") != "tools-workspace" || value.at("package") != panel.id ||
        value.at("digest") != panel.digest)
        reject();
    auto favourites = value.at("favourites").get<std::set<unsigned>>();
    if (favourites.size() > 128)
        reject();
    const auto actions = Sdk2Registered(panel);
    for (auto action : favourites)
        if (!actions.contains(action))
            reject();
    const auto& numbers = value.at("numbers");
    if (!numbers.is_object() || numbers.size() > 256)
        reject();
    std::map<std::string, std::array<float, 4>> parsed_numbers;
    for (const auto& [key, array] : numbers.items()) {
        if (key.size() > 160 || !array.is_array() || array.size() != 4)
            reject();
        auto components = array.get<std::array<float, 4>>();
        for (auto component : components)
            if (!std::isfinite(component) || std::abs(component) > 1e7F)
                reject();
        parsed_numbers.emplace(key, components);
    }
    const auto& text = value.at("text");
    if (!text.is_object() || text.size() > 256)
        reject();
    std::map<std::string, std::array<char, 257>> parsed_text;
    for (const auto& [key, item] : text.items()) {
        bool search = false;
        for (const auto& page : panel.schema.at("pages"))
            for (const auto& field : page.at("fields"))
                if (field.at("type") == "search" && field.at("id") == key)
                    search = true;
        if (!search || !item.is_string() || item.get_ref<const std::string&>().size() > 256)
            reject();
        std::array<char, 257> buffer{};
        const auto source = item.get<std::string>();
        std::copy(source.begin(), source.end(), buffer.begin());
        parsed_text.emplace(key, buffer);
    }
    const auto& watches = value.at("watches");
    if (!watches.is_array() || watches.size() > 32)
        reject();
    std::vector<Sdk2Watch> parsed_watches;
    for (const auto& row : watches) {
        Sdk2Watch watch;
        watch.racer = row.at("racer").get<int>();
        watch.object_id = row.at("object_id").get<unsigned>();
        watch.behaviour = row.at("behaviour").get<unsigned>();
        watch.property = row.at("property").get<unsigned>();
        if (watch.racer < -1 || watch.racer > 7 || watch.object_id > 65535 || watch.behaviour > 65535 ||
            watch.property < 1 || watch.property > 65535)
            reject();
        const auto position = row.at("position").get<std::array<float, 3>>();
        for (unsigned i = 0; i < 3; ++i) {
            if (!std::isfinite(position[i]) || std::abs(position[i]) > 1e6F)
                reject();
            watch.position[i] = position[i];
        }
        parsed_watches.push_back(watch);
    }
    const auto& macro = value.at("macro");
    if (!macro.is_array() || macro.size() > 32)
        reject();
    std::vector<std::pair<unsigned, std::array<std::uint32_t, 13>>> parsed_macro;
    const bool same_scene = value.at("scene") == panel.scene && value.at("local_session") == snapshot.local_session &&
                            value.at("macro_scene") == panel.scene;
    for (const auto& row : macro) {
        const auto action = row.at("action").get<unsigned>();
        if (!actions.contains(action))
            reject();
        const auto args = row.at("arguments").get<std::array<std::uint32_t, 13>>();
        if (same_scene)
            parsed_macro.emplace_back(action, args);
    }
    const unsigned page = value.at("page").get<unsigned>();
    if (page >= panel.schema.at("pages").size())
        reject();
    // Every field is checked before committing the local draft. No service,
    // authoritative command, game edit or RNG call occurs here.
    draft.favourites = std::move(favourites);
    draft.numbers = std::move(parsed_numbers);
    for (const auto& [key, buffer] : parsed_text)
        draft.text[key] = buffer;
    draft.watches = std::move(parsed_watches);
    draft.macro = std::move(parsed_macro);
    draft.macro_scene = same_scene ? panel.scene : 0;
    draft.page = page;
    draft.recording = false;
    dkr::sdk2::notice(
        same_scene ? "Workspace loaded. No gameplay command was executed."
                   : "Workspace loaded. Old-session macros were dropped; watches must resolve uniquely in this scene.");
}
void DrawSdk2Workspace(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft) {
    ImGui::SetNextWindowSize(
        {std::max(300.F, ImGui::GetIO().DisplaySize.x - 80), std::max(240.F, ImGui::GetIO().DisplaySize.y - 80)},
        ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Tools workspace", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar))
        return;
    ImGui::TextWrapped(
        "Local favourites, watch definitions and tool presets. Imports never run commands automatically.");
    ImGui::BeginChild("workspace-page", {0, std::max(40.F, ImGui::GetContentRegionAvail().y - 58)}, false);
    ImGui::TextUnformatted("WATCHES");
    for (unsigned i = 0; i < draft.watches.size();) {
        ImGui::PushID(int(i));
        auto& watch = draft.watches[i];
        const auto* entity = Sdk2ResolveWatch(panel, snapshot, watch);
        const dkr::sdk2::PropertyView* property = nullptr;
        if (entity)
            for (const auto& item : entity->properties)
                if (item.id == watch.property)
                    property = &item;
        if (property) {
            if (property->type == 1)
                ImGui::Text("%s: %.3f %s", property->name.c_str(), std::bit_cast<float>(property->bits),
                            property->unit.c_str());
            else
                ImGui::Text("%s: %d %s", property->name.c_str(), int(property->bits), property->unit.c_str());
        } else
            ImGui::TextDisabled("Field %u: stale, absent or ambiguous target", watch.property);
        if (ImGui::Button("Rebind to selected", {180, 34}) && panel.inspector)
            for (const auto& selected : *panel.inspector)
                if (selected.slot == draft.arguments[0] && selected.generation == draft.arguments[1]) {
                    const auto field = watch.property;
                    draft.watches.erase(draft.watches.begin() + i);
                    Sdk2PinWatch(panel, snapshot, draft, selected, field);
                    break;
                }
        if (ImGui::GetContentRegionAvail().x >= 298)
            ImGui::SameLine();
        const bool remove = ImGui::Button("Remove", {110, 34});
        ImGui::PopID();
        if (remove)
            draft.watches.erase(draft.watches.begin() + i);
        else
            ++i;
    }
    if (draft.watches.empty())
        ImGui::TextDisabled("Select a property in an inspector and choose Pin this field to Watches.");
    ImGui::Separator();
    ImGui::TextUnformatted("FAVOURITES");
    const auto actions = Sdk2Registered(panel);
    for (const auto& [action, field] : actions) {
        ImGui::PushID(int(action));
        bool pinned = draft.favourites.contains(action);
        const auto caption = field.at("label").get<std::string>();
        if (ImGui::Checkbox(caption.c_str(), &pinned)) {
            if (pinned && draft.favourites.size() < 128)
                draft.favourites.insert(action);
            else
                draft.favourites.erase(action);
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextUnformatted("EXPORT / REVIEW WORKSPACE");
    auto [entry, inserted] = draft.text.try_emplace("workspace-file", std::array<char, 257>{});
    auto& path = entry->second;
    if (inserted) {
        const auto native = (g_config_directory / "development" / (panel.id + "-tools.dkrtools")).u8string();
        const std::string text(reinterpret_cast<const char*>(native.data()), native.size());
        std::copy_n(text.begin(), std::min(text.size(), path.size() - 1), path.begin());
    }
    ImGui::SetNextItemWidth(std::max(80.F, ImGui::GetContentRegionAvail().x - 58));
    ImGui::InputText("##workspace-file", path.data(), path.size());
    ImGui::SameLine();
    if (ImGui::Button("Keys", {50, 0}))
        RequestCustomTextKeyboard("Workspace file (.dkrtools)", path.data(), path.size(),
                                  [digest = panel.digest](const std::string& text) {
                                      auto& to = g_sdk2_drafts[digest].text["workspace-file"];
                                      to.fill(0);
                                      std::copy_n(text.begin(), std::min(text.size(), to.size() - 1), to.begin());
                                  });
    bool busy = draft.preferences_job.valid();
    if (busy && draft.preferences_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const auto value = draft.preferences_job.get();
            if (value.starts_with("{"))
                draft.preferences_preview = Sdk2Json::parse(value);
            else
                dkr::sdk2::notice(value);
        } catch (const std::exception& e) {
            dkr::sdk2::notice(e.what());
        }
        busy = false;
    }
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Export new workspace", {240, 38})) {
        const auto selected = std::filesystem::u8path(path.data());
        const auto data = Sdk2WorkspaceDocument(panel, snapshot, draft).dump(2);
        draft.preferences_job = std::async(std::launch::async, [selected, data] {
            if (selected.extension() != ".dkrtools" || data.size() > 32768)
                throw std::runtime_error("Choose a .dkrtools name; keep the workspace within 32 KiB.");
            std::filesystem::create_directories(selected.parent_path());
            dkr::mods::write_new_file(selected,
                                      dkr::mods::View(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
            return std::string("Workspace exported. Existing files were not overwritten.");
        });
    }
    if (ImGui::Button("Read / Review workspace", {240, 38})) {
        const auto selected = std::filesystem::u8path(path.data());
        draft.preferences_job = std::async(std::launch::async, [selected] {
            if (selected.extension() != ".dkrtools")
                throw std::runtime_error("Choose a .dkrtools workspace.");
            const auto bytes = dkr::mods::read_file(selected, 32768);
            return dkr::mods::code::parse_document(bytes, 32768).dump();
        });
    }
    ImGui::EndDisabled();
    if (draft.preferences_preview) {
        ImGui::TextWrapped("Preview: %s", draft.preferences_preview->dump(2).c_str());
        if (ImGui::Button("Apply reviewed local preset", {260, 38})) {
            try {
                Sdk2ApplyWorkspace(*draft.preferences_preview, panel, snapshot, draft);
                draft.preferences_preview.reset();
            } catch (const std::exception& e) {
                dkr::sdk2::notice(e.what());
            }
        }
        if (ImGui::Button("Discard preview", {180, 38}))
            draft.preferences_preview.reset();
    }
    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("Back", {160, 40}) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

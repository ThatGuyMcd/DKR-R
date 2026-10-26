// Generic mod-defined tools, rendered with the same existing paddock theme.
// No ImGui/process pointers enter the VM. Nothing here writes guest RAM.
struct Sdk2Watch {
    int racer = -1;
    unsigned object_id = 0, behaviour = 0, property = 0, scene = 0, slot = 0, generation = 0;
    float position[3]{};
    std::uint64_t session = 0;
};
struct Sdk2PanelDraft {
    std::map<std::string, std::array<char, 257>> text;
    std::map<std::string, std::array<float, 4>> numbers;
    std::array<std::uint32_t, 13> arguments{};
    unsigned page = 0;
    bool initialized = false;
    bool recording = false;
    unsigned macro_scene = 0;
    std::vector<std::pair<unsigned, std::array<std::uint32_t, 13>>> macro;
    std::deque<std::string> command_history;
    std::optional<dkr::mods::code::Json> imported;
    std::future<std::string> file_job;
    std::future<std::shared_ptr<const dkr::mods::code::Package>> reload_job;
    std::shared_ptr<const dkr::mods::code::Package> reload_package;
    std::set<unsigned> favourites;
    std::vector<Sdk2Watch> watches;
    std::future<std::string> preferences_job;
    std::optional<dkr::mods::code::Json> preferences_preview;
};
std::map<std::string, Sdk2PanelDraft> g_sdk2_drafts;
bool Sdk2ActionButton(const std::string& caption) {
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = std::min(available, std::max(170.F, ImGui::CalcTextSize(caption.c_str()).x + 40.F));
    std::istringstream words(caption);
    std::string word, line, wrapped;
    unsigned lines = 1;
    while (words >> word) {
        const auto next = line.empty() ? word : line + " " + word;
        if (!line.empty() && ImGui::CalcTextSize(next.c_str()).x > width - 24.F) {
            wrapped += line + "\n";
            line = word;
            ++lines;
        } else
            line = next;
    }
    wrapped += line + "###action";
    return ImGui::Button(wrapped.c_str(), {width, std::max(42.F, ImGui::GetTextLineHeight() * lines + 16.F)});
}
bool Sdk2Submit(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft,
                unsigned action, const std::array<std::uint32_t, 13>& arguments) {
    const bool accepted = dkr::sdk2::submit(panel.owner, action, arguments, panel.digest, snapshot.generation);
    if (accepted && draft.recording) {
        if (draft.macro.size() < 32) {
            draft.macro.emplace_back(action, arguments);
            draft.macro_scene = panel.scene;
        } else {
            draft.recording = false;
            dkr::sdk2::notice("Macro recording reached its 32-command limit.");
        }
    }
    return accepted;
}
#include "mods/sdk2_tools_documents.inl"
#include "mods/sdk2_tools_console.inl"
#include "mods/sdk2_tools_camera.inl"
#include "mods/sdk2_tools_reload.inl"
#include "mods/sdk2_tools_workspace.inl"
void DrawSdk2Debug() {
    if (!dkr::sdk2::debug_available())
        return;
    const auto lines = dkr::sdk2::debug_lines();
    const auto display = ImGui::GetIO().DisplaySize;
    auto* draw = ImGui::GetForegroundDrawList();
    for (const auto& line : *lines) {
        const auto rgba = line.rgba;
        const auto colour = IM_COL32((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255);
        draw->AddLine({line.x1 * display.x, line.y1 * display.y}, {line.x2 * display.x, line.y2 * display.y}, colour,
                      1.5F);
        if (!line.label.empty())
            draw->AddText({line.x1 * display.x, line.y1 * display.y}, colour, line.label.c_str());
    }
}
void DrawSdk2Tools() {
    auto snapshot = dkr::sdk2::snapshot();
    bool open = true;
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::max(320.F, display.x - 48.F), std::max(240.F, display.y - 48.F)},
                             ImGuiCond_Appearing);
    if (!ImGui::Begin("CUSTOM MAGIC CODE TOOLS", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        ImGui::PopStyleVar();
        if (!open) {
            dkr::sdk2::tools_open = false;
            g_sdk2_key_capture = false;
        }
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        open = false;
    if (!snapshot->active || snapshot->panels.empty())
        ImGui::TextWrapped("Enable a Custom Magic Code with tools, then start a game.");
    else {
        static unsigned selected = 0;
        if (selected >= snapshot->panels.size())
            selected = 0;
        if (snapshot->panels.size() > 1) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##tools-package", snapshot->panels[selected].name.c_str())) {
                for (unsigned i = 0; i < snapshot->panels.size(); ++i)
                    if (ImGui::Selectable(snapshot->panels[i].name.c_str(), selected == i))
                        selected = i;
                ImGui::EndCombo();
            }
        }
        const auto& panel = snapshot->panels[selected];
        auto& draft = g_sdk2_drafts[panel.digest];
        const auto& pages = panel.schema.at("pages");
        draft.page = std::min<unsigned>(draft.page, unsigned(pages.size() - 1));
        ImGui::TextUnformatted(panel.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s | %s | scene %u / tick %u", snapshot->online ? "Online" : "Offline",
                            snapshot->development ? (snapshot->host ? "Host tools" : "Client requests") : "Read-only",
                            panel.scene, panel.tick);
        float line_width = 0;
        const float available = ImGui::GetContentRegionAvail().x;
        if (available < 760.F) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##tools-screen",
                                  pages[draft.page].at("label").get_ref<const std::string&>().c_str())) {
                for (unsigned i = 0; i < pages.size(); ++i)
                    if (ImGui::Selectable(pages[i].at("label").get_ref<const std::string&>().c_str(), draft.page == i))
                        draft.page = i;
                ImGui::EndCombo();
            }
        } else
            for (unsigned i = 0; i < pages.size(); ++i) {
                const auto label = pages[i].at("label").get<std::string>();
                const float width = ImGui::CalcTextSize(label.c_str()).x + 32;
                if (line_width && line_width + width + 8 < available)
                    ImGui::SameLine();
                else
                    line_width = 0;
                ImGui::PushID(int(i));
                if (ImGui::Button(label.c_str(), {width, 40}))
                    draft.page = i;
                ImGui::PopID();
                line_width += width + 8;
            }
        ImGui::Separator();
        ImGui::BeginChild("sdk2-page", {0, std::max(48.F, ImGui::GetContentRegionAvail().y - 136.F)}, false);
        const auto& page = pages[draft.page];
        for (const auto& field : page.at("fields")) {
            const auto id = field.at("id").get<std::string>(), label = field.at("label").get<std::string>(),
                       type = field.at("type").get<std::string>();
            if (field.contains("visible") && !panel.values.value(field.at("visible").get<std::string>(), false))
                continue;
            ImGui::PushID(id.c_str());
            const auto value = panel.values.find(id);
            const auto number = [&](float fallback) {
                return value != panel.values.end() && value->is_number() ? value->get<float>() : fallback;
            };
            const bool read_only = field.value("readonly", false);
            const bool locked = read_only || !snapshot->development || !panel.development;
            if (type == "text") {
                ImGui::TextWrapped("%s", label.c_str());
                if (value != panel.values.end()) {
                    const auto text = value->is_string() ? value->get<std::string>() : value->dump();
                    ImGui::TextWrapped("%s", text.c_str());
                }
            } else if (type == "button") {
                ImGui::BeginDisabled(locked);
                if (Sdk2ActionButton(label)) {
                    if (field.value("confirm", false))
                        ImGui::OpenPopup("Confirm action");
                    else
                        Sdk2Submit(panel, *snapshot, draft, field.at("action").get<unsigned>(), draft.arguments);
                }
                if (ImGui::BeginPopupModal("Confirm action", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextWrapped("%s?", label.c_str());
                    if (ImGui::Button("Confirm", {160, 40})) {
                        Sdk2Submit(panel, *snapshot, draft, field.at("action").get<unsigned>(), draft.arguments);
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::GetContentRegionAvail().x >= 328)
                        ImGui::SameLine();
                    if (ImGui::Button("Cancel", {160, 40}))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
                ImGui::EndDisabled();
            } else if (type == "reload") {
                DrawSdk2Reload(panel, *snapshot, draft, id);
            } else if (type == "inspector") {
                ImGui::TextUnformatted(label.c_str());
                const auto data = panel.inspector;
                if (!data || data->empty())
                    ImGui::TextDisabled("Waiting for an entity snapshot from the current scene.");
                else {
                    auto& filter = draft.text[id];
                    ImGui::SetNextItemWidth(std::max(100.F, ImGui::GetContentRegionAvail().x - 58.F));
                    ImGui::InputText("##filter", filter.data(), filter.size());
                    ImGui::SameLine();
                    if (ImGui::Button("Keys", {50, 0}))
                        RequestCustomTextKeyboard(
                            "Find entity", filter.data(), filter.size(),
                            [digest = panel.digest, id](const std::string& text) {
                                auto& target = g_sdk2_drafts[digest].text[id];
                                target.fill(0);
                                std::copy_n(text.begin(), std::min(text.size(), target.size() - 1), target.begin());
                            });
                    std::string preview = "Choose an entity";
                    const dkr::sdk2::EntityView* selected_entity = nullptr;
                    for (const auto& entity : *data)
                        if (entity.slot == draft.arguments[0] && entity.generation == draft.arguments[1])
                            selected_entity = &entity;
                    if (selected_entity)
                        preview = std::string(selected_entity->racer ? "Racer " : "Object ") +
                                  std::to_string(selected_entity->slot) + " / behaviour " +
                                  std::to_string(selected_entity->behaviour);
                    if (ImGui::BeginCombo("##entity", preview.c_str())) {
                        for (const auto& entity : *data) {
                            if (field.value("racer_only", false) && !entity.racer)
                                continue;
                            const auto text = std::string(entity.racer ? "Racer " : "Object ") +
                                              std::to_string(entity.slot) + " | behaviour " +
                                              std::to_string(entity.behaviour) + " | ID " +
                                              std::to_string(entity.object_id);
                            auto haystack = text, needle = std::string(filter.data());
                            for (auto& ch : haystack)
                                ch = char(std::tolower(static_cast<unsigned char>(ch)));
                            for (auto& ch : needle)
                                ch = char(std::tolower(static_cast<unsigned char>(ch)));
                            if (!needle.empty() && haystack.find(needle) == std::string::npos)
                                continue;
                            if (ImGui::Selectable(text.c_str(), selected_entity == &entity)) {
                                draft.arguments[0] = entity.slot;
                                draft.arguments[1] = entity.generation;
                                selected_entity = &entity;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (selected_entity) {
                        auto& selection = draft.numbers[id + "-property"];
                        if (ImGui::BeginTable("properties", 3,
                                              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                            for (const auto& property : selected_entity->properties) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::PushID(int(property.id));
                                if (ImGui::Selectable(property.name.c_str(), unsigned(selection[0]) == property.id,
                                                      ImGuiSelectableFlags_SpanAllColumns)) {
                                    selection[0] = float(property.id);
                                    union {
                                        std::uint32_t bits;
                                        float real;
                                    } value{property.bits};
                                    selection[1] = property.type == 1   ? value.real
                                                   : property.type == 2 ? float(std::int32_t(property.bits))
                                                                        : float(property.bits);
                                }
                                ImGui::TableNextColumn();
                                union {
                                    std::uint32_t bits;
                                    float real;
                                } value{property.bits};
                                if (property.type == 1)
                                    ImGui::Text("%.3f", value.real);
                                else if (property.type == 2)
                                    ImGui::Text("%d", std::int32_t(property.bits));
                                else
                                    ImGui::Text("%u", property.bits);
                                ImGui::TableNextColumn();
                                ImGui::TextDisabled("%s", property.unit.c_str());
                                ImGui::PopID();
                            }
                            ImGui::EndTable();
                        }
                        const auto property =
                            std::find_if(selected_entity->properties.begin(), selected_entity->properties.end(),
                                         [&](const auto& p) { return p.id == unsigned(selection[0]); });
                        if (property != selected_entity->properties.end() &&
                            ImGui::Button("Pin this field to Watches", {230, 36}))
                            Sdk2PinWatch(panel, *snapshot, draft, *selected_entity, property->id);
                        if (property != selected_entity->properties.end() && field.contains("edit_action") &&
                            (property->flags & 1)) {
                            ImGui::TextWrapped("Edit %s", property->name.c_str());
                            ImGui::BeginDisabled(locked);
                            const bool compact = ImGui::GetContentRegionAvail().x < 300.F;
                            ImGui::SetNextItemWidth(compact ? ImGui::GetContentRegionAvail().x
                                                            : ImGui::GetContentRegionAvail().x - 218.F);
                            if (property->type == 1)
                                ImGui::InputFloat("##edit", &selection[1], 0, 0, "%.3f");
                            else {
                                int number = int(selection[1]);
                                if (ImGui::InputInt("##edit", &number))
                                    selection[1] = float(number);
                            }
                            if (!compact)
                                ImGui::SameLine();
                            if (ImGui::Button("Keys", {50, 0})) {
                                char initial[64];
                                std::snprintf(initial, sizeof(initial), "%.3f", selection[1]);
                                RequestCustomTextKeyboard(
                                    property->name.c_str(), initial, 64,
                                    [digest = panel.digest, key = id + "-property"](const std::string& text) {
                                        std::istringstream stream(text);
                                        float number = 0;
                                        if (stream >> number && number >= -1e7F && number <= 1e7F)
                                            g_sdk2_drafts[digest].numbers[key][1] = number;
                                    });
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Apply property", {150, 0})) {
                                auto arguments = draft.arguments;
                                arguments[2] = property->id;
                                union {
                                    float real;
                                    std::uint32_t bits;
                                } value{selection[1]};
                                arguments[3] =
                                    property->type == 1 ? value.bits : std::uint32_t(std::int32_t(selection[1]));
                                Sdk2Submit(panel, *snapshot, draft, field.at("edit_action").get<unsigned>(), arguments);
                            }
                            ImGui::EndDisabled();
                        }
                    }
                }
            } else if (type == "debug") {
                bool enabled = dkr::sdk2::debug_enabled.load();
                if (ImGui::Checkbox(label.c_str(), &enabled))
                    dkr::sdk2::debug_enabled = enabled;
                ImGui::TextWrapped(
                    "Local overlay only. Does not change another player's view or the shared simulation.");
            } else if (type == "camera") {
                DrawSdk2Camera(panel, *snapshot, draft);
            } else if (type == "console") {
                DrawSdk2Console(panel, *snapshot, draft, id, label);
            } else if (type == "document") {
                DrawSdk2Document(panel, *snapshot, draft, field);
            } else if (type == "authority") {
                ImGui::TextWrapped("%s", label.c_str());
                if (!snapshot->online)
                    ImGui::TextWrapped("Offline tools use this development session. Ordinary saves remain separate.");
                else {
                    ImGui::TextWrapped(
                        "The host orders every shared change. Clients can inspect and request actions; approval never changes a client locally.");
                    if (snapshot->host)
                        for (unsigned peer = 1; peer < snapshot->players; ++peer) {
                            ImGui::PushID(int(peer));
                            bool enabled = (snapshot->grants & (1U << peer)) != 0;
                            const auto caption =
                                "Allow P" + std::to_string(peer + 1) + " tool requests without asking each time";
                            if (ImGui::Checkbox(caption.c_str(), &enabled))
                                dkr::sdk2::grant(peer, enabled);
                            ImGui::PopID();
                        }
                    for (const auto& request : snapshot->requests) {
                        ImGui::PushID(int(request.peer * 65536 + request.id));
                        ImGui::TextWrapped("P%u: %s action %u (#%u)", request.peer + 1, request.package.c_str(),
                                           request.command.words[2], request.id);
                        if (ImGui::Button("Approve", {140, 38}))
                            dkr::sdk2::decide(request.peer, request.id, true);
                        if (ImGui::GetContentRegionAvail().x >= 288)
                            ImGui::SameLine();
                        if (ImGui::Button("Decline", {140, 38}))
                            dkr::sdk2::decide(request.peer, request.id, false);
                        ImGui::PopID();
                    }
                    if (snapshot->requests.empty())
                        ImGui::TextDisabled("No pending requests.");
                }
                for (const auto& line : snapshot->history)
                    ImGui::TextWrapped("%s", line.c_str());
            } else if (type == "diagnostics") {
                ImGui::TextWrapped("%s", label.c_str());
                ImGui::TextWrapped(
                    "Local physical callback costs, including replay. These measurements never affect gameplay or shared RNG.");
                if (panel.diagnostics.contains("packages"))
                    for (const auto& cost : panel.diagnostics.at("packages")) {
                        ImGui::TextWrapped("%s | %s | %u KiB resident | %u KiB heap",
                                           cost.at("id").get_ref<const std::string&>().c_str(),
                                           cost.at("profile").get_ref<const std::string&>().c_str(),
                                           cost.at("resident").get<unsigned>() / 1024,
                                           cost.at("heap").get<unsigned>() / 1024);
                        ImGui::TextWrapped("%llu callbacks | %llu instructions | %llu native calls | %.2f ms CPU",
                                           cost.at("callbacks").get<unsigned long long>(),
                                           cost.at("instructions").get<unsigned long long>(),
                                           cost.at("native_calls").get<unsigned long long>(),
                                           cost.at("cpu_ms").get<double>());
                    }
            } else if (type == "table") {
                ImGui::TextUnformatted(label.c_str());
                if (value != panel.values.end() && value->is_array() && !value->empty()) {
                    if (ImGui::BeginTable("values", 2,
                                          ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
                        unsigned row = 0;
                        for (const auto& item : *value) {
                            if (row++ >= 128)
                                break;
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            const auto title = item.is_object()
                                                   ? item.value("name", std::string("Entry ") + std::to_string(row))
                                                   : std::to_string(row);
                            ImGui::PushID(int(row));
                            if (field.value("select", false) && item.is_object() && item.contains("slot") &&
                                item.contains("generation")) {
                                const bool selected_entity =
                                    draft.arguments[0] == item.at("slot").get<unsigned>() &&
                                    draft.arguments[1] == item.at("generation").get<unsigned>();
                                if (ImGui::Selectable(title.c_str(), selected_entity,
                                                      ImGuiSelectableFlags_SpanAllColumns)) {
                                    draft.arguments[0] = item.at("slot").get<unsigned>();
                                    draft.arguments[1] = item.at("generation").get<unsigned>();
                                }
                            } else
                                ImGui::TextWrapped("%s", title.c_str());
                            ImGui::TableNextColumn();
                            const auto details = item.is_object() ? item.value("detail", std::string{}) : item.dump();
                            ImGui::TextWrapped("%s", details.c_str());
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                } else
                    ImGui::TextDisabled("No entries in this scene.");
            } else if (type == "search") {
                auto& buffer = draft.text[id];
                ImGui::TextUnformatted(label.c_str());
                ImGui::SetNextItemWidth(-58);
                ImGui::InputText("##text", buffer.data(), buffer.size());
                ImGui::SameLine();
                if (ImGui::Button("Keys", {50, 0}))
                    RequestCustomTextKeyboard(label.c_str(), buffer.data(), buffer.size(),
                                              [digest = panel.digest, id](const std::string& text) {
                                                  auto& target = g_sdk2_drafts[digest].text[id];
                                                  target.fill(0);
                                                  std::copy_n(text.begin(), std::min(text.size(), target.size() - 1),
                                                              target.begin());
                                              });
                const auto argument = field.value("argument", 13U);
                if (argument < 12) {
                    const unsigned capacity = (12 - argument) * 4;
                    for (unsigned i = 0; i < capacity; ++i) {
                        const unsigned word = argument + i / 4, shift = 24 - (i % 4) * 8;
                        draft.arguments[word] =
                            (draft.arguments[word] & ~(255U << shift)) |
                            (std::uint32_t(i < buffer.size() ? static_cast<unsigned char>(buffer[i]) : 0) << shift);
                    }
                }
            } else {
                auto [it, inserted] = draft.numbers.try_emplace(id, std::array<float, 4>{});
                auto& values = it->second;
                if (inserted)
                    values[0] = number(field.value("default", 0.F));
                const auto argument = field.value("argument", 13U);
                ImGui::BeginDisabled(read_only);
                ImGui::TextUnformatted(label.c_str());
                const float keys = (type == "integer" || type == "vector" || type == "number") ? 58.F : 0.F;
                ImGui::SetNextItemWidth(std::max(40.F, std::min(560.F, ImGui::GetContentRegionAvail().x - keys)));
                if (type == "toggle") {
                    bool enabled = values[0] != 0;
                    if (ImGui::Checkbox("##value", &enabled))
                        values[0] = enabled ? 1.F : 0.F;
                } else if (type == "enum") {
                    const auto& choices = field.at("choices");
                    int choice = std::clamp(int(values[0]), 0, int(choices.size() - 1));
                    const auto preview = choices[choice].get<std::string>();
                    if (ImGui::BeginCombo("##value", preview.c_str())) {
                        for (unsigned i = 0; i < choices.size(); ++i)
                            if (ImGui::Selectable(choices[i].get_ref<const std::string&>().c_str(), int(i) == choice))
                                values[0] = float(i);
                        ImGui::EndCombo();
                    }
                } else if (type == "integer") {
                    int integer = int(values[0]);
                    if (ImGui::InputInt("##value", &integer))
                        values[0] = float(std::clamp(integer, field.value("min", 0), field.value("max", 100)));
                } else if (type == "vector")
                    ImGui::InputFloat3("##value", values.data(), "%.2f");
                else if (type == "colour")
                    ImGui::ColorEdit4("##value", values.data(), ImGuiColorEditFlags_NoInputs);
                else
                    ImGui::SliderFloat("##value", values.data(), field.value("min", 0.F), field.value("max", 100.F),
                                       "%.3f");
                if (type == "integer" || type == "vector" || type == "number") {
                    ImGui::SameLine();
                    if (ImGui::Button("Keys", {50, 0})) {
                        char initial[128];
                        const unsigned components = type == "vector" ? 3U : 1U;
                        if (components == 3)
                            std::snprintf(initial, sizeof(initial), "%.4f %.4f %.4f", values[0], values[1], values[2]);
                        else
                            std::snprintf(initial, sizeof(initial), "%.0f", values[0]);
                        RequestCustomTextKeyboard(
                            label.c_str(), initial, 128,
                            [digest = panel.digest, id, components](const std::string& text) {
                                std::array<float, 4> numbers{};
                                std::istringstream stream(text);
                                for (unsigned i = 0; i < components; ++i)
                                    if (!(stream >> numbers[i]) || !(numbers[i] >= -1e7F && numbers[i] <= 1e7F)) {
                                        dkr::sdk2::notice("Enter finite numbers, separated by spaces.");
                                        return;
                                    }
                                stream >> std::ws;
                                if (!stream.eof()) {
                                    dkr::sdk2::notice("Enter only the requested numbers.");
                                    return;
                                }
                                g_sdk2_drafts[digest].numbers[id] = numbers;
                            });
                    }
                }
                ImGui::EndDisabled();
                if (argument < 12)
                    for (unsigned n = 0; n < (type == "vector"   ? 3U
                                              : type == "colour" ? 4U
                                                                 : 1U) &&
                                         argument + n < 12;
                         ++n) {
                        union {
                            float real;
                            std::uint32_t bits;
                        } raw{values[n]};
                        draft.arguments[argument + n] = type == "integer" || type == "enum" || type == "toggle"
                                                            ? std::uint32_t(std::int32_t(values[n]))
                                                            : raw.bits;
                    }
            }
            if (field.contains("help"))
                ImGui::TextWrapped("%s", field.at("help").get_ref<const std::string&>().c_str());
            ImGui::Dummy({0, 8});
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (!snapshot->notice.empty())
            ImGui::TextWrapped("%s", snapshot->notice.c_str());
        ImGui::TextDisabled("Selected entity: %u / lifetime %u", draft.arguments[0], draft.arguments[1]);
        if (ImGui::Button("Back / Close", {160, 38}))
            open = false;
        if (ImGui::GetContentRegionAvail().x > 330)
            ImGui::SameLine();
        if (ImGui::Button("Workspace", {150, 38}))
            ImGui::OpenPopup("Tools workspace");
        if (ImGui::GetContentRegionAvail().x > 200)
            ImGui::SameLine();
        if (ImGui::Button("Shortcut settings", {200, 38}))
            ImGui::OpenPopup("Tools shortcut settings");
        DrawSdk2Workspace(panel, *snapshot, draft);
        if (ImGui::BeginPopupModal("Tools shortcut settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const std::string shortcut =
                g_sdk2_key_capture ? "Press a key (Esc cancels)"
                                   : std::string("Keyboard: ") + SDL_GetScancodeName(SDL_Scancode(g_sdk2_tools_key));
            if (ImGui::Button(shortcut.c_str(), {280, 40}))
                g_sdk2_key_capture = true;
            if (ImGui::Checkbox("Back/Select + Y controller shortcut", &g_sdk2_tools_chord))
                SaveSettings();
            if (ImGui::Button("Close", {160, 38})) {
                g_sdk2_key_capture = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    if (!open) {
        dkr::sdk2::tools_open = false;
        g_sdk2_key_capture = false;
    }
}

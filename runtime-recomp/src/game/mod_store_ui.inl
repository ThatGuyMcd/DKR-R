// Native Mod Store: included only in the staged launcher translation unit.
// No browser engine, remote script, store texture or network worker enters RT64.
namespace {
using StoreJson = dkr::store::Json;
struct ModStoreUi {
    int page = 0, category = 0, sort = 0, detail_tab = 0, staff_tab = 0, rating = 5, request_kind = 0;
    int visible = 12, request_category = 1, request_relation = 0, request_page = 0, account_page = 0,
        assets_visible = 24;
    int installed_section = 0, installed_category = 0, installed_state = 0, installed_sort = 0, download_section = 0,
        download_filter = 0, editor_step = 0, staff_filter = 0, gallery_index = 0;
    std::string installed_selected, update_selected, update_detail, my_request, staff_report, staff_listing,
        upload_selection, message_page, last_error, last_status, nav_screen;
    std::map<std::string, int> limits;
    struct Position {
        float scroll = 0;
        ImGuiID focus = 0;
    };
    std::map<std::string, Position> positions;
    ImGuiID restore_focus = 0, page_focus = 0;
    ImGuiID last_row_action = 0;
    bool focus_first = false, restoring = false, picker_upload = false, review_editing = false, request_picker = false,
         body_navigation = false;
    std::function<void()> pending_navigation;
    StoreJson gallery = StoreJson::array(), release_details = StoreJson::object();
    std::string gallery_title, release_title, popup_request, confirm_title, confirm_message, confirm_label,
        confirm_identity;
    std::function<void()> confirmation;
    std::map<std::string, std::uint64_t> texture_used;
    std::set<std::string> frame_images;
    std::uint64_t image_frame = 0;
    std::uint64_t editor_generation = 0, pending_editor_generation = 0;
    bool opened = false, detail = false, featured = false, metadata = false, preview = false, related_open = false,
         editing = false, dirty = false, previewed = false, request_credit = false;
    bool checks[4]{};
    std::string selected, request, editor, report, upload_kind = "download", upload_category = "tracks", pending_key;
    std::string identity, last_saved, screen, request_target, filter_key;
    std::string remove_instance, remove_title;
    bool remove_requested = false;
    std::string link_key;
    bool link_requested = false;
    std::set<std::string> selected_updates;
    std::set<std::string> install_after_download;
    unsigned pending_type = 0;
    std::uint64_t pending_generation = 0;
    std::uint64_t mutation = 0;
    StoreJson draft = StoreJson::object();
    std::map<std::string, std::string> fields;
    std::map<std::string, SDL_Texture*> textures;
    std::vector<SDL_Texture*> retired_textures;
    std::set<std::string> failed_covers;
} g_mod_store;
void StoreControlFocus() {
    if (!g_mod_store.body_navigation || (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_Popup))
        return;
    const auto id = ImGui::GetItemID();
    if (g_mod_store.restore_focus == id || g_mod_store.focus_first) {
        ImGui::SetFocusID(id, ImGui::GetCurrentWindow());
        g_mod_store.restore_focus = 0;
        g_mod_store.focus_first = false;
    }
    if (ImGui::IsItemFocused())
        g_mod_store.page_focus = id;
}
SDL_Renderer* g_mod_store_renderer = nullptr;
constexpr std::array<const char*, 6> kStoreCategoryNames = {
    "All", "Tracks", "Characters", "Vehicles", "Custom Magic Codes", "Texture packs"};
constexpr std::array<const char*, 6> kStoreCategoryIds = {"", "tracks", "characters", "legacy", "magic", "textures"};

std::string StoreText(const StoreJson& row, const char* key, std::size_t max = 4096) {
    return dkr::store::text(row, key, max);
}
const StoreJson& StoreItems(const dkr::store::Snapshot& store) {
    return *store.items;
}
int StoreInt(const StoreJson& value, const char* key, int fallback = 0) {
    if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer())
        return fallback;
    const auto number = value[key].get<std::int64_t>();
    return number >= 0 && number <= INT_MAX ? int(number) : fallback;
}
const StoreJson* StoreFind(const StoreJson& rows, const std::string& id) {
    if (rows.is_array())
        for (const auto& row : rows)
            if (StoreText(row, "id", 160) == id)
                return &row;
    return nullptr;
}
void StoreNote(const std::string& value, float width, bool muted = true) {
    PaddockText(PaddockReading(14.F, false, 1.45F), PaddockRgb(muted ? 0xBDCFD9 : 0xFFF4D3), value, width);
}
void StoreHeading(const std::string& value, float width, float size = 23.F) {
    PaddockHeading(value.c_str(), size, 0xFFCD67, width);
    PaddockGap(9.F);
}
bool StoreButton(const std::string& label, float width = 0, bool primary = false) {
    width = std::min(
        width > 0 ? width
                  : PaddockButtonWidth(label.c_str(), primary ? PaddockButtonKind::Primary : PaddockButtonKind::Plain),
        std::max(1.F, ImGui::GetContentRegionAvail().x));
    const bool pressed =
        PaddockButton(label.c_str(), primary ? PaddockButtonKind::Primary : PaddockButtonKind::Plain, width, 44.F);
    StoreControlFocus();
    return pressed;
}
bool StoreCheck(const std::string& label, bool& value, float width) {
    ImGui::PushID(label.c_str());
    ImGui::BeginGroup();
    const bool changed = ImGui::Checkbox("##value", &value);
    StoreControlFocus();
    const float box = ImGui::GetItemRectSize().x;
    const float check_height = ImGui::GetItemRectSize().y;
    ImGui::SameLine(0, 8.F);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.F, (check_height - 20.3F) * .5F));
    const auto* end = ImGui::FindRenderedTextEnd(label.c_str());
    StoreNote(std::string(label.c_str(), end), std::max(1.F, width - box - 8.F), false);
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}
void StoreActionLine(const std::vector<std::pair<std::string, std::function<void()>>>& actions, float width) {
    const auto start = ImGui::GetCursorScreenPos();
    float x = 0, y = 0, height = 0;
    for (const auto& [label, action] : actions) {
        const float w = std::min(width, std::max(90.F, PaddockButtonWidth(label.c_str(), PaddockButtonKind::Plain)));
        if (x > 0 && x + w > width) {
            x = 0;
            y += height + 8;
            height = 0;
        }
        ImGui::SetCursorScreenPos({start.x + x, start.y + y});
        if (StoreButton(label, w))
            action();
        height = std::max(height, ImGui::GetItemRectSize().y);
        x += w + 8;
    }
    ImGui::SetCursorScreenPos({start.x, start.y + y + height + 9});
}
void StoreField(const char* key, const char* label, float width, std::size_t maximum = 500, bool multiline = false, bool review = false) {
    width = std::max(1.F, std::min(width, ImGui::GetContentRegionAvail().x));
    auto& value = g_mod_store.fields[key];
    ImGui::PushID(key);
    const auto open_keyboard = [&] {
        const std::string name = key, identity = g_mod_store.identity;
        RequestCustomTextKeyboard(label, value, maximum + 1, [name, identity](const std::string& accepted) {
            if (g_mod_store.identity == identity) {
                g_mod_store.fields[name] = accepted;
                if (name.starts_with("draft-")) {
                    g_mod_store.dirty = true;
                    g_mod_store.previewed = false;
                }
            }
        });
    };
    const float keyboard = std::max(104.F, PaddockButtonWidth("Keyboard", PaddockButtonKind::Plain));
    if (width >= keyboard + 150.F) {
        const auto origin = ImGui::GetCursorScreenPos();
        StoreNote(label, width - keyboard - 10.F);
        const float bottom = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos({origin.x + width - keyboard, origin.y});
        if (StoreButton("Keyboard", keyboard))
            open_keyboard();
        ImGui::SetCursorScreenPos({origin.x, std::max(bottom, ImGui::GetCursorScreenPos().y) + 4.F});
    } else {
        StoreNote(label, width);
        if (StoreButton("Keyboard", std::min(width, keyboard)))
            open_keyboard();
    }
    const float edit = width;
    ImGui::SetNextItemWidth(edit);
    std::vector<char> buffer(maximum + 1, 0);
    const auto copy = std::min(maximum, value.size());
    std::memcpy(buffer.data(), value.data(), copy);
    if (review)
        ImGui::PushFont(PaddockReading(14.F, false, 1.45F).font);
    const bool changed = review && multiline
        ? DkrReviewInputText("##text", buffer.data(), buffer.size(), {edit, ImGui::GetTextLineHeight() * 6.F + ImGui::GetStyle().FramePadding.y * 2.F})
        : multiline ? ImGui::InputTextMultiline("##text", buffer.data(), buffer.size(), {edit, 90.F})
                    : ImGui::InputText("##text", buffer.data(), buffer.size());
    if (review)
        ImGui::PopFont();
    const bool controller_keyboard = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) &&
                                     !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    StoreControlFocus();
    if (changed) {
        value = buffer.data();
        if (std::string(key).starts_with("draft-")) {
            g_mod_store.dirty = true;
            g_mod_store.previewed = false;
        }
    }
    if (controller_keyboard)
        open_keyboard();
    ImGui::PopID();
    PaddockGap(9.F);
}
int StoreCombo(const char* id, int current, const std::vector<std::string>& labels, float width) {
    if (labels.empty())
        return 0;
    current = std::clamp(current, 0, int(labels.size() - 1));
    StoreNote(id, width);
    PaddockGap(4.F);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(std::min(width, ImGui::GetContentRegionAvail().x));
    const bool open = ImGui::BeginCombo("##value", labels[current].c_str());
    StoreControlFocus();
    if (open) {
        for (int i = 0; i < int(labels.size()); ++i)
            if (ImGui::Selectable(labels[i].c_str(), i == current))
                current = i;
        ImGui::EndCombo();
    }
    ImGui::PopID();
    PaddockGap(9.F);
    return current;
}
#include "mod_store_style.inl"
#include "store_polish_controls.inl"
void ReleaseModStoreTextures() {
    ReleaseVehicleTextures();
    for (auto& [url, texture] : g_mod_store.textures)
        if (texture)
            SDL_DestroyTexture(texture);
    g_mod_store.textures.clear();
    g_mod_store.texture_used.clear();
    g_mod_store.frame_images.clear();
    for (auto* texture : g_mod_store.retired_textures)
        SDL_DestroyTexture(texture);
    g_mod_store.retired_textures.clear();
    g_mod_store_renderer = nullptr;
}
void StoreCover(const StoreJson& row, float width, float height = 0, bool contain = false) {
    height = height > 0 ? height : width * .53F;
    if (!ImGui::IsRectVisible({width, height})) {
        ImGui::Dummy({width, height});
        PaddockGap(9.F);
        return;
    }
    auto url = StoreText(row, "image", 2048);
    if (url.empty())
        url = StoreText(row, "imageUrl", 2048);
    // These are website asset aliases, not missing external URLs.
    if (url.empty() || url == "pattern" || url == "pattern-two")
        url = "https://positron-portal.com/dkr-r/assets/" +
              (url == "pattern-two" ? std::string("pattern-two") : std::string("pattern")) + ".png";
    else if (url.starts_with("assets/") || url.starts_with("images/"))
        url = "https://positron-portal.com/dkr-r/" + url;
    else if (url.starts_with("/dkr-r/"))
        url = "https://positron-portal.com" + url;
    height = height > 0 ? height : width * .53F;
    const auto at = ImGui::GetCursorScreenPos();
    SDL_Texture* texture = nullptr;
    g_mod_store.frame_images.insert(url);
    g_mod_store.texture_used[url] = g_mod_store.image_frame;
    if (g_mod_store_renderer && dkr::store::trusted_url(url)) {
        auto found = g_mod_store.textures.find(url);
        if (found != g_mod_store.textures.end())
            texture = found->second;
        else if (auto image = dkr::store::service().image(url)) {
            if (g_mod_store.textures.size() >= 32) {
                auto old = g_mod_store.textures.end();
                for (auto it = g_mod_store.textures.begin(); it != g_mod_store.textures.end(); ++it)
                    if (!g_mod_store.frame_images.contains(it->first) &&
                        (old == g_mod_store.textures.end() ||
                         g_mod_store.texture_used[it->first] < g_mod_store.texture_used[old->first]))
                        old = it;
                // Draw data may still reference this image until presentation.
                if (old != g_mod_store.textures.end()) {
                    g_mod_store.retired_textures.push_back(old->second);
                    g_mod_store.texture_used.erase(old->first);
                    g_mod_store.textures.erase(old);
                }
            }
            if (g_mod_store.textures.size() < 32)
                texture = SDL_CreateTexture(g_mod_store_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                            int(image->width), int(image->height));
            if (texture) {
                if (SDL_UpdateTexture(texture, nullptr, image->rgba.data(), int(image->width * 4))) {
                    SDL_DestroyTexture(texture);
                    texture = nullptr;
                } else {
                    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                    g_mod_store.textures[url] = texture;
                }
            }
        } else
            dkr::store::service().request_image(url);
    }
    if (texture) {
        const auto image = dkr::store::service().image(url);
        const float source = image ? float(image->width) / float(image->height) : 1.8F,
                    dest = width / std::max(1.F, height);
        ImVec2 a{0, 0}, b{1, 1};
        ImVec2 image_min = at, image_max{at.x + width, at.y + height};
        if (contain) {
            ImGui::GetWindowDrawList()->AddRectFilled(at, image_max, PaddockCol(0x041822), 6.F);
            if (source > dest) {
                const float view = width / source;
                image_min.y += (height - view) * .5F;
                image_max.y = image_min.y + view;
            } else {
                const float view = height * source;
                image_min.x += (width - view) * .5F;
                image_max.x = image_min.x + view;
            }
        } else if (source > dest) {
            const float visible = dest / source;
            a.x = (1 - visible) * .5F;
            b.x = 1 - a.x;
        } else {
            const float visible = source / dest;
            a.y = (1 - visible) * .5F;
            b.y = 1 - a.y;
        }
        ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(texture), image_min, image_max, a, b);
    } else
        ImGui::GetWindowDrawList()->AddRectFilled(at, {at.x + width, at.y + height}, PaddockCol(0x184459), 8.F);
    ImGui::Dummy({width, height});
    PaddockGap(9.F);
}
std::vector<const StoreJson*> StoreFiltered(const dkr::store::Snapshot& store) {
    std::vector<const StoreJson*> rows;
    std::string search = g_mod_store.fields["search"];
    std::transform(search.begin(), search.end(), search.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (StoreItems(store).is_array())
        for (const auto& row : StoreItems(store)) {
            if (row.contains("archived") && row["archived"] == true)
                continue;
            if (g_mod_store.category && StoreText(row, "category", 32) != kStoreCategoryIds[g_mod_store.category])
                continue;
            std::string match = StoreText(row, "title", 200) + " " + StoreText(row, "author", 200);
            if (row.contains("tags") && row["tags"].is_array())
                for (const auto& tag : row["tags"])
                    if (tag.is_string())
                        match += " " + tag.get<std::string>();
            std::transform(match.begin(), match.end(), match.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (search.empty() || match.find(search) != std::string::npos)
                rows.push_back(&row);
        }
    std::stable_sort(rows.begin(), rows.end(), [&](const auto* a, const auto* b) {
        if (g_mod_store.sort == 1)
            return StoreText(*a, "title", 200) < StoreText(*b, "title", 200);
        if (g_mod_store.sort == 2) {
            auto score = [](const auto& row) {
                if (!row.contains("rating") || !row["rating"].is_object())
                    return 0.;
                const auto& r = row["rating"];
                return r.contains("average") && r["average"].is_number() ? r["average"].template get<double>() : 0.;
            };
            const auto x = score(*a), y = score(*b);
            if (x != y)
                return x > y;
        } else if (g_mod_store.sort == 0) {
            const bool x = a->contains("featured") && (*a)["featured"] == true,
                       y = b->contains("featured") && (*b)["featured"] == true;
            if (x != y)
                return x > y;
        }
        return StoreText(*a, "updatedAt", 80) > StoreText(*b, "updatedAt", 80);
    });
    return rows;
}
StoreJson StoreCurrentRelease(const StoreJson& row) {
    return dkr::store::current_release(row);
}
void StoreOpenMod(const StoreJson& row, bool detail) {
    g_mod_store.review_editing = false;
    g_mod_store.selected = StoreText(row, "id", 160);
    g_mod_store.detail = detail;
    g_mod_store.detail_tab = 0;
    dkr::store::service().reviews(g_mod_store.selected);
}
#include "mod_store_management_ui.inl"
#include "store_polish_modals.inl"
void StoreInstall(const dkr::store::Download& job, bool locked) {
    StoreManagerInstall(job, locked);
}
void StorePumpInstall() {
    StoreManagerPump();
}
void PumpModStoreLauncher() {
    StoreSyncIdentity(*dkr::store::service().view());
    ++g_mod_store.image_frame;
    g_mod_store.frame_images.clear();
    static std::uint64_t named_texture_generation = UINT64_MAX;
    const auto texture_generation = dkr::runtime::texture_packs::generation();
    if (named_texture_generation != texture_generation && dkr::store::service().refresh_names()) {
        named_texture_generation = texture_generation;
    }
    for (auto* texture : g_mod_store.retired_textures)
        SDL_DestroyTexture(texture);
    g_mod_store.retired_textures.clear();
    StorePumpInstall();
    StoreManagerModals();
    StorePolishModals();
    const auto url = dkr::store::service().take_browser_url();
    if (!url.empty())
        SDL_OpenURL(url.c_str());
}
void StoreDownloadButton(const StoreJson& row, const StoreJson& release, float width, bool compact = false) {
    const auto digest = StoreText(release, "sha256", 64);
    const auto managed = dkr::store::installations().view();
    bool installed_release = false, update_available = false;
    std::string repair_instance, installed_instance, update_instance;
    for (const auto& record : managed->records) {
        if (StoreText(record, "item", 160) != StoreText(row, "id", 160) || StoreText(record, "digest", 64) != digest)
            continue;
        const auto state = StoreText(record, "state", 80);
        if (state == "Installed" || record.value("stored", false)) {
            installed_release = true;
            installed_instance = StoreText(record, "instance", 32);
        } else if (state == "Needs repair" || state == "Missing")
            repair_instance = StoreText(record, "instance", 32);
    }
    for (const auto& update : managed->updates)
        if (StoreText(update["item"], "id", 160) == StoreText(row, "id", 160)) {
            update_available = true;
            update_instance = StoreText(update, "instance", 32);
        }
    const bool selected_update =
        update_available && StoreText(release, "id", 160) == StoreText(StoreCurrentRelease(row), "id", 160);
    if (installed_release || selected_update) {
        if (StoreButton(selected_update ? "Update available" : compact ? "Installed" : "Installed - manage", width)) {
            g_mod_store.page = 4;
            g_mod_store.installed_section = selected_update ? 1 : 0;
            if (selected_update)
                g_mod_store.update_detail = update_instance;
            else
                g_mod_store.installed_selected = installed_instance;
        }
        return;
    }
    if (!repair_instance.empty()) {
        if (StoreButton("Repair install", width))
            StoreQueueRelease(row, release, "reinstall", repair_instance);
        return;
    }
    const auto snapshot = dkr::store::service().view();
    const auto id = StoreText(row, "id", 160), version = StoreText(release, "id", 160);
    const dkr::store::Download* found = nullptr;
    for (const auto& job : snapshot->downloads)
        if (job.item == id && job.release == version) {
            found = &job;
            break;
        }
    const bool retry = found && (found->state == "Failed" || found->state == "Paused");
    const bool ready = found && (found->state == "Ready to install" || found->state == "Install failed" ||
                                 found->state == "Installed");
    const bool installed = false; // Native receipts, never download history, establish installation.
    const auto label = installed ? "Manage mod"
                       : ready   ? "Install mod"
                       : retry   ? "Retry download"
                       : found   ? "View download"
                       : compact ? "Install"
                                 : "Download & install";
    ImGui::BeginDisabled(version.empty());
    if (StoreButton(std::string(label) + "##download", width, true)) {
        if (installed) {
            g_mods_page.section = 0;
            const auto category = StoreText(row, "category", 32);
            g_mods_page.category = category == "magic" ? 2 : category == "characters" ? 1 : category == "legacy" || category == "vehicles" ? 3 : 0;
        } else if (ready) {
            StoreQueueRelease(row, release);
        } else if (!found || retry) {
            g_mod_store.install_after_download.insert(id + "_" + version);
            dkr::store::service().download(row, release);
        } else
            g_mod_store.page = 2;
    }
    ImGui::EndDisabled();
    if (compact && !found && ImGui::IsItemHovered())
        ImGui::SetTooltip("Download, check and import this mod. Code permissions still need your approval.");
    if (found && !installed && found->state != "Install failed") {
        PaddockGap(5.F);
        if (found->total && found->state != "Failed" && found->state != "Paused")
            ImGui::ProgressBar(std::clamp(float(double(found->received) / double(found->total)), 0.F, 1.F),
                               {width, 7.F}, "");
        StoreNote(found->state == "Failed"      ? "Download unavailable - see Downloads"
                  : found->state == "Installed" ? "Previously installed download"
                                                : found->state,
                  width);
    }
}
void StoreStartQueuedInstall(bool locked) {
    if (locked || dkr::store::installations().view()->busy || !g_mod_store.pending_key.empty() ||
        StoreVehiclesBusy() || g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||
        DialogJobRunning())
        return;
    const auto store = dkr::store::service().view();
    for (const auto& job : store->downloads) {
        if (!g_mod_store.install_after_download.contains(job.key))
            continue;
        if (job.state == "Failed" || job.state == "Paused") {
            g_mod_store.install_after_download.erase(job.key);
            continue;
        }
        if (job.state == "Ready to install" || job.state == "Install failed") {
            StoreInstall(job, false);
            if (!g_mod_store.pending_key.empty())
                g_mod_store.install_after_download.erase(job.key);
            return;
        }
    }
}
void StoreCardCover(const StoreJson& row, float width, float height, bool detail) {
    const auto start = ImGui::GetCursorScreenPos();
    StoreCover(row, width, height);
    const auto after = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(start);
    const auto press = PaddockBeginPress("##cover", {width, height});
    PaddockEndPress(press, 8.F);
    const auto label = StoreCategory(row);
    if (PaddockBadgeSize(label).x <= width - 16.F)
        PaddockBadge(ImGui::GetWindowDrawList(), {start.x + 8.F, start.y + 8.F}, label, PaddockTone::Neutral);
    if (press.pressed)
        StoreOpenMod(row, detail);
    ImGui::SetCursorScreenPos(after);
}
void StoreGridCard(const StoreJson& row, float width, float title_height, float body_height) {
    StorePanel card(width);
    const auto inner = card.inner();
    StoreCardCover(row, inner, inner / 1.8F, true);
    const auto title_start = ImGui::GetCursorScreenPos();
    const auto title_press = PaddockBeginPress("##title", {inner, title_height});
    PaddockTextStyle title_style;
    title_style.colour = PaddockApply(PaddockRgb(title_press.hover > .1F ? 0xFFCD67 : 0xFFF6DA));
    PaddockTextAt(ImGui::GetWindowDrawList(), PaddockSign(22.F, 1.25F), title_start, inner,
                  StoreText(row, "title", 200), title_style);
    PaddockEndPress(title_press, 4.F);
    if (title_press.pressed)
        StoreOpenMod(row, true);
    PaddockGap(6.F);
    StoreNote("by " + StoreText(row, "author", 100), inner);
    PaddockGap(7.F);
    StoreTags(row, inner);
    ImGui::SetCursorScreenPos({title_start.x, std::max(ImGui::GetCursorScreenPos().y, title_start.y + body_height)});
    ImGui::Separator();
    PaddockGap(9.F);
    if (inner >= 240.F &&
        ImGui::BeginTable("card-footer", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                          {inner, 0})) {
        ImGui::TableSetupColumn("Rating", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Install", ImGuiTableColumnFlags_WidthFixed, 130.F);
        ImGui::TableNextColumn();
        StoreNote(StoreRating(row), ImGui::GetContentRegionAvail().x);
        ImGui::TableNextColumn();
        StoreDownloadButton(row, StoreCurrentRelease(row), ImGui::GetContentRegionAvail().x, true);
        ImGui::EndTable();
    } else {
        StoreNote(StoreRating(row), inner);
        PaddockGap(7.F);
        StoreDownloadButton(row, StoreCurrentRelease(row), inner);
    }
}
#include "store_player_details.inl"
void StoreBrowser(float width, const dkr::store::Snapshot& store) {
    if (g_mod_store.detail) {
        if (const auto* row = StoreFind(StoreItems(store), g_mod_store.selected))
            StoreDetails(*row, width, store, false);
        else {
            StoreNote("This listing is no longer available.", width);
            if (StoreButton("Back to library"))
                g_mod_store.detail = false;
        }
        return;
    }
    if (width >= 720.F &&
        ImGui::BeginTable("store-search-sort", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                          {width, 0})) {
        ImGui::TableSetupColumn("Search", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Sort", ImGuiTableColumnFlags_WidthFixed, 240.F);
        ImGui::TableNextColumn();
        StoreField("search", "Search mods, creators or tags", ImGui::GetContentRegionAvail().x, 160);
        ImGui::TableNextColumn();
        g_mod_store.sort =
            StoreCombo("Sort by", g_mod_store.sort, {"Featured", "Name", "Top rated", "Recently updated"},
                       ImGui::GetContentRegionAvail().x);
        ImGui::EndTable();
    } else {
        StoreField("search", "Search mods, creators or tags", width, 160);
        g_mod_store.sort = StoreCombo("Sort by", g_mod_store.sort,
                                      {"Featured", "Name", "Top rated", "Recently updated"}, std::min(width, 260.F));
    }
    if (width >= 700.F) {
        const auto chosen = StoreTabLine({"Library grid", "Browse and inspect"}, store.layout, std::min(width, 400.F),
                                         "store-view-tabs");
        if (chosen != store.layout)
            dkr::store::service().layout(chosen);
    } else {
        const auto chosen = StoreCombo("View", store.layout, {"Library grid", "Browse and inspect"}, width);
        if (chosen != store.layout)
            dkr::store::service().layout(chosen);
    }
    PaddockGap(12.F);
    std::vector<std::string> categories;
    constexpr const char* short_categories[] = {"All downloads",      "Tracks",      "Characters",
                                                "Vehicles", "Magic Codes", "Texture packs"};
    for (int i = 0; i < 6; ++i) {
        std::size_t count = 0;
        for (const auto& item : StoreItems(store))
            if (!(item.contains("archived") && item["archived"] == true) &&
                (i == 0 || StoreText(item, "category", 32) == kStoreCategoryIds[i]))
                ++count;
        categories.push_back(std::string(short_categories[i]) + " (" + std::to_string(count) + ")");
    }
    const auto category = StoreTabLine(categories, g_mod_store.category, width, "store-category-tabs");
    if (category != g_mod_store.category) {
        g_mod_store.category = category;
        g_mod_store.visible = 12;
    }
    const auto filter_key = g_mod_store.fields["search"] + ":" + std::to_string(g_mod_store.category) + ":" +
                            std::to_string(g_mod_store.sort);
    if (filter_key != g_mod_store.filter_key) {
        g_mod_store.filter_key = filter_key;
        g_mod_store.visible = 12;
    }
    auto rows = StoreFiltered(store);
    StoreNote(std::to_string(rows.size()) + " mods" + (store.cached ? " - saved catalogue" : ""), width);
    PaddockGap(12.F);
    if (rows.empty()) {
        StoreNotice(store.loading ? "Loading the library" : "No mods found",
                    store.loading ? "The catalogue is on its way." : "Try another search or category.", width);
        if (!store.loading && StoreButton("Clear filters")) {
            g_mod_store.fields["search"].clear();
            g_mod_store.category = 0;
        }
        return;
    }
    if (std::none_of(rows.begin(), rows.end(),
                     [&](const auto* row) { return StoreText(*row, "id", 160) == g_mod_store.selected; }))
        g_mod_store.selected = StoreText(*rows.front(), "id", 160);
    const int limit = std::min(int(rows.size()), g_mod_store.visible);
    if (store.layout == 1 && width >= 760.F) {
        if (ImGui::BeginTable("store-inspect", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Mods", ImGuiTableColumnFlags_WidthStretch, .75F);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch, 1.25F);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const float list_width = ImGui::GetContentRegionAvail().x;
            for (int i = 0; i < limit; ++i) {
                const auto& row = *rows[i];
                ImGui::PushID(StoreText(row, "id", 160).c_str());
                {
                    StorePanel card(list_width, StoreText(row, "id", 160) == g_mod_store.selected);
                    const float inner = card.inner();
                    if (inner >= 240.F &&
                        ImGui::BeginTable("inspect-row", 2,
                                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                                          {inner, 0})) {
                        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthFixed, 88.F);
                        ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableNextColumn();
                        StoreCardCover(row, 88.F, 64.F, false);
                        ImGui::TableNextColumn();
                        if (StoreButton(StoreText(row, "title", 200) + "###select-mod",
                                        ImGui::GetContentRegionAvail().x))
                            StoreOpenMod(row, false);
                        StoreNote("by " + StoreText(row, "author", 100), ImGui::GetContentRegionAvail().x);
                        ImGui::EndTable();
                    } else {
                        if (StoreButton(StoreText(row, "title", 200) + "###select-mod", inner))
                            StoreOpenMod(row, false);
                        StoreNote("by " + StoreText(row, "author", 100), inner);
                    }
                }
                ImGui::PopID();
            }
            ImGui::TableSetColumnIndex(1);
            if (const auto* row = StoreFind(StoreItems(store), g_mod_store.selected))
                StoreDetails(*row, ImGui::GetContentRegionAvail().x, store, true);
            ImGui::EndTable();
        }
    } else {
        const int columns = store.layout == 1 ? 1 : std::clamp(int((width + 16.F) / 290.F), 1, 4);
        if (ImGui::BeginTable("store-grid", columns,
                              ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings, {width, 0})) {
            for (int base = 0; base < limit; base += columns) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const float text_width = std::max(1.F, ImGui::GetContentRegionAvail().x - 32.F);
                float title_height = 0, author_height = 0;
                for (int j = base; j < std::min(limit, base + columns); ++j) {
                    title_height =
                        std::max(title_height, PaddockTextHeight(PaddockSign(22.F, 1.25F),
                                                                 StoreText(*rows[j], "title", 200), text_width));
                    author_height = std::max(author_height,
                                             PaddockTextHeight(PaddockReading(14.F, false, 1.45F),
                                                               "by " + StoreText(*rows[j], "author", 100), text_width));
                }
                for (int j = base; j < std::min(limit, base + columns); ++j) {
                    ImGui::TableSetColumnIndex(j - base);
                    const auto& row = *rows[j];
                    ImGui::PushID(StoreText(row, "id", 160).c_str());
                    StoreGridCard(row, ImGui::GetContentRegionAvail().x, title_height,
                                  title_height + author_height + PaddockBadgeSize("x").y + 32.F +
                                      6.F * ImGui::GetStyle().ItemSpacing.y);
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    if (limit < int(rows.size()) && StoreButton("Show more mods", std::min(240.F, width)))
        g_mod_store.visible = std::min(10000, g_mod_store.visible + 12);
}
#include "store_player_activity.inl"
void StoreLoadDraft(const StoreJson& row) {
    ++g_mod_store.editor_generation;
    g_mod_store.editor_step = 0;
    g_mod_store.editing = true;
    g_mod_store.metadata = false;
    g_mod_store.preview = false;
    g_mod_store.related_open = false;
    g_mod_store.draft = row;
    const auto id = StoreText(row, "id", 160);
    g_mod_store.editor = id.starts_with("draft_") ? id : std::string{};
    g_mod_store.dirty = g_mod_store.editor.empty();
    g_mod_store.previewed = false;
    for (const char* key :
         {"title", "author", "summary", "image", "version", "fileUrl", "fileName", "format", "requirements", "install"})
        g_mod_store.fields[std::string("draft-") + key] = StoreText(row, key, 6000);
    g_mod_store.featured = row.contains("featured") && row["featured"] == true;
    g_mod_store.fields["draft-category"] = StoreText(row, "category", 32);
    g_mod_store.fields["draft-tags"].clear();
    if (row.contains("tags") && row["tags"].is_array())
        for (const auto& tag : row["tags"])
            if (tag.is_string()) {
                if (!g_mod_store.fields["draft-tags"].empty())
                    g_mod_store.fields["draft-tags"] += ", ";
                g_mod_store.fields["draft-tags"] += tag.get<std::string>();
            }
}
void StoreEnsureEditor() {
    if (!g_mod_store.editing) {
        g_mod_store.request.clear();
        StoreLoadDraft(StoreJson::object());
        g_mod_store.fields["draft-category"] = g_mod_store.upload_category;
    }
}
StoreJson StoreDraft() {
    auto value = g_mod_store.draft;
    if (g_mod_store.editor.empty())
        value.erase("id");
    for (const char* key :
         {"title", "author", "summary", "image", "version", "fileUrl", "fileName", "format", "requirements", "install"})
        value[key] = g_mod_store.fields[std::string("draft-") + key];
    value["category"] = g_mod_store.fields["draft-category"].empty() ? "tracks" : g_mod_store.fields["draft-category"];
    value["featured"] = g_mod_store.featured;
    value["tags"] = StoreJson::array();
    std::stringstream stream(g_mod_store.fields["draft-tags"]);
    std::string tag;
    while (std::getline(stream, tag, ',')) {
        const auto first = tag.find_first_not_of(" \t"), last = tag.find_last_not_of(" \t");
        if (first != tag.npos)
            value["tags"].push_back(tag.substr(first, last - first + 1));
    }
    return value;
}
void StoreChooseUpload(std::string kind, std::string resume = {}) {
    const auto category = g_mod_store.upload_category;
    StartDialogJob([kind, resume, category]() -> std::function<void()> {
        if (NFD_Init() != NFD_OKAY)
            return [] {};
        nfdu8char_t* result = nullptr;
        const nfdfilteritem_t image[] = {{"Cover image", "png,jpg,jpeg,webp"}},
                              packages[] = {{"Mod package", "zip,rtz,dkrmap,dkrkart,dkrhover,dkrplane,xdelta,nrm,dkrcode,bps,ips,7z"}};
        const auto dialog = NFD_OpenDialogU8(&result, kind == "image" ? image : packages, 1, nullptr);
        std::filesystem::path path;
        if (dialog == NFD_OKAY) {
            path = std::filesystem::u8path(result);
            NFD_FreePathU8(result);
        }
        NFD_Quit();
        return [path, kind, resume, category] {
            if (!path.empty())
                dkr::store::service().upload(path, kind, category, resume);
        };
    });
}
#include "store_staff_pages.inl"
#include "store_account_page.inl"
#include "store_page_shell.inl"
} // namespace

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
    "All", "Tracks", "Characters", "Legacy / ROM hacks", "Custom Magic Codes", "Texture packs"};
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
void StoreField(const char* key, const char* label, float width, std::size_t maximum = 500, bool multiline = false) {
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
    bool changed = multiline ? ImGui::InputTextMultiline("##text", buffer.data(), buffer.size(), {edit, 90.F})
                             : ImGui::InputText("##text", buffer.data(), buffer.size());
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
            g_mods_page.category = category == "magic" ? 2 : category == "characters" ? 1 : 0;
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
        g_legacy_imports.snapshot().busy || g_code_library.view()->busy || TexturePackImportRunning() ||
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
void StoreReviewForm(const StoreJson& row, float width, const dkr::store::Snapshot& store) {
    StorePanel form(std::min(width, 760.F));
    width = form.inner();
    if (store.user.is_null()) {
        StoreNote("Sign in with Discord to leave a review.", width);
        if (StoreButton("Sign in with Discord"))
            dkr::store::service().login();
        return;
    }
    const auto item = StoreText(row, "id", 160), review_key = "review-" + item, version_key = "review-version-" + item,
               rating_key = "rating-" + item;
    if (!g_mod_store.fields.contains(review_key)) {
        g_mod_store.fields[review_key] = "";
        g_mod_store.fields[version_key] = "";
        g_mod_store.fields[rating_key] = "5";
        const auto reviews = store.personal.value("reviews", StoreJson::array());
        for (const auto& own : reviews)
            if (StoreText(own, "itemId", 160) == item) {
                g_mod_store.fields[review_key] = StoreText(own, "text", 4000);
                g_mod_store.fields[version_key] = StoreText(own, "version", 80);
                g_mod_store.fields[rating_key] = std::to_string(std::clamp(StoreInt(own, "score", 5), 1, 5));
                break;
            }
    }
    const auto saved_rating = g_mod_store.fields[rating_key];
    g_mod_store.rating =
        saved_rating.size() == 1 && saved_rating[0] >= '1' && saved_rating[0] <= '5' ? saved_rating[0] - '0' : 5;
    g_mod_store.rating = StoreCombo("Rating", g_mod_store.rating - 1,
                                    {"1 star", "2 stars", "3 stars", "4 stars", "5 stars"}, std::min(width, 220.F)) +
                         1;
    g_mod_store.fields[rating_key] = std::to_string(g_mod_store.rating);
    StoreField(review_key.c_str(), "Your review", width, 4000, true);
    StoreField(version_key.c_str(), "Version used", width, 80);
    if (StoreButton("Save review", 0, true))
        dkr::store::service().action("review.save", {{"itemId", StoreText(row, "id", 160)},
                                                     {"score", g_mod_store.rating},
                                                     {"text", g_mod_store.fields[review_key]},
                                                     {"version", g_mod_store.fields[version_key]}});
}
void StoreDetails(const StoreJson& row, float width, const dkr::store::Snapshot& store, bool inspect) {
    if (!inspect) {
        if (StoreButton("Back to library"))
            g_mod_store.detail = false;
        PaddockGap(14.F);
    }
    const bool hero =
        !inspect && width >= 780.F &&
        ImGui::BeginTable("store-detail-hero", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                          {width, 0});
    if (hero) {
        ImGui::TableSetupColumn("Cover", ImGuiTableColumnFlags_WidthStretch, 1.4F);
        ImGui::TableSetupColumn("Release", ImGuiTableColumnFlags_WidthStretch, 1.F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        StoreCover(row, ImGui::GetContentRegionAvail().x, 280.F);
        ImGui::TableSetColumnIndex(1);
    } else
        StoreCover(row, width, std::min(280.F, width * .53F));
    const float hero_width = hero ? ImGui::GetContentRegionAvail().x : width;
    {
        StorePanel release_panel(hero_width);
        width = release_panel.inner();
        StoreBadge(StoreCategory(row), width);
        StoreHeading(StoreText(row, "title", 200), width);
        StoreNote("by " + StoreText(row, "author", 200), width);
        StoreNote(StoreRating(row), width);
        StoreNote("Version " + StoreText(row, "version", 80), width);
        const auto release = StoreCurrentRelease(row);
        StoreNote("Package " + StoreText(release, "format", 40) + "  |  Updated " + StoreDate(row), width);
        StoreTags(row, width);
        PaddockGap(12.F);
        StoreDownloadButton(row, StoreCurrentRelease(row), width);
    }
    if (hero)
        ImGui::EndTable();
    width = std::min(920.F, ImGui::GetContentRegionAvail().x);
    PaddockGap(13.F);
    g_mod_store.detail_tab = StoreTabLine({"Overview", "Reviews", "Installation", "Releases & files"},
                                          g_mod_store.detail_tab, width, "store-detail-tabs");
    if (g_mod_store.detail_tab == 0) {
        StoreHeading("About this mod", width, 21);
        const auto description = StoreText(row, "description", 16000);
        StoreNote(description.empty() ? StoreText(row, "summary", 4000) : description, width, false);
        PaddockGap(13.F);
        StoreHeading("Compatibility", width, 18);
        const auto requirements = StoreText(row, "requirements", 2000);
        StoreNote(requirements.empty() ? "Requirements not supplied by the creator." : requirements, width);
        if (row.contains("gallery") && row["gallery"].is_array()) {
            unsigned n = 0;
            for (const auto& image : row["gallery"]) {
                if (n++ == 12)
                    break;
                if (image.is_string()) {
                    StoreJson picture = {{"image", image}};
                    PaddockGap(10.F);
                    StoreCover(picture, width);
                }
            }
        }
        if (row.contains("related") && row["related"].is_array()) {
            PaddockGap(12.F);
            for (const auto& related : row["related"])
                if (related.is_string())
                    if (const auto* companion = StoreFind(StoreItems(store), related.get<std::string>()))
                        if (StoreButton("Related: " + StoreText(*companion, "title", 200), width))
                            StoreOpenMod(*companion, !inspect);
        }
    } else if (g_mod_store.detail_tab == 1) {
        const auto id = StoreText(row, "id", 160);
        if (StoreButton("Refresh reviews"))
            dkr::store::service().reviews(id);
        if (store.reviews.contains(id) && store.reviews[id].is_array()) {
            if (store.reviews[id].empty())
                StoreNote("No reviews yet.", width);
            for (const auto& review : store.reviews[id]) {
                ImGui::PushID(StoreText(review, "id", 160).c_str());
                {
                    StorePanel card(width);
                    const float outer = width;
                    width = card.inner();
                    PaddockGap(14.F);
                    StoreNote(StoreText(review, "displayName", 100) + " - " +
                                  std::to_string(StoreInt(review, "score")) + " / 5",
                              width, false);
                    StoreNote(StoreText(review, "text", 4000), width);
                    if (!store.user.is_null()) {
                        if (StoreButton("Report review"))
                            g_mod_store.report = StoreText(review, "id", 160);
                        if (g_mod_store.report == StoreText(review, "id", 160)) {
                            StoreField("report-reason", "Reason", width, 1600, true);
                            if (StoreButton("Send report"))
                                dkr::store::service().action("review.report",
                                                             {{"reviewId", g_mod_store.report},
                                                              {"reason", g_mod_store.fields["report-reason"]}});
                        }
                    }
                    width = outer;
                }
                ImGui::PopID();
            }
        }
        PaddockGap(18.F);
        StoreHeading("Your review", width, 18);
        StoreReviewForm(row, width, store);
    } else if (g_mod_store.detail_tab == 2) {
        const auto install = StoreText(row, "install", 6000);
        StoreNote(install.empty() ? "Choose Download & install. DKR-R will open the correct importer for this package."
                                  : install,
                  width);
        PaddockGap(12.F);
        StoreNote("Store downloads use DKR-R's normal importers. Code permissions are never approved automatically. "
                  "Your original ROM may be needed to prepare a patch.",
                  width);
    } else if (row.contains("releases") && row["releases"].is_array()) {
        for (const auto& release : row["releases"]) {
            ImGui::PushID(StoreText(release, "id", 160).c_str());
            {
                StorePanel panel(width);
                const float outer = width;
                width = panel.inner();
                StoreHeading("Version " + StoreText(release, "version", 80), width, 18);
                StoreNote(StoreText(release, "fileName", 180), width);
                StoreNote(StoreDate(release, "createdAt") + "  |  " + StoreText(release, "format", 40), width);
                const auto requirements = StoreText(release, "requirements", 2000);
                if (!requirements.empty())
                    StoreNote(requirements, width);
                StoreDownloadButton(row, release, width);
                PaddockGap(16.F);
                width = outer;
            }
            ImGui::PopID();
        }
    }
}
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
                                                "Legacy / ROM hacks", "Magic Codes", "Texture packs"};
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
void StoreDownloads(float width, bool locked, const dkr::store::Snapshot& store) {
    width = std::min(width, 980.F);
    StoreHeading("Downloads", width);
    if (store.downloads.empty()) {
        StoreNotice("Your downloads will appear here",
                    "Choose a mod in the library. Downloads and checks happen in the background.", width);
        if (StoreButton("Browse mods", 0, true))
            g_mod_store.page = 0;
    }
    StorePumpInstall();
    std::set<std::pair<std::string, std::string>> active_downloads;
    for (const auto& record : dkr::store::installations().view()->records)
        if (StoreText(record, "state", 80) == "Installed" && !record.value("stored", false))
            active_downloads.emplace(StoreText(record, "item", 160), StoreText(record, "digest", 64));
    for (const auto& job : store.downloads) {
        ImGui::PushID(job.key.c_str());
        {
            StorePanel card(width);
            const float inner = card.inner();
            StoreHeading(job.title, inner, 21);
            const auto shown_state = job.state == "Installed" && !active_downloads.contains({job.item, job.digest})
                                         ? "Previously installed"
                                         : job.state;
            StoreBadge(shown_state, inner,
                       job.state == "Installed" || job.state == "Ready to install" ? PaddockTone::On
                       : job.state == "Failed" || job.state == "Install failed"    ? PaddockTone::Warning
                                                                                   : PaddockTone::Neutral);
            if (job.total) {
                const auto fraction = float(double(job.received) / double(job.total));
                ImGui::ProgressBar(std::clamp(fraction, 0.F, 1.F), {inner, 16.F}, "");
                char size[100];
                std::snprintf(size, sizeof(size), "%.1f / %.1f MiB", double(job.received) / 1048576.,
                              double(job.total) / 1048576.);
                StoreNote(size, inner);
            }
            if (!job.error.empty())
                StoreNote(job.error, inner);
            if (job.state == "Queued" || job.state == "Downloading" || job.state == "Preparing download") {
                if (StoreButton("Pause"))
                    dkr::store::service().pause_download(job.key);
            } else if (job.state == "Failed" || job.state == "Paused") {
                if (StoreButton("Retry / resume"))
                    dkr::store::service().resume_download(job.key);
                if (const auto* item = StoreFind(StoreItems(store), job.item)) {
                    if (StoreButton("View mod details")) {
                        StoreOpenMod(*item, true);
                        g_mod_store.page = 0;
                    }
                    // The exact same public URL as the website; never fabricate
                    // an alternate package or mark an error page as installable.
                    if (item->contains("releases") && (*item)["releases"].is_array())
                        for (const auto& release : (*item)["releases"])
                            if (StoreText(release, "id", 160) == job.release) {
                                const auto url = StoreText(release, "fileUrl", 2048);
                                if (dkr::store::trusted_url(url) && StoreButton("Open download link in browser"))
                                    SDL_OpenURL(url.c_str());
                                break;
                            }
                }
            } else if (job.state == "Ready to install" || job.state == "Install failed") {
                ImGui::BeginDisabled(locked || !g_mod_store.pending_key.empty());
                if (StoreButton("Install in My mods", 0, true))
                    StoreInstall(job, locked);
                ImGui::EndDisabled();
                if (locked)
                    StoreNote("Leave the online lobby before installing.", inner);
            } else if (job.state == "Installed") {
                if (StoreButton("Manage installed mods"))
                    g_mod_store.page = 4;
            }
        }
        ImGui::PopID();
    }
}
void StoreRequestForm(float width, const dkr::store::Snapshot& store) {
    StoreHeading("Requests", width);
    if (store.user.is_null()) {
        StoreNote("Sign in with Discord to send requests or submit your own mod.", width);
        if (StoreButton("Sign in with Discord"))
            dkr::store::service().login();
        return;
    }
    g_mod_store.request_page =
        StoreTabLine({"Request a mod", "My requests"}, g_mod_store.request_page, width, "store-request-pages");
    if (g_mod_store.request_page == 0) {
        StoreFormLayout layout("store-request-layout", width);
        layout.sidebar([&](float side) {
            StorePanel preview(side);
            const auto inner = preview.inner();
            StoreHeading("Listing preview", inner, 20);
            const StoreJson item = {{"image", g_mod_store.fields["request-screenshot"]}};
            if (!g_mod_store.fields["request-screenshot"].empty())
                StoreCover(item, inner);
            StoreHeading(g_mod_store.fields["request-title"].empty() ? "Your mod" : g_mod_store.fields["request-title"],
                         inner, 23);
            StoreNote(g_mod_store.fields["request-author"].empty() ? "Creator credit"
                                                                   : "by " + g_mod_store.fields["request-author"],
                      inner);
            StoreNote(g_mod_store.fields["request-summary"].empty()
                          ? "Add the description, links and requirements. Staff "
                            "review the request before it appears in the library."
                          : g_mod_store.fields["request-summary"],
                      inner);
        });
        {
            StorePanel form(layout.width);
            width = form.inner();
            PaddockSeparatorText("Mod details", width);
            g_mod_store.request_kind = StoreCombo("Request type", g_mod_store.request_kind,
                                                  {"Suggest an existing mod", "I made this mod"}, width);
            StoreFieldPair("request-title", "Mod name", "request-author", "Creator", width);
            int category = std::clamp(g_mod_store.request_category - 1, 0, 4);
            category = StoreCombo("Category", category,
                                  {"Custom Tracks", "Custom Characters", "Legacy Mods / ROM hacks",
                                   "Custom Magic Codes", "Texture Packs"},
                                  width);
            g_mod_store.request_category = category + 1;
            g_mod_store.request_relation = StoreCombo("Request for", g_mod_store.request_relation,
                                                      {"New library listing", "Update an existing listing"}, width);
            if (g_mod_store.request_relation == 1) {
                std::vector<std::string> names;
                std::vector<const StoreJson*> targets;
                for (const auto& item : StoreItems(store))
                    if (StoreText(item, "category", 32) == kStoreCategoryIds[g_mod_store.request_category]) {
                        names.push_back(StoreText(item, "title", 200));
                        targets.push_back(&item);
                    }
                int current = 0;
                for (int i = 0; i < int(targets.size()); ++i)
                    if (StoreText(*targets[i], "id", 160) == g_mod_store.request_target)
                        current = i;
                current = StoreCombo("Existing listing", current, names, width);
                if (!targets.empty())
                    g_mod_store.request_target = StoreText(*targets[current], "id", 160);
                else
                    g_mod_store.request_target.clear();
            }
            PaddockGap(8.F);
            PaddockSeparatorText("Links and compatibility", width);
            StoreField("request-source", "Original source link", width, 1500);
            StoreField("request-download", "Download link (optional)", width, 1500);
            StoreField("request-summary", "Description and creator credits", width, 4000, true);
            StoreField("request-requirements", "Requirements", width, 2000, true);
            StoreFieldPair("request-tested", "DKR-R version used (optional)", "request-screenshot",
                           "Screenshot link (optional)", width, 80, 1500);
            StoreCheck("I have credited the original creator##request-credit", g_mod_store.request_credit, width);
            const bool valid =
                !g_mod_store.fields["request-title"].empty() && !g_mod_store.fields["request-author"].empty() &&
                !g_mod_store.fields["request-summary"].empty() &&
                g_mod_store.fields["request-source"].starts_with("https://") && g_mod_store.request_credit &&
                (!g_mod_store.request_relation || !g_mod_store.request_target.empty());
            ImGui::BeginDisabled(!valid);
            if (StoreButton("Send request", 0, true)) {
                StoreJson input = {{"title", g_mod_store.fields["request-title"]},
                                   {"author", g_mod_store.fields["request-author"]},
                                   {"category", kStoreCategoryIds[g_mod_store.request_category]},
                                   {"sourceUrl", g_mod_store.fields["request-source"]},
                                   {"downloadUrl", g_mod_store.fields["request-download"]},
                                   {"summary", g_mod_store.fields["request-summary"]},
                                   {"requirements", g_mod_store.fields["request-requirements"]},
                                   {"testedVersion", g_mod_store.fields["request-tested"]},
                                   {"screenshotUrl", g_mod_store.fields["request-screenshot"]},
                                   {"kind", g_mod_store.request_kind == 1 ? "creator" : "suggestion"},
                                   {"relation", g_mod_store.request_relation ? "update" : "new"}};
                if (g_mod_store.request_relation)
                    input["targetId"] = g_mod_store.request_target;
                dkr::store::service().action("request.submit", std::move(input));
            }
            ImGui::EndDisabled();
            StoreNote("Your request is visible to you and the moderation team.", width);
        }
        return;
    }
    width = std::min(width, 980.F);
    PaddockGap(20.F);
    StoreHeading("Your requests", width, 19);
    if (store.personal.contains("requests") && store.personal["requests"].is_array())
        for (const auto& request : store.personal["requests"]) {
            ImGui::PushID(StoreText(request, "id", 160).c_str());
            {
                StorePanel card(width);
                const auto inner = card.inner();
                StoreHeading(StoreText(request, "title", 200), inner, 20);
                StoreBadge(StoreText(request, "status", 50), inner);
                StoreNote(StoreText(request, "decisionMessage", 2400), inner);
                if (request.contains("events") && request["events"].is_array())
                    for (const auto& event : request["events"]) {
                        StoreNote(StoreText(event, "label", 200), inner, false);
                        StoreNote(StoreText(event, "message", 3000), inner);
                    }
                if (StoreText(request, "status", 40) == "changes") {
                    const auto prefix = "resubmit-" + StoreText(request, "id", 160) + "-";
                    for (const char* key :
                         {"sourceUrl", "downloadUrl", "screenshotUrl", "requirements", "testedVersion"})
                        if (!g_mod_store.fields.contains(prefix + key))
                            g_mod_store.fields[prefix + key] = StoreText(request, key, 2000);
                    StoreField((prefix + "reply").c_str(), "Reply describing your changes", inner, 2400, true);
                    StoreField((prefix + "sourceUrl").c_str(), "Updated source link", inner, 1500);
                    StoreField((prefix + "downloadUrl").c_str(), "Updated download link", inner, 1500);
                    StoreField((prefix + "requirements").c_str(), "Updated requirements", inner, 2000, true);
                    StoreFieldPair((prefix + "testedVersion").c_str(), "Version used",
                                   (prefix + "screenshotUrl").c_str(), "Screenshot link", inner, 80, 1500);
                    if (StoreButton("Send updated request"))
                        dkr::store::service().action("request.resubmit",
                                                     {{"id", request["id"]},
                                                      {"expectedRevision", StoreInt(request, "revision")},
                                                      {"message", g_mod_store.fields[prefix + "reply"]},
                                                      {"sourceUrl", g_mod_store.fields[prefix + "sourceUrl"]},
                                                      {"downloadUrl", g_mod_store.fields[prefix + "downloadUrl"]},
                                                      {"screenshotUrl", g_mod_store.fields[prefix + "screenshotUrl"]},
                                                      {"requirements", g_mod_store.fields[prefix + "requirements"]},
                                                      {"testedVersion", g_mod_store.fields[prefix + "testedVersion"]}});
                }
                PaddockGap(13.F);
            }
            ImGui::PopID();
        }
}
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
                              packages[] = {{"Mod package", "zip,rtz,dkrmap,xdelta,nrm,dkrcode,bps,ips,7z"}};
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
void StoreUploads(float width, const dkr::store::Snapshot& store) {
    StoreHeading("Staff uploads", width, 19);
    int category = 0;
    for (int i = 1; i < 6; ++i)
        if (g_mod_store.upload_category == kStoreCategoryIds[i])
            category = i - 1;
    category = StoreCombo("Upload category", category,
                          {"Tracks", "Characters", "Legacy", "Magic Codes", "Texture Packs"}, width);
    g_mod_store.upload_category = kStoreCategoryIds[category + 1];
    StoreActionLine({{"Upload package", [] { StoreChooseUpload("download"); }},
                     {"Upload cover", [] { StoreChooseUpload("image"); }},
                     {"Refresh files", [] { dkr::store::service().load_staff_files(); }},
                     {"Pause upload", [] { dkr::store::service().pause_upload(); }}},
                    width);
    StoreField("hosted-search", "Find an upload or hosted file", std::min(width, 680.F), 160);
    auto query = g_mod_store.fields["hosted-search"];
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (!store.upload.empty()) {
        StorePanel card(width);
        const float outer = width;
        width = card.inner();
        StoreNote(StoreText(store.upload, "fileName", 180), width, false);
        if (store.upload.contains("offset") && store.upload.contains("size") && store.upload["offset"].is_number() &&
            store.upload["size"].is_number()) {
            const auto total = store.upload["size"].get<double>();
            if (total > 0)
                ImGui::ProgressBar(float(store.upload["offset"].get<double>() / total), {width, 16.F});
        }
        StoreBadge(StoreText(store.upload, "state", 40), width);
        if (StoreText(store.upload, "state", 40) == "completed" && store.upload.contains("asset")) {
            const auto& asset = store.upload["asset"];
            if (StoreButton("Use completed upload in listing")) {
                StoreEnsureEditor();
                g_mod_store.dirty = true;
                g_mod_store.previewed = false;
                if (StoreText(asset, "kind", 32) == "image")
                    g_mod_store.fields["draft-image"] = StoreText(asset, "url", 1500);
                else {
                    g_mod_store.fields["draft-fileUrl"] = StoreText(asset, "url", 1500);
                    g_mod_store.fields["draft-fileName"] = StoreText(asset, "fileName", 180);
                    g_mod_store.fields["draft-format"] = StoreText(asset, "format", 40);
                }
                g_mod_store.staff_tab = 2;
            }
        }
        width = outer;
    }
    if (store.uploads.is_array())
        for (const auto& upload : store.uploads) {
            if (StoreText(upload, "category", 32) != g_mod_store.upload_category)
                continue;
            auto match = StoreText(upload, "fileName", 180);
            std::transform(match.begin(), match.end(), match.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (!query.empty() && match.find(query) == std::string::npos)
                continue;
            ImGui::PushID(StoreText(upload, "id", 160).c_str());
            {
                StorePanel card(width);
                const float outer = width;
                width = card.inner();
                StoreNote(StoreText(upload, "fileName", 180), width, false);
                const auto state = StoreText(upload, "state", 40);
                StoreBadge(state, width, state == "completed" ? PaddockTone::On : PaddockTone::Neutral);
                if (state == "open" || state == "completing") {
                    if (StoreButton("Resume with original file")) {
                        g_mod_store.upload_category = StoreText(upload, "category", 32);
                        StoreChooseUpload(StoreText(upload, "kind", 32), StoreText(upload, "id", 160));
                    }
                    if (StoreButton("Cancel unfinished upload"))
                        dkr::store::service().cancel_upload(StoreText(upload, "id", 160));
                } else if (state == "completed" && upload.contains("asset")) {
                    const auto asset = upload["asset"];
                    if (StoreButton("Use in listing")) {
                        StoreEnsureEditor();
                        g_mod_store.dirty = true;
                        g_mod_store.previewed = false;
                        if (StoreText(asset, "kind", 32) == "image")
                            g_mod_store.fields["draft-image"] = StoreText(asset, "url", 1500);
                        else {
                            g_mod_store.fields["draft-fileUrl"] = StoreText(asset, "url", 1500);
                            g_mod_store.fields["draft-fileName"] = StoreText(asset, "fileName", 180);
                            g_mod_store.fields["draft-format"] = StoreText(asset, "format", 40);
                        }
                        g_mod_store.staff_tab = 2;
                    }
                }
                if (state == "completed" && StoreButton("Prepare original file")) {
                    g_mod_store.upload_category = StoreText(upload, "category", 32);
                    StoreChooseUpload(StoreText(upload, "kind", 32), StoreText(upload, "id", 160));
                }
                PaddockGap(12.F);
                width = outer;
            }
            ImGui::PopID();
        }
    StoreHeading("Hosted files", width, 20);
    unsigned matching = 0;
    if (store.assets.is_array())
        for (const auto& asset : store.assets) {
            if (StoreText(asset, "category", 32) != g_mod_store.upload_category)
                continue;
            auto match = StoreText(asset, "fileName", 180);
            std::transform(match.begin(), match.end(), match.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (!query.empty() && match.find(query) == std::string::npos)
                continue;
            if (++matching > unsigned(g_mod_store.assets_visible))
                continue;
            ImGui::PushID(StoreText(asset, "url", 1500).c_str());
            {
                StorePanel card(width);
                const float outer = width;
                width = card.inner();
                StoreNote(StoreText(asset, "fileName", 180), width);
                if (StoreButton("Use hosted file")) {
                    StoreEnsureEditor();
                    g_mod_store.dirty = true;
                    g_mod_store.previewed = false;
                    if (StoreText(asset, "kind", 32) == "image")
                        g_mod_store.fields["draft-image"] = StoreText(asset, "url", 1500);
                    else {
                        g_mod_store.fields["draft-fileUrl"] = StoreText(asset, "url", 1500);
                        g_mod_store.fields["draft-fileName"] = StoreText(asset, "fileName", 180);
                        g_mod_store.fields["draft-format"] = StoreText(asset, "format", 40);
                    }
                    g_mod_store.staff_tab = 2;
                }
                width = outer;
            }
            ImGui::PopID();
        }
    if (!matching)
        StoreNote("No hosted files match this category and search.", width);
    if (matching > unsigned(g_mod_store.assets_visible) && StoreButton("Show more files"))
        g_mod_store.assets_visible = std::min(10000, g_mod_store.assets_visible + 24);
}
void StoreEditor(float width, const dkr::store::Snapshot& store) {
    StoreHeading("Listing editor", width, 19);
    if (!dkr::store::staff(store.user))
        return;
    if (g_mod_store.related_open) {
        StorePanel panel(std::min(width, 820.F));
        width = panel.inner();
        StoreHeading("Choose companion downloads", width, 21);
        StoreField("related-search", "Find a mod", width, 160);
        auto query = g_mod_store.fields["related-search"];
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        auto& related = g_mod_store.draft["related"];
        if (!related.is_array())
            related = StoreJson::array();
        unsigned shown = 0;
        for (const auto& item : StoreItems(store)) {
            const auto id = StoreText(item, "id", 160);
            if (id == StoreText(g_mod_store.draft, "targetId", 160) || id == StoreText(g_mod_store.draft, "id", 160))
                continue;
            auto name = StoreText(item, "title", 100), match = name;
            std::transform(match.begin(), match.end(), match.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (!query.empty() && match.find(query) == std::string::npos)
                continue;
            if (shown++ >= 48)
                continue;
            bool selected = std::find(related.begin(), related.end(), StoreJson(id)) != related.end();
            if (StoreCheck(name + "###related-" + id, selected, width)) {
                g_mod_store.dirty = true;
                g_mod_store.previewed = false;
                if (selected && related.size() < 10)
                    related.push_back(id);
                else if (!selected)
                    related.erase(std::remove(related.begin(), related.end(), StoreJson(id)), related.end());
            }
        }
        StoreNote("Choose up to 10 optional companions. These are not installed automatically.", width);
        if (StoreButton("Done"))
            g_mod_store.related_open = false;
        return;
    }
    if (g_mod_store.preview) {
        StorePanel panel(std::min(width, 920.F));
        width = panel.inner();
        const auto draft = StoreDraft();
        StoreHeading("Listing preview", width);
        StoreCover(draft, width, std::min(260.F, width * .5F));
        StoreHeading(StoreText(draft, "title", 100), width, 23);
        StoreNote("by " + StoreText(draft, "author", 100), width);
        StoreNote(StoreText(draft, "summary", 4000), width, false);
        StoreNote(StoreText(draft, "requirements", 2000), width);
        StoreNote(StoreText(draft, "install", 6000), width);
        if (StoreButton("Back to editing"))
            g_mod_store.preview = false;
        return;
    }
    StoreFormLayout layout("store-editor-layout", width);
    layout.sidebar([&](float side) {
        StorePanel preview(side);
        const auto inner = preview.inner();
        const auto draft = StoreDraft();
        StoreHeading("Listing preview", inner, 20);
        StoreCover(draft, inner);
        StoreHeading(StoreText(draft, "title", 100).empty() ? "Your mod" : StoreText(draft, "title", 100), inner, 23);
        StoreNote("by " + StoreText(draft, "author", 100), inner);
        StoreNote(StoreSummary(draft), inner);
        StoreBadge(g_mod_store.dirty ? "Unsaved changes" : "Saved draft", inner,
                   g_mod_store.dirty ? PaddockTone::Warning : PaddockTone::On);
    });
    StorePanel form(layout.width);
    width = form.inner();
    PaddockSeparatorText("Listing details", width);
    StoreFieldPair("draft-title", "Title", "draft-author", "Creator credit", width);
    int category = 0;
    for (int i = 1; i < 6; ++i)
        if (g_mod_store.fields["draft-category"] == kStoreCategoryIds[i])
            category = i - 1;
    const int old_category = category;
    category = StoreCombo("Listing category", category,
                          {"Tracks", "Characters", "Legacy", "Custom Magic Codes", "Texture Packs"}, width);
    g_mod_store.fields["draft-category"] = kStoreCategoryIds[category + 1];
    if (old_category != category) {
        g_mod_store.dirty = true;
        g_mod_store.previewed = false;
    }
    StoreField("draft-summary", "Description", width, 4000, true);
    StoreField("draft-tags", "Tags, separated by commas", width, 500);
    StoreField("draft-image", "Cover image URL", width, 1500);
    if (StoreButton("Choose or upload files")) {
        g_mod_store.upload_category = g_mod_store.fields["draft-category"];
        g_mod_store.staff_tab = 3;
        dkr::store::service().load_staff_files();
    }
    if (StoreCheck("Featured", g_mod_store.featured, width)) {
        g_mod_store.dirty = true;
        g_mod_store.previewed = false;
    }
    if (StoreButton("Choose companion downloads"))
        g_mod_store.related_open = true;
    if (g_mod_store.metadata) {
        if (StoreButton("Save listing details", 0, true)) {
            auto input = StoreDraft();
            input["id"] = g_mod_store.draft["id"];
            input["expectedRevision"] = StoreInt(g_mod_store.draft, "revision");
            dkr::store::service().action("catalogue.update", std::move(input));
        }
        return;
    }
    PaddockGap(8.F);
    PaddockSeparatorText("Release package", width);
    StoreFieldPair("draft-version", "Release version", "draft-format", "Package format", width, 80, 40);
    StoreField("draft-fileUrl", "Package download URL", width, 1500);
    StoreField("draft-fileName", "Package filename", width, 180);
    StoreField("draft-requirements", "Requirements", width, 2000, true);
    StoreField("draft-install", "Installation instructions", width, 6000, true);
    StoreActionLine({{"Save draft",
                      [] {
                          auto draft = StoreDraft();
                          StoreJson input = {{"draft", draft}};
                          if (!g_mod_store.editor.empty()) {
                              input["id"] = g_mod_store.editor;
                              input["expectedRevision"] = StoreInt(g_mod_store.draft, "revision");
                          }
                          dkr::store::service().action("catalogue.save", std::move(input));
                      }},
                     {"Preview listing",
                      [] {
                          g_mod_store.preview = true;
                          g_mod_store.previewed = true;
                      }}},
                    width);
    if (!g_mod_store.request.empty()) {
        const auto requests = store.staff.value("requests", StoreJson::array());
        if (const auto* request = StoreFind(requests, g_mod_store.request)) {
            ImGui::BeginDisabled(!g_mod_store.previewed || StoreText(*request, "status", 32) != "approved");
            if (StoreButton("Publish approved request", 0, true))
                ImGui::OpenPopup("Publish approved listing?");
            ImGui::EndDisabled();
            if (ImGui::BeginPopupModal("Publish approved listing?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                StoreNote("Publish this request and its current listing details to the shared library?",
                          std::min(width, 500.F));
                if (StoreButton("Publish", 0, true)) {
                    dkr::store::service().action("request.publish",
                                                 {{"id", g_mod_store.request},
                                                  {"expectedRevision", StoreInt(*request, "revision")},
                                                  {"draft", StoreDraft()}});
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (StoreButton("Cancel"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    } else if (!g_mod_store.editor.empty()) {
        ImGui::BeginDisabled(g_mod_store.dirty || !g_mod_store.previewed);
        if (StoreButton("Publish saved draft", 0, true))
            ImGui::OpenPopup("Publish this listing?");
        ImGui::EndDisabled();
        if (ImGui::BeginPopupModal("Publish this listing?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            StoreNote("Publish the saved draft to the shared library?", std::min(width, 500.F));
            if (StoreButton("Publish", 0, true)) {
                dkr::store::service().action(
                    "catalogue.publish",
                    {{"id", g_mod_store.editor}, {"expectedRevision", StoreInt(g_mod_store.draft, "revision")}});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (StoreButton("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    StoreNote(g_mod_store.dirty ? "Save your changes, then preview before publishing."
                                : "Preview the saved listing before publishing.",
              width);
}
void StoreManagement(float width, const dkr::store::Snapshot& store) {
    if (!dkr::store::staff(store.user))
        return;
    StoreHeading("Library management", width);
    std::vector<std::string> tabs = {"Requests", "Reports", "Listings", "Uploads"};
    if (dkr::store::admin(store.user)) {
        tabs.push_back("Activity");
        tabs.push_back("Staff access");
    } else if (g_mod_store.staff_tab > 3)
        g_mod_store.staff_tab = 0;
    const auto tab = StoreTabLine(tabs, g_mod_store.staff_tab, width, "store-staff-tabs");
    if (tab != g_mod_store.staff_tab) {
        g_mod_store.staff_tab = tab;
        if (tab == 3)
            dkr::store::service().load_staff_files();
    }
    if (g_mod_store.staff_tab != 2 || !g_mod_store.editing)
        width = std::min(width, 980.F);
    if (g_mod_store.staff_tab == 0) {
        const auto requests = store.staff.value("requests", StoreJson::array());
        for (const auto& row : requests) {
            ImGui::PushID(StoreText(row, "id", 160).c_str());
            {
                StorePanel card(width);
                const float saved_width = width;
                width = card.inner();
                StoreHeading(StoreText(row, "title", 200), width, 19);
                StoreNote(StoreText(row, "status", 40) + " - " + StoreText(row, "author", 100), width);
                StoreNote(StoreText(row, "summary", 4000), width);
                StoreNote(StoreText(row, "sourceUrl", 1500), width);
                const auto id = StoreText(row, "id", 160);
                if (StoreButton("Open request")) {
                    g_mod_store.request = id;
                    g_mod_store.fields["staff-note"].clear();
                    for (int i = 0; i < 4; ++i) {
                        const char* keys[] = {"source", "files", "requirements", "credits"};
                        g_mod_store.checks[i] = row.contains("checks") && row["checks"].is_object() &&
                                                row["checks"].contains(keys[i]) && row["checks"][keys[i]] == true;
                    }
                }
                if (g_mod_store.request == id) {
                    StoreNote(StoreText(row, "downloadUrl", 1500), width);
                    StoreNote(StoreText(row, "requirements", 2000), width);
                    if (row.contains("events") && row["events"].is_array())
                        for (const auto& event : row["events"]) {
                            StoreNote(StoreText(event, "label", 200), width, false);
                            StoreNote(StoreText(event, "message", 3000), width);
                        }
                    StoreField("staff-note", "Staff note or decision message", width, 2400, true);
                    if (StoreButton("Save staff note"))
                        dkr::store::service().action("request.note", {{"id", id},
                                                                      {"expectedRevision", StoreInt(row, "revision")},
                                                                      {"value", g_mod_store.fields["staff-note"]}});
                    if (StoreText(row, "status", 40) == "pending") {
                        if (StoreButton("Start review"))
                            dkr::store::service().action("request.start",
                                                         {{"id", id}, {"expectedRevision", StoreInt(row, "revision")}});
                    } else if (StoreText(row, "status", 40) == "review") {
                        for (int i = 0; i < 4; ++i) {
                            const char* labels[] = {"Source and creator checked", "Package contents checked",
                                                    "Requirements checked", "Credits checked"};
                            StoreCheck(labels[i], g_mod_store.checks[i], width);
                        }
                        for (const char* decision : {"approve", "changes", "reject"}) {
                            if (StoreButton(decision)) {
                                StoreJson checks = {{"source", g_mod_store.checks[0]},
                                                    {"files", g_mod_store.checks[1]},
                                                    {"requirements", g_mod_store.checks[2]},
                                                    {"credits", g_mod_store.checks[3]}};
                                dkr::store::service().action("request.decide",
                                                             {{"id", id},
                                                              {"expectedRevision", StoreInt(row, "revision")},
                                                              {"decision", decision},
                                                              {"checks", checks},
                                                              {"message", g_mod_store.fields["staff-note"]}});
                            }
                        }
                    } else if (StoreText(row, "status", 40) == "approved") {
                        if (StoreButton("Prepare listing", 0, true)) {
                            StoreLoadDraft(row);
                            g_mod_store.request = id;
                            g_mod_store.fields["draft-fileUrl"] = StoreText(row, "downloadUrl", 1500);
                            g_mod_store.fields["draft-image"] = StoreText(row, "screenshotUrl", 1500);
                            if (StoreText(row, "relation", 32) == "update") {
                                const auto target_id = StoreText(row, "targetId", 160);
                                g_mod_store.draft["target"] = "existing";
                                g_mod_store.draft["targetId"] = target_id;
                                if (const auto* target = StoreFind(StoreItems(store), target_id))
                                    g_mod_store.draft["targetRevision"] = StoreInt(*target, "revision");
                            }
                            g_mod_store.staff_tab = 2;
                        }
                    }
                }
                width = saved_width;
            }
            ImGui::PopID();
            PaddockGap(18.F);
        }
    } else if (g_mod_store.staff_tab == 1) {
        const auto reports = store.staff.value("reports", StoreJson::array());
        for (const auto& row : reports) {
            ImGui::PushID(StoreText(row, "id", 160).c_str());
            {
                StorePanel card(width);
                const float saved_width = width;
                width = card.inner();
                StoreHeading("Review report", width, 18);
                StoreNote(StoreText(row, "reason", 1600), width);
                StoreField("moderation-reason", "Decision reason", width, 1600, true);
                const auto review_rows = store.staff.value("reviews", StoreJson::array());
                const StoreJson* review = StoreFind(review_rows, StoreText(row, "reviewId", 160));
                if (review) {
                    StoreNote(StoreText(*review, "displayName", 100), width, false);
                    StoreNote(StoreText(*review, "text", 4000), width);
                }
                for (const char* action : {"keep", "hide", "restore"})
                    if (StoreButton(action))
                        dkr::store::service().action(
                            "report.resolve", {{"reportId", row["id"]},
                                               {"expectedRevision", StoreInt(row, "revision")},
                                               {"expectedReviewRevision", review ? StoreInt(*review, "revision") : 0},
                                               {"action", action},
                                               {"reason", g_mod_store.fields["moderation-reason"]}});
                PaddockGap(15.F);
                width = saved_width;
            }
            ImGui::PopID();
        }
    } else if (g_mod_store.staff_tab == 2) {
        if (g_mod_store.editing) {
            if (StoreButton("Back to listings"))
                g_mod_store.editing = false;
            PaddockGap(12.F);
            StoreEditor(width, store);
            return;
        }
        if (dkr::store::staff(store.user)) {
            if (StoreButton("New listing")) {
                g_mod_store.request.clear();
                StoreLoadDraft(StoreJson::object());
            }
            const auto drafts = store.admin.value("drafts", StoreJson::array());
            for (const auto& row : drafts) {
                if (StoreText(row, "status", 40) != "draft")
                    continue;
                ImGui::PushID(StoreText(row, "id", 160).c_str());
                if (StoreButton("Edit draft: " + StoreText(row, "title", 200), width)) {
                    g_mod_store.request.clear();
                    StoreLoadDraft(row.contains("draft") ? row["draft"] : row);
                    g_mod_store.editor = StoreText(row, "id", 160);
                    g_mod_store.draft["revision"] = StoreInt(row, "revision");
                }
                ImGui::PopID();
            }
        }
        const auto managed = store.admin.value("items", StoreJson::array());
        const auto& listings = managed.empty() ? StoreItems(store) : managed;
        for (const auto& item : listings) {
            ImGui::PushID(StoreText(item, "id", 160).c_str());
            {
                StorePanel card(width);
                const float outer = width;
                width = card.inner();
                StoreHeading(StoreText(item, "title", 200), width, 18);
                StoreNote(StoreCategory(item) + "  |  by " + StoreText(item, "author", 100), width);
                StoreActionLine({{"Edit details",
                                  [&] {
                                      g_mod_store.request.clear();
                                      StoreLoadDraft(item);
                                      g_mod_store.editor.clear();
                                      g_mod_store.metadata = true;
                                  }},
                                 {"Add release",
                                  [&] {
                                      g_mod_store.request.clear();
                                      StoreLoadDraft(item);
                                      g_mod_store.editor.clear();
                                      g_mod_store.draft["target"] = "existing";
                                      g_mod_store.draft["targetId"] = item["id"];
                                      g_mod_store.draft["targetRevision"] = StoreInt(item, "revision");
                                  }}},
                                width);
                if (dkr::store::admin(store.user)) {
                    const bool archived = item.contains("archived") && item["archived"] == true;
                    if (StoreButton(archived ? "Restore listing" : "Archive listing")) {
                        ImGui::OpenPopup("Change listing availability?");
                    }
                    if (ImGui::BeginPopupModal("Change listing availability?", nullptr,
                                               ImGuiWindowFlags_AlwaysAutoResize)) {
                        StoreNote("This changes the public library. Players' installed copies are kept.",
                                  std::min(width, 500.F));
                        if (StoreButton("Confirm")) {
                            dkr::store::service().action("catalogue.archive",
                                                         {{"id", item["id"]},
                                                          {"archived", !archived},
                                                          {"expectedRevision", StoreInt(item, "revision")}});
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (StoreButton("Cancel"))
                            ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                }
                PaddockGap(12.F);
                width = outer;
            }
            ImGui::PopID();
        }
    } else if (g_mod_store.staff_tab == 3)
        StoreUploads(width, store);
    else if (g_mod_store.staff_tab == 4) {
        for (const auto& row : store.admin.value("audit", StoreJson::array())) {
            StoreNote(StoreText(row, "action", 200), width, false);
            StoreNote(StoreText(row, "message", 2000), width);
            PaddockGap(12.F);
        }
    } else if (dkr::store::admin(store.user)) {
        for (const auto& row : store.admin.value("accounts", StoreJson::array())) {
            ImGui::PushID(StoreText(row, "id", 160).c_str());
            StoreHeading(StoreText(row, "displayName", 100), width, 18);
            StoreNote(StoreText(row, "role", 40), width);
            for (const char* role : {"player", "moderator", "admin"}) {
                if (StoreButton(std::string("Set ") + role)) {
                    g_mod_store.fields["role-account"] = StoreText(row, "id", 160);
                    g_mod_store.fields["role-value"] = role;
                    ImGui::OpenPopup("Change staff role?");
                }
            }
            if (ImGui::BeginPopupModal("Change staff role?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                StoreNote("Change this account's website and launcher permissions?", std::min(width, 500.F));
                if (StoreButton("Confirm role change")) {
                    dkr::store::service().action("admin.role", {{"accountId", g_mod_store.fields["role-account"]},
                                                                {"role", g_mod_store.fields["role-value"]}});
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (StoreButton("Cancel"))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
            PaddockGap(14.F);
        }
    } else
        StoreNote("Administrator access is required.", width);
}
void StoreAccount(float width, const dkr::store::Snapshot& store) {
    StoreFormLayout layout("store-account-layout", width, 560.F);
    layout.sidebar([&](float side) {
        StorePanel info(side);
        StoreHeading("Made by the community", info.inner(), 23);
        StoreNote(
            "Browse and install without an account. Sign in to leave reviews, request mods or manage the library.",
            info.inner());
        PaddockGap(12.F);
        if (StoreButton("My requests", info.inner())) {
            g_mod_store.page = 1;
            g_mod_store.request_page = 1;
        }
        if (StoreButton("Open website", info.inner()))
            SDL_OpenURL("https://positron-portal.com/dkr-r/index.html");
    });
    StorePanel panel(layout.width);
    width = panel.inner();
    StoreHeading("Your account", width);
    if (store.user.is_null()) {
        StoreNote("Browse and install without signing in. Discord is needed for reviews, requests and staff tools.",
                  width);
        if (store.signing_in) {
            StoreNote("Finish signing in through your browser, then return here.", width);
            if (StoreButton("Cancel sign-in"))
                dkr::store::service().cancel_login();
        } else {
#ifdef _WIN32
            bool remember = store.remember;
            if (StoreCheck("Keep me signed in on this PC", remember, width))
                dkr::store::service().remember(remember);
#endif
            if (StoreButton("Sign in with Discord", 0, true))
                dkr::store::service().login();
        }
        StoreNote("Discord opens in your browser. Remembered sign-ins use protected Windows storage.", width);
        return;
    }
    StoreHeading(StoreText(store.user, "displayName", 100), width, 23);
    StoreBadge(StoreText(store.user, "role", 20), width);
    g_mod_store.account_page =
        StoreTabLine({"Profile", "My reviews"}, g_mod_store.account_page, width, "store-account-tabs");
    if (g_mod_store.account_page == 1) {
        const auto reviews = store.personal.value("reviews", StoreJson::array());
        if (reviews.empty())
            StoreNote("You haven't reviewed a mod yet. Open a mod and choose Reviews.", width);
        for (const auto& review : reviews) {
            ImGui::PushID(StoreText(review, "id", 160).c_str());
            {
                StorePanel card(width);
                const auto* item = StoreFind(StoreItems(store), StoreText(review, "itemId", 160));
                StoreHeading(item ? StoreText(*item, "title", 200) : "Mod review", card.inner(), 20);
                StoreNote(std::to_string(StoreInt(review, "score")) + " / 5", card.inner());
                StoreNote(StoreText(review, "text", 4000), card.inner());
                if (item && StoreButton("Edit my review")) {
                    StoreOpenMod(*item, true);
                    g_mod_store.detail_tab = 1;
                    g_mod_store.page = 0;
                }
            }
            ImGui::PopID();
        }
        return;
    }
    StoreField("profile-name", "Display name", width, 60);
    if (StoreButton("Save display name"))
        dkr::store::service().action("account.profile", {{"displayName", g_mod_store.fields["profile-name"]}});
    PaddockGap(15.F);
    if (StoreButton("Sign out"))
        dkr::store::service().logout();
    PaddockGap(20.F);
    if (StoreButton("Delete library account"))
        ImGui::OpenPopup("Delete your library account?");
    if (ImGui::BeginPopupModal("Delete your library account?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const float modal_width = std::min(width, 500.F);
        StoreNote("This deletes your library account and personal data. Installed mods and your Discord account are "
                  "kept. Type DELETE to confirm.",
                  modal_width);
        StoreField("account-delete", "Confirmation", modal_width, 6);
        ImGui::BeginDisabled(g_mod_store.fields["account-delete"] != "DELETE");
        if (StoreButton("Delete account")) {
            dkr::store::service().action("account.delete", {{"confirmation", "DELETE"}});
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (StoreButton("Cancel"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
void DrawModStore(float width, bool locked) {
    auto& service = dkr::store::service();
    if (!g_mod_store.opened) {
        g_mod_store.opened = true;
        service.refresh();
    }
    auto store = service.view();
    const auto identity = StoreText(store->user, "id", 160) + ":" + StoreText(store->user, "role", 32);
    if (identity != g_mod_store.identity) {
        const auto search = g_mod_store.fields["search"];
        g_mod_store.fields.clear();
        g_mod_store.fields["search"] = search;
        g_mod_store.fields["profile-name"] = StoreText(store->user, "displayName", 60);
        g_mod_store.identity = identity;
        g_mod_store.request.clear();
        g_mod_store.editor.clear();
        g_mod_store.report.clear();
        g_mod_store.draft = StoreJson::object();
        g_mod_store.preview = g_mod_store.metadata = g_mod_store.editing = g_mod_store.related_open = false;
        g_mod_store.dirty = g_mod_store.previewed = false;
        g_mod_store.last_saved.clear();
        g_mod_store.request_target.clear();
        g_mod_store.request_credit = false;
        g_mod_store.install_after_download.clear();
        if (g_mod_store.page >= 7 && !dkr::store::staff(store->user))
            g_mod_store.page = 3;
    }
    if (!store->writing && store->mutation != g_mod_store.mutation) {
        g_mod_store.mutation = store->mutation;
        if (store->last_action == "catalogue.save" && store->last_result.is_string()) {
            const auto id = store->last_result.get<std::string>();
            const auto drafts = store->admin.value("drafts", StoreJson::array());
            if (const auto* saved = StoreFind(drafts, id)) {
                StoreLoadDraft(*saved);
                g_mod_store.editor = id;
                g_mod_store.last_saved = id;
            }
        }
    }
    if (auto url = service.take_browser_url(); !url.empty())
        SDL_OpenURL(url.c_str());
    StorePumpInstall();
    StoreStartQueuedInstall(locked || store->writing);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.F);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.F, 8.F});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, PaddockCol(0x102F43));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, PaddockCol(0x1C4C66));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, PaddockCol(0x205774));
    ImGui::PushStyleColor(ImGuiCol_Border, PaddockCol(0x496579));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, PaddockCol(0xFFC15A));
    if (width >= 600.F &&
        ImGui::BeginTable("store-toolbar", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings,
                          {width, 0})) {
        ImGui::TableSetupColumn("Library", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Account", ImGuiTableColumnFlags_WidthFixed, 270.F);
        ImGui::TableNextColumn();
        StoreHeading("MOD STORE", ImGui::GetContentRegionAvail().x, 29);
        StoreNote("Community creations, one starting grid.", ImGui::GetContentRegionAvail().x);
        ImGui::TableNextColumn();
        StoreNote(store->user.is_null() ? "Browse without an account"
                                        : "Signed in as " + StoreText(store->user, "displayName", 100),
                  ImGui::GetContentRegionAvail().x);
        StoreActionLine({{"Refresh", [&] { service.refresh(); }},
                         {"Website", [] { SDL_OpenURL("https://positron-portal.com/dkr-r/index.html"); }}},
                        ImGui::GetContentRegionAvail().x);
        ImGui::EndTable();
    } else {
        StoreHeading("MOD STORE", width, 29);
        if (StoreButton("Refresh library"))
            service.refresh();
    }
    PaddockGap(10.F);
    const auto active = std::count_if(store->downloads.begin(), store->downloads.end(),
                                      [](const auto& job) { return job.state != "Installed"; });
    std::vector<std::string> pages = {"Library",
                                      "Requests",
                                      "Downloads (" + std::to_string(active) + ")",
                                      store->user.is_null() ? "Sign in" : "Your account",
                                      "Installed",
                                      "Updates (" + std::to_string(dkr::store::installations().view()->updates.size()) +
                                          ")",
                                      "Duplicates"};
    if (dkr::store::staff(store->user))
        pages.push_back("Manage library");
    g_mod_store.page = StoreTabLine(pages, g_mod_store.page, width, "store-main-tabs");
    if (!store->error.empty()) {
        StoreNotice("Couldn't complete that action", store->error, width, true);
    } else if (!store->status.empty()) {
        // Routine transfer and catalogue messages belong to their own cards.
        if (store->status != "Library up to date." && store->status != "Download prepared." &&
            !store->status.starts_with("Checking release file:")) {
            StoreNote(store->status, width);
            PaddockGap(8.F);
        }
    }
    const auto installation = dkr::store::installations().view();
    if (!installation->ready && !installation->busy) {
        StoreNote("Mod installation recovery needs attention before starting a game or changing mods.", width);
        if (StoreButton("Retry installation recovery", 0, true))
            dkr::store::installations().retry_recovery();
    }
    if (installation->busy) {
        StoreNote(installation->stage, width);
        ImGui::ProgressBar(installation->progress, {width, 10.F}, "");
    }
    if (!installation->error.empty())
        StoreNotice("Installation stopped safely", installation->error, width, true);
    else if (!installation->result.empty())
        StoreNote(installation->result, width);
    const auto screen = std::to_string(g_mod_store.page) + ":" + std::to_string(g_mod_store.staff_tab) + ":" +
                        std::to_string(g_mod_store.detail) + ":" + std::to_string(g_mod_store.editing) + ":" +
                        std::to_string(g_mod_store.preview) + ":" + std::to_string(g_mod_store.related_open) + ":" +
                        (g_mod_store.detail ? g_mod_store.selected : "") + ":" +
                        std::to_string(g_mod_store.request_page);
    if (screen != g_mod_store.screen) {
        g_mod_store.screen = screen;
        ImGui::SetScrollY(0);
    }
    ImGui::BeginDisabled(store->writing);
    if (g_mod_store.page == 0)
        StoreBrowser(width, *store);
    else if (g_mod_store.page == 1)
        StoreRequestForm(width, *store);
    else if (g_mod_store.page == 2)
        StoreDownloads(width, locked, *store);
    else if (g_mod_store.page == 3)
        StoreAccount(width, *store);
    else if (g_mod_store.page == 4)
        StoreInstalledPage(width, *store, locked);
    else if (g_mod_store.page == 5)
        StoreUpdatesPage(width, *store, locked);
    else if (g_mod_store.page == 6)
        StoreDuplicatesPage(width, *store, locked);
    else
        StoreManagement(width, *store);
    ImGui::EndDisabled();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
}
} // namespace

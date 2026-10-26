// Included inside the launcher namespace after the existing library/Store UI.
namespace vehicle_mods = dkr::mods::vehicles;
struct VehicleLibraryView {
    std::vector<vehicle_mods::Item> items;
    bool busy = false;
    std::string error, message;
    std::shared_ptr<const vehicle_mods::ImportBatch> pending;
};
class VehicleLibraryUI {
    std::mutex mutex_;
    std::filesystem::path root_;
    std::shared_ptr<const VehicleLibraryView> view_ = std::make_shared<VehicleLibraryView>();
    std::jthread worker_;

  public:
    ~VehicleLibraryUI() {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }
    std::shared_ptr<const VehicleLibraryView> view() {
        std::lock_guard lock(mutex_);
        return view_;
    }
    bool job(std::function<void(vehicle_mods::Library&, VehicleLibraryView&)> action = {}, std::string done = {}) {
        std::filesystem::path root;
        {
            std::lock_guard lock(mutex_);
            if (view_->busy || root_.empty() ||
                (dkr::store::installations().view()->ready && dkr::store::installations().view()->busy))
                return false;
            auto next = std::make_shared<VehicleLibraryView>(*view_);
            next->busy = true;
            next->error.clear();
            next->message.clear();
            view_ = next;
            root = root_;
        }
        if (worker_.joinable())
            worker_.join();
        try {
            worker_ =
                std::jthread([this, root, action = std::move(action), done = std::move(done)](std::stop_token stop) {
                    auto next = std::make_shared<VehicleLibraryView>();
                    try {
                        vehicle_mods::Library library(root);
                        if (action)
                            action(library, *next);
                        next->items = library.scan(stop);
                        if (!next->pending)
                            next->message = done;
                    } catch (const std::exception& e) {
                        std::lock_guard lock(mutex_);
                        next->items = view_->items;
                        next->pending = view_->pending;
                        next->error = e.what();
                    }
                    std::lock_guard lock(mutex_);
                    view_ = std::move(next);
                });
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            auto next = std::make_shared<VehicleLibraryView>(*view_);
            next->busy = false;
            next->error = e.what();
            view_ = next;
            return false;
        }
        return true;
    }
    void configure(std::filesystem::path root) {
        if (worker_.joinable())
            worker_.join();
        {
            std::lock_guard lock(mutex_);
            root_ = std::move(root);
        }
        job();
    }
};
VehicleLibraryUI g_vehicle_library;
std::array<char, 161> g_vehicle_search{};
std::string g_vehicle_details;
bool g_vehicle_details_request = false, g_vehicle_remove_confirm = false;
std::map<std::string, SDL_Texture*> g_vehicle_portraits;
int g_vehicle_last_draw = -2;
const VehicleLibraryView* g_vehicle_preview_view = nullptr;
void ReleaseVehicleTextures() {
    for (auto& [hash, t] : g_vehicle_portraits)
        if (t)
            SDL_DestroyTexture(t);
    g_vehicle_portraits.clear();
}
void DrawVehiclePortrait(const vehicle_mods::Item& item, float width, float height) {
    const auto at = ImGui::GetCursorScreenPos();
    if (g_mod_store_renderer && ImGui::IsRectVisible({width, height})) {
        auto it = g_vehicle_portraits.find(item.digest);
        if (it == g_vehicle_portraits.end() && g_vehicle_portraits.size() < 512) {
            const auto& p = item.portrait;
            auto* t = SDL_CreateTexture(g_mod_store_renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
                                        int(p.width), int(p.height));
            if (t && SDL_UpdateTexture(t, nullptr, p.rgba.data(), int(p.width * 4))) {
                SDL_DestroyTexture(t);
                t = nullptr;
            }
            if (t)
                SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            it = g_vehicle_portraits.emplace(item.digest, t).first;
        }
        if (it != g_vehicle_portraits.end() && it->second) {
            const float ratio = float(item.portrait.width) / item.portrait.height;
            const float w = std::min(width, height * ratio), h = w / ratio;
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(it->second),
                                                 {at.x + (width - w) * .5F, at.y + (height - h) * .5F},
                                                 {at.x + (width + w) * .5F, at.y + (height + h) * .5F});
        }
    }
    ImGui::Dummy({width, height});
}
void ImportVehicleWithDialog() {
    if (dkr::store::installations().view()->busy || g_vehicle_library.view()->busy ||
        g_vehicle_library.view()->pending || DialogJobRunning())
        return;
    StartDialogJob([]() -> std::function<void()> {
        std::filesystem::path source;
        std::string error;
        if (NFD_Init() != NFD_OKAY)
            error = "The file picker could not open.";
        else {
            nfdu8char_t* result = nullptr;
            const nfdfilteritem_t filters[] = {{"Custom car or vehicle ZIP", "dkrkart,zip"}};
            const auto dialog = NFD_OpenDialogU8(&result, filters, 1, nullptr);
            if (dialog == NFD_OKAY) {
                source = std::filesystem::u8path(result);
                NFD_FreePathU8(result);
            } else if (dialog == NFD_ERROR)
                error = NFD_GetError();
            NFD_Quit();
        }
        return [source, error] {
            if (!error.empty()) {
                g_legacy_import_status = error;
                return;
            }
            if (source.empty())
                return;
            g_mods_page.category = 3;
            g_mods_page.section = kModsSectionLibrary;
            g_vehicle_library.job(
                [source](auto& library, auto& view) {
                    auto batch = library.prepare_import(source);
                    if (!batch.conflicts.empty())
                        view.pending = std::make_shared<const vehicle_mods::ImportBatch>(std::move(batch));
                    else
                        library.import_prepared(batch, vehicle_mods::ImportPolicy::RejectDuplicates);
                },
                "Cars imported. Choose yours after selecting Car in Track Select.");
        };
    });
}
void DrawVehicleLibrary(float requested_width, bool locked) {
    const auto snapshot = g_vehicle_library.view();
    const auto frame = ImGui::GetFrameCount();
    if (frame != g_vehicle_last_draw + 1 && !locked && !snapshot->busy && !snapshot->pending)
        g_vehicle_library.job();
    g_vehicle_last_draw = frame;
    if (g_vehicle_preview_view != snapshot.get()) {
        ReleaseVehicleTextures();
        g_vehicle_preview_view = snapshot.get();
    }
    const bool busy = locked || snapshot->busy || bool(snapshot->pending);
    const float width = std::max(1.F, std::min(requested_width, ImGui::GetContentRegionAvail().x));
    ImGui::PushID("vehicle-card-library");
    const auto search =
        PaddockSearch("search", g_vehicle_search.data(), g_vehicle_search.size(), "Search vehicles...", width);
    if (search == PaddockFieldResult::GamepadActivated)
        RequestCustomTextKeyboard(
            "Search vehicles", g_vehicle_search.data(), g_vehicle_search.size(), [](const std::string& s) {
                g_vehicle_search.fill(0);
                std::copy_n(s.begin(), std::min(s.size(), g_vehicle_search.size() - 1), g_vehicle_search.begin());
            });
    PaddockGap(14);
    if (snapshot->busy)
        DrawToolIntro("Updating vehicle library...", width);
    if (!snapshot->error.empty())
        DrawIssue(snapshot->error, dkr::runtime::copy::Area::Mods);
    if (!snapshot->message.empty())
        DrawToolIntro(snapshot->message, width);
    auto query = std::string(g_vehicle_search.data());
    std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    std::vector<const vehicle_mods::Item*> shown;
    for (const auto& item : snapshot->items) {
        auto name = item.name;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (query.empty() || name.find(query) != name.npos)
            shown.push_back(&item);
    }
    const auto count =
        std::count_if(snapshot->items.begin(), snapshot->items.end(), [](const auto& item) { return item.enabled; });
    PaddockText(PaddockReading(12, false, 1.5F), PaddockRgb(0x9BB0BD),
                std::to_string(count) + " cars enabled. Track Select only.", width);
    PaddockGap(16);
    if (shown.empty()) {
        PaddockBox box(width, {24, 28});
        PaddockHeading(snapshot->items.empty() ? "Your vehicle library starts here" : "No matching vehicles", 24,
                       0xFFF0C2, box.Inner());
        PaddockGap(12);
        ImGui::BeginDisabled(busy);
        if (PaddockButton("Import car", PaddockButtonKind::Primary))
            ImportVehicleWithDialog();
        ImGui::EndDisabled();
        box.End(CupCardBackground);
    } else {
        const int columns = std::max(1, int((width + kModsCardGap) / (265 + kModsCardGap)));
        const float card_width = std::floor((width - kModsCardGap * (columns - 1)) / columns);
        const auto grid = ImGui::GetCursorScreenPos();
        float y = grid.y;
        for (std::size_t first = 0; first < shown.size(); first += columns) {
            std::vector<LibraryCardView> cards;
            float height = 0;
            for (std::size_t i = first; i < std::min(first + columns, shown.size()); ++i) {
                const auto& item = *shown[i];
                LibraryCardView card;
                card.native = true;
                card.id = item.digest;
                card.title = item.name;
                card.byline = vehicle_mods::type_name(item.type);
                card.info = "Vehicle only";
                card.enabled = item.enabled;
                card.gold = item.enabled;
                card.toggle_allowed = !busy;
                card.status = item.enabled ? "On for next launch" : "Off";
                card.tone = item.enabled ? PaddockTone::On : PaddockTone::Neutral;
                card.preview_height = 100;
                card.preview = [item_ptr = &item](float w, float h) { DrawVehiclePortrait(*item_ptr, w, h); };
                height = std::max(height, MeasureLibraryCard(card, card_width));
                cards.push_back(std::move(card));
            }
            for (std::size_t col = 0; col < cards.size(); ++col) {
                const auto& item = *shown[first + col];
                ImGui::PushID(item.digest.c_str());
                const ImVec2 at{grid.x + col * (card_width + kModsCardGap), y};
                const bool details = DrawLibraryCard(
                    cards[col], at, card_width, height, false, busy, [hash = item.digest](bool enabled) {
                        g_vehicle_library.job([hash, enabled](auto& library, auto&) { library.enable(hash, enabled); });
                    });
                if (details) {
                    g_vehicle_details = item.digest;
                    g_vehicle_details_request = true;
                }
                ImGui::PopID();
            }
            y += height + kModsCardGap;
        }
        ImGui::SetCursorScreenPos(grid);
        ImGui::Dummy({width, std::max(0.F, y - grid.y - kModsCardGap + 5)});
    }
    ImGui::PopID();
}
void DrawVehicleImportConfirm(bool locked) {
    constexpr const char* name = "Import updated cars";
    const auto snapshot = g_vehicle_library.view();
    static std::weak_ptr<const vehicle_mods::ImportBatch> shown;
    if (snapshot->pending && (shown.owner_before(snapshot->pending) || snapshot->pending.owner_before(shown))) {
        shown = snapshot->pending;
        ImGui::OpenPopup(name);
    }
    if (!ImGui::IsPopupOpen(name))
        return;
    const auto screen = ImGui::GetIO().DisplaySize;
    const float width = std::max(240.F, std::min(640.F, screen.x - 48));
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, std::max(240.F, screen.y - 48)});
    const PaddockFlatScope paddock;
    if (!BeginPaddockModalWindow(name, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (!snapshot->pending) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    PaddockModalText(
        "Some cars have the same names as cars in your library. Replace the older copies, or keep both versions?");
    for (const auto& row : snapshot->pending->conflicts)
        PaddockModalText(row.name);
    PaddockGap(10);
    PaddockModalText("Replacing keeps the old files in recovery history. Your downloads and saves are not changed.");
    if (!snapshot->error.empty())
        PaddockModalText(snapshot->error);
    if (snapshot->busy)
        PaddockModalText("Importing cars...");
    ImGui::BeginDisabled(locked || snapshot->busy);
    const auto batch = snapshot->pending;
    if (PaddockButton("Replace older copies", PaddockButtonKind::Primary))
        g_vehicle_library.job(
            [batch](auto& library, auto&) {
                library.import_prepared(*batch, vehicle_mods::ImportPolicy::ReplaceSameName);
            },
            "Cars updated. Older copies were kept in recovery history.");
    PaddockGap(8);
    if (PaddockButton("Keep both versions", PaddockButtonKind::Plain))
        g_vehicle_library.job(
            [batch](auto& library, auto&) { library.import_prepared(*batch, vehicle_mods::ImportPolicy::KeepBoth); },
            "Cars imported. Both versions were kept.");
    PaddockGap(8);
    if (PaddockButton("Cancel", PaddockButtonKind::Link))
        g_vehicle_library.job();
    ImGui::EndDisabled();
    ImGui::EndPopup();
}
void DrawVehicleDetails(bool locked) {
    DrawVehicleImportConfirm(locked);
    constexpr const char* name = "Vehicle details";
    if (g_vehicle_details_request) {
        ImGui::OpenPopup(name);
        g_vehicle_details_request = false;
        g_vehicle_remove_confirm = false;
    }
    if (!ImGui::IsPopupOpen(name))
        return;
    const auto screen = ImGui::GetIO().DisplaySize;
    const float width = std::max(240.F, std::min(620.F, screen.x - 48));
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, std::max(240.F, screen.y - 48)});
    const PaddockFlatScope paddock;
    if (!BeginPaddockModalWindow(name, ImGuiWindowFlags_AlwaysAutoResize))
        return;
    const auto snapshot = g_vehicle_library.view();
    const auto it = std::find_if(snapshot->items.begin(), snapshot->items.end(),
                                 [](const auto& item) { return item.digest == g_vehicle_details; });
    if (it != snapshot->items.end()) {
        const auto inner = ImGui::GetContentRegionAvail().x;
        PaddockHeading(it->name, 28, 0xFFCD67, inner);
        PaddockGap(12);
        DrawVehiclePortrait(*it, inner, 180);
        PaddockGap(12);
        PaddockModalText(
            "Replaces the car visuals, not your chosen character or its handling. Available from Track Select.");
        ImGui::BeginDisabled(locked || snapshot->busy);
        bool enabled = it->enabled;
        if (PaddockSwitch("Enabled for next game", &enabled, "On", "Off"))
            g_vehicle_library.job(
                [hash = it->digest, enabled](auto& library, auto&) { library.enable(hash, enabled); });
        PaddockGap(14);
        if (!g_vehicle_remove_confirm) {
            if (PaddockButton("Remove from library", PaddockButtonKind::Link))
                g_vehicle_remove_confirm = true;
        } else {
            PaddockModalText("Remove this car? Your download and saves will be kept.");
            if (PaddockButton("Remove car", PaddockButtonKind::Plain)) {
                g_vehicle_library.job([hash = it->digest](auto& library, auto&) { library.remove(hash); },
                                      "Car removed. Your download was kept.");
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndDisabled();
    } else
        PaddockModalText("This car is no longer installed.");
    PaddockGap(18);
    if (PaddockButton("Close", PaddockButtonKind::Link))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

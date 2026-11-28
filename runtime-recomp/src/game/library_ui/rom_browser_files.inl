RomBrowserState g_rom_browser;

// The launcher owns ROM registration and the platform locations. The browser
// only chooses a candidate for the same validation used by the ROM catalog.
std::filesystem::path RomBrowserHome() {
#if defined(DKR_IOS)
    return dkr::runtime::ios::documents_directory();
#elif defined(_WIN32)
    const wchar_t* home = _wgetenv(L"USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
#if !defined(DKR_IOS)
    return home ? std::filesystem::path(home) : std::filesystem::path{};
#endif
}

void OpenRomBrowser(const std::filesystem::path& selected, ImGuiID opener, ImRect bounds) {
    using Entry = dkr::library_ui::RomBrowser::Entry;
    std::vector<Entry> locations;
    const auto add_directory = [&](const std::filesystem::path& path) {
        std::error_code error;
        if (!path.empty() && std::filesystem::is_directory(path, error) &&
            std::none_of(locations.begin(), locations.end(), [&](const Entry& entry) { return entry.path == path; })) {
            locations.push_back({path, true});
        }
    };

    const auto home = RomBrowserHome();
    if (!home.empty()) {
        add_directory(home / "Downloads");
        add_directory(home / "Documents");
        add_directory(home);
    }
    std::error_code error;
    add_directory(std::filesystem::current_path(error));
#if defined(_WIN32)
    const DWORD drives = GetLogicalDrives();
    for (int drive = 0; drive < 26; ++drive) {
        if ((drives & (1UL << drive)) != 0) {
            std::wstring root = L"A:\\";
            root.front() = static_cast<wchar_t>(L'A' + drive);
            // A removable drive may report no media until the user inserts it.
            locations.push_back({std::filesystem::path(root), true});
        }
    }
#else
    add_directory("/");
#endif

    auto start = selected.parent_path();
    if (start.empty() || !std::filesystem::is_directory(start, error))
        start = locations.empty() ? std::filesystem::path{} : locations.front().path;

    g_rom_browser.folders.open(std::move(start), std::move(locations));
    g_rom_browser.opener = ImGui::GetCurrentWindow();
    g_rom_browser.opener_id = opener;
    g_rom_browser.opener_scope = ImGui::GetCurrentFocusScope();
    g_rom_browser.opener_bounds = bounds;
    g_rom_browser.message.clear();
    g_rom_browser.open = true;
}

void OpenRomBrowser(const std::filesystem::path& selected) {
    OpenRomBrowser(selected, ImGui::GetItemID(), {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()});
}

bool AcceptBrowserRom(const std::filesystem::path& path, std::filesystem::path& selected,
                      std::vector<RomCatalogEntry>& catalog, std::string& status) {
    dkr::runtime::rom::Identity identity{};
    if (!dkr::runtime::ValidateRomForLauncher(path, identity, g_rom_browser.message))
        return false;
    CommitRomSelection(path, identity, selected, catalog, status);
    return true;
}

bool SelectRomWithDialog(std::filesystem::path& selected, std::vector<RomCatalogEntry>& catalog, std::string& status) {
    if (NFD_Init() != NFD_OKAY) {
        g_rom_browser.message = "The system file picker could not be initialized.";
        return false;
    }
    nfdu8char_t* result = nullptr;
    const nfdfilteritem_t filters[] = {{"Nintendo 64 ROM", "z64,v64,n64"}};
    const auto directory = PathUtf8(g_rom_browser.folders.directory());
    const nfdresult_t dialog = NFD_OpenDialogU8(&result, filters, 1, directory.empty() ? nullptr : directory.c_str());
    std::filesystem::path candidate;
    if (dialog == NFD_OKAY) {
        candidate = std::filesystem::u8path(result);
        NFD_FreePathU8(result);
    } else if (dialog == NFD_ERROR) {
        g_rom_browser.message = NFD_GetError();
    }
    NFD_Quit();
    return dialog == NFD_OKAY && AcceptBrowserRom(candidate, selected, catalog, status);
}

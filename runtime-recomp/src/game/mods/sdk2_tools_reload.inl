void DrawSdk2Reload(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft,
                    const std::string& id) {
    if (snapshot.online) {
        ImGui::TextWrapped(
            "Online code changes need everyone to return to the lobby and readmit the same new mod edition. Live replacement is offline only.");
        return;
    }
    ImGui::TextWrapped(
        "Instruction-only offline reload. Both editions must opt in; state layout, imports, hooks, resources and initial data must match. Everything else needs a relaunch.");
    auto& path = draft.text[id + "-path"];
    ImGui::SetNextItemWidth(std::max(80.F, ImGui::GetContentRegionAvail().x - 58));
    ImGui::InputText("##reload-path", path.data(), path.size());
    ImGui::SameLine();
    if (ImGui::Button("Keys", {50, 0}))
        RequestCustomTextKeyboard("New .nrm code package", path.data(), path.size(),
                                  [digest = panel.digest, id](const std::string& value) {
                                      auto& to = g_sdk2_drafts[digest].text[id + "-path"];
                                      to.fill(0);
                                      std::copy_n(value.begin(), std::min(value.size(), to.size() - 1), to.begin());
                                  });
    bool busy = draft.reload_job.valid();
    if (busy && draft.reload_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            draft.reload_package = draft.reload_job.get();
            dkr::sdk2::notice("New edition inspected. Review its identity before running new code.");
        } catch (const std::exception& e) {
            dkr::sdk2::notice(e.what());
        }
        busy = false;
    }
    ImGui::BeginDisabled(busy || !snapshot.development);
    if (ImGui::Button("Read / Review new code", {240, 40})) {
        const auto selected = std::filesystem::u8path(path.data());
        draft.reload_package.reset();
        draft.reload_job = std::async(std::launch::async, [selected] {
            using namespace dkr::mods;
            const auto size = std::filesystem::file_size(selected);
            if (selected.extension() != ".nrm" || !std::filesystem::is_regular_file(selected) ||
                size > code::MaxPackage)
                throw Error("Choose a bounded .nrm package.");
            Bytes bytes(std::size_t(size), 0);
            std::ifstream input(selected, std::ios::binary);
            input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!input || input.peek() != std::char_traits<char>::eof())
                throw Error("Package changed or could not be read completely.");
            return code::inspect(bytes, DKR_RELEASE_VERSION);
        });
    }
    if (draft.reload_package) {
        const auto& package = *draft.reload_package;
        ImGui::TextWrapped("%s / %s / %s", package.id.c_str(), package.version.c_str(), package.digest.c_str());
        ImGui::TextWrapped(
            "This executes trusted mod code. Installed files are unchanged; migration is limited to private VM state. Native operations are forbidden during cleanup/reload.");
        if (ImGui::Button("I trust this code - reload", {260, 40})) {
            if (dkr::sdk2::request_reload({panel.owner, panel.digest, draft.reload_package}, snapshot.generation))
                dkr::sdk2::notice("Reload queued at the next retired offline boundary.");
            else
                dkr::sdk2::notice("The session changed or another reload is pending.");
            draft.reload_package.reset();
        }
    }
    ImGui::EndDisabled();
}

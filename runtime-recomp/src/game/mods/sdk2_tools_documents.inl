void DrawSdk2Document(const dkr::sdk2::Panel& panel, const dkr::sdk2::Snapshot& snapshot, Sdk2PanelDraft& draft,
                      const dkr::mods::code::Json& field) {
    using Json = dkr::mods::code::Json;
    const auto key = field.at("document").get<std::string>();
    const auto label = field.at("label").get<std::string>();
    ImGui::TextWrapped("%s", label.c_str());
    auto [entry, inserted] =
        draft.text.try_emplace(field.at("id").get<std::string>() + "-path", std::array<char, 257>{});
    auto& path = entry->second;
    if (inserted) {
        const auto initial = (g_config_directory / "development" / (panel.id + "-" + key + ".dkrdev")).u8string();
        const std::string value(reinterpret_cast<const char*>(initial.data()), initial.size());
        std::copy_n(value.begin(), std::min(value.size(), path.size() - 1), path.begin());
    }
    ImGui::TextWrapped("File location (.dkrdev). Exports never overwrite an existing file.");
    ImGui::SetNextItemWidth(std::max(80.F, ImGui::GetContentRegionAvail().x - 58));
    ImGui::InputText("##path", path.data(), path.size());
    ImGui::SameLine();
    if (ImGui::Button("Keys", {50, 0}))
        RequestCustomTextKeyboard("Development document file", path.data(), path.size(),
                                  [digest = panel.digest, id = entry->first](const std::string& text) {
                                      auto& target = g_sdk2_drafts[digest].text[id];
                                      target.fill(0);
                                      std::copy_n(text.begin(), std::min(text.size(), target.size() - 1),
                                                  target.begin());
                                  });
    bool busy = draft.file_job.valid();
    if (busy && draft.file_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            const auto result = draft.file_job.get();
            if (result.starts_with("{")) {
                auto value = Json::parse(result);
                if (value.at("schema") != 2 || value.at("package") != panel.id || value.at("digest") != panel.digest ||
                    value.at("kind") != key)
                    throw std::runtime_error("This document belongs to another tool or mod edition.");
                draft.imported = std::move(value);
                dkr::sdk2::notice("Document read. Review it below before applying.");
            } else
                dkr::sdk2::notice(result);
        } catch (const std::exception& e) {
            dkr::sdk2::notice(e.what());
        }
        busy = false;
    }
    const auto document = panel.documents.find(key);
    ImGui::BeginDisabled(busy || document == panel.documents.end());
    if (ImGui::Button("Export document", {210, 40})) {
        const auto selected = std::filesystem::u8path(path.data());
        const auto data = document->second.dump(2);
        draft.file_job = std::async(std::launch::async, [selected, data] {
            if (selected.extension() != ".dkrdev")
                throw std::runtime_error("Use a .dkrdev filename.");
            if (std::filesystem::exists(selected))
                throw std::runtime_error("That file already exists. Choose another name to keep the original.");
            std::filesystem::create_directories(selected.parent_path());
            std::filesystem::path scratch;
            for (unsigned i = 0; i < 16 && scratch.empty(); ++i) {
                const auto candidate =
                    selected.parent_path() /
                    (".sdk2-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     "-" + std::to_string(i));
                if (std::filesystem::create_directory(candidate))
                    scratch = candidate;
            }
            if (scratch.empty())
                throw std::runtime_error("Could not create temporary export storage.");
            const auto temporary = scratch / "document.dkrdev";
            try {
                std::ofstream stream(temporary, std::ios::binary);
                if (!stream)
                    throw std::runtime_error("Could not create that document.");
                stream.write(data.data(), data.size());
                stream.close();
                if (!stream)
                    throw std::runtime_error("The document could not be written completely.");
                // No overwrite_existing: publishing must fail if another file
                // appeared after the initial UI check.
                std::filesystem::copy_file(temporary, selected, std::filesystem::copy_options::none);
                std::filesystem::remove(temporary);
                std::filesystem::remove(scratch);
            } catch (...) {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                std::filesystem::remove(scratch, ignored);
                throw;
            }
            return std::string("Document exported. Source tracks and saves were not changed.");
        });
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(busy || !snapshot.host || !snapshot.development);
    if (ImGui::Button("Read / Review import", {210, 40})) {
        const auto selected = std::filesystem::u8path(path.data());
        draft.file_job = std::async(std::launch::async, [selected] {
            if (selected.extension() != ".dkrdev" || !std::filesystem::is_regular_file(selected) ||
                std::filesystem::file_size(selected) > 32768)
                throw std::runtime_error("Choose a .dkrdev document smaller than 32 KiB.");
            std::ifstream stream(selected, std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(stream)), {});
            if (!stream.eof() && stream.fail())
                throw std::runtime_error("Could not read that document.");
            const auto value = Json::parse(bytes);
            if (value.dump().size() > 16384)
                throw std::runtime_error("The canonical document exceeds 16 KiB.");
            return value.dump();
        });
    }
    if (draft.imported && draft.imported->at("kind") == key) {
        ImGui::TextWrapped("Preview: %s", draft.imported->dump(2).c_str());
        if (ImGui::Button("Queue reviewed import", {210, 40})) {
            unsigned index = 0;
            bool found = false;
            for (const auto& page : panel.schema.at("pages"))
                for (const auto& item : page.at("fields"))
                    if (item.at("type") == "document") {
                        if (!found && item.at("document") == key)
                            found = true;
                        else if (!found)
                            ++index;
                    }
            if (found && dkr::sdk2::upload(panel.owner, index, *draft.imported, panel.digest, snapshot.generation))
                draft.imported.reset();
            else
                dkr::sdk2::notice("The import target changed or the upload queue is busy. No new document was queued.");
        }
        if (ImGui::Button("Discard preview", {210, 40}))
            draft.imported.reset();
    }
    ImGui::EndDisabled();
    if (busy)
        ImGui::TextDisabled("Transferring the selected local file...");
}

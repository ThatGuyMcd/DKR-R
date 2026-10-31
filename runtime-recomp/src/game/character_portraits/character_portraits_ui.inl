SDL_Renderer* g_character_portrait_renderer = nullptr;
dkr::character_portraits::Service g_character_portraits;
struct CharacterPortraitTexture {
    SDL_Texture* texture = nullptr;
    unsigned width = 0, height = 0;
    int used = 0;
};
std::map<std::string, CharacterPortraitTexture> g_character_portrait_textures;
void ReleaseCharacterPortraits() {
    for (auto& [key, entry] : g_character_portrait_textures)
        if (entry.texture)
            SDL_DestroyTexture(entry.texture);
    g_character_portrait_textures.clear();
    g_character_portrait_renderer = nullptr;
}
void DrawCharacterPortrait(const dkr::mods::TrackCatalogItem& item, float width, float height) {
    const auto at = ImGui::GetCursorScreenPos();
    if (g_character_portrait_renderer && ImGui::IsRectVisible({width, height})) {
        dkr::character_portraits::Request request{g_config_directory / "mods" / "legacy",
                                                  item.id,
                                                  item.artifact,
                                                  item.group,
                                                  item.storage,
                                                  item.revision,
                                                  {}};
        const auto key = request.key();
        const int frame = ImGui::GetFrameCount();
        auto found = g_character_portrait_textures.find(key);
        if (found == g_character_portrait_textures.end()) {
            const auto image = g_character_portraits.request(std::move(request));
            if (image && !image->rgba.empty()) {
                if (g_character_portrait_textures.size() >= 64) {
                    auto oldest =
                        std::min_element(g_character_portrait_textures.begin(), g_character_portrait_textures.end(),
                                         [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                    // SDL consumes ImGui texture IDs later in this frame. Do
                    // not destroy an image referenced by the current draw list.
                    if (oldest->second.used < frame - 1) {
                        SDL_DestroyTexture(oldest->second.texture);
                        g_character_portrait_textures.erase(oldest);
                    }
                }
                if (g_character_portrait_textures.size() < 64) {
                    auto* texture = SDL_CreateTexture(g_character_portrait_renderer, SDL_PIXELFORMAT_RGBA32,
                                                      SDL_TEXTUREACCESS_STATIC, int(image->width), int(image->height));
                    if (texture &&
                        SDL_UpdateTexture(texture, nullptr, image->rgba.data(), int(image->width * 4)) != 0) {
                        SDL_DestroyTexture(texture);
                        texture = nullptr;
                    }
                    if (texture) {
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
                        found = g_character_portrait_textures
                                    .emplace(key, CharacterPortraitTexture{texture, image->width, image->height, frame})
                                    .first;
                    }
                }
            }
        }
        if (found != g_character_portrait_textures.end()) {
            auto& entry = found->second;
            entry.used = frame;
            const float ratio = float(entry.width) / entry.height, w = std::min(width, height * ratio), h = w / ratio;
            ImGui::GetWindowDrawList()->AddImage(reinterpret_cast<ImTextureID>(entry.texture),
                                                 {at.x + (width - w) * .5F, at.y + (height - h) * .5F},
                                                 {at.x + (width + w) * .5F, at.y + (height + h) * .5F});
        }
    }
    ImGui::Dummy({width, height});
}

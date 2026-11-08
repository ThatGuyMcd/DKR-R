// Included in the existing UI owner and the avatar lifetime fixture. Only this
// owner creates/retires SDL textures; background downloads publish immutable pixels.
struct OlProfileTexture {
    SDL_Texture* texture = nullptr;
    std::shared_ptr<const dkr::store::Image> pixels;
    int last_frame = -1;
};
std::map<std::string, OlProfileTexture> g_profile_textures;
SDL_Renderer* g_profile_texture_renderer = nullptr;

void ReleaseDiscordProfileTexture() {
    for(auto& [key,entry]:g_profile_textures)if(entry.texture)SDL_DestroyTexture(entry.texture);
    g_profile_textures.clear();g_profile_texture_renderer=nullptr;
}

void DrawOlProfileAvatar(ImDrawList* draw,ImVec2 at,float size,std::string_view name,
                         const std::string& url,bool dimmed) {
    if(!ImGui::IsRectVisible(at,{at.x+size,at.y+size}))return;
    const auto fallback=[&]{DrawOlAvatar(draw,at,size,name,dimmed);};
    const bool preview=url.starts_with("profile-preview:")&&url.size()<=160;
    if((!dkr::profile::avatar_url(url)&&!preview)||!g_mod_store_renderer){fallback();return;}
    if(g_profile_texture_renderer!=g_mod_store_renderer){
        ReleaseDiscordProfileTexture();g_profile_texture_renderer=g_mod_store_renderer;
    }
    auto& store=dkr::store::service();
    const int frame=ImGui::GetFrameCount();
    auto found=g_profile_textures.find(url);
    auto pixels=store.image(url);
    if(pixels&&(!pixels->width||!pixels->height||pixels->width>512||pixels->height>512||
        pixels->rgba.size()!=std::size_t(pixels->width)*pixels->height*4))pixels.reset();
    // The download cache holds fewer images than this texture cache. Its
    // eviction must not hide a picture or start another download each frame.
    if(!pixels&&found!=g_profile_textures.end())pixels=found->second.pixels;
    if(found==g_profile_textures.end()) {
        if(!pixels){if(!preview)store.request_image(url);fallback();return;}
        if(g_profile_textures.size()>=48) {
            const auto victim=std::find_if(g_profile_textures.begin(),g_profile_textures.end(),[&](const auto& row){return row.second.last_frame!=frame;});
            if(victim==g_profile_textures.end()){fallback();return;}
            if(victim->second.texture)SDL_DestroyTexture(victim->second.texture);
            g_profile_textures.erase(victim);
        }
        found=g_profile_textures.emplace(url,OlProfileTexture{}).first;
    }
    auto& entry=found->second;
    if(pixels&&entry.pixels!=pixels&&entry.last_frame!=frame) {
        auto* texture=SDL_CreateTexture(g_mod_store_renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(pixels->width),int(pixels->height));
        if(texture) {
            if(SDL_UpdateTexture(texture,nullptr,pixels->rgba.data(),int(pixels->width*4))||SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND))SDL_DestroyTexture(texture);
            else {if(entry.texture)SDL_DestroyTexture(entry.texture);entry.texture=texture;entry.pixels=pixels;}
        }
    }
    if(!entry.texture){g_profile_textures.erase(found);fallback();return;}
    entry.last_frame=frame;
    // Picture and initial are exclusive. A neutral backing also prevents
    // initials showing through transparent pixels or the dimmed image.
    draw->AddCircleFilled({at.x+size*.5F,at.y+size*.5F},size*.5F,PaddockCol(0x061D2C),32);
    draw->AddImageRounded(reinterpret_cast<ImTextureID>(entry.texture),at,{at.x+size,at.y+size},{0,0},{1,1},
                          PaddockCol(0xFFFFFF,dimmed?170U:255U),size*.5F);
    draw->AddCircle({at.x+size*.5F,at.y+size*.5F},size*.5F-1.F,PaddockCol(0xFFFFFF,46U),32,2.F);
}

void DrawOlSelfAvatar(ImDrawList* draw,ImVec2 at,float size,std::string_view name) {
    DrawOlProfileAvatar(draw,at,size,name,g_discord_avatar_url,false);
}

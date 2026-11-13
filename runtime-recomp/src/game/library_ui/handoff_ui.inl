// The SDL owner and RT64 owner use the same caption/logo drawing. Only the
// cloud texture belongs to a different renderer; no SDL GPU objects cross over.
void DrawLauncherFade(const dkr::library_ui::LauncherFade& fade,double now) {
    const float alpha=fade.opacity(now);
    if(alpha<=0)return;
    const auto* viewport=ImGui::GetMainViewport();
    auto* draw=ImGui::GetForegroundDrawList();
    const ImVec2 end{viewport->Pos.x+viewport->Size.x,viewport->Pos.y+viewport->Size.y};
    if(fade.frame())draw->AddImage(reinterpret_cast<ImTextureID>(fade.frame()),viewport->Pos,end,{0,0},{1,1},
        IM_COL32(255,255,255,int(255*alpha)));
    else draw->AddRectFilled(viewport->Pos,end,IM_COL32(12,24,34,int(255*alpha)));
}

void DrawRuntimeStartupClouds(double seconds) {
    struct Clouds {
        std::vector<unsigned char> pixels;
        int width=0,height=0;
        dkr::launcher_themes::Theme theme=dkr::launcher_themes::Theme::Original;
        bool tried=false;
    };
    static Clouds clouds;
    const auto theme=dkr::launcher_themes::displayed;
    if(!clouds.tried||clouds.theme!=theme) {
        clouds={};clouds.tried=true;clouds.theme=theme;
        int w=0,h=0,c=0;
        auto* pixels=stbi_load(PathUtf8(LauncherBackgroundPath()).c_str(),&w,&h,&c,4);
        if(pixels&&w>0&&h>0&&w<=8192&&h<=8192) {
            const float scale=std::min({1.F,512.F/w,512.F/h});
            clouds.width=std::max(1,int(w*scale));clouds.height=std::max(1,int(h*scale));
            clouds.pixels.resize(std::size_t(clouds.width)*clouds.height*4);
            for(int y=0;y<clouds.height;++y)for(int x=0;x<clouds.width;++x) {
                const float sx=(x+.5F)*w/clouds.width-.5F,sy=(y+.5F)*h/clouds.height-.5F;
                const int x0=std::clamp(int(std::floor(sx)),0,w-1),y0=std::clamp(int(std::floor(sy)),0,h-1);
                const int x1=std::min(x0+1,w-1),y1=std::min(y0+1,h-1);
                const float fx=std::clamp(sx-x0,0.F,1.F),fy=std::clamp(sy-y0,0.F,1.F);
                for(int channel=0;channel<4;++channel) {
                    const auto sample=[&](int xx,int yy){return float(pixels[(std::size_t(yy)*w+xx)*4+channel]);};
                    clouds.pixels[(std::size_t(y)*clouds.width+x)*4+channel]=static_cast<unsigned char>(
                        (sample(x0,y0)*(1-fx)+sample(x1,y0)*fx)*(1-fy)+(sample(x0,y1)*(1-fx)+sample(x1,y1)*fx)*fy);
                }
            }
            dkr::launcher_themes::tint(clouds.pixels,clouds.width,clouds.height,theme);
        }
        stbi_image_free(pixels);
    }
    const auto at=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(at,{at.x+size.x,at.y+size.y},IM_COL32(50,145,226,255));
    if(clouds.pixels.empty())return;
    const auto texture=dkr::store::overlay::image("dkr-startup-clouds-"+std::to_string(int(theme)),
        clouds.width,clouds.height,clouds.pixels);
    if(!texture.id)return;
    const float height=size.y*1.03F,width=height*float(clouds.width)/clouds.height;
    for(const auto& tile:dkr::runtime::launcher::background_tiles(size.x,size.y,width,height,
            seconds*12.5)) {
        draw->AddImage(texture.id,{at.x+tile.left,at.y+tile.top},{at.x+tile.right,at.y+tile.bottom},
            {tile.mirrored?1.F-tile.u0:tile.u0,tile.v0},{tile.mirrored?1.F-tile.u1:tile.u1,tile.v1});
    }
}

bool DrawRuntimeStartingGame() {
    auto& intro=dkr::library_ui::runtime_intro;
    const auto online=dkr::runtime::netplay::session().runtime_view();
    const auto owned=dkr::runtime::netplay::experimental::runtime_view();
    const bool expects_owned=online.launch_descriptor&&online.launch_descriptor->synchronization==
        dkr::runtime::netplay::SynchronizationMode::ExperimentalRollback;
    const bool initial=!online.owned_match_ending&&(dkr::library_ui::initial_sync(online.active,expects_owned,
        owned.active,owned.initial_admission,owned.epoch)||
        (online.active&&!expects_owned&&online.state==dkr::runtime::netplay::ConnectionState::Loading));
    // Direct online diagnostic/command-line launches also use the same UI.
    if(!intro.active()&&intro.may_begin()&&initial)intro.begin(true);
    if(!intro.active())return false;
    const bool ready=intro.online()?online.active&&
        (owned.active?!owned.initial_admission&&owned.frame>0:
            !expects_owned&&online.state==dkr::runtime::netplay::ConnectionState::Running):true;
    const float alpha=intro.opacity(ImGui::GetTime(),ready);
    if(alpha<=0)return false;
    if(intro.online()&&(!online.active||online.owned_match_ending)){intro.clear();return false;}
    BeginMainWindow("DKR-R Starting",ImGuiWindowFlags_NoBackground);
    auto* draw=ImGui::GetWindowDrawList();const int first=draw->VtxBuffer.Size;
    const double seconds=intro.advance(ImGui::GetIO().DeltaTime);
    DrawRuntimeStartupClouds(seconds);
    dkr::launcher_themes::weather(draw,ImGui::GetWindowPos(),ImGui::GetWindowSize(),
        dkr::launcher_themes::displayed,seconds,dkr::launcher_themes::preferences.calendar);
    dkr::mods::ModLaunchView state;
    state.stage=initial?"Syncing game...":"Starting game...";
    const bool pressed=DrawStartingContent(state,intro.online(),intro.online(),seconds);
    for(int i=first;i<draw->VtxBuffer.Size;++i) {
        auto& colour=draw->VtxBuffer[i].col;
        const unsigned a=(colour>>IM_COL32_A_SHIFT)&255U;
        colour=(colour&~IM_COL32_A_MASK)|(unsigned(a*alpha)<<IM_COL32_A_SHIFT);
    }
    ImGui::End();
    static bool logged=false;
    if(!logged){logged=true;std::fprintf(stderr,"[startup][presentation] shared RT64 starting/syncing screen; real admission gates retained\n");}
    if(pressed) {
        intro.clear();g_lifecycle_request.store(dkr::runtime::ui::LifecycleRequest::StopGame,std::memory_order_release);
        g_overlay_visible=false;ultramodern::quit();
    }
    return true;
}

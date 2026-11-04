// Explicit opt-in checks use isolated profiles. Ordinary startup never arms
// them or redirects a page.
bool g_library_navigation_check=false;
std::array<std::optional<std::chrono::steady_clock::time_point>,2> g_library_check_start;
std::array<unsigned,2> g_library_check_seen{};
int g_library_check_previous_page=0;
bool g_library_check_launcher_done=false;
std::filesystem::path g_launcher_layout_root;
int g_launcher_layout_step=0;
bool g_launcher_layout_sized=false;
bool g_localisation_cycle_check=false;
double g_launcher_layout_at=0;
constexpr std::array<int,44> kLauncherLayoutPages{0,6,6,6,6,6,10,1,2,3,4,5,8,9,7,10,10,10,10,10,10,10,10,3,3,3,3,6,6,6,4,4,4,4,5,5,5,5,5,1,7,10,10,10};
bool g_surface_scroll_bottom=false;
#include "control_check.inl"
#include "startup_check.inl"
#include "handoff_check.inl"

void LibrarySelectCheckView(int step,int& page) {
    if(LibraryManualHandoffCheck()){page=0;return;}
    if(g_localisation_cycle_check) {
        const auto languages=dkr::localisation::languages();
        dkr::localisation::select(languages[std::size_t(step)%languages.size()].code);
    }
    page=kLauncherLayoutPages[std::size_t(step)];
    if(page==kPageControls)g_controls_section=step>=23&&step<=26?step-22:0;
    g_compact_card_capture=step==22;
    g_surface_scroll_bottom=step==39||step==40;
    if(step>=27&&step<=29)g_mods_page.section=g_mods_page.entry_section=step-26;
    if(page==kPageSaveManager){g_saves_page.scope=step==32?3:step==33?1:0;g_saves_page.section=step==30?1:step==31?2:0;}
    if(page==kPageOnlineMp) {
        const std::array<OlSection,5> sections{OlSection::Play,OlSection::Settings,OlSection::Profile,OlSection::Friends,OlSection::Overlays};
        g_online_page.section=step>=34&&step<=38?sections[std::size_t(step-34)]:OlSection::Lobbies;
    }

    if(step>=1&&step<=5) {
        g_mods_page.section=g_mods_page.entry_section=step==1?-1:0;
        g_mods_page.category=std::max(0,step-2);
    }
    if(page==kPageTextures) {
        page=kPageModsHacks;
        g_mods_page.section=g_mods_page.entry_section=dkr::library_ui::TexturesSection;
    }
    if(page==kPageModLibrary) {
        const std::array<int,8> pages{4,2,1,3,4,4,0,0};
        g_mod_store.page=step>=15&&step<=22?pages[std::size_t(step-15)]:0;
        g_mod_store.installed_section=step==19?1:step==20?2:0;
        g_mod_store.detail=step==21||step>=41;
        g_mod_store.detail_tab=step==43?4:step==42?1:0;
        if(g_mod_store.detail) {
            const auto store=dkr::store::service().view();
            if(store->items&&store->items->is_array()&&!store->items->empty())
                g_mod_store.selected=StoreText((*store->items)[0],"id",160);
        }
    }
    LibraryControlsCheckRoute(step,page);
}

bool LauncherLayoutCheckRoute(int& page) {
    if(g_launcher_layout_root.empty())return false;
    if(g_launcher_layout_step>=int(kLauncherLayoutPages.size()))return true;
    LibrarySelectCheckView(g_launcher_layout_step,page);
    return true;
}

// Captures only an explicitly authenticated, disposable launcher test profile.
// Ordinary startup performs no pixel readback, window resize or image writes.
void LauncherLayoutCheckCapture(SDL_Renderer* renderer) {
    if(g_launcher_layout_root.empty()||g_launcher_layout_step>=int(kLauncherLayoutPages.size()))return;
    if(!g_launcher_layout_sized) {
        const std::string_view size=std::getenv("DKR_OWNED_CHECK_LAYOUT_UI");
        const bool deck=size=="deck",wide=size=="wide";
        auto* window=SDL_RenderGetWindow(renderer);
        SDL_SetWindowFullscreen(window,0);SDL_SetWindowSize(window,deck?1280:wide?2560:1920,deck?800:wide?1440:1080);
        g_launcher_layout_sized=true;g_launcher_layout_at=PaddockClock();return;
    }
    if(PaddockClock()-g_launcher_layout_at<1.0)return;
    int width=0,height=0;
    if(SDL_GetRendererOutputSize(renderer,&width,&height)!=0||width<1||height<1||width>4096||height>2160)return;
    std::vector<unsigned char> pixels(std::size_t(width)*std::size_t(height)*4);
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*4)!=0)return;
    auto* surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),width,height,32,width*4,SDL_PIXELFORMAT_RGBA32);
    if(!surface)return;
    const auto path=g_launcher_layout_root/("layout-"+std::to_string(g_launcher_layout_step)+".bmp");
    const bool saved=SDL_SaveBMP(surface,path.string().c_str())==0;SDL_FreeSurface(surface);
    if(!saved)return;
    std::fprintf(stderr,"[launcher-layout][isolated-check] step=%d page=%d width=%d height=%d captured\n",
        g_launcher_layout_step,kLauncherLayoutPages[std::size_t(g_launcher_layout_step)],width,height);
    std::fprintf(stderr,"[localisation][isolated-check] step=%d language=%s atlas=%dx%d texture=%p\n",
        g_launcher_layout_step,dkr::localisation::active().data(),ImGui::GetIO().Fonts->TexWidth,
        ImGui::GetIO().Fonts->TexHeight,ImGui::GetIO().Fonts->TexID);
    if(g_localisation_cycle_check&&g_launcher_layout_step+1==int(kLauncherLayoutPages.size()))SaveSettings();
    ++g_launcher_layout_step;g_launcher_layout_at=PaddockClock();
}

bool g_surface_probe=false;
int g_overlay_layout_step=0;
double g_overlay_layout_at=-1;
bool g_overlay_layout_sized=false;
void LibrarySurfaceProbe(bool live,int page,float width,float height) {
    if(!live||!g_surface_probe||g_overlay_layout_step>=int(kLauncherLayoutPages.size()))return;
    if(!g_overlay_layout_sized) {
        const char* value=std::getenv("DKR_OWNED_CHECK_LAYOUT_UI");
        const std::string_view size=value?value:"desktop";
        if(auto* window=static_cast<SDL_Window*>(dkr::runtime::platform::sdl_window())) {
            SDL_SetWindowFullscreen(window,0);
            SDL_SetWindowSize(window,size=="deck"?1280:size=="wide"?2560:1920,size=="deck"?800:size=="wide"?1440:1080);
        }
        g_overlay_layout_sized=true;g_overlay_layout_at=PaddockClock();return;
    }
    if(PaddockClock()-g_overlay_layout_at<.6)return;
    std::fprintf(stderr,"[ui-surface][isolated-check] surface=overlay step=%d page=%d width=%.1f height=%.1f viewport=%.0fx%.0f rendered\n",
        g_overlay_layout_step,page,width,height,ImGui::GetIO().DisplaySize.x,ImGui::GetIO().DisplaySize.y);
    ++g_overlay_layout_step;g_overlay_layout_at=PaddockClock();g_overlay_focus_requested=true;
}
void LibraryNavigationCheckInit() {
    if(const char* value=std::getenv("DKR_OWNED_CHECK_UI_SURFACES");value&&std::string_view(value)=="1") {
        g_surface_probe=g_library_navigation_check=true;
        g_store_overlay_test=true;g_store_overlay_test_after=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    }

    if(const char* value=std::getenv("DKR_OWNED_CHECK_MODS_UI");value&&std::string_view(value)=="1")
        g_library_navigation_check=true;
}

void LibraryNavigationCheckLauncherInit(const std::filesystem::path& root) {
    const char* value=std::getenv("DKR_LAUNCHER_UI_CHECK_TOKEN");
    if(!value || !root.is_absolute() ||
       !root.filename().string().starts_with("dkr-ui-navigation-check-"))return;
    const std::string token(value);
    if(token.size()!=64 || token.find_first_not_of("0123456789abcdef")!=token.npos)return;
    std::ifstream marker(root/"navigation-check-token");
    std::string expected;std::getline(marker,expected);
    if(marker && expected==token) {
        const char* localisation=std::getenv("DKR_OWNED_CHECK_LOCALISATION");
        if(localisation) {
            g_localisation_cycle_check=std::string_view(localisation)=="cycle";
            dkr::localisation::debug_pseudo(std::string_view(localisation)=="pseudo");
        }
        g_library_navigation_check=true;
        if(const char* layout=std::getenv("DKR_OWNED_CHECK_LAYOUT_UI");layout&&
           (std::string_view(layout)=="desktop"||std::string_view(layout)=="deck"||std::string_view(layout)=="wide"))g_launcher_layout_root=root;
        const char* controls=std::getenv("DKR_LIBRARY_CONTROLS_CHECK");
        g_library_controls_check=!g_launcher_layout_root.empty()&&controls&&std::string_view(controls)=="1";
    }
}

void LibraryNavigationCheckRoute(bool game,int& page) {
    if(!g_library_navigation_check || (!game&&g_library_check_launcher_done))return;
    if(!game&&LauncherLayoutCheckRoute(page))return;
    const auto now=std::chrono::steady_clock::now();
    if(game && (!g_store_overlay_test || now<g_store_overlay_test_after+std::chrono::seconds(7)))return;
    auto& start=g_library_check_start[game?1:0];
    if(!start) {start=now;if(!game)g_library_check_previous_page=page;}
    const int step=int(std::chrono::duration<double>(now-*start).count()/0.25);
    const bool feedback_check=std::getenv("DKR_OWNED_CHECK_FEEDBACK_UI")!=nullptr;
    const int sections=int(dkr::library_ui::ModsSections.size())+1;
    if(feedback_check&&step>=sections&&step<sections+4) {
        page=kPageControls;g_controls_section=0;g_selected_player=std::size_t(step-sections);return;
    }
    if(step>=(feedback_check?sections+4:sections)) {
        if(!game){page=g_library_check_previous_page;g_library_check_launcher_done=true;}
        return;
    }
    page=kPageModsHacks;
    g_mods_page.section=g_mods_page.entry_section=step-1;
}

void LibraryNavigationCheckRendered(bool game) {
    if(!g_library_navigation_check)return;
    const int bit=g_mods_page.section+1;
    if(bit<0||bit>int(dkr::library_ui::ModsSections.size()))return;
    auto& seen=g_library_check_seen[game?1:0];
    if(seen&(1U<<bit))return;
    seen|=1U<<bit;
    std::fprintf(stderr,"[library-ui][isolated-check] surface=%s section=%d labels=%zu rendered\n",
        game?"overlay":"launcher",g_mods_page.section,dkr::library_ui::ModsSections.size());
}

void LibraryNavigationCheckOverlayRoute() {
    int page=g_overlay_page.load();
    LibraryRedirectTextures(page);
    if(g_surface_probe) {
        if(g_overlay_layout_step<int(kLauncherLayoutPages.size()))LibrarySelectCheckView(g_overlay_layout_step,page);
    } else LibraryNavigationCheckRoute(true,page);
    g_overlay_page.store(page);
    if(g_surface_probe)g_overlay_sidebar_selection.store(page);
}

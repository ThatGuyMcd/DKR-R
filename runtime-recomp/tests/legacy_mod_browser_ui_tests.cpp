// Headless layout checks of the actual MODS / HACKS library drawing code.
// No SDL window, renderer, user configuration, ROM or filesystem mutation.
#include "mods/legacy_mod_library.hpp"
#include "mods/legacy_mod_browser.hpp"
#include "custom_tracks.hpp"
#include "generated/racing_banana_font.h"
#include "generated/jumpman_font.h"
#include "generated/selawik_font.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>

namespace dkr::runtime::support {bool open_directory(const std::filesystem::path&,std::string&){return false;}}
namespace {
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct ModBrowserState {
    char search[160]{};
    int sort=0,state=0,compatibility=0,visibility=0,format=0;
    std::string source,manage_id,remove_id,hide_id;
    std::shared_ptr<const dkr::mods::TrackCatalogView> snapshot;
    std::vector<dkr::mods::browser::Card> all;
    std::vector<int> shown;
    std::string filter_key,layout_key;
    std::vector<float> row_heights;
};
std::array<ModBrowserState,2> g_mod_browsers;
unsigned g_mod_browser_revision=1;std::uint64_t g_font_generation=1;
std::filesystem::path g_config_directory;std::string g_legacy_import_status;
dkr::mods::ModLibrary g_legacy_imports;
int g_race_button_flat=0;ImFont* g_race_button_flat_font=nullptr;
const ImVec4 kAccent{0,1,1,1},kWarm{1,0.7F,0,1};
std::string FormatManagedTexturePackSize(std::uintmax_t bytes){return std::to_string(bytes)+" B";}
enum class TextEntryTarget{CustomTrackSearch,CustomCharacterSearch};
void RequestTextEntryKeyboard(TextEntryTarget){}
struct RomEntry{std::filesystem::path path;};
std::vector<RomEntry> LoadRomCatalog(){return {};}
#include "../src/game/runtime_ui_paddock.inl"
#include "../src/game/runtime_mod_library_ui.inl"

void fonts(float size) {
    auto& io=ImGui::GetIO();
    static const ImWchar text_ranges[]{0x20,0x2f,0x3a,0xff,0};
    static const ImWchar digits[]{0x30,0x39,0};
    const auto racing=[&](float px) {
        ImFontConfig base{};base.FontDataOwnedByAtlas=false;base.GlyphRanges=text_ranges;
        auto* font=io.Fonts->AddFontFromMemoryTTF(const_cast<char*>(dkr_racing_banana_font),static_cast<int>(dkr_racing_banana_font_size),px,&base,text_ranges);
        ImFontConfig merge{};merge.FontDataOwnedByAtlas=false;merge.MergeMode=true;merge.GlyphRanges=digits;
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(dkr_jumpman_font),static_cast<int>(dkr_jumpman_font_size),px*1.42F,&merge,digits);
        return font;
    };
    io.FontDefault=racing(size);
    for(std::size_t index=0;index<kPaddockSignSizes.size();++index)g_paddock_sign[index]=racing(kPaddockSignSizes[index]*size/19);
    LoadPaddockReadingFonts(io.Fonts);
    unsigned char* pixels;int width,height;io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);check(pixels!=nullptr,"Font atlas failed.");
    // A real texture id, so draw commands that lose it are detectable.
    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<std::intptr_t>(1)));
}

std::shared_ptr<dkr::mods::TrackCatalogView> catalogue() {
    auto view=std::make_shared<dkr::mods::TrackCatalogView>();
    for(int n=0;n<200;++n) {
        dkr::mods::TrackCatalogItem item;item.id=std::to_string(1000+n);item.name=n%3==0?
            "A Very Long Custom Name With All Its Words Preserved - Rainbow Road Special Anniversary Edition 2026":
            n%3==1?"AnExtremelyLongUnbrokenCustomCharacterOrTrackNameThatMustNeverClipOrBeTruncated123456789":"Zebra Track";
        item.revision="us.v77";item.source_name=n%2?"A Source Pack":"Another Pack.zip";item.enabled=n<2;
        item.managed_bytes=1000U*static_cast<unsigned>(n);item.vehicles=n%8;
        if(n%3==1)item.details="Car-only; other vehicle assets are not supplied.";
        if(n%5==0)item.details=std::string(900,'W');
        view->tracks.push_back(item);
    }
    return view;
}

std::vector<DkrLibraryTrack> native_tracks() {
    std::vector<DkrLibraryTrack> tracks;
    for(int n=0;n<4;++n) {
        DkrLibraryTrack track;track.id="native-"+std::to_string(n);track.name=n?"Crystal Caverns":"Zebra Coast";
        track.author=n%2?"TrackSmith":"";track.source=track.id+".dkrmap";track.bytes=n*4096U;track.hd_textures=n==3;
        tracks.push_back(track);
    }
    return tracks;
}

void frame(float width,const std::function<void()>& body) {
    ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({width,780});
    ImGui::Begin("Browser",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    body();
    ImGui::PopStyleVar();ImGui::End();ImGui::Render();
}

// Every wrapped line of every card fits the card's text column.
void check_card_text(const std::vector<LibraryCardView>& views,float card_width) {
    const float inner=card_width-36;
    for(const auto& view:views) {
        for(const auto& line:PaddockWrapBalanced(PaddockSign(25,1.15F),view.title,inner))check(line.width<=inner+0.5F,"A card title overflows.");
        for(const auto& line:PaddockWrap(PaddockReading(13,false,1.55F),view.note,inner))check(line.width<=inner+0.5F,"A card note overflows.");
        check(MeasureLibraryCard(view,card_width)>=180,"A card is implausibly short.");
    }
}
}

int main() {
    try {
        const auto view=catalogue();
        const auto tracks=native_tracks();
        unsigned rendered=0;
        for(const float size:{19.0F,24.0F})for(const float width:{360.0F,750.0F,1150.0F,1700.0F})for(const bool characters:{false,true}) {
            ImGui::CreateContext();g_mod_browsers={};g_mods_page={};fonts(size);
            auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={width+80,800};io.DeltaTime=1.0F/60;
            io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::GetStyle().WindowPadding={0,0};ImGui::GetStyle().FramePadding={16,11};
            dkr::mods::ModLibraryView mods;mods.tracks=mods.characters=view;
            auto& state=g_mod_browsers[characters?1:0];
            const std::size_t total=200+(characters?0:tracks.size());
            for(int step=0;step<3;++step)frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
            check(state.all.size()==200,"Cards lost during layout.");
            check(state.shown.size()==total,"Unfiltered library is incomplete.");
            const int columns=std::max(static_cast<int>((width+kModsCardGap)/(265+kModsCardGap)),1);
            check(state.row_heights.size()==(total+columns-1)/columns,"Card rows are missing.");
            const float card_width=std::floor((width-kModsCardGap*(columns-1))/columns);
            const auto& views=g_library_card_views[characters?1:0];
            check(views.size()==total,"Card descriptions are missing.");
            check_card_text(views,card_width);
            for(std::size_t index=0;index<views.size();++index)
                check(MeasureLibraryCard(views[index],card_width)<=state.row_heights[index/columns]+0.5F,"A card is taller than its row.");
            ++rendered;

            // Search, format and sort filters.
            std::snprintf(state.search,sizeof(state.search),"zebra");
            frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
            check(state.shown.size()==(characters?66U:67U),"Search does not match names.");
            state.search[0]=0;
            if(!characters) {
                state.format=2;
                frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
                check(state.shown.size()==tracks.size(),"The DKR format filter is wrong.");
                for(const int index:state.shown)check(index<0,"A legacy card passed the DKR filter.");
                state.format=1;
                frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
                check(state.shown.size()==200,"The legacy format filter is wrong.");
                state.format=0;
            }
            state.sort=2;
            frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
            std::uint64_t previous=UINT64_MAX;
            for(const int index:state.shown) {
                const auto entry=ResolveLibraryEntry(state,tracks,index);
                const auto bytes=entry.dkr?entry.dkr->bytes:entry.legacy->item.managed_bytes;
                check(bytes<=previous,"Largest-first sorting is wrong.");previous=bytes;
            }
            state.sort=0;state.state=1;
            frame(width,[&]{DrawModCardBrowser(width,characters,mods,false,tracks,{});});
            check(state.shown.size()==2+(characters?0:tracks.size()),"The active filter is wrong.");
            ResetLibraryFilters(state);
            check(!LibraryFiltered(state),"Filters did not reset.");

            // A long-notes modal through the production modal renderer.
            state.all.front().item.details=std::string(3500,'W');state.manage_id=state.all.front().item.id;
            for(int step=0;step<3;++step) {
                ImGui::NewFrame();ImGui::Begin("Modal owner");if(!step)ImGui::OpenPopup("Manage custom mod");
                DrawModManagement(state,characters,true);ImGui::End();ImGui::Render();
            }
            bool modal=false;
            for(auto* window:ImGui::GetCurrentContext()->Windows)if(window->Active&&(window->Flags&ImGuiWindowFlags_Modal)) {
                modal=true;check(window->Size.y<=io.DisplaySize.y-47,"Manage modal exceeds viewport.");
                check(!(window->Flags&ImGuiWindowFlags_NoScrollbar),"Long mod notes cannot scroll.");
                check(window->ContentSize.x<=window->Size.x-window->WindowPadding.x*2-window->ScrollbarSizes.x+1,"Modal text clips horizontally.");
                for(const auto& command:window->DrawList->CmdBuffer)
                    if(command.ElemCount)check(command.TextureId==io.Fonts->TexID,"Modal lost its font texture.");
            }
            check(modal,"Manage modal did not open.");
            check(g_race_button_flat==0,"A flat button scope leaked.");
            ImGui::DestroyContext();
        }
        check(rendered>0,"No libraries were checked.");

        // Native details and uninstall confirmation remain scrollable and fit
        // small launchers, including long track names and locked operations.
        for(const float width:{360.0F,750.0F})for(const bool confirm:{false,true}) {
            ImGui::CreateContext();g_mods_page={};fonts(19);
            auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
            io.DisplaySize={width,600};io.DeltaTime=1.0F/60;
            auto native=native_tracks();native.front().installed=true;
            native.front().name=std::string(180,'W');
            g_mods_page.dkr_details_id=native.front().id;
            g_mods_page.request_dkr_details=true;
            for(int step=0;step<4;++step) {
                ImGui::NewFrame();ImGui::Begin("Native modal owner");
                if(step)g_mods_page.dkr_remove_confirm=confirm;
                DrawDkrTrackDetails(native,false,true);ImGui::End();ImGui::Render();
            }
            auto* modal=ImGui::FindWindowByName("Track details");
            check(modal&&modal->Active,"Native track modal did not open.");
            check(modal->Size.y<=io.DisplaySize.y-47,"Native track modal exceeds viewport.");
            check(!(modal->Flags&ImGuiWindowFlags_NoScrollbar),"Native track modal cannot scroll.");
            check(modal->ContentSize.x<=modal->Size.x-modal->WindowPadding.x*2-modal->ScrollbarSizes.x+1,
                  "Native track modal clips horizontally.");
            check(g_mods_page.uninstall_track_request.empty(),"Opening a modal requested an uninstall.");
            ImGui::DestroyContext();
        }

        // Word wrap: unbroken words break, balance keeps the line count.
        ImGui::CreateContext();fonts(19);ImGui::GetIO().DisplaySize={800,600};ImGui::NewFrame();
        const auto type=PaddockReading(14);
        const std::string word(200,'M');
        for(const auto& line:PaddockWrap(type,word,120))check(line.width<=120.5F&&line.end>line.begin,"A long word overflowed.");
        const std::string_view sentence="Choose the folder containing your tracks to start testing right away";
        check(PaddockWrapBalanced(type,sentence,300).size()==PaddockWrap(type,sentence,300).size(),"Balanced wrap changed the line count.");
        check(PaddockWrap(type,"one\ntwo",500).size()==2,"Newlines do not break.");
        ImGui::EndFrame();ImGui::DestroyContext();
        std::cout<<checks<<" headless real-font library, filter and modal checks passed.\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

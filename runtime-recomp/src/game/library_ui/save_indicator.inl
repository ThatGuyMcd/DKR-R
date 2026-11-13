// Reads one already-selected EEPROM on a worker. It never prepares a launch,
// seeds a mod save, walks directories or changes the save owner's route.
struct PlaySavePreviewData {
    std::optional<dkr::runtime::saves::codec::SaveImage> image;
    std::string_view status;
};
struct PlaySavePreview {
    std::filesystem::path path;
    std::future<std::pair<std::filesystem::path,PlaySavePreviewData>> pending;
    PlaySavePreviewData summary;
    double next=0;
};
PlaySavePreview g_play_save_preview;
PlaySavePreviewData PlayReadSavePreview(const std::filesystem::path& path) {
    namespace codec=dkr::runtime::saves::codec;
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file)return {{},"No saved progress for this session yet."};
    if(file.tellg()!=std::streamoff(codec::kImageSize))return {{},"Save preview unavailable. Check Save Manager."};
    std::array<std::uint8_t,codec::kImageSize> bytes{};file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    codec::SaveImage image;
    if(!file||!codec::decode(bytes,image))return {{},"Save preview unavailable. Check Save Manager."};
    return {std::move(image),{}};
}
void DrawPlaySaveSummary(float width,bool live) {
    using namespace dkr::library_ui;
    namespace saves=dkr::runtime::saves;
    SaveScope scope=SaveScope::Vanilla;std::filesystem::path path;
    bool shared_modded=false;
    const bool lobby=dkr::runtime::netplay::session().active();
    if(live) {
        path=saves::runtime_save_path();
        scope=running_save_scope(g_config_directory,path,saves::runtime_save_selection().online);
        shared_modded=scope==SaveScope::Vanilla&&dkr::session_content::running_modded.load();
    } else if(lobby)scope=SaveScope::Online;
    else if(dkr::session_content::next_enabled.load()) {
        const auto mods=g_legacy_imports.snapshot();
        for(const auto& catalogue:{mods.tracks,mods.characters})if(catalogue)
            for(const auto& item:catalogue->tracks)shared_modded|=item.enabled&&!item.hidden;
        for(const auto& item:g_code_library.view()->items)shared_modded|=item.selected&&item.enabled&&item.approved&&item.compatibility.empty();
        for(const auto& item:g_vehicle_library.view()->items)shared_modded|=item.enabled;
        for(const auto& item:dkr::runtime::custom_tracks::tracks())shared_modded|=item.enabled;
        for(const auto& item:dkr::runtime::texture_packs::snapshot())shared_modded|=item.enabled&&item.compatible;
        shared_modded|=dkr::runtime::magic_codes::selected_mask()!=0;
    }
    const auto indicator=shared_modded?SaveScope::Modded:scope;
    const char* heading=indicator==SaveScope::Online?"ONLINE ADVENTURE":indicator==SaveScope::Modded?"MODDED ADVENTURE":"OFFLINE ADVENTURE";
    PaddockText(PaddockReading(16.F,true,1.45F),PaddockRgb(0xFFF6DA),dkr::localisation::source(heading),width);
    PaddockGap(6.F);
    const auto at=ImGui::GetCursorScreenPos();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(at,{at.x+62.F,at.y+22.F},PaddockCol(0x041B26),11.F);
    constexpr std::array<unsigned,3> colours{0x24D493,0xFFC04D,0x57CDE8};
    for(int i=0;i<3;++i) {
        const bool active=int(indicator)==i+1;
        draw->AddCircleFilled({at.x+11.F+20.F*i,at.y+11.F},6.F,PaddockCol(active?colours[i]:0x304752),14);
    }
    const auto save_caption=dkr::localisation::source(shared_modded?std::string_view("Modded game - shared personal save"):save_label(scope));
    const std::string label=dkr::localisation::format(live?"save.using":"save.next",{{"save",save_caption}});
    const auto type=PaddockReading(14.F,true,1.4F);
    const float label_width=std::max(1.F,width-72.F);
    const float label_height=PaddockTextHeight(type,label,label_width);
    PaddockTextStyle label_style;label_style.colour=PaddockCol(0xFFF3C9);
    PaddockTextAt(draw,type,{at.x+72.F,at.y+1.F},label_width,label,label_style);
    ImGui::Dummy({width,std::max(24.F,label_height)});PaddockGap(6.F);
    std::string preview;
    if(scope==SaveScope::Vanilla) {
        const auto& save=CachedSaveManagerView();
        preview=save.image?dkr::localisation::save_preview(*save.image):
            std::string(dkr::localisation::source(save.adventure.exists?"Save preview unavailable. Check Save Manager.":"No offline Adventure save yet."));
    } else if(!live) {
        preview=std::string(dkr::localisation::source("The host supplies a separate online save when the match starts."));
    } else if(path.empty())preview=std::string(dkr::localisation::source("Waiting for the session save owner."));
    else {
        auto& cache=g_play_save_preview;
        if(cache.pending.valid()&&cache.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
            try{auto result=cache.pending.get();if(result.first==path){cache.path=result.first;cache.summary=std::move(result.second);}}
            catch(...){cache.path=path;cache.summary={{},"Save preview unavailable. Check Save Manager."};}
        }
        if(!cache.pending.valid()&&(cache.path!=path||PaddockClock()>=cache.next)) {
            cache.next=PaddockClock()+2;
            cache.pending=std::async(std::launch::async,[path]{return std::pair{path,PlayReadSavePreview(path)};});
        }
        preview=cache.path==path?(cache.summary.image?dkr::localisation::save_preview(*cache.summary.image):
            std::string(dkr::localisation::source(cache.summary.status))):std::string(dkr::localisation::source("Reading this session's saved progress..."));
    }
    PaddockText(PaddockReading(14.F,false,1.5F),PaddockRgb(0xB0C9CC),preview,width);
    PaddockText(PaddockReading(13.F,false,1.5F),PaddockRgb(0xB0C9CC),
        dkr::localisation::source(live?"Ordinary offline mods share your personal Adventure. Online and development saves stay separate. Preview shows saved progress.":
             "Ordinary offline mods share your personal Adventure. Online and development saves stay separate."),width);
    PaddockGap(12.F);
}

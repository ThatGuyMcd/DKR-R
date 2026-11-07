"""Reuse the vehicle card preview hook for background-decoded character portraits."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()

def library_copy(text):
    # Change static UI captions only. C++ names, URLs and storage keys stay put.
    return re.sub(r'"(?:\\.|[^"\\])*"',lambda m:re.sub(r'\bStore\b','Library',
        m[0].replace('MOD STORE','MOD LIBRARY').replace('MODS / HACKS','INSTALLED MODS')
            .replace('Mods / Hacks','Installed Mods').replace('Mods/Hacks','Installed Mods')),text)
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Portrait anchor differs: '+old[:120])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def parity_play(text):
    ui=ROOT/'runtime-recomp/src/game/library_ui'
    text=once(text,'    std::vector<PlayNotice> notices;','    std::vector<PlayNotice> notices;\n    if(!dkr::session_content::next_enabled.load())return notices;')
    text=once(text,'const std::string_view title = ready ? "Ready to race" : "ROM not loaded";',
        'const auto title=dkr::localisation::source(ready?"Ready to race":"ROM not loaded");')
    text=once(text,'const std::string_view no_rom = "Choose a Diddy Kong Racing ROM to start.";',
        'const auto no_rom=dkr::localisation::source("Choose a Diddy Kong Racing ROM to start.");')
    text=once(text,'"Removed " + PathUtf8(state.removed.filename()) + " from your list."',
        'dkr::localisation::format("play.rom_removed",{{"file",PathUtf8(state.removed.filename())}})')
    text=once(text,'std::to_string(count) + " ROMs available."','dkr::localisation::plural("play.rom_count",count)')
    text=once(text,'meta = "Accepted files: .z64, .n64, .v64";',
        'meta = dkr::localisation::source("Accepted files: .z64, .n64, .v64");')
    text=once(text,'"Track preview: " + name','dkr::localisation::format("play.track_preview",{{"name",name}})')
    text=once(text,'? "Starts directly in this track as Diddy."\n                               : "Uses Modern graphics."',
        '? std::string(dkr::localisation::source("Starts directly in this track as Diddy."))\n                               : std::string(dkr::localisation::source("Uses Modern graphics."))')
    start=text.index('                               one ? "1 enabled mod needs preparing"')
    end=text.index('                               true, "Open Mods"',start)
    text=text[:start]+'''                               dkr::localisation::plural("play.mods_prepare",names.size()),
                               dkr::localisation::format("play.mods_prepare_help",{{"names",list}}),
'''+text[end:]
    text=once(text,'            const bool one = names.size() == 1U;\n','')
    text=once(text,'void DrawPlayLaunchOptions(float width,bool entered) {',
        'void DrawLanguageSetting(float);\n#include "'+(ui/'save_indicator.inl').as_posix()+'"\n#include "'+(ui/'automatic_ui.inl').as_posix()+'"\n#include "'+(ui/'handoff_ui.inl').as_posix()+'"\n\nvoid DrawPlayLaunchOptions(float width,bool entered,bool live) {\n    ImGui::BeginDisabled(live);')
    start=text.index('    {\n        const auto& save = CachedSaveManagerView();',text.index('void DrawPlayLaunchOptions'))
    end=text.index('\n}\n\nvoid DrawPlayPage',start)
    text=text[:start]+'    ImGui::EndDisabled();\n    DrawSkipLauncherSetting(width);\n    DrawLanguageSetting(width);\n    DrawPlaySaveSummary(width,live);'+text[end:]
    text=once(text,'void DrawPlayPage(float available_width, const PlayPageContext& play) {',
        'void DrawPlayPage(float available_width, const PlayPageContext& play,bool live=false) {')
    text=once(text,'        DrawPlayUpdateButton(action);','        ImGui::BeginDisabled(live);DrawPlayUpdateButton(action);ImGui::EndDisabled();')
    text=once(text,'    DrawLauncherFeatured(width,false);','    DrawLauncherFeatured(width,live);')
    text=once(text,'        const float content_width=side?width-options_width-32.F:width;\n        const float content_x=at.x;',
        '        const float slot_width=side?width-options_width-32.F:width;\n        const float content_width=std::min(slot_width,rom_picker_width+pass_chrome);\n        const float content_x=at.x+std::floor((slot_width-content_width)*.5F);')
    text=once(text,'false, lobby || launch_modal,','false, live || lobby || launch_modal,')
    text=once(text,'        if (PaddockRaceButton(primary, {primary_width, 46.0F}, look)) {',
        '        ImGui::BeginDisabled(live || lobby || launch_modal);\n        if (PaddockRaceButton(primary, {std::min(body_width,primary_width), 46.0F}, look)) {')
    text=once(text,'        ImGui::PopStyleColor();\n        const float options_x=',
        '        ImGui::EndDisabled();\n        ImGui::PopStyleColor();\n        const float options_x=')
    text=once(text,'DrawPlayLaunchOptions(options_width,entered);','DrawPlayLaunchOptions(options_width,entered,live);')
    text=once(text,'std::vector<PlayNotice> notices = BuildPlayNotices(play.rom_ready);',
        'std::vector<PlayNotice> notices = live?std::vector<PlayNotice>{}:BuildPlayNotices(play.rom_ready);')
    text=once(text,'const bool can_start = play.rom_ready && !imports_busy && !launch_modal && !g_play_offline_after_leave;',
        'const bool can_start = live || (play.rom_ready && !imports_busy && !launch_modal && !g_play_offline_after_leave);')
    text=once(text,'PaddockRaceButton(lobby ? "LEAVE LOBBY & PLAY OFFLINE" : "PLAY OFFLINE - Diddy Kong Racing",',
        'PaddockRaceButton(live?"RETURN TO THE RACE":lobby ? "LEAVE LOBBY & PLAY OFFLINE" : "PLAY OFFLINE - Diddy Kong Racing",')
    text=once(text,'            if (lobby) ImGui::OpenPopup("Leave lobby and play offline?");',
        '            if(live)g_overlay_visible.store(false,std::memory_order_release);\n            else if (lobby) ImGui::OpenPopup("Leave lobby and play offline?");')
    text=once(text,'void DrawPlayPage(float available_width, const PlayPageContext& play,bool live=false) {',
        '#include "'+(ui/'play_launch_bar.inl').as_posix()+'"\n\nvoid DrawPlayPage(float available_width, const PlayPageContext& play,bool live=false) {')
    text=once(text,'        state.arrived_at = -10.0;\n        state.focus = PlayPageState::Focus::None;',
        '        state.arrived_at = -10.0;\n        state.focus = play.rom_ready ? PlayPageState::Focus::Start : PlayPageState::Focus::Browse;')
    text=once(text,'    const float width = std::max(available_width, 1.0F);',
        '    float width = std::max(available_width, 1.0F);')
    text=once(text,'    PaddockGap(20.0F);\n\n    DrawLauncherFeatured(width,live);',
        '''    PaddockGap(12.0F);
    const auto launch_summary=PlayLaunchSummary(play,live);
    const float launch_height=PlayLaunchBarHeight(width,launch_summary);
    const float body_height=std::max(1.F,ImGui::GetContentRegionAvail().y-launch_height);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0.F,0.F});
    const float body_width=std::max(1.F,PanelFooterBounds(ImGui::GetCurrentWindow()).Max.x-ImGui::GetCursorScreenPos().x);
    ImGui::BeginChild("play-scroll",{body_width,body_height},false,
        ImGuiWindowFlags_NavFlattened);
    ImGui::PopStyleVar();
    LibraryNavigationScrollWindow(ImGui::GetCurrentWindow());
    width=std::max(1.F,std::min(available_width,ImGui::GetContentRegionAvail().x-8.F));
    DrawLauncherFeatured(width,live);''')
    text=once(text,'    {\n        const bool can_start = live ||',
        '''    PaddockGap(16.F);
    PlayLaunchBodyCheck();
    ImGui::EndChild();
    width=std::max(1.F,available_width);
    DrawPlayLaunchSummary(width,launch_summary);
    {
        const bool can_start = live ||''')
    text=once(text,'        RaceButtonLook look;\n        look.hover_in = 0.12F;\n        if (can_start)',
        '        RaceButtonLook look;\n        look.font_px=21.F;\n        look.hover_in = 0.12F;\n        if (can_start)')
    text=once(text,'        if (state.focus == PlayPageState::Focus::Start && can_start) {\n            if (nav) ImGui::SetKeyboardFocusHere();',
        '        if (state.focus == PlayPageState::Focus::Start && can_start && ImGui::GetCurrentContext()->OpenPopupStack.empty()) {\n            ImGui::GetCurrentContext()->NavDisableHighlight=false;\n            ImGui::SetKeyboardFocusHere();')
    text=once(text,'"PLAY OFFLINE - Diddy Kong Racing", {width, 68.0F}, look)',
        'dkr::localisation::text("play.start_game").data(), {PlayLaunchButtonWidth(width), kPlayStartHeight}, look)')
    text=once(text,'        ImGui::EndDisabled();\n        ImGui::PopStyleVar();\n    }',
        '        ImGui::EndDisabled();\n        PlayLaunchButtonCheck(can_start);\n        ImGui::PopStyleVar();\n    }')
    text=once(text,'''    if (!play.rom_ready) {
        PaddockGap(8.0F);
        const PaddockType hint = PaddockReading(14.0F, false, 1.5F);
        constexpr std::string_view text = "Load a ROM to start.";
        const ImVec2 at = ImGui::GetCursorScreenPos();
        PaddockTextStyle style;
        style.colour = PaddockCol(0xB0C9CC);
        style.centre = true;
        PaddockTextAt(ImGui::GetWindowDrawList(), hint, at, width, text, style);
        ImGui::Dummy({width, hint.line});
    }
''','')
    return text

def consistent_browser(text):
    start=text.index('    // Toolbar: search, status, sort and the filter switch.')
    filters=text.index('    if (g_mods_page.filters_open) {',start)
    key=text.index('    const std::string key =',filters)
    matches=text.index('    const auto duplicate_count =',key)
    end=text.index('    if (characters) {\n        const std::string slots',matches)
    advanced=text[filters:key]
    for options in ('kFormats','kCompatibility','kVisibility','kDuplicates'):
        advanced=once(advanced,', '+options+', field_width)',', LocalisedUi::enum_items('+options+'), field_width)')
    advanced=once(advanced,'std::vector<std::string> source_items{"All packs"};',
        'std::vector<std::string> source_items{std::string(dkr::localisation::source("All packs"))};')
    advanced=once(advanced,'        PaddockGap(16.0F);\n','')
    advanced=once(advanced,'        PaddockBox box(width, {16.0F, 16.0F});',
        '        PaddockBox box(width, {16.0F, 16.0F});\n        if(!characters){PaddockText(PaddockReading(13.F,false,1.5F),PaddockRgb(0xC0D4DF),"LEGACY: Original-ROM patches. DKR: Native .dkrmap tracks.",box.Inner());PaddockGap(12.F);}')
    toolbar='''
    const float row=PaddockFieldLabelType().line+6.F+44.F;
    static const std::vector<std::string> kStates{"All","On","Off"};
    static const std::vector<std::string> kSorts{"Name A-Z","Name Z-A","Largest first","Smallest first","Newest first","Oldest first"};
    DrawInstalledToolbar(width,[&](float available) {
        const auto result=PaddockSearch("##search",state.search,sizeof(state.search),"Search by name or source pack...",available);
        if(result==PaddockFieldResult::GamepadActivated)
            RequestTextEntryKeyboard(characters?TextEntryTarget::CustomCharacterSearch:TextEntryTarget::CustomTrackSearch);
    },state.state,kStates,state.sort,kSorts);
'''
    results='''
    const auto duplicate_count=std::count_if(state.duplicate_matches.begin(),state.duplicate_matches.end(),
        [](const auto& entry){return entry.second.matches();});
    const int advanced_count=(!state.source.empty()?1:0)+(state.compatibility!=0?1:0)+
        (state.visibility!=0?1:0)+(state.format!=0?1:0)+(state.duplicates!=0?1:0);
    std::vector<InstalledResultAction> actions;
    const std::string filters_label=advanced_count?"Filters ("+std::to_string(advanced_count)+")##filters":"Filters##filters";
    actions.push_back({filters_label,g_mods_page.filters_open?PaddockButtonKind::SettingsSelected:PaddockButtonKind::Settings,[&] {
        g_mods_page.filters_open=!g_mods_page.filters_open;g_mods_page.filters_entered_at=PaddockClock();
    }});
    if(duplicate_count!=0&&state.duplicates==0)actions.push_back({"Review duplicates##review-duplicates",PaddockButtonKind::Settings,[&] {
        ResetLibraryFilters(state);state.duplicates=1;state.visibility=1;
    }});
    if(LibraryFiltered(state)||state.sort!=0)actions.push_back({"Clear filters##results",PaddockButtonKind::Link,[&]{ResetLibraryFilters(state);}});
    const std::size_t total=state.all.size()+(characters?0U:dkr_tracks.size());
    DrawInstalledResults(width,dkr::localisation::format("library.results",{
        {"shown",std::to_string(state.shown.size())},{"total",std::to_string(total)},
        {"category",dkr::localisation::source(noun)}}),actions);
    if(state.duplicates!=0){PaddockText(PaddockReading(13.F,false,1.5F),PaddockRgb(0xC7DCE6),
        "Matching names may be different versions. Compare Details before removing a copy.",width);PaddockGap(12.F);}
'''
    text=text[:start]+toolbar+text[key:matches]+results+advanced+text[end:]
    text=once(text,'std::to_string(active) + " custom characters enabled"',
        'dkr::localisation::plural("library.characters_enabled",active)')
    text=once(text,'                          active >= MaxActiveStageCharacters',
        '                          dkr::localisation::source(active >= MaxActiveStageCharacters')
    text=once(text,'"Four custom racers appear on the stage. Select one and use L / R to browse more characters.",',
        '"Four custom racers appear on the stage. Select one and use L / R to browse more characters."),')
    text=once(text,'const auto snapshot = characters ? mods.characters : mods.tracks;','const auto snapshot = LibraryConsistencyView(characters ? mods.characters : mods.tracks,characters);')
    return once(text,'const bool locked = mods_locked || mods.busy;','const bool locked = mods_locked || mods.busy || g_library_controls_check;')

def consistent_codes(text):
    begin=text.index('void DrawCustomMagicCodeLibrary(');end=text.find('\nvoid ',begin+1)
    end=len(text) if end<0 else end
    before,after=text[:begin],text[end:];text=text[begin:end]
    text=once(text,'const auto snapshot=g_code_library.view();','const auto snapshot=LibraryConsistencyView(g_code_library.view());')
    text=once(text,'const bool busy=locked||snapshot->busy;','const bool busy=locked||snapshot->busy||g_library_controls_check;')
    start=text.index('    const float status_width=',text.index('void DrawCustomMagicCodeLibrary'))
    end=text.index('    if(snapshot->busy)',start)
    text=text[:start]+'''
    DrawInstalledToolbar(width,[&](float available) {
        const auto result=PaddockSearch("search",g_code_search.data(),g_code_search.size(),"Search Custom Magic Codes...",available);
        if(result==PaddockFieldResult::GamepadActivated)
            RequestCustomTextKeyboard("Search Custom Magic Codes",g_code_search.data(),g_code_search.size(),[](const std::string& value){
                g_code_search.fill(0);std::copy_n(value.begin(),std::min(value.size(),g_code_search.size()-1),g_code_search.begin());
            });
    },g_code_state_filter,{"All","On","Off","Needs approval"},g_code_sort,{"Name A-Z","Name Z-A"});
'''+text[end:]
    start=text.index('    PaddockText(PaddockReading(12,false,1.5F)',text.index('void DrawCustomMagicCodeLibrary'))
    end=text.index('    DrawSessionAnalogueCard(width);',start)
    text=text[:start]+'''
    std::vector<InstalledResultAction> actions;
    if(g_code_search[0]||g_code_state_filter||g_code_sort)actions.push_back({"Clear filters##results",PaddockButtonKind::Link,[]{
        g_code_search.fill(0);g_code_state_filter=g_code_sort=0;
    }});
    DrawInstalledResults(width,dkr::localisation::format("library.results",{
        {"shown",std::to_string(shown.size())},{"total",std::to_string(snapshot->items.size())},
        {"category",dkr::localisation::source("Custom Magic Codes")}}),actions);
'''+text[end:]
    text=once(text,'height=std::max(height,MeasureLibraryCard(card,card_width));',
        'ResolveLibraryCardCopy(card);height=std::max(height,MeasureLibraryCard(card,card_width));')
    text=once(text,'card.byline="Version "+item.version;',
        'card.byline=dkr::localisation::format("library.version",{{"version",item.version}});')
    return before+text+after

def consistent_vehicle(text):
    begin=text.index('void DrawVehicleLibrary(');end=text.find('\nvoid ',begin+1)
    end=len(text) if end<0 else end
    before,after=text[:begin],text[end:];text=text[begin:end]
    text=once(text,'const auto snapshot=g_vehicle_library.view();','const auto snapshot=LibraryConsistencyView(g_vehicle_library.view());')
    text=once(text,'const bool busy=locked||snapshot->busy||bool(snapshot->pending);','const bool busy=locked||snapshot->busy||bool(snapshot->pending)||g_library_controls_check;')
    text='int g_vehicle_state_filter=0,g_vehicle_sort=0;\n'+text
    start=text.index('    { PaddockEnter toolbar_enter(2);',text.index('void DrawVehicleLibrary'))
    end=text.index('    if(snapshot->busy)',start)
    text=text[:start]+'''
    DrawInstalledToolbar(width,[&](float available) {
        const auto result=PaddockSearch("search",g_vehicle_search.data(),g_vehicle_search.size(),"Search vehicles...",available);
        if(result==PaddockFieldResult::GamepadActivated)
            RequestCustomTextKeyboard("Search vehicles",g_vehicle_search.data(),g_vehicle_search.size(),[](const std::string& value){
                g_vehicle_search.fill(0);std::copy_n(value.begin(),std::min(value.size(),g_vehicle_search.size()-1),g_vehicle_search.begin());
            });
    },g_vehicle_state_filter,{"All","On","Off"},g_vehicle_sort,{"Name A-Z","Name Z-A"});
'''+text[end:]
    start=text.index('    auto query=std::string(g_vehicle_search.data());',text.index('void DrawVehicleLibrary'))
    end=text.index('    const auto count=',start)
    text=text[:start]+'''
    std::vector<const vehicle_mods::Item*> shown;
    for(const auto& item:snapshot->items)
        if(dkr::library_ui::vehicle_matches(item.name,item.enabled,g_vehicle_search.data(),g_vehicle_state_filter))shown.push_back(&item);
    std::sort(shown.begin(),shown.end(),[](const auto* a,const auto* b){
        return dkr::library_ui::vehicle_before(a->name,a->digest,b->name,b->digest,g_vehicle_sort);
    });
    std::vector<InstalledResultAction> actions;
    if(g_vehicle_search[0]||g_vehicle_state_filter||g_vehicle_sort)actions.push_back({"Clear filters##results",PaddockButtonKind::Link,[]{
        g_vehicle_search.fill(0);g_vehicle_state_filter=g_vehicle_sort=0;
    }});
    DrawInstalledResults(width,dkr::localisation::format("library.results",{
        {"shown",std::to_string(shown.size())},{"total",std::to_string(snapshot->items.size())},
        {"category",dkr::localisation::source("vehicles")}}),actions);
'''+text[end:]
    # Empty filtered results must offer the same reset rather than an import.
    text=once(text,'        if(PaddockButton("Import car",PaddockButtonKind::Primary))ImportVehicleWithDialog();',
        '        if(snapshot->items.empty()){if(PaddockButton("Import car",PaddockButtonKind::Primary))ImportVehicleWithDialog();}\n'
        '        else if(PaddockButton("Clear filters",PaddockButtonKind::Plain)){g_vehicle_search.fill(0);g_vehicle_state_filter=g_vehicle_sort=0;}')
    text=once(text,'height=std::max(height,MeasureLibraryCard(card,card_width));',
        'ResolveLibraryCardCopy(card);height=std::max(height,MeasureLibraryCard(card,card_width));')
    text=once(text,'card.byline=vehicle_mods::type_name(item.type);',
        'card.byline=dkr::localisation::source(vehicle_mods::type_name(item.type));')
    return before+text+after

def parity_surface(text,out):
    ui=ROOT/'runtime-recomp/src/game/library_ui';themes=ROOT/'runtime-recomp/src/game/launcher_themes'
    locale=ROOT/'runtime-recomp/src/game/localisation'
    text='#include "'+(ROOT/'runtime-recomp/src/game/crash_reports/reporting.hpp').as_posix()+'"\n'+text
    text='#include "'+(locale/'service.hpp').as_posix()+'"\n'+text
    text='#include "'+(locale/'save_preview.hpp').as_posix()+'"\n'+text
    text='#include "'+(locale/'choice.hpp').as_posix()+'"\n'+text
    text='#include "'+(ui/'automatic_start.hpp').as_posix()+'"\n#include "'+(ui/'navigation_icons.hpp').as_posix()+'"\n'+text
    text='#include "'+(ui/'handoff.hpp').as_posix()+'"\n#include "'+(ui/'launcher_fade.hpp').as_posix()+'"\n'+text
    text=once(text,'std::to_string(enabled) + " legacy mods enabled. Applies next game."',
        'dkr::localisation::plural("library.legacy_enabled",enabled)')
    text=once(text,': "Choose a ROM to prepare legacy mods.";',
        ': std::string(dkr::localisation::source("Choose a ROM to prepare legacy mods."));')
    text=once(text,'const float link_width = PaddockMeasure(link_type, link);',
        'const float link_width = PaddockMeasure(link_type, dkr::localisation::source(link));')
    if text.count('PaddockTextLink(link)')!=2:raise ValueError('Expected both summary link layouts')
    text=text.replace('PaddockTextLink(link)','PaddockTextLink(dkr::localisation::label(link))')
    text=once(text,'#define Button DkrRaceButton',
        '#define Button DkrRaceButton\n#include "'+(locale/'imgui_ui.hpp').as_posix()+'"')
    text=once(text,'#include "runtime_ui_paddock.inl"',
        '#include "runtime_ui_paddock.inl"\n#include "'+(locale/'literal_ui.inl').as_posix()+'"\n#include "'+(locale/'font_ui.inl').as_posix()+'"')
    text=once(text,'void LoadSettings() {','void LoadSettings() {\n    dkr::library_ui::skip_launcher=false;\n    dkr::localisation::detect_system_language();')
    text=once(text,'void LoadLauncherFonts() {','void LoadLauncherFonts() {\n    const auto localisation_started=std::chrono::steady_clock::now();')
    text=once(text,'    LoadBrandLogoIntoAtlas();',
        '    LoadBrandLogoIntoAtlas();\n    std::fprintf(stderr,"[ui][localisation] language=%s fonts=%d atlas=%dx%d build_ms=%.1f\\n",\n        dkr::localisation::active().data(),io.Fonts->Fonts.Size,io.Fonts->TexWidth,io.Fonts->TexHeight,\n        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-localisation_started).count());')
    text=once(text,'        if (key == "skip_intro") {',
        '        if(key=="skip_launcher"){dkr::library_ui::skip_launcher=dkr::library_ui::parse_skip_launcher(value);continue;}\n        if(key=="launcher_language"){dkr::localisation::select(value);continue;}\n        if (key == "skip_intro") {')
    text=once(text,'    LoadBrandLogoIntoAtlas();','    MergeLocalisationFonts(io.Fonts);\n    LoadBrandLogoIntoAtlas();')
    text=once(text,'    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == page;',
        '    const auto caption=dkr::localisation::label(label);label=caption.c_str();\n    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == page;')
    text=once(text,'    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == target_page;',
        '    const auto caption=dkr::localisation::label(label);label=caption.c_str();\n    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == target_page;')
    begin=text.index('float TrackedTextWidth(');end=text.index('void DrawPageHeading(',begin)
    text=text[:begin]+'#include "'+(locale/'tracked_text_ui.inl').as_posix()+'"\n\n'+text[end:]
    text=once(text,'void DrawPageHeading(const char* text, bool title = false,\n                     float size_override = 0.0F) {',
        'void DrawPageHeading(const char* text, bool title = false,\n                     float size_override = 0.0F) {\n    text=dkr::localisation::source(text?text:"").data();')
    text='#include "'+(ui/'control_layout.hpp').as_posix()+'"\n#include "'+(ui/'surface_policy.hpp').as_posix()+'"\n#include "'+(themes/'button_pattern_ui.hpp').as_posix()+'"\n'+text
    text='#include "'+(themes/'corner_atlas.hpp').as_posix()+'"\n'+text
    text=once(text,'constexpr int kRaceButtonPatternWidth = 1984;',
        'constexpr int kRaceButtonPatternWidth = dkr::launcher_themes::ButtonPatternWidth;')
    text=once(text,'constexpr int kRaceButtonPatternHeight = 64;',
        'constexpr int kRaceButtonPatternHeight = dkr::launcher_themes::ButtonPatternHeight;')
    start=text.index('    // Keep the racing motif broad and calm.')
    end=text.index('    draw->AddRect(minimum, maximum,',start)
    text=text[:start]+'''    auto* atlas=GetIO().Fonts;
    auto* pattern=g_race_button_pattern_rect>=0?atlas->GetCustomRectByIndex(g_race_button_pattern_rect):nullptr;
    if(pattern&&pattern->IsPacked()&&atlas->TexID&&atlas->TexWidth>0&&atlas->TexHeight>0)
        dkr::launcher_themes::draw_button_pattern(draw,minimum,maximum,*pattern,rounding,amount,GetTime());
    else draw->AddRectFilled(minimum,maximum,faded(226,112,25,255),rounding);
'''+text[end:]
    text=once(text,'    const int pattern_rect_index = atlas->AddCustomRectRegular(',
        '    const int corner_rect_index=atlas->AddCustomRectRegular(dkr::launcher_themes::ArtWidth,dkr::launcher_themes::ArtHeight);\n    const int pattern_rect_index = atlas->AddCustomRectRegular(')
    text=once(text,'    atlas->TexPixelsUseColors = true;',
        '    dkr::launcher_themes::fill_corner_atlas(atlas,pixels,atlas_width,corner_rect_index);\n    atlas->TexPixelsUseColors = true;')
    start=text.index('        constexpr std::array<unsigned char, 4> kCheckerDark')
    end=text.index('        g_race_button_pattern_rect = pattern_rect_index;',start)
    text=text[:start]+'''        dkr::launcher_themes::fill_button_patterns(pixels,atlas_width,pattern->X,pattern->Y);
        std::fprintf(stderr,"[ui][button-patterns] cached seasonal tiles=20\\n");
'''+text[end:]
    # Header layout shared with the Library's orange actions.
    include='#include "'+(out/'runtime_mod_library_ui.inl').as_posix()+'"'
    text=once(text,include,'#include "'+(ui/'installed_toolbar.inl').as_posix()+'"\n'+include+'\n#include "'+(ui/'header_layout.inl').as_posix()+'"')
    start=text.index('    // Header: the page title and the import sign.')
    end=text.index('    DrawLauncherFeatured(width,game_running);',start)
    text=text[:start]+'''    DrawSharedPageHeader(width,"INSTALLED MODS","Your tracks, racers, vehicles and race modifiers.",
        PaddockImportButtonWidth("Import mods"),[&]{
            ImGui::BeginDisabled(mods_locked||mods.busy||picking);
            if(PaddockImportButton("Import mods",std::min(width,PaddockImportButtonWidth("Import mods"))))
                g_mods_page.request_import_chooser=true;
            ImGui::EndDisabled();
        });
    PaddockText(PaddockReading(13.F,false,1.55F),PaddockRgb(0xFFC15A),dkr::localisation::source(dkr::store::LegacyRomInstallNotice),width);
    PaddockGap(12.F);

'''+text[end:]
    text=once(text,'    const float width = game_running ? std::min(available_width, 1400.0F)\n                                      : std::max(available_width, 1.0F);',
        '    const float width = std::max(available_width,1.F);')
    start=text.index('    if (game_running) {\n        PaddockGap(14.0F);',text.index('void DrawModsHacks('))
    end=text.index('    if (mods.busy) {',start)
    text=text[:start]+'''    PaddockGap(14.F);
    PaddockText(PaddockReading(14.F,false,1.5F),PaddockRgb(0xB0C9CC),
        "Mod changes apply next game. Return to the launcher and leave your lobby to change them.",width);
'''+text[end:]

    start=text.index('    if (!characters) {\n        PaddockEnter enter(1);',text.index('void DrawModsLibrarySection('))
    end=text.index('    DrawModCardBrowser(',start)
    text=text[:start]+'    if(!characters)DrawFriendlyStatus(g_track_import_status,width,dkr::runtime::copy::Area::Mods);\n'+text[end:]

    text=once(text,'const std::size_t track_count = g_mod_browsers[0].all.size() + dkr_tracks.size();',
        'const std::size_t track_count = g_library_controls_check?8:g_mod_browsers[0].all.size()+dkr_tracks.size();')
    text=once(text,'const std::size_t character_count = g_mod_browsers[1].all.size();',
        'const std::size_t character_count = g_library_controls_check?8:g_mod_browsers[1].all.size();')
    text=once(text,'track_count, character_count,g_code_library.view()->items.size(),g_vehicle_library.view()->items.size(),width',
        'track_count, character_count,LibraryConsistencyView(g_code_library.view())->items.size(),LibraryConsistencyView(g_vehicle_library.view())->items.size(),width')

    # Input preference uses the ordinary local settings file.
    text=once(text,'        dkr::chat::save_settings(output);',
        '        output << "skip_launcher=" << (dkr::library_ui::skip_launcher?1:0) << \'\\n\';\n        output << "launcher_language=" << dkr::localisation::preference() << \'\\n\';\n        output << "sidebar_navigation=" << int(dkr::library_ui::sidebar_input.load()) << \'\\n\';\n        dkr::chat::save_settings(output);')
    text=once(text,'            if(dkr::launcher_themes::load(key,number))continue;',
        '            if(key=="sidebar_navigation"){dkr::library_ui::sidebar_input.store(number==1?dkr::library_ui::SidebarInput::Dpad:dkr::library_ui::SidebarInput::Shoulders);continue;}\n            if(dkr::launcher_themes::load(key,number))continue;')
    # The input hook is installed into both ImGui contexts, before polling.
    text=once(text,'#include "'+(ui/'navigation_check.inl').as_posix()+'"',
        '#include "'+(ui/'navigation_check.inl').as_posix()+'"\n#include "'+(ui/'navigation_ui.inl').as_posix()+'"')
    start=text.index('void DrawOverlayContent(float content_width) {')
    end=text.index('\n} // namespace',start)
    text=text[:start]+'#include "'+(ui/'shared_page_ui.inl').as_posix()+'''"

void DrawOverlayContent(float content_width) {
    LauncherContentEnter content_enter([]{return g_overlay_page.load();});
    DrawSharedUiPage(content_width,g_overlay_page.load(),true);
}
'''+text[end:]
    start=text.index('        if (page == 0) {',text.index('        g_launcher_rom = &play_context;'))
    end=text.index('        g_launcher_rom = nullptr;',start)
    text=text[:start]+'''        DrawSharedUiPage(right_inner_width,page,false,&play_context);
        }
'''+text[end:]
    # Preserve the experimental ROM handoff previously in the launcher router.
    text=once(text,'        g_launcher_rom = &play_context;',
        '        g_launcher_rom = &play_context;\n#if defined(DKR_EXPERIMENTAL_RACE_TEST)\n        g_online_experimental_rom=selected_rom;\n#endif')
    text=once(text,'    return result;\n}\n\nvoid dkr::runtime::ui::attach',
        '    g_shared_play={selected_rom,rom_status,rom_ready,false,rom_catalog};\n    return result;\n}\n\nvoid dkr::runtime::ui::attach')
    # Reserve the toggle above the divider, keeping eight pages and bottom actions.
    text=once(text,'constexpr float kButtonCount = static_cast<float>(kSidebarOrder.size());',
        'constexpr float kButtonCount = static_cast<float>(kSidebarOrder.size()-1);')
    text=once(text,'const float fixed_height = result.padding * 2.0F + result.brand_gap +',
        'const float fixed_height = dkr::library_ui::NavigationToggleHeight + result.padding * 2.0F + result.brand_gap +')
    if text.count('kSidebarOrder.size()-2);')!=2:raise ValueError('Expected two sidebar placement calls')
    text=text.replace('kSidebarOrder.size()-2);','kSidebarOrder.size()-3);')
    toggle='        ImGui::SetCursorPosY(sidebar_position.actions-sidebar_layout.action_section_gap-sidebar_layout.item_spacing_y);'
    if text.count(toggle)!=2:raise ValueError('Expected two sidebar toggle anchors')
    text=text.replace(toggle,'        ImGui::SetCursorPosY(sidebar_position.actions-sidebar_layout.action_section_gap-sidebar_layout.item_spacing_y-dkr::library_ui::NavigationToggleHeight);\n        DrawSidebarNavigationToggle(nav_inner_width,sidebar_selection);\n'+toggle)
    text=once(text,'            if (HandleInputCaptureEvent(&event)) {\n                return;\n            }',
        '            if (HandleInputCaptureEvent(&event)) {\n                return;\n            }\n            LibrarySidebarEvent(event);')
    text=once(text,'    if (HandleInputCaptureEvent(event)) {\n        return true;\n    }',
        '    if (HandleInputCaptureEvent(event)) {\n        return true;\n    }\n    if(g_overlay_visible.load())LibrarySidebarEvent(*event);')
    # Poll routing replaces raw shoulder page changes so the event and polling
    # paths cannot double-step, and D-pad mode owns all four D-pad directions.
    start=text.index('                } else if (!g_rom_browser.open && !code_keyboard_visible &&\n                           event.cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER)')
    end=text.index('                } else if (!g_rom_browser.open && !code_keyboard_visible &&\n                           event.cbutton.button == SDL_CONTROLLER_BUTTON_A',start)
    text=text[:start]+text[end:]
    text=once(text,'                    if (sidebar_selection == launcher_page_count) {',
        '                    if(dkr::library_ui::sidebar_input_blocked.load())return;\n                    if(sidebar_selection==dkr::library_ui::SidebarToggle)dkr::library_ui::sidebar_toggle_requested.store(true);\n                    else if (sidebar_selection == launcher_page_count) {')
    start=text.index('        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {',text.index('bool dkr::runtime::ui::handle_runtime_event'))
    end=text.index('        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_A) {',start)
    text=text[:start]+text[end:]
    text=once(text,'            if (selected == kMenuPageCount) {',
        '            if(dkr::library_ui::sidebar_input_blocked.load())return false;\n            if(selected==dkr::library_ui::SidebarToggle){dkr::library_ui::sidebar_toggle_requested.store(true);return true;}\n            if (selected == kMenuPageCount) {')
    # Clamp the footer selection range, including the navigation switch.
    text=text.replace('constexpr int overlay_sidebar_count = kMenuPageCount + 2;',
        'constexpr int overlay_sidebar_count = kMenuPageCount + 3;')
    text=text.replace('constexpr int launcher_sidebar_count = kMenuPageCount + 2;',
        'constexpr int launcher_sidebar_count = kMenuPageCount + 3;')
    text=text.replace('constexpr int launcher_sidebar_count = launcher_page_count + 2;',
        'constexpr int launcher_sidebar_count = launcher_page_count + 3;')
    start=text.index('    LoadLauncherFonts();',text.index('StartupResult dkr::runtime::ui::run_startup_screen'))
    text=text[:start]+text[start:].replace('    LoadLauncherFonts();','    LoadLauncherFonts();\n    LibraryInstallNavigationHook(false);',1)
    start=text.index('    LoadLauncherFonts();',text.index('void dkr::runtime::ui::attach'))
    text=text[:start]+text[start:].replace('    LoadLauncherFonts();','    LoadLauncherFonts();\n    LibraryInstallNavigationHook(true);',1)
    text=once(text,'        ImGui::NewFrame();\n        int hud_viewport_width',
        '''        ImGui::NewFrame();
        if(const int step=LibraryTakeSidebarStep(false)) {
            sidebar_selection=StepSidebarSelection(sidebar_selection,step);
            if(sidebar_selection<launcher_page_count){page=sidebar_selection;focus_content=true;}
        }
        if(LibraryTakeContentMotion(false))sidebar_selection=dkr::library_ui::sidebar_parent(page);
        int hud_viewport_width''')
    text=once(text,'    inspector->newFrame(application.framebufferGraphicsWorker.get());',
        '''    inspector->newFrame(application.framebufferGraphicsWorker.get());
    if(show_overlay)if(const int step=LibraryTakeSidebarStep(true)) {
        const int selected=StepSidebarSelection(g_overlay_sidebar_selection.load(),step);
        g_overlay_sidebar_selection.store(selected);
        if(selected<kMenuPageCount){g_overlay_page.store(selected);g_overlay_focus_requested.store(true);}
    }
    if(show_overlay&&LibraryTakeContentMotion(true))
        g_overlay_sidebar_selection.store(dkr::library_ui::sidebar_parent(g_overlay_page.load()));''')
    text=once(text,'    BeginMainWindow("DKR-R Overlay", ImGuiWindowFlags_NoBackground);',
        '''    static dkr::launcher_themes::SystemSettings overlay_region;
    static dkr::launcher_themes::Session overlay_theme;
    overlay_region.refresh(ImGui::GetTime(),true,!ImGui::GetIO().AppFocusLost);
    overlay_theme.tick(ImGui::GetTime(),true,!ImGui::GetIO().AppFocusLost,
        dkr::launcher_themes::preferences,dkr::launcher_themes::local_date);
    dkr::launcher_themes::displayed=overlay_theme.theme();
    dkr::launcher_themes::next_switch=overlay_theme.next();
    BeginMainWindow("DKR-R Overlay", ImGuiWindowFlags_NoBackground);''')
    text=once(text,'        dkr::launcher_themes::corners(ImGui::GetForegroundDrawList(),ImGui::GetMainViewport()->Pos,\n            ImGui::GetMainViewport()->Size,launcher_theme.theme(),reinterpret_cast<ImTextureID>(launcher_art.get()),dkr::launcher_themes::preferences.calendar);',
        '        dkr::launcher_themes::atlas_corners(ImGui::GetForegroundDrawList(),ImGui::GetMainViewport()->Pos,\n            ImGui::GetMainViewport()->Size,launcher_theme.theme(),dkr::launcher_themes::preferences.calendar);')
    text=once(text,'    BeginMainWindow("DKR-R Overlay", ImGuiWindowFlags_NoBackground);',
        '    BeginMainWindow("DKR-R Overlay", ImGuiWindowFlags_NoBackground);\n    dkr::launcher_themes::atlas_corners(ImGui::GetForegroundDrawList(),ImGui::GetMainViewport()->Pos,ImGui::GetMainViewport()->Size,overlay_theme.theme(),dkr::launcher_themes::preferences.calendar);')
    text=once(text,'    bool launch_requested = false;',
        '    dkr::library_ui::AutomaticStart automatic_start(dkr::library_ui::skip_launcher&&!LibraryStartupCheckEnabled(),dkr::library_ui::startup_screen_seen,!preselected_rom.empty(),software_failure_recovery);\n    bool launch_requested = false;')
    text=once(text,'        if (launch_requested) {', '''        {
            const auto preparation=g_mod_launch.snapshot();
            const bool busy=dkr::crash_reports::view().consent.choice==-1||g_legacy_imports.snapshot().busy||g_code_library.view()->busy||
                g_vehicle_library.view()->busy||bool(g_vehicle_library.view()->pending)||dkr::store::installations().view()->busy;
            const auto action=automatic_start.poll({rom_ready,dkr::runtime::netplay::session().active(),busy,
                preparation.modal,preparation.busy,!preparation.error.empty()});
            if(action==dkr::library_ui::AutomaticStart::Action::Start){
                automatic_start.loading_mods(AutomaticStartHasMods());
                std::fprintf(stderr,"[launcher][automatic-start] preparing selected ROM through normal Play flow\\n");launch_requested=true;
            } else if(action==dkr::library_ui::AutomaticStart::Action::ShowLauncher)
                std::fprintf(stderr,"[launcher][automatic-start] showing launcher for selection, cancellation or recovery\\n");
        }
        if(launch_requested&&dkr::crash_reports::view().consent.choice==-1)launch_requested=false;
        if (launch_requested) {''')
    text=once(text,'            if (event.type == SDL_QUIT ||', '''            if(automatic_start.active()&&
               ((event.type==SDL_KEYDOWN&&event.key.keysym.scancode==SDL_SCANCODE_ESCAPE)||
                (event.type==SDL_CONTROLLERBUTTONDOWN&&event.cbutton.button==SDL_CONTROLLER_BUTTON_B))) {
                automatic_start.cancel();g_mod_launch.cancel();return;
            }
            if(automatic_start.active()&&(event.type==SDL_KEYDOWN||event.type==SDL_CONTROLLERBUTTONDOWN))return;
            if (event.type == SDL_QUIT ||''')
    text=once(text,'        BeginMainWindow("DKR-R Startup", ImGuiWindowFlags_NoBackground);',
        '        if(automatic_start.active()||LibraryStartupCheckEnabled())DrawAutomaticStartFrame(automatic_start,launcher_background,renderer,launcher_background_scroll,bounded_software_return,launcher_theme.theme(),launcher_theme.seconds());\n        else {\n        BeginMainWindow("DKR-R Startup", ImGuiWindowFlags_NoBackground);')
    text=once(text,'        PumpDialogJob();\n        PumpSharedSaveJob();\n        PumpModStoreLauncher();',
        '        }\n        PumpDialogJob();\n        PumpSharedSaveJob();\n        PumpModStoreLauncher();')
    text=once(text,'        DrawModLaunchModal();',
        '        if(!dkr::library_ui::inline_preparation(automatic_start.active(),g_mod_launch.snapshot().busy,!g_mod_launch.snapshot().error.empty()))DrawModLaunchModal();')
    if text.count('ApplyPaddockModalDim();')!=2:raise ValueError('Expected launcher and overlay modal passes')
    text=text.replace('ApplyPaddockModalDim();','DrawCrashReportingConsent();\n        ApplyPaddockModalDim();')
    return startup_handoff(text)

def startup_handoff(text):
    text=once(text,'    bool launch_requested = false;','    dkr::library_ui::LauncherFade launch_fade;\n    dkr::library_ui::runtime_intro.clear();\n    bool online_start_pending=false;\n    bool launch_requested = false;')
    text=once(text,'        if (rom_ready &&\n            dkr::runtime::netplay::session().consume_launch_request()) {',
        '        if (rom_ready && !online_start_pending &&\n            dkr::runtime::netplay::session().consume_launch_request()) {')
    text=once(text,'''            result.start_game = true;
            result.rom_path = selected_rom;
            running = false;
            continue;''','''            online_start_pending=true;
        }
        if(online_start_pending&&!dkr::runtime::netplay::session().active()){online_start_pending=false;launch_fade.clear();}\n        if(online_start_pending&&launch_fade.started()&&launch_fade.finished(PaddockClock())) {
            result.start_game=true;result.rom_path=selected_rom;
            dkr::library_ui::runtime_intro.begin(true,launcher_theme.seconds());
            running=false;continue;''')
    text=once(text,'            const auto action=automatic_start.poll(',
        '            const auto action=online_start_pending?dkr::library_ui::AutomaticStart::Action::None:automatic_start.poll(')
    text=once(text,'            } else if(action==dkr::library_ui::AutomaticStart::Action::ShowLauncher)\n                std::fprintf',
        '            } else if(action==dkr::library_ui::AutomaticStart::Action::ShowLauncher)\n                launch_fade.clear();\n            if(action==dkr::library_ui::AutomaticStart::Action::ShowLauncher)std::fprintf')
    text=once(text,'if(!SharedSaveBusy())g_mod_launch.start(g_config_directory,mod_launch_rom,false);',
        'if(!SharedSaveBusy()) {\n                    if(!automatic_start.active()){automatic_start.manual(AutomaticStartHasMods());std::fprintf(stderr,"[startup][manual] shared starting screen\\n");}\n                    g_mod_launch.start(g_config_directory,mod_launch_rom,false);\n                }')
    text=once(text,'if(const auto prepared=g_mod_launch.snapshot();prepared.modal && prepared.prepared && !prepared.busy)',
        'if(const auto prepared=g_mod_launch.snapshot();prepared.modal && prepared.prepared && !prepared.busy && launch_fade.finished(PaddockClock()))')
    text=once(text,'                result.start_game=true;result.rom_path=mod_launch_rom;result.mods=prepared.prepared;',
        '                result.start_game=true;result.rom_path=mod_launch_rom;result.mods=prepared.prepared;\n                dkr::library_ui::runtime_intro.begin(false,launcher_theme.seconds());')
    text=once(text,'        if(automatic_start.active()||LibraryStartupCheckEnabled())DrawAutomaticStartFrame',
        '        if(online_start_pending&&launch_fade.started()) {\n            if(DrawAutomaticStart(automatic_start,launcher_background,renderer,launcher_background_scroll,bounded_software_return,launcher_theme.theme(),launcher_theme.seconds(),nullptr,true)) {\n                online_start_pending=false;launch_fade.clear();dkr::runtime::netplay::session().disconnect("Game startup cancelled.");\n            }\n        }\n        else if(automatic_start.active()||LibraryStartupCheckEnabled())DrawAutomaticStartFrame')
    text=once(text,'        }\n        PumpDialogJob();\n        PumpSharedSaveJob();\n        PumpModStoreLauncher();',
        '        }\n        if(automatic_start.active()||online_start_pending)DrawLauncherFade(launch_fade,PaddockClock());\n        PumpDialogJob();\n        PumpSharedSaveJob();\n        PumpModStoreLauncher();')
    text=once(text,'        LauncherLayoutCheckCapture(renderer);',
        '        if(!launch_requested&&!automatic_start.active()&&!online_start_pending)launch_fade.clear();\n        if((launch_requested&&!automatic_start.active())||(online_start_pending&&!launch_fade.started()))launch_fade.capture(renderer,PaddockClock());\n        LauncherLayoutCheckCapture(renderer);')
    text=once(text,'    SDL_DestroyRenderer(renderer);',
        '    launch_fade.clear();\n    SDL_DestroyRenderer(renderer);')
    text=once(text,'    const bool show_overlay =',
        '    const bool show_starting_game=dkr::library_ui::runtime_intro.active() || (dkr::runtime::netplay::session().runtime_view().active && dkr::runtime::netplay::session().runtime_view().state==dkr::runtime::netplay::ConnectionState::Loading);\n    const bool show_overlay =')
    text=once(text,'        !show_achievement_toast && !show_overlay && !show_fps && !show_crt',
        '        !show_starting_game && !show_achievement_toast && !show_overlay && !show_fps && !show_crt')
    text=once(text,'    if (show_overlay || show_camera_tuning || dkr::chat::typing.load() || dkr::runtime::invites::dialog()) {\n        dkr::runtime::platform::update_ui_gamepad_navigation();',
        '    if (show_starting_game || show_overlay || show_camera_tuning || dkr::chat::typing.load() || dkr::runtime::invites::dialog()) {\n        dkr::runtime::platform::update_ui_gamepad_navigation();')
    text=once(text,'    ApplyStyle();\n    ImGui::GetIO().ConfigFlags |=',
        '    ApplyStyle();\n    if(DrawRuntimeStartingGame()) {\n        DrawOnlineFailureModal();if(show_online_error)DrawOnlineErrorNotification();\n        dkr::store::overlay::end();inspector->endFrame();return;\n    }\n    ImGui::GetIO().ConfigFlags |=')
    text=once(text,'bool dkr::runtime::ui::input_capture_active() {\n    return','bool dkr::runtime::ui::input_capture_active() {\n    return dkr::library_ui::runtime_intro_visible.load() ||')
    text=once(text,'        if(!dkr::library_ui::inline_preparation(',
        '        LibraryManualHandoffRequest(rom_ready&&!g_legacy_imports.snapshot().busy&&!g_code_library.view()->busy&&!g_vehicle_library.view()->busy&&dkr::store::installations().view()->ready&&!dkr::store::installations().view()->busy,launch_requested);\n        if(!dkr::library_ui::inline_preparation(')
    text=once(text,'        LauncherLayoutCheckCapture(renderer);','        LibraryCaptureHandoff(renderer,launch_fade,automatic_start.active());\n        LauncherLayoutCheckCapture(renderer);')
    text=once(text,'            if(automatic_start.active()&&\n','            if((automatic_start.active()||online_start_pending)&&\n')
    text=once(text,'                automatic_start.cancel();g_mod_launch.cancel();return;',
        '                if(online_start_pending){online_start_pending=false;dkr::runtime::netplay::session().disconnect("Game startup cancelled.");}\n                automatic_start.cancel();g_mod_launch.cancel();launch_fade.clear();return;')
    text=once(text,'            if(automatic_start.active()&&(event.type==SDL_KEYDOWN||event.type==SDL_CONTROLLERBUTTONDOWN))return;',
        '            if((automatic_start.active()||online_start_pending)&&(event.type==SDL_KEYDOWN||event.type==SDL_CONTROLLERBUTTONDOWN))return;')
    text=once(text,'    if(HandleSessionChatEvent(event))return true;',
        '    if(dkr::library_ui::runtime_intro_visible.load()&&((event->type==SDL_KEYDOWN&&event->key.keysym.scancode==SDL_SCANCODE_ESCAPE)||(event->type==SDL_CONTROLLERBUTTONDOWN&&event->cbutton.button==SDL_CONTROLLER_BUTTON_B))) {\n        g_lifecycle_request.store(dkr::runtime::ui::LifecycleRequest::StopGame);g_overlay_visible=false;ultramodern::quit();return true;\n    }\n    if(HandleSessionChatEvent(event))return true;')
    text=once(text,'dkr::library_ui::inline_preparation(automatic_start.active(),g_mod_launch.snapshot().busy,',
        'dkr::library_ui::inline_preparation(automatic_start.active(),g_mod_launch.snapshot().modal,')
    text=once(text,'        DrawOnlineWaitingNotification(\n            dkr::runtime::netplay::session().presentation_view());',
        '        if(!online_start_pending)DrawOnlineWaitingNotification(\n            dkr::runtime::netplay::session().presentation_view());')
    return text


def lobby_layout(text):
    # Move the existing content after all lobby/party repairs have composed.
    # Keep callbacks and permissions intact; only the drawing positions move.
    start=text.index('void DrawOlLobby(');end=text.index('// Knocking on the host',start)
    body=text[start:end]
    visibility='''    OlParagraph(visibility,14.F,kOlSoft,width);
    if(host)DrawOlDirectoryVisibility(width,frame.busy||!directory->connected||hosting!=2);
'''
    body=once(body,visibility,'')
    begin=body.index('    OlParagraph("ONLINE SESSION SAVE - supplied by "')
    finish=body.index('    PaddockGap(14.0F);\n    OlPanelLook look;',begin)
    summary=body[begin:finish]
    body=once(body,summary+'    PaddockGap(14.0F);\n','')
    body=once(body,'''        const float plate_width = narrow ? inner : kOlPlateWidth;
        const bool side = !narrow && inner - plate_width - 20.0F >= 260.0F;
        const float title_width = side ? inner - plate_width - 20.0F : inner;''',
        '''        const float aside_target = std::clamp(inner * 0.32F, kOlPlateWidth, 440.0F);
        const bool side = !narrow && inner - aside_target - 20.0F >= 360.0F;
        const float aside_width = side ? aside_target : inner;
        const float plate_width = std::min(inner, narrow ? inner : kOlPlateWidth);
        const float title_width = side ? inner - aside_width - 20.0F : inner;''')
    summary=''.join('        '+line[4:]+'\n' for line in summary.splitlines())
    summary=summary.replace(', width);',', title_width);')
    body=once(body,'        OlParagraph(line, 15.0F, kOlSoft, title_width);',
        '        OlParagraph(line, 15.0F, kOlSoft, title_width);\n        PaddockGap(16.0F);\n'+summary.rstrip())
    body=once(body,'''        const ImVec2 plate_at = side ? ImVec2{at.x + inner - plate_width, at.y} : ImVec2{at.x, at.y + title_height + 20.0F};
        DrawOlPlate(plate_at, plate_width, frame);
        const float height = side ? std::max(title_height, OlPlateHeight(host) + 4.0F)
                                  : title_height + 20.0F + OlPlateHeight(host) + 4.0F;''',
        '''        const ImVec2 aside_at = side ? ImVec2{at.x + inner - aside_width, at.y}
                                         : ImVec2{at.x, at.y + title_height + 20.0F};
        ImGui::SetCursorScreenPos(aside_at);
        ImGui::BeginGroup();
        DrawOlPlate({aside_at.x + aside_width - plate_width, aside_at.y}, plate_width, frame);
        ImGui::SetCursorScreenPos(aside_at);
        ImGui::Dummy({aside_width, OlPlateHeight(host) + 4.0F});
        PaddockGap(12.0F);
        OlParagraph(visibility,14.F,kOlSoft,aside_width);
        if(host)DrawOlDirectoryVisibility(aside_width,frame.busy||!directory->connected||hosting!=2);
        ImGui::EndGroup();
        const float aside_height = ImGui::GetItemRectSize().y;
        const float height = side ? std::max(title_height, aside_height)
                                  : title_height + 20.0F + aside_height;''')
    return text[:start]+body+text[end:]

def localised_online(text):
    begin=text.index('void DrawOlSteps(');end=text.index('    const int columns',begin)
    steps=text[begin:end].replace('constexpr std::array<std::array<const char*, 2>, 4> steps',
        'std::array<std::array<const char*, 2>, 4> steps')
    steps+='    for(auto& step:steps)for(auto& caption:step)caption=dkr::localisation::source(caption).data();\n'
    text=text[:begin]+steps+text[end:]
    text=once(text,'PaddockWrap(type, g_ol_toast.text, maximum)',
        'PaddockWrap(type, dkr::localisation::source(g_ol_toast.text), maximum)')
    text=once(text,'const auto ping=dkr::relay::lobby_card_ping(', 'auto ping=dkr::relay::lobby_card_ping(')
    text=once(text,'        const float ping_width=(ping.ms?19.F:0.F)+PaddockMeasure(meta,ping.text);',
        '        ping.text=ping.ms?dkr::localisation::format("online.ping",{{"ms",std::to_string(ping.ms)}}):std::string(dkr::localisation::source(ping.text));\n        ping.detail=dkr::localisation::source(ping.detail);\n        const float ping_width=(ping.ms?19.F:0.F)+PaddockMeasure(meta,ping.text);')
    text=once(text,'const auto suffix=std::string(" to host");','const auto suffix=dkr::localisation::text("online.to_host");')
    text=once(text,'const auto route=dkr::relay::lobby_card_route(', 'auto route=dkr::relay::lobby_card_route(')
    text=once(text,'        const auto route_text=PaddockEllipsize(meta,route.text,',
        '        route.text=dkr::localisation::relay_caption(route.text,route.proxy);\n        const auto route_text=PaddockEllipsize(meta,route.text,')
    # Paragraphs can contain player names, lobby titles or chat. Preserve
    # dynamic content; only a literal at the call site is application copy.
    for name in ('OlText','OlHeading','OlParagraphHeight','OlParagraphAt','OlParagraph'):
        match=re.search(r'^(?:inline )?float '+name+r'\(',text,re.M)
        if not match:raise ValueError('Online copy helper missing: '+name)
        begin=match.start();body=text.index(') {',begin)+3
        signature=text[begin:body]
        if 'std::string_view text,' not in signature:raise ValueError('Online copy signature changed: '+name)
        text=text[:begin]+'template<class T> '+signature.replace('std::string_view text,','T&& raw,')+\
            '\n    const auto text=LocalisedUi::copy(std::forward<T>(raw));'+text[body:]
    for name in ('OlButtonWidth','OlButton','OlCta','OlSectionTabWidth','OlSectionTab'):
        match=re.search(r'^(?:inline )?(?:float|bool) '+name+r'\(',text,re.M)
        if not match:raise ValueError('Online action helper missing: '+name)
        body=text.index(') {',match.start())+3
        text=text[:body]+'\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();'+text[body:]
    for name in ('OlFieldLabel','OlRange'):
        match=re.search(r'^(?:inline )?(?:float|bool) '+name+r'\(',text,re.M);body=text.index(') {',match.start())+3
        text=text[:body]+'\n    if(label)label=dkr::localisation::source(label).data();'+text[body:]
    begin=text.index('bool OlSwitchRow(');body=text.index(') {',begin)+3
    text=text[:body]+'\n    title=dkr::localisation::source(title).data();\n    if(description)description=dkr::localisation::source(description).data();'+text[body:]
    begin=text.index('bool OlSelect(');body=text.index(') {',begin)+3
    signature=text[begin:body].replace('std::vector<std::string>& items,','std::vector<std::string>& raw_items,')
    text=text[:begin]+signature+'\n    const auto items=LocalisedUi::enum_items(raw_items);'+text[body:]
    begin=text.index('void DrawOlFeature(');body=text.index(') {',begin)+3
    return text[:body]+'\n    title=dkr::localisation::source(title).data();\n    if(description)description=dkr::localisation::source(description).data();'+text[body:]

def parity_online(text):
    text=once(text,'const bool rom_slot = launcher && g_launcher_rom != nullptr;',
        'const bool rom_slot = g_launcher_rom != nullptr;')
    text=once(text,'            if (known) {\n                DrawRomSelect("online-rom", slot_width, true, active,',
        '            ImGui::BeginDisabled(!launcher);\n            if (known) {\n                DrawRomSelect("online-rom", slot_width, true, active || !launcher,')
    text=once(text,'            const std::string_view hint = active ?',
        '            ImGui::EndDisabled();\n            const std::string_view hint = active ?')
    return localised_online(text)

def profile_layout(text):
    return once(text,'''    case OlSection::Profile:
        {
            const OlEnter enter(0);
            DrawOlProfileCard(width, frame, true);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(1);
            DrawOlPrivacy(width);
        }
        PaddockGap(20.0F);
        {
            const OlEnter enter(2);
            DrawOlFriendCodes(width);
        }''', '''    case OlSection::Profile:
        DrawOlProfileSections(width, frame);''')

def stability_ui(text):
    text=once(text,'void DrawOlHostSettings(float width) {',
        '#include "netplay/peer_relay/ui.inl"\n#include "netplay/session_chat/ui.inl"\n\nvoid DrawOlHostSettings(float width) {')
    text=once(text,'void DrawOlOverlays(float width) {',
        'void DrawOlOverlays(float width) {\n    DrawSessionChatPreferences(width);')
    text=once(text,'        DrawOlGrid(inner, frame);','        DrawSessionChatLobby(inner, frame);')
    text=once(text,'        changed |= OlSwitchRow("##record-replay",',
        '        DrawOlProxyHost(inner);\n        PaddockGap(16.F);\n        changed |= OlSwitchRow("##record-replay",')
    text=once(text,'        DrawOlConnection(width, frame);',
        '        DrawOlProxyConnection(width);\n        DrawOlConnection(width, frame);')
    grid_start=text.index('void DrawOlGrid(');grid_end=text.index('void DrawOlGate(',grid_start)
    grid=text[grid_start:grid_end]
    grid=grid.replace('view.room.players','card_players')
    grid=once(grid,'    const SessionView& view = frame.view;',
        '    const SessionView& view = frame.view;\n'
        '    const auto proxy_routes=dkr::relay::view(&dkr::runtime::netplay::session());\n'
        '    auto card_players=view.room.players;\n'
        '    for(unsigned slot=0;slot<card_players.size();++slot)if(auto& player=card_players[slot];player.occupied&&player.peer_id.empty())\n'
        '        player.peer_id=dkr::relay::lobby_card_peer(proxy_routes,player.peer_id,player.host,slot==view.local_slot,slot,view.room.generation);')
    grid=once(grid,'        return items;',
        '''        const auto route=dkr::relay::lobby_card_route(proxy_routes,player.peer_id,player.host,you,unsigned(slot),view.room.generation);
        const auto route_text=PaddockEllipsize(meta,route.text,std::max(40.F,body_width(player,OlPartyActive()||(view.host&&!you))));
        const float route_width=PaddockMeasure(meta,route_text);
        items.push_back({route_width,[&,route,route_text,route_width](ImVec2 at) {
            PaddockDrawRun(draw,meta,at,PaddockCol(route.proxy?kOlGoText:kOlSoft),route_text.data(),route_text.data()+route_text.size());
            if(ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(at,{at.x+route_width,at.y+meta.line}))
                ImGui::SetTooltip("%s\\nConnection to the lobby host. Updates automatically.",route.text.c_str());
        }});
        return items;''')
    # A local client needs its host-relative RTT too. All peer cards use the
    # same fresh, authenticated host snapshot rather than default zero values.
    ping_start=grid.index('        if (you) {\n            items.push_back({PaddockMeasure(meta, "This PC")')
    ping_end=grid.index('        const auto route=dkr::relay::lobby_card_route(',ping_start)
    grid=grid[:ping_start]+'''        const auto ping=dkr::relay::lobby_card_ping(proxy_routes,player.peer_id,player.host,you,unsigned(slot),view.room.generation);
        const float ping_width=(ping.ms?19.F:0.F)+PaddockMeasure(meta,ping.text);
        if(!player.host||you)items.push_back({ping_width,[&,ping,ping_width](ImVec2 at) {
            if(!ping.ms)PaddockDrawRun(draw,meta,at,PaddockCol(kOlSoft),ping.text.data(),ping.text.data()+ping.text.size());
            if(ping.ms){const auto suffix=std::string(" to host");
                const auto x=at.x+DrawOlPing(draw,at,meta.line,ping.ms);
                PaddockDrawRun(draw,meta,{x,at.y},PaddockCol(kOlSoft),suffix.data(),suffix.data()+suffix.size());}
            if(ImGui::IsWindowHovered()&&ImGui::IsMouseHoveringRect(at,{at.x+ping_width,at.y+meta.line}))
                ImGui::SetTooltip("%s",ping.detail.c_str());
        }});
'''+grid[ping_end:]
    text=text[:grid_start]+grid+text[grid_end:]
    text=once(text,'void DrawOlLobby(float width, const OnlineFrame& frame) {',
        'void DrawOlJoiningProgress(float,const OnlineFrame&);\nvoid DrawOlLiveDelay(float);\n'
        'void DrawOlLobby(float width, const OnlineFrame& frame) {')
    start=text.index('void DrawOlLobby(float');end=text.index('// Knocking on the host',start)
    body=text[start:end]
    body=once(body,'    const SessionView& view = frame.view;',
        '    DrawOlJoiningProgress(width,frame);\n    DrawOlLiveDelay(width);\n    const SessionView& view = frame.view;')
    text=text[:start]+body+text[end:]
    text=once(text,'void DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {',
        '#include "online_stability/lobby_ui.inl"\n\nvoid DrawOnlinePage(float available_width, bool launcher, bool rom_ready) {')
    text=text.replace('"Automatic input delay"','"Dynamic Input Delay"').replace('"Use automatic input delay"','"Use Dynamic Input Delay"').replace('"Input delay (frames)"','"Manual input delay (frames)"')
    return text.replace('"Adjusts input delay for the lobby\'s connection."',
        '"Continuously balances input delay and connection stability during gameplay."')

def profile_account_ui(text,out):
    text=once(text,'// .ol-card: a padded panel around content measured as it is drawn.',
        '#include "'+(out/'profile_panels.inl').as_posix()+'"\n\n// .ol-card: a padded panel around content measured as it is drawn.')
    text=once(text,'    PaddockBox box(width, {padding, padding});\n    content(box.Inner());\n    box.End([&](ImDrawList* draw, ImVec2 a, ImVec2 b) { PaintOlPanel(draw, a, b, look); });',
        '    auto* equal = g_equal_profile_row;\n    const bool capture = equal && std::exchange(equal->capture_next, false);\n'
        '    PaddockBox box(width, {padding, padding});\n    content(box.Inner());\n'
        '    box.End([&](ImDrawList* draw, ImVec2 a, ImVec2 b) {\n'
        '        if(capture && equal->count < equal->panels.size()) equal->panels[equal->count++] = {a,b,look,equal->section};\n'
        '        else PaintOlPanel(draw, a, b, look);\n    });')
    text=once(text,'void DrawOlDirectoryAccount(float);',
        'extern bool g_open_discord_profile;\nvoid DrawOlSelfAvatar(ImDrawList*, ImVec2, float, std::string_view);\n'
        'void DrawOlProfileAvatar(ImDrawList*, ImVec2, float, std::string_view, const std::string&, bool = false);\n'
        'void DrawOlPictureControls(float,bool);\nvoid SaveOlProfilePicture(bool = false);\n'
        'void DrawOlRememberSignIn(float,const OnlineFrame&);\nvoid DrawOlSignOut(float,const OnlineFrame&);\n'
        'void DrawOlPrivacy(float);\nvoid DrawOlFriendCodes(float);\n'
        'void DrawOlAccountSettings(float,const OnlineFrame&);\nvoid DrawOlDirectoryAccount(float);')
    begin=text.index('void DrawOlProfileCard(float width, const OnlineFrame& frame, bool profile_section) {')
    end=text.index('// ------------------------------------------------------------ open lobbies',begin)
    text=text[:begin]+'#include "'+(out/'profile_header.inl').as_posix()+'"\n\n'+text[end:]
    text=once(text,'void DrawOlModals(const OnlineFrame& frame) {',
        '#include "'+(out/'profile_ui.inl').as_posix()+'"\n\nvoid DrawOlModals(const OnlineFrame& frame) {')
    return text


def friends_layout(text,out):
    text=once(text,'    std::string name;          // for the avatar',
        '    std::string avatar_url;\n    std::string name;          // for the avatar')
    text=once(text,'void DrawOlFriends(float width, const OnlineFrame& frame) {',
        '#include "'+(out/'friends_cards.inl').as_posix()+'"\n\nvoid DrawOlFriends(float width, const OnlineFrame& frame) {')
    begin=text.index('void DrawOlFriends(');end=text.index('// ------------------------------------------------------------ overlays',begin)
    body=text[begin:end]
    body=once(body,'        row.key = "friend-" + racer.identity;',
        '        row.key = "friend-" + racer.identity;\n        row.avatar_url = racer.blocked?std::string{}:dkr::profile::bridge::peer_avatar(racer.identity);')
    body=once(body,'        row.actions.push_back({"Manage", OlTone::Plain, false, nullptr, [identity, racer] {',
        '''        if(racer.online && racer.hosting && !racer.blocked && !racer.lobby_code.empty() && !frame.in_lobby && !frame.active) {
            row.status="Hosting - "+std::to_string(racer.players)+"/"+std::to_string(racer.maximum_players)+" racers";
            row.actions.push_back({"Join",OlTone::Go,!frame.rom_ready || frame.busy,nullptr,[racer]{
                std::string error;g_online_page.join_attempted=true;g_online_page.join_error.clear();
                ConfigurePartyJoin();if(!dkr::runtime::netplay::session().join(racer.lobby_code,g_online_player_name,error))OlNotify(error);
            }});
        }
        row.actions.push_back({"Manage", OlTone::Plain, false, nullptr, [identity, racer] {''')
    add_start=body.index('        // Add a friend.')
    add_end=body.rindex('    });')
    add=body[body.index('        OlParagraph("Add a friend",',add_start):add_end]
    add=add.replace('OlButtonWidth("Add",','OlButtonWidth("Add Friend",').replace('OlButton("Add",','OlButton("Add Friend",')
    body=body[:add_start]+body[add_end:]
    body=once(body,'        // Toolbar.',add+'        PaddockGap(16.F);\n        // Toolbar.')
    groups_start=body.index('        // Groups.')
    groups_end=body.index('        if (racers.empty())',groups_start)
    body=body[:groups_start]+'''        static bool requests_open=false;
        const auto requests=incoming.size()+outgoing.size();
        const std::string title="Friend requests ("+std::to_string(requests)+")";
        if(OlButton(title.c_str(),OlTone::Plain,inner))requests_open=!requests_open;
        if(requests_open){
            for(const auto* rows:{&incoming,&outgoing})for(const auto& row:*rows){PaddockGap(8.F);DrawOlFriendRow(row,inner);}
        }
        PaddockGap(14.F);
        DrawOlFriendCards(friends,inner);
'''+body[groups_end:]
    text=text[:begin]+body+text[end:]
    text=once(text,'// ------------------------------------------------------------ lobby\n',
        '#include "'+(out/'lobby_friends_ui.inl').as_posix()+'"\n\n// ------------------------------------------------------------ lobby\n')
    grid_start=text.index('void DrawOlGrid(');grid_end=text.index('void DrawOlGate(',grid_start)
    grid=text[grid_start:grid_end]
    grid=once(grid,'        heights[static_cast<std::size_t>(slot)] = std::max(76.0F, body_height + 20.0F);',
        '        heights[static_cast<std::size_t>(slot)] = std::max(76.0F, body_height + 20.0F) +\n'
        '            (OlLobbyFriendAction(player.peer_id,static_cast<std::size_t>(slot)==view.local_slot)?48.F:0.F);')
    grid=once(grid,'            DrawOlAvatar(draw, {x, a.y + std::round((row_height - 38.0F) * 0.5F)}, 38.0F, player.display_name);',
        '            const float friend_footer=OlLobbyFriendAction(player.peer_id,you)?48.F:0.F;\n'
        '            const float content_height=row_height-friend_footer;\n'
        '            DrawOlProfileAvatar(draw, {x, a.y + std::round((content_height - 38.0F) * 0.5F)}, 38.0F, player.display_name,OlLobbyAvatar(player.peer_id,you));')
    grid=once(grid,'            const float top = a.y + std::round((row_height - body_height) * 0.5F);',
        '            const float top = a.y + std::round((content_height - body_height) * 0.5F);')
    grid=once(grid,'a.y + std::round((row_height - 40.0F) * 0.5F)', 'a.y + std::round((content_height - 40.0F) * 0.5F)')
    grid=once(grid,'            ImGui::PopID();\n        }\n        y += row_height + 12.0F;',
        '            if(friend_footer>0.F){ImGui::SetCursorScreenPos({a.x+66.F,b.y-48.F});DrawOlLobbyFriendAction(player.peer_id,cell-78.F);}\n'
        '            ImGui::PopID();\n        }\n        y += row_height + 12.0F;')
    return text[:grid_start]+grid+text[grid_end:]

def account_service(text,out):
    # Extend the retained service at its existing composition point. The history
    # policy is normal first-party source, shared with the ROM-free host tests.
    history=ROOT/'runtime-recomp/src/game/mod_store/download_history.hpp'
    text='#include "'+history.as_posix()+'"\n'+text
    begin=text.index('        Json rows = Json::array();',text.index('    void persist_downloads()'))
    end=text.index('\n    }',begin)
    text=text[:begin]+'''        Json saved;
        {
            std::lock_guard lock(mutex);
            saved = dkr::store::history::save<Json>(state.downloads);
        }
        write_json(root / "downloads.json", saved);'''+text[end:]
    text=once(text,'        const auto saved = read_json(p.root / "downloads.json");',
        '''        const auto saved = read_json(p.root / "downloads.json");
        const auto receipts = read_json(p.root / "installations.json");
        auto restored = dkr::store::history::restore<Download>(saved, receipts, [&](const std::string& path) {
            return dkr::store::history::member_present(p.root.parent_path(), path);
        });''')
    begin=text.index('            if (saved.contains("jobs")')
    end=text.index('\n        });',begin)
    text=text[:begin]+'            s.downloads = std::move(restored);'+text[end:]
    text=once(text,'            s.downloads = std::move(restored);\n        });',
        '''            s.downloads = std::move(restored);
        });
        if (saved.is_object() && saved.contains("jobs") && saved["jobs"].is_array() &&
            saved["jobs"].size() <= 64 && (!saved.contains("schema") || saved["schema"] == 1 || saved["schema"] == 2)) {
            try { p.persist_downloads(); } catch (const std::exception& error) { p.fail(error); }
        }''')
    text=once(text,'namespace dkr::store {',
        '#include "'+(out/'display_name_save.hpp').as_posix()+'"\n#include "'+(out/'profile_bridge.hpp').as_posix()+'"\n#include "'+(out/'picture_codec.hpp').as_posix()+'"\n#include "'+(out/'profile_retry.hpp').as_posix()+'"\n#define DKR_PROFILE_WITH_KEYRING 1\n#include "'+(out/'linux_device_store.hpp').as_posix()+'"\n\nnamespace dkr::store {')
    for operation in ('store','load','clear'):
        text=text.replace('dkr::online::keyring::'+operation+'(', 'dkr::profile::devices::'+operation+'(')
    text=text.replace('if(!dkr::online::keyring::available())s.remember = false;','// Linux supports owner-only device storage when a keyring is unavailable.')
    text=text.replace('if(!dkr::online::keyring::available())remember = false;','// The preference is available on Linux without a keyring too.')
    text=text.replace('The desktop keyring could not remember sign-in. You are signed in for this session only.',
        'Sign-in could not be saved on this device. You are signed in for this session only.')
    text=once(text,'        auto user = me.value("user", Json{});',
        '''        auto user = me.value("user", Json{});
        // Optional on older deployments. Profile metadata never blocks sign-in.
        try {
            auto profile = get("/v1/launcher/discord-profile", stop, true);
            if(profile.is_object() && text(profile,"accountId",80)==text(user,"id",80) &&
               !dkr::profile::display_name(text(profile,"displayName",200)).empty()) user["discordProfile"]=std::move(profile);
        } catch(...) {}''')
    text=once(text,'        // Optional on older deployments. Profile metadata never blocks sign-in.',
        '''        try {
            const auto public_key=dkr::profile::bridge::card().public_key;
            auto profile = get("/v1/launcher/profile?key="+public_key, stop, true);
            if(profile.is_object() && text(profile,"accountId",80)==text(user,"id",80)) user["launcherProfile"]=std::move(profile);
        } catch(...) {}
        // Optional on older deployments. Profile metadata never blocks sign-in.''')
    text=once(text,'    void load_image(const std::string& url, std::stop_token stop) {',
        '#include "'+(out/'profile_service.inl').as_posix()+'"\n\n    void load_image(const std::string& url, std::stop_token stop) {')
    text=once(text,'        "catalogue.update", "admin.role",     "account.profile",   "account.delete"};',
        '        "catalogue.update", "admin.role",     "account.profile",   "account.delete", "launcher.profile", "launcher.profile.preview"};')
    text=once(text,'        Json response;\n        try {\n            response = document(',
        '        if(name=="launcher.profile" || name=="launcher.profile.preview") { p.profile_action(name,input,epoch,credential,stop); return; }\n'
        '        Json response;\n        try {\n            response = document(')
    text=once(text,'        p.update([&](Snapshot& s) {\n            s.status = "Your change was saved.";',
        '\n'.join([
            '        if(name=="account.profile") {',
            '            const auto accepted=response.value("result",Json{});',
            '            if(text(accepted,"id",80)!=text(p.published.load()->user,"id",80) ||',
            '               !dkr::profile::complete_display_name(text(accepted,"displayName",200)))',
            '                throw Error("The account service returned an invalid display name. Refresh and try again.");',
            '        }',
            '        p.update([&](Snapshot& s) {',
            '            if(p.account!=epoch) return;',
            '            if(name=="account.profile") s.user["displayName"]=response["result"]["displayName"];',
            '            s.status = "Your change was saved.";']))
    text=once(text,'        if (name == "account.delete") {',
        '        if(name=="account.profile") { dkr::online::refresh_profile(); return; }\n        if (name == "account.delete") {')
    text=once(text,'            auto r = http({url, "GET", {}, {}, "application/json", {}, 0, MaxImage}, stop);',
        '''            std::string profile_credential;
            if(dkr::profile::custom_avatar_url(url)) {
                std::lock_guard lock(mutex);
                const auto profile=state.user.is_object()?state.user.value("launcherProfile",Json{}):Json{};
                if(text(profile,"avatarUrl",180)==url)profile_credential=token;
            }
            auto r = http({url, "GET", {}, profile_credential, "application/json", {}, 0, MaxImage}, stop);''')
    text=once(text,'prefix(url, std::string(Site) + "/dkr-r/assets/");',
        'prefix(url, std::string(Site) + "/dkr-r/assets/") || dkr::profile::avatar_url(url);')
    text=once(text,'    p.queue([&p](std::stop_token) { p.preferences(); });\n}\nvoid Service::refresh()',
        '''    p.queue([&p](std::stop_token) {
        p.preferences();
        if(!p.published.load()->remember) {
#ifdef _WIN32
            std::error_code error;std::filesystem::remove(p.root/"device.bin",error);
#else
            dkr::profile::devices::clear(p.root.generic_string());
#endif
        }
    });
}
void Service::refresh()''')
    return text

def compose(rows,out):
    if len(rows)!=5 or {Path(r['path']).name for r in rows}!={'runtime_ui.cpp','runtime_mod_library_ui.inl','mod_store.cpp','runtime_platform.cpp','camera_rt64.cpp'}:raise ValueError('Portrait source closure differs')
    files={p.name:p.read_bytes() for p in (AUTH/'native').glob('*')};swaps=[]
    files.update({p.name:p.read_bytes() for p in (ROOT/'runtime-recomp/src/game/online_profile').glob('*') if p.is_file()})
    store_module=ROOT/'runtime-recomp/src/game/mod_store'
    account_source=next(Path(row['path']) for row in rows if Path(row['path']).name=='mod_store.cpp')
    store_header=re.findall(r'^#include "([^"\n]*mod_store.hpp)"',account_source.read_text(),re.M)
    if len(store_header)!=1:raise ValueError('Expected one retained store API header')
    store_base=Path(store_header[0]).parent
    for name in ('profile_bridge.hpp','profile_policy.hpp'):
        # One definition of the bridge registry across UI, account and social TUs.
        files[name]=('#include "'+(ROOT/'runtime-recomp/src/game/online_profile'/name).as_posix()+'"\n').encode()
    def stage_include(text,ending,change,relative_base=None):
        paths=re.findall(r'^#include "([^"\n]*'+re.escape(ending)+r')"',text,flags=re.M)
        if len(paths)!=1:raise ValueError('Expected final include: '+ending)
        original=Path(paths[0]);original=original if original.is_absolute() else (relative_base or store_base)/original
        raw=original.read_bytes();copy=change(raw.decode().replace('\r\n','\n'))
        if ending.endswith('.inl'):
            copy=library_copy(copy)
        def absolute(m):
            path=Path(m[1]);path=path if path.is_absolute() else original.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        copy=re.sub(r'^#include "([^"\n]+)"',absolute,copy,flags=re.M)
        files[ending]=copy.encode()
        swaps.append({'target':'DKRPortGame','path':str(original),'new':str(out/ending),'input_sha256':sha(raw),'output_sha256':sha(copy.encode()),'compiled':False})
        return once(text,'#include "'+paths[0]+'"','#include "'+(out/ending).as_posix()+'"')
    def localised_settings(text):
        # Retained capsule bases stay pinned. These existing presentation
        # boundaries own captions/help, while IDs and choices from ROM files
        # or controller names retain their original bytes.
        begin=text.index('struct SettingsChoice {');end=text.index('\n};',begin)+3
        text=text[:begin]+'using SettingsChoice=dkr::localisation::Choice;'+text[end:]
        text=once(text,'PaddockEllipsize(label_type, choice.label, text_width)',
            'PaddockEllipsize(label_type, choice.display_label(), text_width)')
        text=once(text,'PaddockEllipsize(detail_type, choice.detail, text_width)',
            'PaddockEllipsize(detail_type, choice.display_detail(), text_width)')
        text=once(text,'choices.empty() ? "Unavailable" : choices[static_cast<std::size_t>(*value)].label',
            'choices.empty() ? dkr::localisation::source("Unavailable") : choices[static_cast<std::size_t>(*value)].display_label()')
        for name,parameter in (('SettingsLabel','label'),('SettingsHelp','text'),
                ('SettingsCheck','label'),('SettingsRange','label'),
                ('SettingsCardTitle','title'),('SettingsDisclosure','label')):
            begin=text.index(name+'(');body=text.index(') {',begin)+3
            text=text[:body]+'\n    '+parameter+'=dkr::localisation::source('+parameter+');'+text[body:]
        text=once(text,'for (const char* item : items) choices.push_back({item, {}, false});',
            'for (const char* item : items) choices.push_back({std::string(dkr::localisation::source(item)), {}, false});')
        for name in ('SettingsRaceButton','SettingsButton'):
            begin=text.index('bool '+name+'(');body=text.index(') {',begin)+3
            text=text[:body]+'\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();'+text[body:]
        begin=text.index('float SettingsButtonWidth(');body=text.index(') {',begin)+3
        text=text[:body]+'\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();'+text[body:]
        begin=text.index('bool BeginSettingsDialog(');body=text.index(') {',begin)+3
        return text[:body]+'\n    heading=dkr::localisation::source(heading);'+text[body:]
    def localised_sound(text):
        for name,code in (
                ('DrawSoundRowLabel','label=dkr::localisation::source(label);'),
                ('DrawSoundLinkButton','const auto caption=dkr::localisation::label(label);label=caption.c_str();\n    if(hint)hint=dkr::localisation::source(hint).data();'),
                ('SoundChipWidth','label=dkr::localisation::source(label).data();'),
                ('DrawSoundChip','const auto caption=dkr::localisation::label(label);label=caption.c_str();'),
                ('DrawSoundCardHead','title=dkr::localisation::source(title).data();caption=dkr::localisation::source(caption);')):
            begin=text.index(name+'(');body=text.index(') {',begin)+3
            text=text[:body]+'\n    '+code+text[body:]
        return text
    def localised_messages(text):
        # Classify canonical diagnostics first. Translate only the resulting
        # owned summary; the original detail and clipboard bytes stay intact.
        text=once(text,'DrawColoredWrapped(kWarm, summary);',
            'DrawColoredWrapped(kWarm, dkr::localisation::source(summary));')
        text=once(text,'PaddockFeedback(text, width);',
            'PaddockFeedback(dkr::localisation::source(text), width);')
        return once(text,'if (ImGui::CollapsingHeader(label)) {',
            'const auto caption=dkr::localisation::label(label);\n    if (ImGui::CollapsingHeader(caption.c_str())) {')
    def directory_ui(text):
        text=once(text,'const bool secure_remember=dkr::online::keyring::available();','const bool secure_remember=true;')
        return once(text,'"Protected by this computer\'s credential store."',
            '"Uses the system credential store when available. Linux without a keyring saves a file readable only by your Linux user."')
    def account_ui(text):
        text=full_page_width(text)
        text=once(text,'const bool can_remember=dkr::online::keyring::available();','const bool can_remember=true;')
        text=once(text,'''        StoreField("profile-name", "Display name", profile.inner(), 60);
        ImGui::BeginDisabled(g_mod_store.fields["profile-name"].empty() ||
                             g_mod_store.fields["profile-name"] == StoreText(store.user, "displayName", 60));
        if (StoreButton("Save name", 0, true))
            StoreAction("account.profile", {{"displayName", g_mod_store.fields["profile-name"]}});
        ImGui::EndDisabled();''',
            '        DrawOlStoreProfileName(profile.inner(), StoreBusyAction(store));')
        return once(text,'"Your desktop keyring remembers this sign-in."','"Sign-in is remembered on this device. Linux uses its keyring or a file restricted to your Linux user."')
    def store_shell(text):
        text=once(text,'void DrawModStore(float width, bool locked) {',
            'void DrawModStore(float width, bool locked, bool game_running = false) {\n    g_store_in_game=game_running;')
        text=once(text,'    StorePumpInstall();','    if(!game_running)StorePumpInstall();')
        text=once(text,'StoreNote(store->status, width, false);',
            'StoreNote(dkr::localisation::source(store->status), width, false);')
        text=once(text,'    StoreStartQueuedInstall(locked || store->writing);','    StoreStartQueuedInstall(locked || game_running || store->writing);')
        text=once(text,'"Community mods for DKR-R"','"Free community mods for DKR-R"')
        text=once(text,'"Signed in as " + StoreText(store->user, "displayName", 100)',
            '"Free community mods. Signed in as " + StoreText(store->user, "displayName", 100)')
        begin=text.index('    const bool header =\n')
        end=text.index('    if (store->cached && !store->loading)',begin)
        text=text[:begin]+'''    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0.F,0.F});
    DrawLibraryPageHeader(width,*store);
    ImGui::PopStyleVar();
'''+text[end:]
        text=once(text,'    if (store->cached && !store->loading)',
            '    StoreNote(dkr::localisation::source(dkr::store::LegacyRomInstallNotice),width,false);\n    PaddockGap(8.F);\n    if (store->cached && !store->loading)')
        text=once(text,'    const auto active = std::count_if(',
            '    DrawLauncherFeatured(width,game_running);\n    const auto active = std::count_if(')
        return once(text,'    auto store = service.view();',
            '    auto store = service.view();\n    if(game_running&&g_store_overlay_test)store=StoreOverlayFixture(*store);')
    def store_details(text):
        text=once(text,'{"Overview", "Reviews", "Install help", "Releases"}',
            '{"Overview", "Reviews", "Install help", "Releases", "Comments"}')
        text=once(text,'        StoreNote(StoreRating(row), inner);','        StoreNote(StoreRating(row), inner);\n        StoreCommunityCounts(row,inner);')
        return once(text,'    } else if (row.contains("releases") && row["releases"].is_array()) {',
            '    } else if(g_mod_store.detail_tab==4) {\n        StoreComments(row,width,store);\n    } else if (row.contains("releases") && row["releases"].is_array()) {')
    def full_page_width(text):
        text=re.sub(r'^    width = std::min\(width, \d+\.F\);\n','',text,flags=re.M)
        return re.sub(r'(StorePanel \w+\()std::min\(width, \d+\.F\)(\))',r'\1width\2',text)
    def management_ui(text):
        text=full_page_width(text)
        text=stage_include(text,'store_player_library.inl',full_page_width)
        text=once(text,'    const bool safe = !dkr::runtime::netplay::session().active()',
            '    const bool safe = !g_store_in_game && !dkr::runtime::netplay::session().active()')
        return once(text,
            '                              g_mods_page.entry_section = 4;\n                              g_mods_page.section = 4;\n                              g_page_navigation_request = kPageModsHacks;\n                              g_mod_store.page = 5;',
            '                              OpenModLibrary(5);')
    def store_ui(text):
        anchor='if (!g_mod_store.install_after_download.contains(job.key))'
        if anchor in text:text=once(text,anchor,'if (!g_mod_store.install_after_download.contains(job.key) && job.intent != "activate")')
        text=once(text,'int StoreCombo(const char* id, int current, const std::vector<std::string>& labels, float width) {',
            'int StoreCombo(const char* id, int current, const std::vector<std::string>& raw_labels, float width) {\n    const auto labels=LocalisedUi::enum_items(raw_labels);')
        text=once(text,'    StoreNote(id, width);','    StoreNote(dkr::localisation::source(id), width);')
        text=once(text,'for (const auto& [label, action] : actions) {',
            'for (const auto& [raw_label, action] : actions) {\n        const auto label=dkr::localisation::label(raw_label);')
        for expression in ('selected_update ? "Update available" : compact ? "Installed" : "Installed - manage"',):
            text=once(text,'StoreButton('+expression+',','StoreButton(dkr::localisation::label('+expression+'),')
        text=once(text,'StoreButton(std::string(label) + "##download",',
            'StoreButton(dkr::localisation::label(std::string(label) + "##download"),')
        text=once(text,'void StoreNote(const std::string& value, float width, bool muted = true) {',
            'template<class T> void StoreNote(T&& raw, float width, bool muted = true) {\n    const std::string value(LocalisedUi::copy(std::forward<T>(raw)));')
        text=once(text,'void StoreHeading(const std::string& value, float width, float size = 23.F) {',
            'template<class T> void StoreHeading(T&& raw, float width, float size = 23.F) {\n    const std::string value(LocalisedUi::copy(std::forward<T>(raw)));')
        text=once(text,'bool StoreButton(const std::string& label, float width = 0, bool primary = false) {',
            'template<class T> bool StoreButton(T&& raw, float width = 0, bool primary = false) {\n    const std::string label=LocalisedUi::widget(std::forward<T>(raw));')
        text=once(text,'// Native Mod Store: included only in the staged launcher translation unit.\n// No browser engine, remote script, store texture or network worker enters RT64.',
            '// Native Mod Store shared by launcher and Inspector overlay.\n// Each renderer owns its own textures; HTTP and image decoding stay on workers.')
        text=stage_include(text,'store_account_page.inl',account_ui)
        text=stage_include(text,'store_page_shell.inl',store_shell)
        text=stage_include(text,'mod_store_style.inl',store_style)
        text=stage_include(text,'store_player_details.inl',store_details)
        text=stage_include(text,'store_player_activity.inl',full_page_width)
        text=stage_include(text,'store_staff_pages.inl',full_page_width)
        text=stage_include(text,'mod_store_management_ui.inl',management_ui)
        text=once(text,'void StoreCover(const StoreJson& row, float width, float height = 0, bool contain = false) {',
            'void StoreCover(const StoreJson& row, float width, float height = 0, bool contain = false, float rounding = 8.F) {')
        text=once(text,'PaddockRgb(muted ? 0xBDCFD9 : 0xFFF4D3), value, width);',
            'PaddockRgb(muted ? 0xBDCFD9 : 0xFFF4D3), dkr::library_ui::visible_status(value), width);')
        text=once(text,'SDL_Renderer* g_mod_store_renderer = nullptr;',
            'SDL_Renderer* g_mod_store_renderer = nullptr;\nbool g_store_in_game=false;')
        text=once(text,'void StoreOpenMod(const StoreJson& row, bool detail) {',
            '#include "'+(store_module/'community_ui.inl').as_posix()+'"\n#include "'+(store_module/'overlay_test_ui.inl').as_posix()+'"\n\nvoid StoreOpenMod(const StoreJson& row, bool detail) {')
        text=once(text,'    if (!repair_instance.empty()) {','''    if(g_store_in_game) {
        const bool prepared=!StoreText(release,"id",160).empty()&&StoreText(release,"sha256",64).size()==64;
        ImGui::BeginDisabled(!prepared);
        if(StoreButton(dkr::localisation::label(compact?"Queue":"Queue download for launcher"),width,true))StoreQueueRelease(row,release,
            repair_instance.empty()?"":"reinstall",repair_instance);
        ImGui::EndDisabled();
        if(compact&&(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)||ImGui::IsItemFocused()))
            ImGui::SetTooltip("Queue this mod for the launcher. Installation waits until the game stops.%s",
                prepared?"":" Prepare this release in the launcher before downloading.");
        if(!prepared&&!compact)StoreNote("Prepare this release in the launcher before downloading.",width);
        return;
    }
    if (!repair_instance.empty()) {''')
        begin=text.index('void StoreGridCard(');end=text.index('void StoreBrowser(',begin)
        details=re.search(r'^#include "[^"\n]*store_player_details.inl"',text[begin:end],re.M)
        if not details:raise ValueError('Expected the retained listing details include')
        card='#include "'+(ROOT/'runtime-recomp/src/game/library_ui/compact_cards.inl').as_posix()+'"\n'+text[begin:end][details.start():]
        text=text[:begin]+card+text[end:]
        begin=text.index('                float title_height = 0, author_height = 0;')
        end=text.index('                for (int j = base;',begin)
        # Replace the measurement loop as a whole, keeping the card's existing
        # navigation/install call sites and stable per-listing IDs.
        end=text.index('                for (int j = base;',end+1)
        text=text[:begin]+'''                float title_height=0;
                for(int j=base;j<std::min(limit,base+columns);++j)
                    title_height=std::max(title_height,PaddockTextHeight(PaddockSign(20.F,1.25F),
                        StoreGridTitle(StoreText(*rows[j],"title",200),text_width),text_width));
'''+text[end:]
        text=once(text,'''                    StoreGridCard(row, ImGui::GetContentRegionAvail().x, title_height,
                                  title_height + author_height + PaddockBadgeSize("x").y + 32.F +
                                      6.F * ImGui::GetStyle().ItemSpacing.y);''',
            '                    StoreGridCard(row, ImGui::GetContentRegionAvail().x, title_height);')
        begin=text.index('void StoreField(');end=text.index('int StoreCombo(',begin)
        text=text[:begin]+'#include "'+(ROOT/'runtime-recomp/src/game/library_ui/store_field.inl').as_posix()+'"\n'+text[end:]
        browser=text.index('void StoreBrowser(')
        start=text.index('    if (width >= 720.F &&',browser)
        end=text.index('    std::vector<std::string> categories;',start)
        text=text[:start]+'    DrawLibraryBrowseToolbar(width);\n'+text[end:]
        text=once(text,'void StoreBrowser(float width, const dkr::store::Snapshot& store) {',
            '#include "'+(ROOT/'runtime-recomp/src/game/library_ui/browse_toolbar.inl').as_posix()+'"\nvoid StoreBrowser(float width, const dkr::store::Snapshot& store) {')
        start=text.index('    if (store.layout == 1 && width >= 760.F) {',browser)
        end=text.index('        const int columns = store.layout == 1 ? 1 :',start)
        text=text[:start]+'    {\n'+text[end:]
        text=once(text,'    if (texture) {\n        const auto image = dkr::store::service().image(url);', '''    dkr::store::overlay::View native;
    if(!g_mod_store_renderer&&dkr::store::trusted_url(url)) {
        const auto pixels=dkr::store::service().image(url);
        native=dkr::store::overlay::image(url,pixels?pixels->width:0,pixels?pixels->height:0,
            pixels?std::span<const unsigned char>(pixels->rgba):std::span<const unsigned char>{});
        if(!pixels&&!native.id)dkr::store::service().request_image(url);
    }
    if (texture || native.id) {
        const auto image = dkr::store::service().image(url);''')
        text=once(text,'const float source = image ? float(image->width) / float(image->height) : 1.8F,',
            'const float source = native.id?native.aspect:image ? float(image->width) / float(image->height) : 1.8F,')
        text=once(text,'AddImage(reinterpret_cast<ImTextureID>(texture),',
            'AddImageRounded(native.id?reinterpret_cast<ImTextureID>(native.id):reinterpret_cast<ImTextureID>(texture),')
        text=once(text,'image_min, image_max, a, b);','image_min, image_max, a, b, IM_COL32_WHITE, rounding);')
        text=once(text,'PaddockCol(0x184459), 8.F);','PaddockCol(0x184459), rounding);')
        text=once(text,'const int columns = store.layout == 1 ? 1 : std::clamp(int((width + 16.F) / 290.F), 1, 4);',
            'const int columns = dkr::library_ui::grid_columns(width);\n        if(g_compact_card_capture)ImGui::SetScrollY(std::max(0.F,ImGui::GetCursorPosY()-24.F));')
        text=once(text,'#include "store_polish_modals.inl"',
            '#include "store_polish_modals.inl"\n#include "'+(ROOT/'runtime-recomp/src/game/library_ui/featured_ui.inl').as_posix()+'"\n#include "'+(ROOT/'runtime-recomp/src/game/library_ui/header_ui.inl').as_posix()+'"')
        return text
    def store_style(text):
        text=once(text,'return kStoreCategoryNames[i];','return std::string(dkr::localisation::source(kStoreCategoryNames[i]));')
        text=once(text,'    return "Mod";','    return std::string(dkr::localisation::source("Mod"));')
        start=text.index('std::string StoreRating(');end=text.index('std::string StoreSummary(',start)
        text=text[:start]+'#include "'+(ROOT/'runtime-recomp/src/game/localisation/library_rating.inl').as_posix()+'"\n'+text[end:]
        text=once(text,'{std::min(available, maximum + 360.F), 0}', '{available, 0}')
        text=once(text,'ImGuiTableColumnFlags_WidthFixed, maximum);','ImGuiTableColumnFlags_WidthStretch, 2.F);')
        text=once(text,'ImGuiTableColumnFlags_WidthStretch);','ImGuiTableColumnFlags_WidthStretch, 1.F);')
        text=once(text,'width = std::min(maximum, ImGui::GetContentRegionAvail().x);',
            'width = columns ? ImGui::GetContentRegionAvail().x : available;')
        text=once(text,'StorePanel panel(std::min(width, 980.F), error);','StorePanel panel(width, error);')
        text=once(text,'        const auto kind = i == selected ? PaddockButtonKind::Primary : PaddockButtonKind::Plain;\n','')
        text=once(text,'const float size = std::min(width, PaddockButtonWidth(labels[i].c_str(), kind));',
            'const float size = std::min(width, PaddockSectionTabWidth(labels[i].c_str()));')
        return once(text,'if (PaddockButton((labels[i] + "###tab").c_str(), kind, size, 44.F))',
            'if (PaddockSectionTab((labels[i] + "###tab").c_str(), i == selected, size))')
    def vehicle_ui(text):
        text=text.replace('"On for next launch"','"ON"')
        text=once(text,'    ImGui::PushID("vehicle-card-library");',
            '    ImGui::PushID("vehicle-card-library");\n    { PaddockEnter toolbar_enter(2);')
        text=once(text,'    if(snapshot->busy)DrawToolIntro(',
            '    }\n    if(snapshot->busy)DrawToolIntro(')
        text=once(text,'    if(shown.empty()) {','    if(shown.empty()) {\n        PaddockEnter empty_enter(4);')
        text=once(text,'                const bool details=DrawLibraryCard(cards[col],at,card_width,height,false,busy,',
            '                PaddockEnter card_enter(int(first+col)+4);\n                const bool details=DrawLibraryCard(cards[col],at,card_width,height,false,busy,')
        return consistent_vehicle(text)
    def play_ui(text):
        for name in ('PaddockRaceButtonWidth','PlayTextButtonWidth','PlayTextButton',
                     'PlayNoticeButtonWidth','PlayNoticeButton'):
            begin=text.index(name+'(');body=text.index(') {',begin)+3
            text=text[:body]+'\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();'+text[body:]
        text=once(text,'bool PaddockRaceButton(const char* label, ImVec2 size, const RaceButtonLook& look) {',
            'bool PaddockRaceButton(const char* label, ImVec2 size, const RaceButtonLook& look) {\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();')
        text='#include "'+(ROOT/'runtime-recomp/src/game/library_ui/greeting_ui.inl').as_posix()+'"\n'+text
        text=once(text,'        const float heading_height = ImGui::GetItemRectSize().y;',
            '        const auto heading_size = ImGui::GetItemRectSize();\n        const float heading_height = heading_size.y;')
        text=once(text,'        DrawPlayUpdateButton(action);',
            '        DrawPlayUpdateButton(action);\n        const float header_height=DrawPlayHolidayGreeting(header,width,layout,heading_size.x);')
        text=once(text,'        ImGui::Dummy({width, layout.height});','        ImGui::Dummy({width, header_height});')
        text='#include "'+(ROOT/'runtime-recomp/src/game/library_ui/play_tools.inl').as_posix()+'"\n'+text
        text=once(text,'"Store saves and settings beside DKR-R."',
            '"Keep saves and settings beside DKR-R."')
        begin=text.index('    if (PaddockCheckbox("##skip-intro"')
        end=text.index('    {\n        const bool can_start',begin)
        options=text[begin:end]
        text=text[:begin]+text[end:]
        definition='void DrawPlayLaunchOptions(float width,bool entered) {\n'+options+'}\n\n'
        text=once(text,'void DrawPlayPage(float available_width, const PlayPageContext& play) {',
            definition+'void DrawPlayPage(float available_width, const PlayPageContext& play) {')
        text=once(text,'        const float content_width = std::min(width, rom_picker_width + pass_chrome);\n        const float content_x = at.x + std::floor((width - content_width) * 0.5F);',
            '        const bool side=width>=780.F;\n        const float options_width=side?std::clamp(width*.36F,330.F,640.F):std::max(1.F,width-52.F);\n        const float content_width=side?width-options_width-32.F:width;\n        const float content_x=at.x;\n        ImDrawListSplitter pass_layers;pass_layers.Split(draw,2);pass_layers.SetCurrentChannel(draw,1);')
        text=once(text,'        const ImVec2 end{at.x + width, at.y + height};\n','')
        text=once(text,'        PaddockPanel(draw, at, end, panel);\n','')
        anchor='''        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy({width, height});
    }

    // Anything that changes what START does sits directly above it.'''
        replacement='''        ImGui::PopStyleColor();
        const float options_x=side?at.x+width-options_width-26.F:at.x+26.F;
        const float tools_width=side?options_x-at.x-58.F:width-52.F;
        const float tools_height=PlayToolsHeight(tools_width);
        const float left_height=height+tools_height+16.F;
        const float options_y=side?at.y+26.F:at.y+left_height+8.F;
        ImGui::SetCursorScreenPos({options_x,options_y});
        ImGui::BeginGroup();DrawPlayLaunchOptions(options_width,entered);ImGui::EndGroup();
        const float options_height=ImGui::GetItemRectSize().y;
        const float final_height=side?std::max(left_height,options_height+52.F):left_height+options_height+34.F;
        ImGui::SetCursorScreenPos({at.x+26.F,at.y+(side?final_height:left_height)-26.F-tools_height});
        DrawPlayTools(tools_width);
        if(side)draw->AddLine({options_x-16.F,at.y+26.F},{options_x-16.F,at.y+final_height-26.F},PaddockCol(0x325365));
        pass_layers.SetCurrentChannel(draw,0);
        PaddockPanel(draw,at,{at.x+width,at.y+final_height},panel);
        pass_layers.Merge(draw);
        ImGui::SetCursorScreenPos(at);ImGui::Dummy({width,final_height});
    }

    // Anything that changes what START does sits directly above it.'''
        text=once(text,anchor,replacement)
        text=once(text,'    // The ROM pass: status, the ROM list, and the actions for it.',
            '    DrawLauncherFeatured(width,false);\n\n    // The ROM pass: status, the ROM list, and the actions for it.')
        return parity_play(text)
    def details_ui(text):
        text=once(text,'void DrawAboutSupport(float width) {',
            '#include "'+(ROOT/'runtime-recomp/src/game/crash_reports/ui.inl').as_posix()+'"\n\nvoid DrawAboutSupport(float width) {')
        text=once(text,'        bool logging = dkr::runtime::support::diagnostic_logging_enabled();',
            '        DrawCrashReportingSettings(inner);\n        bool logging = dkr::runtime::support::diagnostic_logging_enabled();')
        for name in ('DetailText','DetailHeading'):
            begin=text.index('void '+name+'(std::string_view text,');body=text.index('{',begin)
            signature=text[begin:body+1]
            text=once(text,signature,'template<class T> '+signature.replace('std::string_view text,','T&& raw,')+
                '\n    const auto text=LocalisedUi::copy(std::forward<T>(raw));')
        text=once(text,'void DrawTexturesPage(float width) {','void DrawTexturesPage(float width, bool embedded = false) {')
        text=once(text,'    DrawPageHeading("TEXTURES");','    if(!embedded)DrawPageHeading("TEXTURES");\n    else PaddockHeading("Textures",30.F,0xFFCD67,width);')
        text=once(text,'    DrawSharedTexturePackModal();','    if(!embedded)DrawSharedTexturePackModal();')
        text=once(text,'    if (DetailSectionSign("CRT OVERLAYS##crt", state.crt_open, width)) {\n        if (modern) {\n            DrawCrtOverlaySection(width);\n        } else {\n            DetailText("Requires Modern graphics.",\n                       width, kSetMuted);\n        }\n    }\n','')
        text=once(text,'    bool crt_open = false;\n','')
        text=once(text,'struct TexturesPageState {\n    bool packs_open = true;\n};\nTexturesPageState g_textures_page;\n','')
        text=once(text,'    TexturesPageState& state = g_textures_page;\n','')
        text=once(text,'    if (DetailSectionSign("TEXTURE PACKS##textures", state.packs_open, width)) {',
            '    PaddockGap(22.F);\n    PaddockHeading("Texture packs",22.F,kSetWarm,width);\n    PaddockGap(16.F);\n    {')
        return once(text,'Personalise the look of DKR-R with texture packs and CRT filters.',
            'Personalise the look of DKR-R with texture packs.')
    def controls_ui(text):
        text=once(text,'if (const auto found = kNames.find(value); found != kNames.end()) return found->second;',
            'if (const auto found = kNames.find(value); found != kNames.end()) return dkr::localisation::directional_caption(found->second);')
        for name,parameter in (('ControlsHeading','text'),('ControlsDisclosure','label')):
            begin=text.index(name+'(');body=text.index(') {',begin)+3
            text=text[:body]+'\n    '+parameter+'=dkr::localisation::source('+parameter+');'+text[body:]
        begin=text.index('void ControlsNote(');body=text.index(') {',begin)+3
        signature=text[begin:body]
        text=text[:begin]+'template<class T> '+signature.replace('std::string_view text,','T&& raw,')+\
            '\n    const auto text=LocalisedUi::copy(std::forward<T>(raw));'+text[body:]
        text=once(text,'''    const std::string text = shared ? std::string("Applies to all players")
                                    : "Player " + std::to_string(g_selected_player + 1U) + " settings";''',
            '    const std::string text=shared?std::string(dkr::localisation::text("controls.all_players")):\n        dkr::localisation::format("controls.player_settings",{{"player",std::to_string(g_selected_player+1U)}});')
        text=text.replace('input::action_label(action)', 'dkr::localisation::source(input::action_label(action)).data()')
        text=once(text,'PaddockText(group_type, PaddockRgb(kSetText), group, width - 16.0F);',
            'PaddockText(group_type, PaddockRgb(kSetText), dkr::localisation::source(group), width - 16.0F);')
        text=once(text,'''    const int picked = SettingsSecondaryNav("controls-sections",
                                            {"Device", "Button mapping", "Stick response", "Motion", "Shortcuts"},
                                            g_controls_section, width);''',
            '    const int picked = LauncherCategoryTabs("controls-sections",dkr::library_ui::ControlsSections,{},g_controls_section,width);')
        text=once(text,'void DrawControlsPage(float available_width, bool live) {',
            'void DrawControlsPage(float available_width, bool live) {\n    g_feedback_live_surface=live;')
        text=once(text,'    if (ControlsDisclosure("##shared", "Shared settings", state.shared_open, width)) {',
            '    DrawFeedbackSettings(width,commit);\n    if (ControlsDisclosure("##shared", "Shared settings", state.shared_open, width)) {')
        text=once(text,'        DrawAnalogueDrivingControls(width);\n','')
        text,count=re.subn(r'^#include "[^"\n]*analog_driving/controls_ui.inl"\n','',text,flags=re.M)
        if count!=1:raise ValueError('Expected the one retained analogue controls include')
        start=text.index('void DrawControlsCaptureModal()');end=text.index('// ------------------------------------------------------------ page',start)
        text=text[:start]+text[start:end].replace('g_selected_player','LibraryCapturePlayer()')+text[end:]
        for suffix in ('binding','keyboard','controller'):
            text=text.replace(' + "##'+suffix+'"',' + "###'+suffix+'"')
        text=once(text,'for (const auto& [shortcut, label] : kShortcuts) {',
            'for (const auto& [shortcut, raw_label] : kShortcuts) {\n        const auto label=dkr::localisation::source(raw_label).data();')
        text=once(text,'const std::array<const char*, 4> kHeads{{"N64 control", "Keyboard", "Controller", "Alternate"}};',
            'std::array<const char*, 4> kHeads{{"N64 control", "Keyboard", "Controller", "Alternate"}};\n    for(auto& caption:kHeads)caption=dkr::localisation::source(caption).data();')
        text=once(text,r'''heading = "Player " + std::to_string(LibraryCapturePlayer() + 1U) + " \xC2\xB7 " +
                  dkr::runtime::input::action_label(static_cast<Action>(g_capture_action));''',
            'heading=dkr::localisation::format("controls.capture_heading",{{"player",std::to_string(LibraryCapturePlayer()+1U)},\n            {"action",dkr::localisation::source(dkr::runtime::input::action_label(static_cast<Action>(g_capture_action)))}});')
        text=once(text,'heading = "Assign Player " + std::to_string(LibraryCapturePlayer() + 1U);',
            'heading=dkr::localisation::format("controls.assign_player",{{"player",std::to_string(LibraryCapturePlayer()+1U)}});')
        text=once(text,'heading = ShortcutActionLabel(g_capture_shortcut_action);',
            'heading = dkr::localisation::source(ShortcutActionLabel(g_capture_shortcut_action));')
        text=once(text,'''help = std::string("Press one ") + (g_capture_device == CaptureDevice::Keyboard ? "key" : "controller button") +
               ", or hold it and press a second. Escape cancels.";''',
            'help=dkr::localisation::text(g_capture_device==CaptureDevice::Keyboard?"controls.shortcut_key_help":"controls.shortcut_button_help");')
        text=once(text,'PaddockRgb(kSetText), help, inner);','PaddockRgb(kSetText), dkr::localisation::source(help), inner);')
        text=once(text,'"Captured: " + FriendlyBinding(ShortcutBindingName(g_capture_device, pending))',
            'dkr::localisation::format("controls.captured",{{"input",FriendlyBinding(ShortcutBindingName(g_capture_device,pending))}})')
        return text
    def texture_ui(text):
        text=once(text,'void DrawTexturesPage(float width) {\n    DrawTexturesPageWithoutDump(width);',
            'void DrawTexturesPage(float width, bool embedded = false) {\n    DrawTexturesPageWithoutDump(width,embedded);')
        text=once(text,'bool g_dump_open=false,g_dump_folder_info=false;','bool g_dump_folder_info=false;')
        start=text.index('    const ImVec2 header=ImGui::GetCursorScreenPos();',text.index('void DrawTextureDumping('))
        end=text.index('        DumpActionRow tabs(width);',start)
        summary=text[text.index('    const std::string summary=',start):text.index('    const auto summary_type=',start)]
        text=text[:start]+'''    PaddockGap(26.F);
    PaddockHeading("Texture dumping",22.F,kSetWarm,width);
    PaddockGap(10.F);
'''+summary+'''    DetailText(summary,width,kSetMuted,12.F);
    {
'''+text[end:]
        text=once(text,'    }else if(!state->error.empty()) {\n        DetailText(state->error,width,kSetWarm,8.F);\n    }',
            '    }')
        return text
    def code_ui(text):
        text=text.replace('"On for next launch"','"ON"')
        text=once(text,'    if(shown.empty()){','    DrawSessionAnalogueCard(width);\n    if(shown.empty()){')
        text=once(text,'        if(!item.description.empty())PaddockModalText(item.description);',
            '        if(item.id==dkr::library_ui::AnalogueId)PaddockModalText(std::string(dkr::library_ui::AnalogueHelp));\n        else if(!item.description.empty())PaddockModalText(item.description);')
        text=once(text,'        ImGui::EndDisabled();\n        if(locked)PaddockModalText("Leave the game and lobby to change code mods.");',
            '        ImGui::EndDisabled();\n        DrawInstalledAnalogueSettings(item);\n        if(locked)PaddockModalText("Leave the game and lobby to change code mods.");')
        return consistent_codes(text)
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n')
        feedback=ROOT/'runtime-recomp/src/game/controller_feedback'
        if source.name=='runtime_platform.cpp':
            text=once(text,'#include <SDL.h>','#include <SDL.h>\n#include "'+(ROOT/'runtime-recomp/src/game/launcher_gpu.hpp').as_posix()+'"')
            text=once(text,'#if defined(__linux__)\n    if (g_window != nullptr) {\n        const Uint32 existing_flags = SDL_GetWindowFlags(g_window);\n        std::fprintf(stderr,\n                     "[boot][window] launcher handoff flags=0x%08X vulkan=%s\\n",\n                     static_cast<unsigned>(existing_flags),\n                     (existing_flags & SDL_WINDOW_VULKAN) != 0 ? "yes" : "no");\n        if ((existing_flags & SDL_WINDOW_VULKAN) == 0) {\n            int x = SDL_WINDOWPOS_CENTERED;\n            int y = SDL_WINDOWPOS_CENTERED;\n            int width = 1440;\n            int height = 900;\n            SDL_GetWindowPosition(g_window, &x, &y);\n            SDL_GetWindowSize(g_window, &width, &height);\n            const bool was_hidden = (existing_flags & SDL_WINDOW_HIDDEN) != 0;\n            const bool was_maximized =\n                (existing_flags & SDL_WINDOW_MAXIMIZED) != 0;\n            const Uint32 fullscreen_mode = existing_flags &\n                (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);\n            SDL_DestroyWindow(g_window);\n            g_window = nullptr;\n\n            Uint32 replacement_flags =\n                SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_VULKAN;\n            if (was_hidden) {\n                replacement_flags |= SDL_WINDOW_HIDDEN;\n            }\n            g_window = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled", x, y, width, height,\n                                        replacement_flags);\n            if (g_window == nullptr) {\n                std::fprintf(stderr,\n                             "[boot][window] Vulkan handoff recreation failed: %s\\n",\n                             SDL_GetError());\n                return {};\n            }\n            SDL_SetWindowMinimumSize(g_window, 800, 600);\n            if (was_maximized) {\n                SDL_MaximizeWindow(g_window);\n            }\n            if (fullscreen_mode != 0U &&\n                SDL_SetWindowFullscreen(g_window, fullscreen_mode) != 0) {\n                std::fprintf(stderr,\n                             "[boot][window] fullscreen handoff restore failed: %s\\n",\n                             SDL_GetError());\n            }\n            std::fprintf(stderr,\n                         "[boot][window] recreated Vulkan-capable game window "\n                         "flags=0x%08X\\n",\n                         static_cast<unsigned>(SDL_GetWindowFlags(g_window)));\n        }\n    }\n#endif','#if defined(__linux__) && !defined(__ANDROID__)\n    if (g_window && !(SDL_GetWindowFlags(g_window) & SDL_WINDOW_VULKAN) &&\n        !dkr::runtime::launcher::replace_surface_window(g_window, SDL_WINDOW_VULKAN, "launcher-to-game")) return {};\n#endif')
            text=once(text,'#if defined(__linux__) && !defined(__ANDROID__)\n    // Qualification switch: never allow SDL/OpenGL to convert a live Vulkan\n    // window. Both renderers have been destroyed at this launcher boundary.\n    const char* opt_in = SDL_getenv("DKR_LINUX_ACCELERATED_LAUNCHER");\n    if (g_window && opt_in && std::string_view(opt_in) == "1" &&\n        (SDL_GetWindowFlags(g_window) & SDL_WINDOW_VULKAN)) {\n        int x, y, width, height;\n        SDL_GetWindowPosition(g_window, &x, &y);\n        SDL_GetWindowSize(g_window, &width, &height);\n        const Uint32 flags = SDL_GetWindowFlags(g_window);\n        // Create before destroying so a failed allocation keeps the proven\n        // software path and its original window intact.\n        auto* replacement = SDL_CreateWindow("DKR-R - Diddy Kong Racing Recompiled",\n            x, y, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);\n        if (replacement) {\n            SDL_DestroyWindow(g_window);\n            g_window = replacement;\n            SDL_SetWindowMinimumSize(g_window, 800, 600);\n            if (flags & SDL_WINDOW_MAXIMIZED) SDL_MaximizeWindow(g_window);\n            const auto fullscreen = flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP);\n            if (fullscreen) SDL_SetWindowFullscreen(g_window, fullscreen);\n            if (!(flags & SDL_WINDOW_HIDDEN)) SDL_ShowWindow(g_window);\n        }\n    }\n#endif','#if defined(__linux__) && !defined(__ANDROID__)\n    if (g_window && dkr::runtime::launcher::linux_acceleration_requested() &&\n        (SDL_GetWindowFlags(g_window) & SDL_WINDOW_VULKAN)) {\n        // Failed replacement retains the original window and CPU fallback.\n        dkr::runtime::launcher::replace_surface_window(g_window, 0, "game-to-launcher");\n    }\n#endif')
            text='#include "'+(ROOT/'runtime-recomp/src/game/library_ui/surface_policy.hpp').as_posix()+'"\n'+text
            start=text.index('void dkr::runtime::platform::update_ui_gamepad_navigation() {')
            end=text.index('\n#endif',start)
            navigation=text[start:end]
            navigation=once(navigation,'    ImGuiIO& io = ImGui::GetIO();',
                '''    ImGuiIO& io = ImGui::GetIO();
    const auto rail_button=[&](ImGuiKey key,bool down) {
        constexpr std::array<ImGuiKey,6> keys{ImGuiKey_GamepadDpadUp,ImGuiKey_GamepadDpadDown,
            ImGuiKey_GamepadDpadLeft,ImGuiKey_GamepadDpadRight,ImGuiKey_GamepadL1,ImGuiKey_GamepadR1};
        for(unsigned i=0;i<keys.size();++i)if(key==keys[i])dkr::library_ui::record_navigation_button(i,down);
        io.AddKeyEvent(key,down);
    };
    const auto rail_axis=[&](ImGuiKey negative,ImGuiKey positive,float value) {
        const unsigned index=negative==ImGuiKey_GamepadLStickLeft?0:negative==ImGuiKey_GamepadLStickUp?1:
            negative==ImGuiKey_GamepadRStickLeft?2:3;
        dkr::library_ui::record_navigation_axis(index,value);
        io.AddKeyAnalogEvent(negative,value<0.F,std::max(-value,0.F));
        io.AddKeyAnalogEvent(positive,value>0.F,std::max(value,0.F));
    };''')
            if navigation.count('io.AddKeyEvent(key, connected &&')!=2:raise ValueError('Expected SDL2 and SDL3 navigation polling')
            navigation=navigation.replace('io.AddKeyEvent(key, connected &&','rail_button(key, connected &&')
            if navigation.count('if (connected) {')!=2:raise ValueError('Expected SDL2 and SDL3 navigation connectivity')
            navigation=navigation.replace('if (connected) {','dkr::library_ui::navigation_connected.store(connected?1:0,std::memory_order_relaxed);\n        if (connected) {')
            navigation,count=re.subn(r'io\.AddKeyAnalogEvent\(negative_key, value < 0\.0F,\s*std::max\(-value, 0\.0F\)\);\s*io\.AddKeyAnalogEvent\(positive_key, value > 0\.0F,\s*std::max\(value, 0\.0F\)\);',
                'rail_axis(negative_key,positive_key,value);',navigation)
            if count!=2:raise ValueError('Expected SDL2 and SDL3 navigation axis polling')
            text=text[:start]+navigation+text[end:]
            text='#include "'+(feedback/'service.hpp').as_posix()+'"\n'+text
            text=re.sub(r'^#include "[^"\n]*sdl3_input_client.hpp"','#include "'+(feedback/'input_client.hpp').as_posix()+'"',text,flags=re.M)
            text=once(text,'void dkr::runtime::platform::poll_input() {',
                'namespace {void FeedbackStart();\n#if DKR_RUNTIME_HAS_RT64\nvoid FeedbackPublishInput(unsigned,bool,bool);\nvoid FeedbackTouch(unsigned,dkr::runtime::input::State&,bool);\n#endif\n}\nvoid dkr::runtime::platform::poll_input() {\n    FeedbackStart();')
            text=once(text,'void dkr::runtime::platform::shutdown() {','void dkr::runtime::platform::shutdown() {\n    dkr::feedback::service().shutdown();')
            text=once(text,'                g_sdl3_gyro_instances[player]);',
                '                g_sdl3_gyro_instances[player]);\n            auto feedback_gyro=gyro?gyro->snapshot:dkr::runtime::controllers::ControllerSnapshot{};\n            if(dkr::feedback::service().status(unsigned(player)).settings.touch_action==3&&!(controller&&controller->feedback.touch_down[0]))feedback_gyro.gyro.valid=false;')
            text=once(text,'                gyro != nullptr ? &gyro->snapshot : nullptr,','                gyro != nullptr ? &feedback_gyro : nullptr,')
            text=once(text,'        dkr::runtime::camera::actions().publish(unsigned(player),',
                '        FeedbackPublishInput(unsigned(player),sdl3_native,!gameplay_blocked&&(!online_routing||player==online_profile));\n        if(sdl3_native)FeedbackTouch(unsigned(player),state,!gameplay_blocked&&(!online_routing||player==online_profile));\n        dkr::runtime::camera::actions().publish(unsigned(player),')
            begin=text.index('void dkr::runtime::platform::set_rumble(');end=text.index('float dkr::runtime::platform::rumble_strength()',begin)
            text=text[:begin]+'#include "'+(feedback/'platform.inl').as_posix()+'"\n'+'''void dkr::runtime::platform::set_rumble(int controller,bool enabled) {
    if(!dkr::runtime::netplay::external_side_effects_allowed())return;
    FeedbackStart();
    controller=dkr::runtime::netplay::physical_rumble_port(g_online_input_routing.load(),g_online_local_slot.load(),g_online_input_profile.load(),controller);
    if(controller>=0&&controller<4)dkr::feedback::service().classic(unsigned(controller),enabled&&g_rumble_enabled.load());
}

'''+text[end:]
            begin=text.index('bool dkr::runtime::platform::identify_controller(')
            end=text.index('bool dkr::runtime::platform::begin_controller_mapping(',begin)
            text=text[:begin]+'''bool dkr::runtime::platform::identify_controller(std::size_t player) {
    FeedbackStart();
    if(player>=4||dkr::feedback::service().status(unsigned(player)).device.instance<0)return false;
    dkr::feedback::service().test(unsigned(player),0,50);return true;
}

'''+text[end:]
        elif source.name=='camera_rt64.cpp':
            text='#include "'+(feedback/'capture.hpp').as_posix()+'"\n'+text
            text=once(text,'    const auto next=activation();',
                '    const auto next=activation();\n    if(next)dkr::feedback::capture(retained,next->revision,stamp,online_owner,physical_profile);')
        elif source.name=='mod_store.cpp':
            storage=(ROOT/'runtime-recomp/src/game/storage/maintenance.hpp').as_posix()
            text='#include "'+storage+'"\n#include "stb/stb_image_write.h"\n'+text
            for anchor in ('    void load_image(const std::string& url, std::stop_token stop) {','void Service::installed(std::string key, bool success, std::string message) {'):
                replacement=anchor+'\n    dkr::storage::Activity storage_activity;'
                if not re.search(re.escape(anchor)+r'\s*dkr::storage::Activity storage_activity;',text):text=once(text,anchor,replacement)
            anchor='                task(stop);'
            if 'dkr::storage::Activity storage_work;' not in text:text=once(text,anchor,'                dkr::storage::Activity storage_work;\n'+anchor)
            anchor='            auto cancelled = download_stop.get_token();'
            replacement=anchor+'\n            dkr::storage::Activity storage_download;'
            if replacement not in text:text=once(text,anchor,replacement)
            anchor='void Service::gameplay(bool active) {'
            if anchor+'\n    dkr::storage::gameplay(active);' not in text:text=once(text,anchor,anchor+'\n    dkr::storage::gameplay(active);')
            anchor='                job.digest = text(integrity, "sha256", 64);'
            if 'const auto expected_digest=job.digest;' not in text:text=once(text,anchor,'''                const auto expected_digest=job.digest;
'''+anchor+'''
                if(job.intent=="activate"&&!expected_digest.empty()&&expected_digest!=job.digest)
                    throw Error("That edition's published checksum changed. The installed mod was kept.");''')
            for anchor,replacement in [
                ('    d.instance = std::move(instance);','    d.instance = std::move(instance);\n    d.digest = text(release,"sha256",64);'),
                ('            found->instance = d.instance;','            found->instance = d.instance;\n            found->digest = d.digest;'),
                ('        p->persist_downloads();','        p->persist_downloads();\n        dkr::storage::request();')]:
                if replacement not in text:text=once(text,anchor,replacement)
            begin=text.index('void Service::installed(');end=text.index('void Service::request_image(',begin)
            part=text[begin:end]
            if 'dkr::storage::installed_package(' not in part:
                part=once(part,'    impl_->update([&](Snapshot& s) {','    std::string installed_digest;\n    impl_->update([&](Snapshot& s) {')
                part=once(part,'                d.error = success || cancelled ? "" : message;',
                    '                d.error = success || cancelled ? "" : message;\n                if(success)installed_digest=d.digest;')
                part=once(part,'    impl_->queue([p = impl_.get()](std::stop_token stop) {',
                    '    impl_->queue([p = impl_.get(), digest=std::move(installed_digest)](std::stop_token stop) {')
                part=once(part,'        dkr::storage::request();','        if(!digest.empty())dkr::storage::installed_package(digest);')
            text=text[:begin]+part+text[end:]
            begin=text.index('    void load_image(');end=text.index('    void image_run(',begin);part=text[begin:end]
            anchor='        int width = 0, height = 0, channels = 0;'
            if 'dkr::storage::used(path);' not in part:part=once(part,anchor,'        dkr::storage::used(path);\n'+anchor)
            if 'dkr::storage::Action::Images' not in part:
                part=once(part,'        std::lock_guard lock(mutex);','''        if(width>512||height>512) {
            std::string encoded;
            const auto write=[](void* context,void* data,int length){static_cast<std::string*>(context)->append(static_cast<const char*>(data),std::size_t(length));};
            if(stbi_write_png_to_func(write,&encoded,int(image->width),int(image->height),4,image->rgba.data(),int(image->width*4))&&encoded.size()<bytes.size())
                dkr::updates::durable_write(path,encoded);
        }
        dkr::storage::request(dkr::storage::Action::Images);
        std::lock_guard lock(mutex);''')
            text=text[:begin]+part+text[end:]
            text=account_service(text,out)
            text='#include "'+(store_module/'community_policy.hpp').as_posix()+'"\n'+text
            text=once(text,'    // Workers are last: joined before the objects captured by their callbacks.',
                '    #include "'+(store_module/'community_service.inl').as_posix()+'"\n    // Workers are last: joined before the objects captured by their callbacks.')
            text=once(text,'    void publish() {','    void publish() {\n        community_fence();')
            text=once(text,'void Service::action(std::string name, Json input) {',
                'void Service::action(std::string name, Json input) {\n    if(impl_->community_action(name,input))return;')
            text=once(text,'                auto write_progress = Clock::now();',
                '                const auto download_attempt=community_download(job.release,marker);\n                auto write_progress = Clock::now();')
            text=once(text,'write_json(marker, {{"sha256", job.digest}, {"url", url}, {"etag", headers.etag}});',
                'write_json(marker, {{"sha256", job.digest}, {"url", url}, {"etag", headers.etag},{"communityAttempt",download_attempt}});')
            text='#include "online_stability/process_services.hpp"\n'+text
            text=once(text,'Service::~Service() {','Service::~Service() {\n    dkr::online_stability::unbind_store_http(this);')
            text=once(text,'    p.configured = true;', '''    p.configured = true;
    dkr::online_stability::bind_store_http(this,[&p] {
        p.login_stop.request_stop();p.download_stop.request_stop();p.stop_upload=true;
        p.worker.request_stop();p.image_worker.request_stop();p.download_worker.request_stop();p.auth_worker.request_stop();
        p.community_tracking_worker.request_stop();
        p.cv.notify_all();
        for(auto* worker:{&p.worker,&p.image_worker,&p.download_worker,&p.auth_worker,&p.community_tracking_worker})
            if(worker->joinable())worker->join();
    });''')
            text=once(text,'    p.download_worker = std::jthread([&p](std::stop_token stop) { p.download_run(stop); });',
                '    p.download_worker = std::jthread([&p](std::stop_token stop) { p.download_run(stop); });\n    p.community_tracking_worker=std::jthread([&p](std::stop_token stop){p.community_tracking(stop);});')
            text=once(text,'    p.auth_worker.request_stop();',
                '    p.auth_worker.request_stop();\n    p.community_tracking_worker.request_stop();')
        elif source.suffix=='.cpp':
            text=once(text,'#include <SDL.h>','#include <SDL.h>\n#include "'+(ROOT/'runtime-recomp/src/game/launcher_gpu.hpp').as_posix()+'"')
            text=once(text,'#if defined(__linux__) && !defined(__ANDROID__)\n    // The launcher and RT64 share this Vulkan-capable SDL window for the\n    // complete process lifetime. An accelerated SDL renderer can replace the\n    // native surface state under Gamescope, so keep the lightweight launcher\n    // on the software backend and let RT64 take over the same window directly.\n    SDL_Renderer* renderer = nullptr;\n    const char* accelerated_launcher = SDL_getenv("DKR_LINUX_ACCELERATED_LAUNCHER");\n    if (!software_failure_recovery && accelerated_launcher && std::string_view(accelerated_launcher) == "1" &&\n        !(SDL_GetWindowFlags(window) & SDL_WINDOW_VULKAN)) {\n        renderer = SDL_CreateRenderer(window, -1,\n            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);\n    }\n    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);','#if defined(__linux__) && !defined(__ANDROID__)\n    SDL_Renderer* renderer = dkr::runtime::launcher::create_linux_launcher_renderer(window);')
            text=once(text,'                           bool software_failure_recovery=false) {\n#if defined(__linux__) || defined(_WIN32)','                           bool software_failure_recovery=false) {\n    if (dkr::runtime::launcher::accelerated_renderer(renderer)) {\n        ImGui_ImplSDLRenderer2_RenderDrawData(data);\n        return;\n    }\n#if defined(__linux__) || defined(_WIN32)')
            text='#include "water_style/appearance.hpp"\n'+text
            text=once(text,'        output << "enhanced_water_quality="',
                '        dkr::runtime::water::style::save_setting(output);\n        output << "enhanced_water_quality="')
            text=once(text,'if(dkr::runtime::water::enhanced::load_tuning(key,number))continue;',
                'if(dkr::runtime::water::style::load_setting(key,number)||dkr::runtime::water::enhanced::load_tuning(key,number))continue;')
            text='#include "'+(store_module/'community_policy.hpp').as_posix()+'"\n#include "'+(store_module/'overlay_images.hpp').as_posix()+'"\n#include "'+(store_module/'install_notice.hpp').as_posix()+'"\n'+text
            text=once(text,'    // The ROM line.\n',
                '    if(g_mods_page.section!=4) {\n        PaddockText(PaddockReading(13.F,false,1.55F),PaddockRgb(0xFFC15A),\n                    dkr::store::LegacyRomInstallNotice,width);\n        PaddockGap(12.F);\n    }\n\n    // The ROM line.\n')
            text=once(text,'            if(index==4&&game_running)continue;\n','')
            text=once(text,'        if (game_running && g_mods_page.section == 4) g_mods_page.section = kModsSectionLibrary;\n','')
            text=once(text,'            DrawModStore(width, mods_locked);','            DrawModStore(width, mods_locked, game_running);')
            text=once(text,'    inspector->newFrame(application.framebufferGraphicsWorker.get());',
                '    inspector->newFrame(application.framebufferGraphicsWorker.get());\n    dkr::store::overlay::begin(application);\n    if(show_overlay)StoreOverlayTestFrame();')
            text=once(text,'    const bool show_overlay =',
                '    if(!g_overlay_visible.load())LibraryCancelAnalogueCapture();\n    if(g_store_overlay_test&&std::chrono::steady_clock::now()>=g_store_overlay_test_after) {\n        g_overlay_visible=true;g_overlay_page=kPageModLibrary;\n    }\n    const bool show_overlay =')
            text=once(text,'    // This function is reachable only after the NEW isolated-profile check.',
                '    if(const char* test=std::getenv("DKR_OWNED_CHECK_MOD_STORE");test&&std::string_view(test)=="1") {\n        g_store_overlay_test=true;g_store_overlay_test_after=std::chrono::steady_clock::now()+std::chrono::seconds(70);\n    }\n    // This function is reachable only after the NEW isolated-profile check.')
            text=once(text,'    inspector->endFrame();','    dkr::store::overlay::end();\n    inspector->endFrame();')
            text=once(text,'        const PaddockLauncherTypographyScope modal_typography(show_overlay);',
                '        const PaddockLauncherTypographyScope modal_typography(show_overlay);\n        if(g_store_in_game)StorePolishModals();')
            text=once(text,'        dkr::runtime::crt::release();','        dkr::store::overlay::release();\n        dkr::runtime::crt::release();')
            text=once(text,'    std::fprintf(stderr, "[boot][ui] in-game overlay attached (F1/Escape/Back)\\n");',
                '    dkr::store::service().gameplay(true);\n    dkr::store::installations().safe_to_write(false);\n    std::fprintf(stderr, "[boot][ui] in-game overlay attached (F1/Escape/Back)\\n");')
            text='#include "netplay/session_chat/chat.hpp"\n#include "netplay/session_chat/feed_policy.hpp"\n#include "netplay/session_chat/settings.hpp"\n#include "netplay/session_chat/input_policy.hpp"\n#include "netplay/peer_relay/settings.hpp"\n#include "online_stability/control.hpp"\n#include "'+(out/'display_name_save.hpp').as_posix()+'"\n#include "'+(out/'profile_bridge.hpp').as_posix()+'"\n'+text
            text=once(text,'        output << "online_automatic_delay=" << (g_online_automatic_delay ? 1 : 0) << \'\\n\';',
                '        dkr::chat::save_settings(output);\n        dkr::relay::save_settings(output);\n        output << "online_automatic_delay=" << (g_online_automatic_delay ? 1 : 0) << \'\\n\';')
            text=once(text,'            const int number = std::stoi(value);',
                '            const int number = std::stoi(value);\n            if(dkr::chat::load_setting(key,number)||dkr::relay::load_setting(key,number))continue;')
            text=once(text,'    g_online_mod_sync=std::make_unique<dkr::mods::online::ModSync>(*g_online_mod_link);',
                '    g_online_mod_sync=std::make_unique<dkr::mods::online::ModSync>(*g_online_mod_link);\n    dkr::online_stability::bind_mod_sync(g_online_mod_link.get(),&dkr::runtime::netplay::session());')
            text=once(text,'    g_online_mod_sync.reset();g_online_mod_link.reset();',
                '    g_online_mod_sync.reset();dkr::online_stability::unbind_mod_sync(g_online_mod_link.get());g_online_mod_link.reset();')
            text=once(text,'char g_online_profile_name[25] = "Racer";',
                'char g_online_profile_name[25] = "Racer";\nstd::string g_discord_choice_account, g_discord_avatar_url;\nbool g_profile_share_picture = true;\nvoid DrawOlDisplayName(float,bool);\nvoid DrawOlDisplayNameStatus(float);\nvoid DrawOlStoreProfileName(float,bool);\nbool SaveOlDisplayName(std::string_view);')
            text=once(text,'        output << "online_player_name="',
                '        output << "discord_choice_account=" << g_discord_choice_account << \'\\n\';\n'
                '        output << "discord_avatar_url=" << g_discord_avatar_url << \'\\n\';\n        output << "online_player_name="')
            text=once(text,'        output << "discord_avatar_url="',
                '        output << "profile_share_picture=" << (g_profile_share_picture?1:0) << \'\\n\';\n        output << "discord_avatar_url="')
            text=once(text,'        if (key == "online_player_name") {',
                '        if (key == "discord_choice_account") { if(value.size()<=80 && value.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-")==value.npos)g_discord_choice_account=value; continue; }\n'
                '        if (key == "discord_avatar_url") { if(dkr::profile::avatar_url(value))g_discord_avatar_url=value; continue; }\n'
                '        if (key == "online_player_name") {')
            text=once(text,'        if (key == "discord_avatar_url") {',
                '        if (key == "profile_share_picture") { g_profile_share_picture=value!="0"; continue; }\n        if (key == "discord_avatar_url") {')
            if text.count('        DrawTextEntryKeyboard();')!=2:raise ValueError('Launcher/overlay keyboard surface changed')
            text=text.replace('        DrawTextEntryKeyboard();','        PumpSessionChatUi();\n        DrawDiscordProfilePrompt();\n        DrawTextEntryKeyboard();')
            text=once(text,'    DrawNetworkOverlay();','    DrawNetworkOverlay();\n    DrawSessionChatOverlay(show_overlay);')
            text=once(text,'    const bool keep_owned_ui =',
                '    const bool show_session_chat = dkr::runtime::netplay::session().presentation_active() && (dkr::chat::show_feed.load() || dkr::chat::typing.load() || dkr::chat::open_requested.load());\n    const bool keep_owned_ui =')
            text=once(text,'!show_camera_tuning && !keep_owned_ui &&',
                '!show_camera_tuning && !show_session_chat && !keep_owned_ui &&')
            text=once(text,'if (show_overlay || show_camera_tuning || dkr::runtime::invites::dialog()) {',
                'if (show_overlay || show_camera_tuning || dkr::chat::typing.load() || dkr::runtime::invites::dialog()) {')
            text=once(text,'bool dkr::runtime::ui::handle_runtime_event(SDL_Event* event) {\n    if (event == nullptr) {\n        return false;\n    }',
                'bool dkr::runtime::ui::handle_runtime_event(SDL_Event* event) {\n    if (event == nullptr) {\n        return false;\n    }\n    if(HandleSessionChatEvent(event))return true;')
            text=once(text,'bool dkr::runtime::ui::input_capture_active() {\n    return',
                'bool dkr::runtime::ui::input_capture_active() {\n    return dkr::chat::captures_game_input() ||')
            text=once(text,'void dkr::runtime::ui::update_camera_mouse() {',
                'void dkr::runtime::ui::update_camera_mouse() {\n    if(dkr::chat::captures_game_input())return;')
            text='#include "'+(out/'character_portraits.hpp').as_posix()+'"\n'+text
            online=re.findall(r'^#include "([^"\n]*runtime_online_ui\.inl)"',text,flags=re.M)
            if len(online)!=1:raise ValueError('Expected one final lobby UI include')
            online_source=Path(online[0]);online_source=online_source if online_source.is_absolute() else source.parent/online_source
            online_text=stage_include(online_source.read_text(encoding='utf-8'),'directory_ui.inl',directory_ui)
            # Keep the ledger-covered chat input exact. Its existing relative
            # include resolves beside this final composed lobby source.
            chat_source=ROOT/'runtime-recomp/src/game/netplay/session_chat/ui.inl'
            chat_raw=chat_source.read_bytes()
            chat_ui=once(chat_raw.decode().replace('\r\n','\n'),
                'OlParagraph(std::to_string(unread)+" unread message"+(unread==1?"":"s"),',
                'OlParagraph(dkr::localisation::plural("chat.unread",unread),')
            files['netplay/session_chat/ui.inl']=chat_ui.encode()
            swaps.append({'target':row['target'],'path':str(chat_source),'new':str(out/'netplay/session_chat/ui.inl'),
                'input_sha256':sha(chat_raw),'output_sha256':sha(chat_ui.encode()),'compiled':False})
            online_text=stage_include(online_text,'lobby_controls.inl',lambda value:once(value,
                '            OlParagraph(m->name,20.F,kOlCream,width,true);PaddockGap(12.F);',
                '            OlParagraph(m->name,20.F,kOlCream,width,true);PaddockGap(12.F);\n'
                '            if(OlLobbyFriendAction(std::to_string(m->id),m->id==g_ol_party.link.local)){DrawOlLobbyFriendAction(std::to_string(m->id),width);PaddockGap(12.F);}') )
            files['runtime_online_ui.inl']=parity_online(stability_ui(friends_layout(profile_account_ui(profile_layout(lobby_layout(online_text)),out),out))).encode()
            swaps.append({'target':row['target'],'path':str(online_source),'new':str(out/'runtime_online_ui.inl'),
                'input_sha256':sha(online_source.read_bytes()),'output_sha256':sha(files['runtime_online_ui.inl']),'compiled':False})
            text=once(text,'#include "'+online[0]+'"','#include "'+(out/'runtime_online_ui.inl').as_posix()+'"')
            text=stage_include(text,'mod_store_ui.inl',store_ui)
            # Retained UI-only adapter: first-party theme owners live in runtime-recomp.
            # No gameplay source, identity digest or guest hook is involved.
            themes=ROOT/'runtime-recomp/src/game/launcher_themes'
            text='#include "'+(themes/'greeting.hpp').as_posix()+'"\n'+text
            text='#include "'+(themes/'settings.hpp').as_posix()+'"\n#include "'+(themes/'system.hpp').as_posix()+'"\n#include "'+(themes/'session.hpp').as_posix()+'"\n#include "'+(themes/'draw.hpp').as_posix()+'"\n#include "'+(themes/'art_sdl.hpp').as_posix()+'"\n'+text
            text=once(text,'        dkr::chat::save_settings(output);',
                '        dkr::launcher_themes::save(output);\n        dkr::chat::save_settings(output);')
            text=once(text,'            const int number = std::stoi(value);',
                '            const int number = std::stoi(value);\n            if(dkr::launcher_themes::load(key,number))continue;')
            text=stage_include(text,'runtime_details_ui.inl',details_ui)
            text=stage_include(text,'runtime_online_mods_ui.inl',lambda value:value,ROOT/'runtime-recomp/src/game')
            text=stage_include(text,'texture_dump_ui.inl',texture_ui)
            text=once(text,'LauncherBackgroundTexture LoadLauncherBackground(SDL_Renderer* renderer,\n                                                  bool software_return = false) {',
                'LauncherBackgroundTexture LoadLauncherBackground(SDL_Renderer* renderer,\n                                                  bool software_return = false, dkr::launcher_themes::Theme theme = dkr::launcher_themes::Theme::Original) {')
            text=once(text,'    result.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,',
                '    dkr::launcher_themes::tint(std::span<unsigned char>(pixels,std::size_t(result.width)*std::size_t(result.height)*4),result.width,result.height,theme);\n    result.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,')
            text=once(text,'    LauncherBackgroundTexture launcher_background =\n        LoadLauncherBackground(renderer, bounded_software_return);',
                '''    LibraryCancelAnalogueCapture();
    dkr::launcher_themes::SystemSettings launcher_region;
    launcher_region.refresh(0.0,true,true);
    dkr::launcher_themes::ArtTexture launcher_art(renderer);
    dkr::launcher_themes::Session launcher_theme;
    launcher_theme.tick(0.0,true,true,dkr::launcher_themes::preferences,dkr::launcher_themes::local_date);
    dkr::launcher_themes::displayed=launcher_theme.theme();
    dkr::launcher_themes::next_switch=launcher_theme.next();
    LauncherBackgroundTexture launcher_background =
        LoadLauncherBackground(renderer, bounded_software_return,launcher_theme.theme());
    std::fprintf(stderr,"[launcher][theme] region=%s active=%s; automatic local calendar; cached cloud tint; launcher only\\n",launcher_region.country().empty()?"unspecified":launcher_region.country().c_str(),dkr::launcher_themes::Names[int(launcher_theme.theme())]);''')
            text=once(text,'        launcher_background_scroll =\n            g_launcher_animation_seconds * kBackgroundScrollPixelsPerSecond;',
                '''        launcher_region.refresh(g_launcher_animation_seconds,launcher_window_visible,launcher_window_focused);
        if(launcher_theme.tick(g_launcher_animation_seconds,launcher_window_visible,
                launcher_window_focused,dkr::launcher_themes::preferences,dkr::launcher_themes::local_date)) {
            auto next_background=LoadLauncherBackground(renderer,bounded_software_return,launcher_theme.theme());
            if(!next_background.texture) {
                ReleaseLauncherBackground(next_background);
                next_background=LoadLauncherBackground(renderer,bounded_software_return);
            }
            ReleaseLauncherBackground(launcher_background);
            launcher_background=next_background;
            std::fprintf(stderr,"[launcher][theme] active=%s; cached cloud tint; launcher only\\n",dkr::launcher_themes::Names[int(launcher_theme.theme())]);
        }
        dkr::launcher_themes::displayed=launcher_theme.theme();
        dkr::launcher_themes::next_switch=launcher_theme.next();
        launcher_background_scroll = launcher_theme.seconds() * kBackgroundScrollPixelsPerSecond;''')
            text=once(text,'        DrawLauncherBackdrop(launcher_background, renderer, launcher_background_scroll,\n                             bounded_software_return);',
                '''        DrawLauncherBackdrop(launcher_background, renderer, launcher_background_scroll,
                             bounded_software_return);
        dkr::launcher_themes::weather(ImGui::GetWindowDrawList(),ImGui::GetMainViewport()->Pos,
            ImGui::GetMainViewport()->Size,launcher_theme.theme(),launcher_theme.seconds(),dkr::launcher_themes::preferences.calendar);''')
            text=once(text,'        ImGui::End();\n        PumpDialogJob();',
                '''        ImGui::End();
        dkr::launcher_themes::corners(ImGui::GetForegroundDrawList(),ImGui::GetMainViewport()->Pos,
            ImGui::GetMainViewport()->Size,launcher_theme.theme(),reinterpret_cast<ImTextureID>(launcher_art.get()),dkr::launcher_themes::preferences.calendar);
        PumpDialogJob();''')
            text=once(text,'\n    ReleaseLauncherBackground(launcher_background);',
                '\n    launcher_art.release();\n    ReleaseLauncherBackground(launcher_background);')
            text=once(text,'#include "runtime_mod_library_ui.inl"','#include "'+(out/'runtime_mod_library_ui.inl').as_posix()+'"')
            text=once(text,'    ReleaseModStoreTextures();','    ReleaseDiscordProfileTexture();\n    ReleaseCharacterPortraits();\n    ReleaseModStoreTextures();')
            text=once(text,'    g_mod_store_renderer = renderer;','    g_mod_store_renderer = renderer;\n    g_store_in_game=false;\n    g_character_portrait_renderer = renderer;')
            # Desktop page reorganisation uses the existing final UI adapter.
            # Local calibration stays outside the gameplay/input identity ledger.
            library_ui=ROOT/'runtime-recomp/src/game/library_ui'
            text='#include "'+(library_ui/'policy.hpp').as_posix()+'"\n#include "'+(library_ui/'presentation.hpp').as_posix()+'"\n'+text
            text=once(text,'constexpr int kMenuPageCount = 10;',
                'constexpr int kPageModLibrary = dkr::library_ui::LibraryPage;\nconstexpr int kMenuPageCount = dkr::library_ui::PageCount;')
            text=once(text,'constexpr std::array<int,kMenuPageCount+2> kSidebarOrder{0,5,6,1,2,3,4,8,9,7,10,11};',
                'constexpr auto kSidebarOrder=dkr::library_ui::SidebarOrder;')
            text=once(text,'static_cast<float>(kMenuPageCount + 2);','static_cast<float>(kSidebarOrder.size());')
            brand='        ImGui::Dummy({0.0F, sidebar_layout.brand_gap});'
            if text.count(brand)!=2:raise ValueError('Expected launcher and overlay sidebar brand blocks')
            text=text.replace(brand,brand+'''
        const auto sidebar_position=dkr::library_ui::sidebar_placement(
            ImGui::GetWindowSize().y,nav_padding,ImGui::GetCursorPosY(),
            sidebar_layout.button_height,sidebar_layout.item_spacing_y,
            sidebar_layout.action_section_gap,sidebar_layout.action_gap,kSidebarOrder.size()-2);
        ImGui::SetCursorPosY(sidebar_position.navigation);''')
            action='        SidebarActionGap(nav_inner_width, sidebar_layout.action_section_gap);'
            if text.count(action)!=2:raise ValueError('Expected launcher and overlay sidebar actions')
            text=text.replace(action,'''        ImGui::SetCursorPosY(sidebar_position.actions-sidebar_layout.action_section_gap-sidebar_layout.item_spacing_y);
'''+action)
            text=once(text,'        LauncherSidebarButton("TEXTURES", kPageTextures, page, sidebar_selection,\n                              nav_inner_width, sidebar_layout.button_height);\n','')
            text=once(text,'        SidebarButton("TEXTURES", kPageTextures, sidebar_selection, nav_inner_width,\n                      sidebar_layout.button_height);\n','')
            for name,label,page_id in [('LauncherSidebarButton','SAVE MANAGER','kPageSaveManager'),
                                      ('LauncherSidebarButton','ACHIEVEMENTS','kPageAchievements'),
                                      ('SidebarButton','SAVE MANAGER','kPageSaveManager'),
                                      ('SidebarButton','ACHIEVEMENTS','kPageAchievements')]:
                pattern=rf'        {name}\("{label}",\s*{page_id},[^;]+;\n'
                text,count=re.subn(pattern,'',text)
                if count!=1:raise ValueError('Expected one relocated '+name+' '+label)
            text=once(text,'    const bool current = sidebar_selection == page;',
                '    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == page;')
            text=once(text,'    const bool current = sidebar_selection == target_page;',
                '    const bool current = dkr::library_ui::sidebar_parent(sidebar_selection) == target_page;')
            text=once(text,'int StepSidebarSelection(int current,int direction) {',
                'int StepSidebarSelection(int current,int direction) {\n    current=dkr::library_ui::sidebar_parent(current);')
            text=once(text,'        LauncherSidebarButton("MODS / HACKS",',
                '        LauncherSidebarButton("MOD LIBRARY", kPageModLibrary, page, sidebar_selection,\n                              nav_inner_width, sidebar_layout.button_height);\n        LauncherSidebarButton("MODS / HACKS",')
            text=once(text,'        SidebarButton("MODS / HACKS",',
                '        SidebarButton("MOD LIBRARY", kPageModLibrary, sidebar_selection, nav_inner_width,\n                      sidebar_layout.button_height);\n        SidebarButton("MODS / HACKS",')
            text=once(text,'    } else if (g_overlay_page == kPageModsHacks) {',
                '    } else if (g_overlay_page == kPageModLibrary) {\n        DrawModLibraryPage(content_width,true);\n    } else if (g_overlay_page == kPageModsHacks) {')
            text=once(text,'        } else if (page == kPageModsHacks) {',
                '        } else if (page == kPageModLibrary) {\n            DrawModLibraryPage(right_inner_width,false);\n        } else if (page == kPageModsHacks) {')
            text=once(text,'void DrawModsHacks(float available_width, bool game_running = false) {',
                'void DrawModLibraryPage(float,bool);\nvoid DrawTexturesPage(float,bool);\nvoid DrawModsHacks(float available_width, bool game_running = false) {')
            text=once(text,'constexpr std::array<const char*, 5> labels{{\n            "My mods##section", "Magic Codes##section", "Track Lab##section",\n            "Help & imports##section", "Mod Store##section"}};',
                'constexpr auto& labels=dkr::library_ui::ModsSections;')
            text=once(text,'for (int index = 0; index < 5; ++index) {\n            const float tab = PaddockSectionTabWidth(labels[index]);',
                'for (int visual = 0; visual < static_cast<int>(labels.size()); ++visual) {\n            const int index=dkr::library_ui::ModsSectionOrder[visual];\n            const float tab = PaddockSectionTabWidth(labels[visual]);')
            text=once(text,'PaddockSectionTab(labels[index], open)','PaddockSectionTab(labels[visual], open)')
            text=once(text,'        if (g_mods_page.section == 4) {\n            DrawModStore(width, mods_locked, game_running);\n        } else if (g_mods_page.section == kModsSectionLibrary) {',
                '        if (g_mods_page.section == kModsSectionLibrary) {')
            text=once(text,'    g_mods_page.last_frame = frame;',
                '    g_mods_page.last_frame = frame;\n    if(g_mods_page.section>=int(dkr::library_ui::ModsSections.size()))g_mods_page.section=-1;')
            text=once(text,'        } else {\n            constexpr std::array<std::array<const char*, 2>, 4> titles{{',
                '        } else if(g_mods_page.section==dkr::library_ui::TexturesSection) {\n            DrawTexturesPage(width,true);\n        } else {\n            constexpr std::array<std::array<const char*, 2>, 4> titles{{')
            store_includes=re.findall(r'^#include "([^"\n]*mod_store_ui.inl)"',text,re.M)
            text=once(text,'#include "'+store_includes[0]+'"',
                'void OpenModLibrary(int);\n#include "'+store_includes[0]+'"')
            code_includes=re.findall(r'^#include "([^"\n]*runtime_code_mods_ui.inl)"',text,re.M)
            if len(code_includes)!=1:raise ValueError('Expected one code details owner')
            text=once(text,'#include "'+code_includes[0]+'"',
                'void DrawInstalledAnalogueSettings(const dkr::mods::code::LibraryItem&);\nvoid DrawSessionAnalogueCard(float);\n#include "'+code_includes[0]+'"')
            text=stage_include(text,'runtime_code_mods_ui.inl',code_ui,Path(row['codeUi']).parent)
            text=stage_include(text,'runtime_controls_ui.inl',controls_ui)
            def parity_graphics(value):
                value=once(value,'live ? "Changes apply while playing unless marked otherwise."\n                     : ""',
                    '"Changes apply while playing unless marked otherwise."')
                value=once(value,'        if (live) {\n            ImGui::Indent(36.0F);',
                    '        {\n            ImGui::Indent(36.0F);')
                return once(value,'if(live&&camera_provider&&camera_provider->editor&&SettingsButton("Camera++ settings (F7)",inner))',
                    'if(SettingsButton("Camera++ settings (F7)",inner,!live||!camera_provider||!camera_provider->editor,SettingsButtonTone::Plain,false,"Available in-game when a camera mod supplies an editor."))')
            text=stage_include(text,'graphics.inl',parity_graphics)
            def parity_saves(value):
                value=once(value,'    const float visible_bottom = window->InnerRect.Max.y;\n    const float top = std::min(natural.y, visible_bottom - bar_height);',
                    '''    const float visible_bottom = PanelFooterBounds(window).Max.y;
    const float top = std::min(natural.y, visible_bottom - bar_height);
    DrawPanelFooterBackground(window, top, PaddockCol(0x0A202C), PaddockCol(kSetBorder));
    if(SaveDraftLayoutCheck())g_save_draft_geometry={PanelFooterBounds(window),
        {{window->Pos.x+window->WindowBorderSize,window->Pos.y+window->WindowBorderSize},
         {window->Pos.x+window->Size.x-window->WindowBorderSize-window->ScrollbarSizes.x,
          window->Pos.y+window->Size.y-window->WindowBorderSize-window->ScrollbarSizes.y}},top,dirty};''')
                value=once(value,'''    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled({natural.x, top}, {natural.x + width, top + bar_height}, PaddockCol(0x0A202C));
    draw->AddRectFilled({natural.x, top}, {natural.x + width, top + 1.0F}, PaddockCol(kSetBorder));
''','')
                start=value.index('void DrawSaveManagerPage(')
                body=value[start:]
                first=body.index('    if(live) {')
                end=body.index('    else {\n        const auto& view=CachedSaveManagerView();',first)
                body=body[:first]+'''    SavesNote("Return to the launcher and leave your lobby before changing saves.",width);
    if(state.scope!=0)DrawStoredSaves(state,width,locked);
'''+body[end:]
                body=once(body,'        if(!locked) {','        {\n            ImGui::BeginDisabled(locked);')
                body=once(body,'            const auto section=SettingsSectionSigns(',
                    '            ImGui::EndDisabled();\n            const auto section=SettingsSectionSigns(')
                body=once(body,'            if(state.section==1)DrawSavesEditor(',
                    '            ImGui::BeginDisabled(locked);\n            if(state.section==1)DrawSavesEditor(')
                body=once(body,'        } else if(!SharedSaveBusy()&&valid&&SavesRaceButton("EXPORT PERSONAL COPY"))ExportAdventureWithDialog();',
                    '            ImGui::EndDisabled();\n        }')
                return value[:start]+body
            text=stage_include(text,'runtime_saves_ui.inl',parity_saves,Path(row['savesUi']).parent)

            text=stage_include(text,'runtime_play_ui.inl',play_ui,ROOT/'runtime-recomp/src/game')
            text=stage_include(text,'vehicle_ui.inl',vehicle_ui)
            control_includes=re.findall(r'^#include "([^"\n]*runtime_controls_ui.inl)"',text,re.M)
            text=once(text,'#include "'+control_includes[0]+'"',
                'std::size_t LibraryCapturePlayer();\n#include "'+control_includes[0]+'"\n#include "'+(library_ui/'analogue_details.inl').as_posix()+'"\n#include "'+(library_ui/'page.inl').as_posix()+'"\n#include "'+(library_ui/'navigation_check.inl').as_posix()+'"')
            text=once(text,'        LauncherSidebarButton("PLAY",',
                '        LibraryNavigationCheckRoute(false,page);\n        if(!g_launcher_layout_root.empty())sidebar_selection=page;\n        if(LibraryRedirectTextures(page))sidebar_selection=page;\n        LauncherSidebarButton("PLAY",')
            text=once(text,'        SidebarButton("PLAY",',
                '        LibraryNavigationCheckOverlayRoute();\n        SidebarButton("PLAY",')
            text=once(text,'            DrawModsHacks(right_inner_width);',
                '            DrawModsHacks(right_inner_width);\n            LibraryNavigationCheckRendered(false);')
            text=once(text,'        DrawModsHacks(content_width, true);',
                '        DrawModsHacks(content_width, true);\n        LibraryNavigationCheckRendered(true);')
            text=once(text,'    // This function is reachable only after the NEW isolated-profile check.',
                '    LibraryNavigationCheckInit();\n    // This function is reachable only after the NEW isolated-profile check.')
            text=once(text,'void dkr::runtime::ui::configure(const std::filesystem::path& config_directory) {',
                'void dkr::runtime::ui::configure(const std::filesystem::path& config_directory) {\n    LibraryNavigationCheckLauncherInit(config_directory);')
            text='#include "'+(feedback/'settings.hpp').as_posix()+'"\n'+text
            text=once(text,'        dkr::chat::save_settings(output);','        dkr::feedback::save_settings(output);\n        dkr::chat::save_settings(output);')
            text=once(text,'            const int number = std::stoi(value);',
                '            const int number = std::stoi(value);\n            if(dkr::feedback::load_setting(key,number))continue;')
            text=once(text,'std::size_t LibraryCapturePlayer();',
                'bool g_feedback_live_surface=false;\nvoid DrawFeedbackSettings(float,const std::function<void()>&);\nstd::size_t LibraryCapturePlayer();')
            text=once(text,'#include "'+(library_ui/'navigation_check.inl').as_posix()+'"',
                '#include "'+(library_ui/'navigation_check.inl').as_posix()+'"\n#include "'+(feedback/'ui.inl').as_posix()+'"')
            text=once(text,'    const bool show_overlay =','    PumpFeedbackUi();\n    const bool show_overlay =')
            start=text.index('bool HandleInputCaptureEvent(');end=text.index('void DrawOverlayContent(',start)
            text=text[:start]+text[start:end].replace('g_selected_player','LibraryCapturePlayer()')+text[end:]
            if text.count('        DrawTextEntryKeyboard();')!=2:raise ValueError('Shared launcher/overlay modal owners changed')
            text=text.replace('        DrawTextEntryKeyboard();',
                '        DrawSessionAnalogueModal();\n        LibraryFinishUiFrame();\n        DrawTextEntryKeyboard();')
            if text.count('        LibraryFinishUiFrame();')!=2:raise ValueError('Feedback needs both retained modal owners')
            text=text.replace('        LibraryFinishUiFrame();','        LibraryFinishUiFrame();\n        PumpFeedbackUi(true);')
            text=once(text,'    // The ROM line.','    DrawLauncherFeatured(width,game_running);\n\n    // The ROM line.')
            text=once(text,'void DrawOverlayContent(float content_width) {',
                '#include "'+(library_ui/'motion_ui.inl').as_posix()+'"\n\nvoid DrawOverlayContent(float content_width) {\n    LauncherContentEnter content_enter([]{return g_overlay_page.load();});')
            text=once(text,'        ImGui::PopStyleColor(2);\n    } else if (g_overlay_page == kPageGraphics) {',
                '        ImGui::PopStyleColor(2);\n        PaddockGap(24.F);DrawPlayTools(content_width);\n    } else if (g_overlay_page == kPageGraphics) {')
            for call,width in [('DrawSaveManagerPage(content_width, true);','content_width'),
                               ('DrawAchievementsPage(content_width);','content_width'),
                               ('DrawSaveManagerPage(right_inner_width, false);','right_inner_width'),
                               ('DrawAchievementsPage(right_inner_width);','right_inner_width')]:
                text=once(text,call,'DrawPlayToolBack('+width+');\n            '+call)
            text=once(text,'        g_launcher_rom = &play_context;',
                '        g_launcher_rom = &play_context;\n        { LauncherContentEnter content_enter([&]{return page;});')
            text=once(text,'        ImGui::Dummy({0.0F, 54.0F});\n        g_launcher_rom = nullptr;',
                '        ImGui::Dummy({0.0F, 54.0F});\n        }\n        g_launcher_rom = nullptr;')
            text=once(text,'        SDL_RenderPresent(renderer);',
                '        LauncherLayoutCheckCapture(renderer);\n        SDL_RenderPresent(renderer);')
            text=parity_surface(text,out)
            text=stage_include(text,'runtime_settings_ui.inl',localised_settings,ROOT/'runtime-recomp/src/game')
            text=stage_include(text,'runtime_messages_ui.inl',localised_messages,ROOT/'runtime-recomp/src/game')
            text=once(text,'#include "'+(out/'runtime_settings_ui.inl').as_posix()+'"',
                '#include "'+(out/'runtime_settings_ui.inl').as_posix()+'"\n#include "'+(ROOT/'runtime-recomp/src/game/localisation/settings_ui.inl').as_posix()+'"')
            text=stage_include(text,'runtime_sound_ui.inl',localised_sound,ROOT/'runtime-recomp/src/game')
            # All compiled UI copy uses the new label. Service/storage IDs stay intact.
            text=library_copy(text)
            # Reuse the real preparation/admission flow for an explicitly
            # requested car check. configure_owned_online_check is reached
            # only after game_main has required a NEW isolated profile.
            text='#include "vehicle_lighting/check.hpp"\n'+text
            text=once(text,'    if(!host){dkr::runtime::spectators::request_party_join(&online,false);return true;}',
                '''    if(std::getenv("DKR_OWNED_CHECK_CARS")||std::getenv("DKR_OWNED_CHECK_ROAD_WHEELS")) {
        g_online_mod_identity=identity;
        dkr::runtime::vehicles::lighting::owned_car_check=std::getenv("DKR_OWNED_CHECK_CARS")!=nullptr;
        dkr::runtime::vehicles::lighting::owned_road_wheel_check=std::getenv("DKR_OWNED_CHECK_ROAD_WHEELS")!=nullptr;
        dkr::runtime::vehicles::lighting::owned_check_pump=[] {
            PumpOnlineMods();
            if(!g_online_mod_sync)return;
            const auto view=g_online_mod_sync->snapshot();
            if(view.phase==dkr::mods::online::SyncPhase::Review && view.manifest) {
                const bool cars_only=view.manifest->code.empty() && !view.manifest->content.empty() &&
                    std::ranges::all_of(view.manifest->content,[](const auto& content){return content.kind==dkr::mods::online::CarContent;});
                // This check accepts data-only cars from its disposable local
                // host. It never auto-approves executable code or keeps mods.
                const bool characters_only=dkr::runtime::vehicles::lighting::owned_road_wheel_check &&
                    view.manifest->code.empty() && !view.manifest->content.empty() &&
                    std::ranges::all_of(view.manifest->content,[](const auto& content){return content.kind==dkr::mods::online::Kind::Character;});
                g_online_mod_sync->consent(cars_only||characters_only,false);
            }
        };
    }
    if(!host){dkr::runtime::spectators::request_party_join(&online,false);return true;}''')
            text=once(text,'    if(!FinishOnlineLobby(request,{},0)){error=g_online_action_status;return false;}',
                '''    if(std::getenv("DKR_OWNED_CHECK_CARS")||std::getenv("DKR_OWNED_CHECK_ROAD_WHEELS")) {
        // The fresh-profile launcher starts asynchronous library scans. Let
        // those release their ordinary library locks before freezing cars.
        const auto ready=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        while(g_vehicle_library.view()->busy || g_legacy_imports.snapshot().busy || g_code_library.view()->busy) {
            g_legacy_imports.tick();
            if(std::chrono::steady_clock::now()>=ready){error="The isolated car library scan timed out.";return false;}
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto settings=OnlineModSettings();
        if(!g_online_mod_sync || !g_online_mod_sync->prepare_host(settings,g_config_directory/"mods"/"legacy",{},
                std::string(dkr::mods::online::asset_revision(identity.revision)),true,
                OnlineModRuntimePreparation(settings.imported_roms,request.rules.synchronization))) {
            error="The isolated car preparation could not start.";return false;
        }
        g_online_mod_host_request=request;
        return true;
    }
    if(!FinishOnlineLobby(request,{},0)){error=g_online_action_status;return false;}''')
        else:
            begin=text.index('LibraryCardView DescribeLibraryCard(');end=text.index('float MeasureLibraryCard(',begin)
            section=text[begin:end]
            if section.count('return view;')!=2:raise ValueError('Library card copy owner changed')
            section=section.replace('return view;','ResolveLibraryCardCopy(view);return view;')
            section=once(section,'view.info += name;','view.info += dkr::localisation::source(name);')
            section=once(section,'"By " + track.author','dkr::localisation::format("library.by",{{"author",track.author}})')
            text=text[:begin]+'#include "'+(ROOT/'runtime-recomp/src/game/localisation/library_card.inl').as_posix()+'"\n'+section+text[end:]
            start=text.index('int DrawLibraryCategories(')
            body=text.index('{',start)
            end=text.index('\n}\n',body)
            text=text[:start]+'#include "'+(ROOT/'runtime-recomp/src/game/library_ui/category_tabs.inl').as_posix()+'"\n'+text[start:body+1]+'''
    const std::array<const char*,4> labels{"Tracks","Characters","Custom Magic Codes","Vehicles"};
    const std::array<std::string,4> counts{std::to_string(tracks),std::to_string(characters),std::to_string(codes),std::to_string(vehicles)};
    return LauncherCategoryTabs("mods-categories",labels,counts,current,available);
'''+text[end:]
            text=text.replace('"On for next launch"','"ON"')
            text=once(text,'bool PaddockImportButton(const char* label, float forced_width = 0.0F, bool update_icon = false) {',
                'bool PaddockImportButton(const char* label, float forced_width = 0.0F, bool update_icon = false, bool website_icon = false, int tool_icon = 0) {\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();')
            text=once(text,'float PaddockImportButtonWidth(const char* label) {',
                'float PaddockImportButtonWidth(const char* label) {\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();')
            text=once(text,'    if (update_icon) {', '''    if (website_icon) {
        draw->AddRect({symbol.x+7.F,symbol.y+11.F},{symbol.x+19.F,symbol.y+23.F},ink,2.F,0,2.F);
        draw->AddLine({symbol.x+13.F,symbol.y+17.F},{symbol.x+23.F,symbol.y+7.F},ink,2.F);
        draw->AddLine({symbol.x+16.F,symbol.y+7.F},{symbol.x+23.F,symbol.y+7.F},ink,2.F);
        draw->AddLine({symbol.x+23.F,symbol.y+7.F},{symbol.x+23.F,symbol.y+14.F},ink,2.F);
    } else if (tool_icon==1) {
        draw->AddLine({symbol.x+9.F,symbol.y+7.F},{symbol.x+21.F,symbol.y+7.F},ink,2.F);
        draw->PathArcTo({symbol.x+15.F,symbol.y+12.F},6.F,0.F,3.141593F,16);draw->PathStroke(ink,0,2.F);
        draw->AddLine({symbol.x+9.F,symbol.y+7.F},{symbol.x+9.F,symbol.y+12.F},ink,2.F);
        draw->AddLine({symbol.x+21.F,symbol.y+7.F},{symbol.x+21.F,symbol.y+12.F},ink,2.F);
        draw->AddLine({symbol.x+15.F,symbol.y+18.F},{symbol.x+15.F,symbol.y+23.F},ink,2.F);
        draw->AddLine({symbol.x+10.F,symbol.y+23.F},{symbol.x+20.F,symbol.y+23.F},ink,2.F);
    } else if (tool_icon==3) {
        const bool on=dkr::session_content::next_enabled.load();
        draw->AddRectFilled({symbol.x+3.F,symbol.y+9.F},{symbol.x+27.F,symbol.y+21.F},PaddockCol(on?0x13B99B:0x32576A),6.F);
        draw->AddCircleFilled({symbol.x+(on?21.F:9.F),symbol.y+15.F},4.F,ink,16);
    } else if (tool_icon==2) {
        draw->AddRect({symbol.x+7.F,symbol.y+7.F},{symbol.x+23.F,symbol.y+23.F},ink,2.F,0,2.F);
        draw->AddRect({symbol.x+11.F,symbol.y+7.F},{symbol.x+19.F,symbol.y+13.F},ink,0.F,0,2.F);
        draw->AddRect({symbol.x+11.F,symbol.y+17.F},{symbol.x+19.F,symbol.y+23.F},ink,0.F,0,2.F);
    } else if (update_icon) {''')
            text=once(text,'// ------------------------------------------------------------ library data',
                '#include "'+(out/'character_portraits_ui.inl').as_posix()+'"\n\n// ------------------------------------------------------------ library data')
            text=once(text,'        view.info = "Custom racer";',
                '        view.info = "Custom racer";\n        view.preview_height=100;view.preview=[item](float w,float h){DrawCharacterPortrait(item,w,h);};')
            text=consistent_browser(text)
            for signature in ('void PaddockModalText(const ImVec4& colour, std::string_view text) {',
                    'void PaddockModalText(std::string_view text) {'):
                text=once(text,signature,'template<class T> '+signature.replace('std::string_view text','T&& raw')+
                    '\n    const auto text=LocalisedUi::copy(std::forward<T>(raw));')
            text=once(text,'bool PaddockModalButton(const char* label, float height = 42.0F) {',
                'bool PaddockModalButton(const char* label, float height = 42.0F) {\n    const auto caption=dkr::localisation::label(label);label=caption.c_str();')
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        files[source.name]=text.encode();swaps.append({**row,'new':str(out/source.name),'input_sha256':sha(raw),'output_sha256':sha(text.encode()),'compiled':source.suffix=='.cpp'})
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Portrait outputs must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Character portraits staged through the existing vehicle card preview hook.')

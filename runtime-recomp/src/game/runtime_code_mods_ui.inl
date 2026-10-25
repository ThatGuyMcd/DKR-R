// Shared launcher/overlay presentation. All persistent mutations go through
// the single storage worker; details retain an exact package hash, not a row.
// Cup editing and data-only sharing use this same layout on every platform.
std::string g_code_details;
bool g_code_details_request=false;
bool g_code_consent=false;
std::uint64_t g_code_details_generation=UINT64_MAX;
dkr::mods::code::Json g_code_settings;
std::string g_code_settings_error;
bool g_code_settings_dirty=false,g_code_settings_pending=false;
struct CupEditor {
    bool loaded=false,dirty=false,catalog_requested=false,catalog_pending=false,catalog_ready=false,save_pending=false;
    unsigned selected=0;std::uint64_t seed=1,job_generation=0;
    std::vector<dkr::mods::code::trophy::Definition> cups;
    dkr::mods::code::Json pending;
    std::array<char,161> search{};
    int course_filter=0,replace_round=-1;
    int transfer_pending=0;std::uint64_t transfer_generation=0;
    int page=1,focused_round=0,focused_control=0,move_origin=-1;
    bool focus_round=false,close_requested=false,pick_requested=false,options_requested=false,round_actions=false;
    unsigned next_key=1;
    std::map<std::string,std::vector<unsigned>> round_keys;
    bool file_requested=false,browse_pending=false;
    int file_mode=0;std::uint64_t browse_generation=0;
    std::array<char,4097> folder{};
    std::array<char,4097> filename{};
    std::filesystem::path file_folder;
    std::string error,notice;
} g_cup_editor;
void BeginCupFilePicker(bool exporting,bool all=false) {
    auto& e=g_cup_editor;e.file_mode=exporting?(all?2:1):0;e.file_requested=true;
    const std::string name=exporting?"custom-trophy-cups.dkrcup":"";
    e.filename.fill(0);std::copy(name.begin(),name.end(),e.filename.begin());
}
void CupFileDialog(bool exporting,bool all=false) {
    auto& editor=g_cup_editor;
    if(g_code_library.view()->busy)return;
    if(NFD_Init()!=NFD_OKAY){editor.error="The file picker could not open.";return;}
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"DKR-R Trophy Cups","dkrcup"}};
    const auto dialog=exporting?NFD_SaveDialogU8(&result,filters,1,nullptr,"custom-trophy-cups.dkrcup"):
        NFD_OpenDialogU8(&result,filters,1,nullptr);
    std::filesystem::path path;
    if(dialog==NFD_OKAY){path=std::filesystem::u8path(result);NFD_FreePathU8(result);}
    else if(dialog==NFD_ERROR)editor.error=NFD_GetError();
    NFD_Quit();if(path.empty())return;
    if(exporting&&path.extension()!=".dkrcup")path+=".dkrcup";
    const auto generation=g_code_library.view()->generation;
    const bool submitted=exporting?g_code_library.export_cups(std::move(path),all?editor.cups:
        std::vector<dkr::mods::code::trophy::Definition>{editor.cups[editor.selected]}):g_code_library.import_cups(std::move(path));
    if(submitted){editor.transfer_pending=exporting?2:1;editor.transfer_generation=generation;editor.error.clear();editor.notice.clear();}
}
void RequestCupCourses() {
    auto& editor=g_cup_editor;
    if(g_online_mod_selected_rom.empty()){editor.error="Select your Game ROM in Play first.";return;}
    std::vector<std::filesystem::path> labs;
    for(const auto& track:dkr::runtime::custom_tracks::tracks())if(track.enabled)labs.push_back(track.source);
    const auto generation=g_code_library.view()->generation;
    if(g_code_library.load_courses(g_online_mod_selected_rom,g_config_directory/"mods"/"legacy",std::move(labs))) {
        editor.catalog_requested=true;editor.catalog_pending=true;editor.catalog_ready=false;editor.job_generation=generation;
        editor.error.clear();
    }
}
void CupCardBackground(ImDrawList* draw,ImVec2 a,ImVec2 b) {
    PaddockPanelStyle panel;panel.radii={10.0F,18.0F,10.0F,10.0F};
    panel.fill=PaddockRgb(0x0B304B);panel.border=PaddockRgb(0x39657B);panel.border_width=1;
    PaddockPanel(draw,a,b,panel);
}
std::string NewCupId() {
    unsigned next=1;
    while(std::any_of(g_cup_editor.cups.begin(),g_cup_editor.cups.end(),[&](const auto& c){return c.id=="cup_"+std::to_string(next);}))++next;
    return "cup_"+std::to_string(next);
}
bool SaveCustomCups(const dkr::mods::code::LibraryItem& item) {
    auto& editor=g_cup_editor;
    if(!editor.catalog_ready||editor.save_pending)return false;
    try {
        auto data=item.data;
        data["trophies"]=dkr::mods::code::trophy::publication(editor.cups,editor.cups[editor.selected].id,*g_code_library.courses(),editor.seed);
        if(g_code_library.data(item.digest,data)) {
            editor.pending=std::move(data);editor.save_pending=true;editor.error.clear();return true;
        }
    }catch(const std::exception& e){editor.error=e.what();}
    return false;
}
#include "runtime_trophy_editor_ui.inl"

std::array<char,161> g_code_search{};
int g_code_state_filter=0,g_code_sort=0;
void DrawCustomMagicCodeLibrary(float requested_width,bool locked) {
    const auto snapshot=g_code_library.view();
    const float width=std::max(1.0F,std::min(requested_width,ImGui::GetContentRegionAvail().x));
    const bool busy=locked||snapshot->busy;
    ImGui::PushID("code-card-library");
    const float status_width=CupSelectWidth({"All","On","Off","Needs approval"});
    const float sort_width=CupSelectWidth({"Name A-Z","Name Z-A"});
    const bool inline_fields=width>=status_width+sort_width+24+220;
    const bool stacked_fields=!inline_fields&&width<2*std::max(status_width,sort_width)+12;
    const auto origin=ImGui::GetCursorScreenPos();
    const float caption=PaddockFieldLabelType().line+6;
    if(inline_fields)ImGui::SetCursorScreenPos({origin.x,origin.y+caption});
    const auto result=PaddockSearch("search",g_code_search.data(),g_code_search.size(),
        "Search Custom Magic Codes...",inline_fields?width-status_width-sort_width-24:width);
    if(result==PaddockFieldResult::GamepadActivated)
        RequestCustomTextKeyboard("Search Custom Magic Codes",g_code_search.data(),g_code_search.size(),[](const std::string& text){
            g_code_search.fill(0);std::copy(text.begin(),text.end(),g_code_search.begin());
        });
    const auto fields=inline_fields?ImVec2{origin.x+width-status_width-sort_width-12,origin.y}:
        ImVec2{origin.x,ImGui::GetCursorScreenPos().y+6};
    const float status=inline_fields?status_width:stacked_fields?width:(width-12)*0.5F,sort=inline_fields?sort_width:status;
    ImGui::SetCursorScreenPos(fields);
    PaddockSelect("status","Status",&g_code_state_filter,{"All","On","Off","Needs approval"},status);
    ImGui::SetCursorScreenPos(stacked_fields?ImVec2{fields.x,fields.y+caption+52}:ImVec2{fields.x+status+12,fields.y});
    PaddockSelect("sort","Sort by",&g_code_sort,{"Name A-Z","Name Z-A"},sort);
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({width,(inline_fields?0:50)+caption+44+(stacked_fields?caption+52:0)});PaddockGap(14);
    if(snapshot->busy)DrawToolIntro(snapshot->operation,width);
    if(!snapshot->error.empty())DrawIssue(snapshot->error,dkr::runtime::copy::Area::Mods);
    auto query=std::string(g_code_search.data());
    std::transform(query.begin(),query.end(),query.begin(),[](unsigned char c){return char(std::tolower(c));});
    std::vector<const dkr::mods::code::LibraryItem*> shown;
    for(const auto& item:snapshot->items){
        auto text=dkr::store::library_display_name(item.digest, "magic", item.name)+" "+item.id+" "+item.version;
        std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(!query.empty()&&text.find(query)==text.npos)continue;
        if((g_code_state_filter==1&&!item.enabled)||(g_code_state_filter==2&&item.enabled)||
           (g_code_state_filter==3&&item.approved))continue;
        shown.push_back(&item);
    }
    std::sort(shown.begin(),shown.end(),[](const auto* a,const auto* b){
        auto left=a->name,right=b->name;
        std::transform(left.begin(),left.end(),left.begin(),[](unsigned char c){return char(std::tolower(c));});
        std::transform(right.begin(),right.end(),right.begin(),[](unsigned char c){return char(std::tolower(c));});
        return left==right?a->digest<b->digest:left<right;
    });
    if(g_code_sort==1)std::reverse(shown.begin(),shown.end());
    PaddockText(PaddockReading(12,false,1.5F),PaddockRgb(0x9BB0BD),
        std::to_string(shown.size())+" of "+std::to_string(snapshot->items.size())+" Custom Magic Codes",width);
    PaddockGap(16);
    if(shown.empty()){
        PaddockBox box(width,{24,28});
        PaddockHeading(snapshot->items.empty()?"Your library starts here":"No matching mods",24,0xFFF0C2,box.Inner());
        PaddockGap(12);ImGui::BeginDisabled(busy);
        if(snapshot->items.empty()){
            if(PaddockButton("Import mods",PaddockButtonKind::Primary,std::min(box.Inner(),PaddockButtonWidth("Import mods",PaddockButtonKind::Primary))))ImportMagicCodeWithDialog();
        }else if(PaddockButton("Clear filters",PaddockButtonKind::Plain)){
            g_code_search.fill(0);g_code_state_filter=0;
        }
        ImGui::EndDisabled();box.End(CupCardBackground);ImGui::PopID();return;
    }
    const int columns=std::max(1,int((width+kModsCardGap)/(265+kModsCardGap)));
    const float card_width=std::floor((width-kModsCardGap*(columns-1))/columns);
    const auto grid=ImGui::GetCursorScreenPos();float y=grid.y;
    for(std::size_t first=0;first<shown.size();first+=columns){
        std::vector<LibraryCardView> cards;float height=0;
        for(std::size_t i=first;i<std::min(first+columns,shown.size());++i){
            const auto& item=*shown[i];LibraryCardView card;card.code=true;card.id=item.digest;
            card.title=dkr::store::library_display_name(item.digest, "magic", item.name);card.byline="Version "+item.version;
            card.info=item.presentation?"Local camera mod":item.kept_online?"Kept from online":"Gameplay mod";
            card.enabled=item.enabled;card.gold=item.enabled&&item.approved;
            const bool needs_cup=item.id=="dkr_custom_trophies"&&!item.data.contains("trophies");
            card.toggle_allowed=!busy&&(!needs_cup||!item.selected||!item.approved)&&(item.compatibility.empty()||item.enabled);
            card.status=!item.selected?"Other edition":!item.approved?"Needs approval":needs_cup?"Needs a cup":item.enabled?"On for next launch":"Off";
            card.tone=!item.selected?PaddockTone::Neutral:!item.approved||needs_cup?PaddockTone::Warning:item.enabled?PaddockTone::On:PaddockTone::Neutral;
            if(!item.compatibility.empty()){card.status="Update needed";card.tone=PaddockTone::Warning;}
            if(needs_cup&&item.selected&&item.approved)card.note="Create a cup in Details.";
            height=std::max(height,MeasureLibraryCard(card,card_width));cards.push_back(std::move(card));
        }
        for(std::size_t col=0;col<cards.size();++col){
            const auto& item=*shown[first+col];ImGui::PushID(item.digest.c_str());PaddockEnter enter(int(first+col)+4);
            const bool details=DrawLibraryCard(cards[col],{grid.x+col*(card_width+kModsCardGap),y},card_width,height,false,busy,[&](bool enabled){
                if(item.selected&&(!enabled||item.approved))g_code_library.enable(item.digest,enabled);
                else{g_code_details=item.digest;g_code_details_request=true;}
            });
            if(details){g_code_details=item.digest;g_code_details_request=true;}
            ImGui::PopID();
        }
        y+=height+kModsCardGap;
    }
    ImGui::SetCursorScreenPos(grid);ImGui::Dummy({width,std::max(0.0F,y-grid.y-kModsCardGap+5)});
    DrawTextEntryKeyboard();ImGui::PopID();
}
void DrawCustomMagicCodeDetails(bool locked) {
    constexpr const char* id="Custom Magic Code details###code-mod-details";
    const auto view=g_code_library.view();
    const auto found=std::find_if(view->items.begin(),view->items.end(),[](const auto& item){return item.digest==g_code_details;});
    const bool trophy=found!=view->items.end()&&found->id=="dkr_custom_trophies"&&found->selected&&found->approved&&found->compatibility.empty();
    const char* name=trophy?"Custom Trophy Races###code-mod-details":id;
    if(g_code_details_request) {
        ImGui::OpenPopup(name);g_code_details_request=false;g_code_consent=false;
        g_code_details_generation=UINT64_MAX;g_code_settings_error.clear();
        g_code_settings_dirty=false;g_code_settings_pending=false;g_cup_editor=CupEditor{};
    }
    if(!ImGui::IsPopupOpen(name))return;
    const PaddockFlatScope paddock;
    const auto display=ImGui::GetIO().DisplaySize;
    const ImVec2 maximum{std::max(240.0F,display.x-32.0F),std::max(200.0F,display.y-32.0F)};
    ImGui::SetNextWindowSize({std::min(trophy?1800.0F:1500.0F,maximum.x),std::min(1040.0F,maximum.y)},ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints({std::min(640.0F,maximum.x),std::min(400.0F,maximum.y)},maximum);
    if(!BeginPaddockModalWindow(name,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse,trophy))return;
    bool close=false;
    if(trophy) {
        DrawCustomCupEditor(*found,locked);
        if(g_cup_editor.close_requested){
            g_cup_editor.close_requested=false;
            if(g_cup_editor.dirty)ImGui::OpenPopup("Unsaved changes");else close=true;
        }
    }else {
    ImGui::BeginChild("Code mod content",{0,-90},false);
    if(found==view->items.end())PaddockModalText("This code mod is no longer installed.");
    else {
        const auto& item=*found;
        PaddockHeading(dkr::store::library_display_name(item.digest, "magic", item.name),30,0xFFCD67,ImGui::GetContentRegionAvail().x);
        PaddockModalText("Version "+item.version);
        if(!item.description.empty())PaddockModalText(item.description);
        PaddockGap(10.0F);
        ImGui::BeginDisabled(locked||view->busy);
        if(!item.compatibility.empty()) {
            PaddockModalText(item.compatibility);
            PaddockModalText("The update keeps your cups. Approve the new edition and turn it on afterwards.");
            if(PaddockButton("Update trophy mod",PaddockButtonKind::Primary)) {
                auto package=RuntimeAssetPath("Custom-Magic-Codes/dkr_custom_trophies.nrm");
                if(package.empty())package=RuntimeAssetPath("../share/doc/dkr-port/Custom-Magic-Codes/dkr_custom_trophies.nrm");
                if(package.empty())ImportMagicCodeWithDialog();
                else if(g_code_library.update_trophies(std::move(package))) {
                    g_code_details=std::string(dkr::mods::code::TrophyUpdateDigest);g_code_details_request=true;
                }
            }
            if(item.selected&&item.enabled&&PaddockButton("Turn off old edition",PaddockButtonKind::Plain))g_code_library.enable(item.digest,false);
        } else if(!item.selected) {
            PaddockModalText("Selecting this edition turns the previous edition off.");
            if(PaddockButton("Use this edition"))g_code_library.select(item.digest);
        } else if(!item.approved) {
            PaddockModalText("This is executable mod code. It can change gameplay and game memory. Only approve code from an author you trust.");
            ImGui::Checkbox("I trust this code mod",&g_code_consent);
            ImGui::BeginDisabled(!g_code_consent);
            if(PaddockButton("Approve code"))g_code_library.approve(item.digest,true);
            ImGui::EndDisabled();
        } else {
            bool enabled=item.enabled;
            ImGui::BeginDisabled(item.id=="dkr_custom_trophies"&&!item.data.contains("trophies"));
            if(ImGui::Checkbox("Enabled for next game",&enabled))g_code_library.enable(item.digest,enabled);
            ImGui::EndDisabled();
            if(g_code_details_generation!=view->generation&&!g_code_settings_dirty) {
                g_code_settings=item.settings;g_code_details_generation=view->generation;
            }
            if(g_code_settings_pending&&!view->busy) {
                g_code_settings_pending=false;
                if(item.settings==g_code_settings){g_code_settings_dirty=false;g_code_settings_error.clear();}
                else g_code_settings_error=view->error.empty()?"Your settings could not be saved. Please try again.":view->error;
            }
            for(const auto& option:item.options) {
                const auto id=option.at("id").get<std::string>();
                const auto label=option.value("name",id);
                const auto type=option.at("type").get<std::string>();
                ImGui::PushID(id.c_str());
                const float option_width=std::max(180.0F,ImGui::GetContentRegionAvail().x);
                PaddockBox card(option_width,{16,14});
                PaddockHeading(label,21,0xFFF0C2,card.Inner());PaddockGap(8);
                if(type=="Enum") {
                    int selected=g_code_settings.at(id).get<int>();
                    const auto& choices=option.at("options");
                    std::vector<std::string> names;for(const auto& choice:choices)names.push_back(choice.get<std::string>());
                    if(PaddockSelect("setting","",&selected,names,card.Inner())){g_code_settings[id]=selected;g_code_settings_dirty=true;}
                } else if(type=="Number") {
                    double value=g_code_settings.at(id).get<double>();
                    const double low=option.at("min").get<double>(),high=option.at("max").get<double>();
                    ImGui::SetNextItemWidth(card.Inner());
                    if(ImGui::SliderScalar("##value",ImGuiDataType_Double,&value,&low,&high,"%.1f",ImGuiSliderFlags_AlwaysClamp)) {
                        g_code_settings[id]=std::clamp(value,low,high);g_code_settings_dirty=true;
                    }
                } else if(type=="String") {
                    std::array<char,4097> text{};
                    const auto& value=g_code_settings.at(id).get_ref<const std::string&>();
                    std::copy_n(value.data(),std::min(value.size(),text.size()-1),text.data());
                    const auto field=PaddockSearch("value",text.data(),text.size(),label.c_str(),card.Inner());
                    if(field==PaddockFieldResult::Edited){g_code_settings[id]=std::string(text.data());g_code_settings_dirty=true;}
                    if(field==PaddockFieldResult::GamepadActivated){
                        const auto digest=item.digest;
                        RequestCustomTextKeyboard(label.c_str(),value,text.size(),[id,digest](const std::string& result){
                            if(g_code_details==digest){g_code_settings[id]=result;g_code_settings_dirty=true;}
                        });
                    }
                }
                card.End(CupCardBackground);ImGui::PopID();PaddockGap(12);
            }
            if(!item.options.empty()) {
                if(PaddockButton("Save settings",PaddockButtonKind::Primary)&&g_code_library.settings(item.digest,g_code_settings))g_code_settings_pending=true;
                CupNextAction("Reset settings");
                if(PaddockButton("Reset settings",PaddockButtonKind::Link)) {
                    for(const auto& option:item.options) {
                        auto value=option.at("default");
                        if(option.at("type")=="Enum"&&value.is_string()) {
                            const auto& choices=option.at("options");value=int(std::distance(choices.begin(),std::find(choices.begin(),choices.end(),value)));
                        }
                        g_code_settings[option.at("id").get<std::string>()]=value;
                    }
                    g_code_settings_dirty=true;
                }
                if(g_code_settings_dirty)PaddockModalText(g_code_settings_pending?"Saving settings...":"Unsaved settings");
                if(!g_code_settings_error.empty())PaddockModalText(g_code_settings_error);
            }
            if(PaddockButton("Revoke code permission",PaddockButtonKind::Link))g_code_library.approve(item.digest,false);
        }
        PaddockGap(12.0F);
        if(PaddockButton("Remove from library",PaddockButtonKind::Link)) {
            ImGui::OpenPopup("Remove this code mod?");
        }
        if(ImGui::BeginPopupModal("Remove this code mod?",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            PaddockModalText("Remove this mod and its saved settings from your library?");
            if(PaddockButton("Remove mod",PaddockButtonKind::Stop)&&g_code_library.remove(item.digest)){ImGui::CloseCurrentPopup();close=true;}
            CupNextAction("Keep mod");if(PaddockButton("Keep mod"))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        if(locked)PaddockModalText("Leave the game and lobby to change code mods.");
        if(!view->error.empty())PaddockModalText(view->error);
    }
    ImGui::EndChild();PaddockGap(12.0F);
    ImGui::BeginDisabled(g_cup_editor.save_pending||g_code_settings_pending);
    if(PaddockButton("Close",PaddockButtonKind::Plain)||
        (CupTopmost()&&(CupKey(ImGuiKey_GamepadFaceRight)||CupKey(ImGuiKey_Escape)))) {
        if(g_cup_editor.dirty||g_code_settings_dirty)ImGui::OpenPopup("Unsaved changes");else close=true;
    }
    ImGui::EndDisabled();
    }
    CupPopupSize(680,350);
    if(BeginPaddockModalWindow("Unsaved changes",ImGuiWindowFlags_NoSavedSettings)) {
        PaddockModalText("You have unsaved changes. Keep editing or discard them?");
        if(PaddockButton("Keep editing",PaddockButtonKind::Primary)||
            (CupTopmost()&&(CupKey(ImGuiKey_GamepadFaceRight)||CupKey(ImGuiKey_Escape))))ImGui::CloseCurrentPopup();
        CupNextAction("Discard changes");if(PaddockButton("Discard changes",PaddockButtonKind::Plain)){ImGui::CloseCurrentPopup();close=true;}
        ImGui::EndPopup();
    }
    if(close)ImGui::CloseCurrentPopup();
    DrawTextEntryKeyboard();
    ImGui::EndPopup();
}

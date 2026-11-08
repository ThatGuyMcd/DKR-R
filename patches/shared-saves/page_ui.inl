// Complete manager actions and stored-save recovery. This replaces the old
// split-profile UI, but keeps the proven slot/record editor above unchanged.
void SharedPakDialog(int port,bool importing) {
    if(SharedSaveBusy())return;
    if(NFD_Init()!=NFD_OKAY){g_save_manager_status="The system file picker could not be opened.";return;}
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"DKR-R Controller Pak","mpk"}};
    const auto dialog=importing?NFD_OpenDialogU8(&result,filters,1,nullptr):NFD_SaveDialogU8(&result,filters,1,nullptr,"dkr-controller-pak.mpk");
    if(dialog==NFD_OKAY) {
        auto path=std::filesystem::u8path(result);NFD_FreePathU8(result);
        if(importing)g_shared_save_import={path,2,port};
        else BeginSharedSaveJob([path,port](std::string& error){return dkr::runtime::saves::export_controller_pak(port,path,error);},"Controller Pak exported.");
    } else if(dialog==NFD_ERROR)g_save_manager_status=NFD_GetError();
    NFD_Quit();
}
void SharedRawExportDialog(const std::filesystem::path& source) {
    if(NFD_Init()!=NFD_OKAY)return;
    nfdu8char_t* result=nullptr;const nfdfilteritem_t filters[]={{"Original recovery image","bin"}};
    if(NFD_SaveDialogU8(&result,filters,1,nullptr,"original-save-image.bin")==NFD_OKAY) {
        auto dest=std::filesystem::u8path(result);NFD_FreePathU8(result);
        BeginSharedSaveJob([source,dest](std::string& error){return dkr::runtime::saves::shared::export_raw(source,dest,error);},"Original image exported unchanged. It has not been repaired.");
    }
    NFD_Quit();
}
void DrawSavesTransfer(float width,float panel,const SaveManagerViewCache& view) {
    using namespace dkr::runtime::saves;
    SavesNote("Move your personal Adventure, custom-course records and Controller Paks together. Older save bundles can still be imported.",width);
    if(SavesRaceButton("EXPORT SAVE BUNDLE",0,SavesTone::Plain,SharedSaveBusy()))ExportSaveBundleWithDialog();
    PaddockGap(8);
    if(SavesRaceButton("IMPORT SAVE BUNDLE",0,SavesTone::Plain,SharedSaveBusy()))ImportSaveBundleWithDialog();
    SavesSeparator("Controller Paks",width);
    SavesNote("DKR-R virtual memory cards. Use DKR-R-format .mpk files, not an unrelated emulator format.",width);
    for(int port=0;port<4;++port) {
        ImGui::PushID(port);const auto& pak=view.controller_paks[port];
        SavesNote("Controller "+std::to_string(port+1)+": "+(!pak.exists?"not created":pak.valid?"ready":"needs recovery"),width,18,kSetText);
        if(SavesRaceButton("BACK UP PAK",0,SavesTone::Plain,SharedSaveBusy()||!pak.valid))
            BeginSharedSaveJob([port](std::string& error){std::filesystem::path created;return backup_controller_pak(port,created,error);},"Controller Pak backed up.");
        PaddockGap(8);
        if(SavesRaceButton("EXPORT PAK",0,SavesTone::Plain,SharedSaveBusy()||!pak.valid))SharedPakDialog(port,false);
        PaddockGap(8);
        if(SavesRaceButton("IMPORT PAK",0,SavesTone::Plain,SharedSaveBusy()))SharedPakDialog(port,true);
        PaddockGap(18);ImGui::PopID();
    }
    (void)panel;
}
void DrawSavesConfirmations(SavesPageState& state) {
    using namespace dkr::runtime::saves;
    using Ask=SavesPageState::Ask;
    static Ask shown=Ask::None;
    if(state.ask==Ask::None)return;
    if(shown!=state.ask){shown=state.ask;ImGui::OpenPopup("##saves-confirm");}
    std::string heading="Confirm save change",detail,confirm="CONFIRM";
    bool replace=true;
    static std::filesystem::path preview_source;static std::string preview_text;
    if(preview_source!=state.restore){preview_source=state.restore;preview_text=state.restore.empty()?std::string{}:shared::describe(state.restore);}
    switch(state.ask) {
        case Ask::Repair: heading="Repair personal save?";detail="Only checksums can be repaired. Damaged progress cannot be reconstructed. An unchanged original is preserved.";confirm="REPAIR & KEEP ORIGINAL";break;
        case Ask::Reset: heading="Reset personal save?";detail="Reset all three slots, unlocks and records. Your current Adventure is backed up first.";confirm="BACK UP & RESET";break;
        case Ask::Restore: heading="Use this as your personal save?";detail=preview_text;confirm="BACK UP & USE THIS SAVE";break;
        case Ask::MaxOut: heading=state.slot<0?"Complete the entire save?":"Complete this Adventure?";detail=state.slot<0?"Complete all three slots and unlock Drumstick, T.T. and Adventure Two. Existing initials and personal records are kept.":"Complete this slot and unlock Drumstick, T.T. and Adventure Two. Other slots and personal records are kept.";confirm="BACK UP & SAVE COMPLETION";break;
        case Ask::UnlockAll: heading="Unlock racers and modes?";detail="Unlock Drumstick, T.T. and Adventure Two. Your Adventure progress and personal best times are kept.";confirm="BACK UP & SAVE UNLOCKS";break;
        case Ask::Discard: heading="Discard editor changes?";detail="Reload your current personal save. Nothing on disk will be replaced.";confirm="DISCARD DRAFT";replace=false;break;
        case Ask::Apply: heading="Apply editor changes?";detail="Save the draft to your personal Adventure. A backup is created first.";confirm="BACK UP & APPLY";break;
        case Ask::Import: heading="Import personal save?";detail=preview_text;confirm="BACK UP & IMPORT";break;
        case Ask::Bundle: heading="Import save bundle?";detail="Replace the personal Adventure and Controller Paks included in this bundle. Originals are preserved in a recoverable transaction.";confirm="BACK UP & IMPORT BUNDLE";break;
        case Ask::Pak: heading="Import Controller Pak?";detail="Replace Controller "+std::to_string(state.slot+1)+"'s Pak. An existing valid image is backed up first.";confirm="BACK UP & IMPORT PAK";break;
        case Ask::RepairUse: heading="Repair and use a copy?";detail="Attempt checksum repair, then replace your personal save with that copy. Corrupt progress cannot be reconstructed. Both originals are retained.";confirm="REPAIR COPY & USE";break;
        case Ask::Host: heading="Use this online Adventure?";detail=preview_text+". The current host Adventure is backed up. Personal saves are unchanged.";confirm="USE FOR HOSTING";break;
        case Ask::DevelopmentCopy: heading="Refresh the next development save?";detail="Copy your saved personal progress into the next offline development session. The existing development save will be preserved before replacement. Your personal save will not be changed. Unapplied editor changes are not included.";confirm="QUEUE DEVELOPMENT COPY";replace=false;break;
        default:break;
    }
    if(!BeginSettingsDialog("##saves-confirm",heading)) {
        if(!ImGui::IsPopupOpen("##saves-confirm")){state.ask=shown=Ask::None;}
        return;
    }
    const auto inner=SettingsDialogInner();
    SavesNote(detail,inner);
    if(!state.restore.empty()&&(state.ask==Ask::Import||state.ask==Ask::Restore||state.ask==Ask::RepairUse||state.ask==Ask::Host))SavesNote(PathUtf8(state.restore),inner,14);
    if(replace&&state.ask!=Ask::Host)SavesNote("Normal play and mods use this personal save. Online and development saves stay separate.",inner);
    if(SavesDraftDirty()&&state.ask!=Ask::Apply&&state.ask!=Ask::MaxOut&&state.ask!=Ask::UnlockAll)SavesNote("Your unapplied editor draft will be discarded after this succeeds.",inner);
    const float height=44;const bool stack=PaddockRaceButtonWidth(confirm.c_str())+PaddockRaceButtonWidth("CANCEL")+12>inner;
    const float cell=stack?inner:(inner-12)*0.5f;
    bool close=false;
    if(SavesRaceButton("CANCEL",cell))close=true;
    if(!stack)ImGui::SameLine(0,12);else PaddockGap(10);
    if(SavesRaceButton(confirm.c_str(),cell,SavesTone::Primary,SharedSaveBusy())) {
        const auto source=state.restore;const auto port=state.slot;
        if(state.ask==Ask::Discard) {SavesForgetDraft();g_shared_draft_revision.clear();}
        else if(state.ask==Ask::MaxOut||state.ask==Ask::UnlockAll||state.ask==Ask::Apply) {
            codec::SaveImage image;std::string error;std::string expected=g_shared_draft_revision;
            bool ready=false;
            if(g_save_builder_image){image=*g_save_builder_image;ready=true;}
            else {expected=shared::revision();ready=load_adventure(image,error);}
            if(ready) {
                if(state.ask==Ask::MaxOut)shared::complete(image,state.slot);
                if(state.ask==Ask::UnlockAll)shared::unlock(image);
                BeginSharedSaveJob([image=std::move(image),expected](std::string& error){return shared::apply(image,expected,error);},"Personal save updated and read-back verified. Existing Dev Menu copies are separate; use Copy Personal Save to Development to refresh one.");
            }else g_save_manager_status=error;
        } else if(state.ask==Ask::Reset)BeginSharedSaveJob([](std::string& error){return shared::reset(error);},"Personal save reset. Previous progress and custom records are kept in Recovery.");
        else if(state.ask==Ask::Bundle)BeginSharedSaveJob([source](std::string& error){return import_bundle(source,error);},"Save bundle imported. Original images are kept in the transaction archive.");
        else if(state.ask==Ask::Pak)BeginSharedSaveJob([source,port](std::string& error){return shared::prepare(g_config_directory,error)&&import_controller_pak(port,source,error);},"Controller Pak imported.");
        else if(state.ask==Ask::Host)BeginSharedSaveJob([source](std::string& error){return shared::import_host(source,error);},"Online Adventure selected. Personal progress was not changed.");
        else if(state.ask==Ask::DevelopmentCopy)BeginSharedSaveJob([](std::string& error){return shared::queue_development_copy(error);},"Personal save snapshot queued for the next offline development launch. Existing development progress will be preserved.");
        else if(state.ask==Ask::Repair)BeginSharedSaveJob([](std::string& error){bool changed;std::filesystem::path original;return shared::prepare(g_config_directory,error)&&repair_adventure_checksums(changed,original,error);},"Checksum repair complete. Check your progress; the original was retained.");
        else {const bool repair=state.ask==Ask::RepairUse;BeginSharedSaveJob([source,repair](std::string& error){return shared::adopt(source,repair,error);},"Personal save replaced. Both the selected original and previous progress were retained.");}
        close=true;
    }
    if(close){ImGui::CloseCurrentPopup();state.ask=shown=Ask::None;state.restore.clear();}
    EndSettingsDialog();(void)height;
}

struct SharedSaveInventory {
    std::vector<dkr::runtime::saves::StoredAdventure> records;
    bool truncated=false;
};
void DrawStoredSaves(SavesPageState& state,float width,bool locked) {
    using namespace dkr::runtime::saves;
    static std::future<SharedSaveInventory> pending;
    static SharedSaveInventory inventory;
    static std::size_t limit=256;
    static std::uint64_t generation=0;
    static auto refresh=std::chrono::steady_clock::time_point{};
    const auto now=std::chrono::steady_clock::now();
    const bool reload=generation!=g_shared_save_generation||now>=refresh;
    if(!pending.valid()&&(reload||SavesRaceButton("REFRESH SAVES"))) {
        generation=g_shared_save_generation;refresh=now+std::chrono::seconds(10);
        pending=std::async(std::launch::async,[budget=limit]{SharedSaveInventory result;result.records=stored_adventures(budget,&result.truncated);return result;});
    }
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try{
            inventory=pending.get();
            std::map<std::tuple<int,int,std::string>,std::size_t> groups;
            std::vector<StoredAdventure> compact;
            for(auto& record:inventory.records) {
                const auto bucket=record.scope==0?0:record.scope==1?1:3;
                const auto key=std::tuple{bucket,int(record.kind),record.content_hash};
                const auto [it,added]=groups.emplace(key,compact.size());
                if(added)compact.push_back(std::move(record));else ++compact[it->second].copies;
            }
            inventory.records=std::move(compact);
        }catch(const std::exception& e){g_save_manager_status=e.what();}
    }
    if(pending.valid())SavesNote("Reading saved progress...",width);
    SavesNote(state.scope==1?"Online Adventures are separate. The host can continue with different mods without switching saves.":
        "Choose an older save to use as your personal Adventure. Original mod-set copies and backups are never removed automatically.",width);
    bool found=false;
    for(const auto& record:inventory.records) {
        if(state.scope==1?(record.scope!=1):(record.scope==0||record.scope==1))continue;
        found=true;ImGui::PushID(PathUtf8(record.info.path).c_str());
        PaddockBox box(width,{19,19});const auto inner=box.Inner();
        const auto label=record.kind==StoredSaveKind::CustomRecords?"Custom-course records":
            record.kind==StoredSaveKind::ControllerPak?"Controller Pak backup":record.kind==StoredSaveKind::ExperimentalPaks?"Online Controller Paks":
            state.scope==1?"Online Adventure":record.scope==2?"Older mod-set Adventure":"Adventure backup";
        SavesNote(label,inner,17,kSetText);
        SavesNote(record.preview,inner);SavesNote(SavesBackupTime(record.info.path),inner,14);
        if(record.copies>1)SavesNote(std::to_string(record.copies)+" identical copies grouped here. All originals are kept.",inner,14);
        const bool adventure=record.kind==StoredSaveKind::Adventure;
        const bool development=record.info.path.generic_string().find("/development/")!=std::string::npos;
        if(development)SavesNote("Development image - contains tool-session progress, not ordinary play.",inner,14,kSetWarm);
        if(adventure&&!locked) {
            if(SavesRaceButton(record.info.valid?(state.scope==1?"USE FOR HOSTING":"USE AS PERSONAL SAVE"):"REPAIR & USE COPY",0,SavesTone::Primary,SharedSaveBusy()||(!record.info.valid&&record.info.size!=512))) {
                state.restore=record.info.path;state.ask=!record.info.valid?SavesPageState::Ask::RepairUse:state.scope==1?SavesPageState::Ask::Host:SavesPageState::Ask::Restore;
            }
            PaddockGap(8);
        }
        if(SavesRaceButton(record.info.valid?"EXPORT COPY":"EXPORT ORIGINAL IMAGE",0,SavesTone::Plain,SharedSaveBusy())) {
            if(record.info.valid)ExportAdventureWithDialog(record.info.path,record.kind);else SharedRawExportDialog(record.info.path);
        }
        PaddockGap(8);
        if(SavesRaceButton("COPY LOCATION"))SDL_SetClipboardText(PathUtf8(record.info.path).c_str());
        box.End([](ImDrawList* draw,ImVec2 a,ImVec2 b){PaddockPanel(draw,a,b,{PaddockRound(16),PaddockRgb(0x0B2E40,245),PaddockRgb(kSetBorder),1});});
        PaddockGap(14);ImGui::PopID();
    }
    if(!found&&!pending.valid())SavesNote("No saved images found here.",width);
    if(inventory.truncated) {
        SavesNote("More saved images are available. The list is limited to keep scanning responsive.",width);
        if(limit<50000&&SavesRaceButton("LOAD MORE SAVES")){limit=std::min<std::size_t>(limit*4,50000);refresh={};}
    }
}

void DrawSaveManagerPage(float available_width,bool live) {
    using namespace dkr::runtime::saves;
    auto& state=g_saves_page;const auto width=available_width;
    static std::uint64_t generation=0;
    const auto frame=ImGui::GetFrameCount();const bool entered=state.context!=ImGui::GetCurrentContext()||state.last_frame!=frame-1;
    if(entered||generation!=g_shared_save_generation) {
        state.summary.reset();InvalidateSaveManagerViewCache();++g_shared_save_generation;generation=g_shared_save_generation;
        // Preserve a dirty draft, but compare its revision at commit time.
        // A clean draft can be safely reloaded after gameplay/return.
        if(!SavesDraftDirty())SavesForgetDraft();
    }
    state.context=ImGui::GetCurrentContext();state.last_frame=frame;
    PumpSharedSaveJob();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{0,0});
    DrawPageHeading("SAVE MANAGER");PaddockGap(14);
    if(!g_save_manager_status.empty())DrawFriendlyStatus(g_save_manager_status,width,dkr::runtime::copy::Area::Saves);
    const bool lobby=dkr::runtime::netplay::session().active();const bool locked=live||lobby||SharedSaveBusy();
    const int current=state.scope==0?0:state.scope==1?2:1;
    const int picked=SettingsSectionSigns({"YOUR SAVE","BACKUPS & RECOVERY","ONLINE SAVES"},current,width);
    if(picked>=0){state.scope=picked==0?0:picked==2?1:3;state.ask=SavesPageState::Ask::None;state.restore.clear();}
    PaddockGap(20);
    if(live) {
        SavesNote("Save editing is unavailable while the game is running. Return to the launcher first.",width);
        SavesNote("Active save: "+PathUtf8(runtime_save_path()),width,14);
    } else if(state.scope!=0)DrawStoredSaves(state,width,locked);
    else {
        const auto& view=CachedSaveManagerView();const auto& info=view.adventure;
        if(!view.valid){SavesNote("Reading your personal save...",width);ImGui::PopStyleVar();return;}
        const bool valid=info.exists&&info.valid;
        SavesNote(valid?"YOUR PERSONAL ADVENTURE":info.exists?"YOUR SAVE NEEDS RECOVERY":"NO PERSONAL SAVE YET",width,19,valid?kSavesAccent:kSetWarm);
        if(lobby)SavesNote("Leave the lobby before changing saves. You can export your personal save.",width);
        if(SharedSaveBusy())SavesNote("Finishing the save operation. Launch is temporarily held until it is safe.",width);
        if(!locked) {
            if(!info.valid&&info.exists&&info.size==512) {
                if(SavesRaceButton("BACK UP & REPAIR CHECKSUMS",0,SavesTone::Primary))state.ask=SavesPageState::Ask::Repair;
                PaddockGap(12);
            }
            const auto section=SettingsSectionSigns({"OVERVIEW","EDIT PROGRESS","TRANSFER & PAKS"},state.section,width);
            if(section>=0)state.section=section;
            PaddockGap(18);
            if(state.section==1)DrawSavesEditor(state,width,width,valid,info.exists);
            else if(state.section==2)DrawSavesTransfer(width,width,view);
            else DrawSavesOverview(state,width,width,view,valid);
        } else if(!SharedSaveBusy()&&valid&&SavesRaceButton("EXPORT PERSONAL COPY"))ExportAdventureWithDialog();
    }
    if(!g_shared_save_import.path.empty()&&!locked) {
        state.restore=std::move(g_shared_save_import.path);state.slot=g_shared_save_import.port;
        state.ask=g_shared_save_import.kind==1?SavesPageState::Ask::Bundle:g_shared_save_import.kind==2?SavesPageState::Ask::Pak:SavesPageState::Ask::Import;
        g_shared_save_import={};
    }
    ImGui::PopStyleVar();
    if(!locked)DrawSavesConfirmations(state);
}

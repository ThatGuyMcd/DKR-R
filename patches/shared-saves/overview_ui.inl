void DrawSavesOverview(SavesPageState& state,float width,float panel,const SaveManagerViewCache& view,bool editable) {
    using namespace dkr::runtime::saves;
    SavesNote("One personal save for normal play, with or without mods. Three Adventure slots, shared unlocks and records.",width);
    const bool busy=SharedSaveBusy();
    const auto button=[&](const char* label,bool disabled=false,SavesTone tone=SavesTone::Plain){
        return SavesRaceButton(label,0,tone,busy||disabled);
    };
    if(button("BACK UP SAVE",!editable,SavesTone::Primary))BeginSharedSaveJob([](std::string& error){std::filesystem::path created;return backup_adventure(created,error);},"Personal save backed up.");
    PaddockGap(8);
    if(button("EXPORT SAVE",!editable))ExportAdventureWithDialog();
    PaddockGap(8);
    if(button("IMPORT SAVE"))ImportAdventureWithDialog();
    PaddockGap(14);
    if(!view.adventure.exists) {
        if(button("CREATE PERSONAL SAVE"))state.ask=SavesPageState::Ask::Reset;
        return;
    }
    if(editable) {
        if(!state.summary&&view.image)state.summary=view.image;
        if(state.summary) {
            SavesSeparator("Adventure slots",width);
            for(unsigned i=0;i<3;++i) {
                const auto& slot=state.summary->slots[i];
                SavesNote("Slot "+std::to_string(i+1)+" - "+(slot.name.empty()?"---":slot.name)+" - "+std::to_string(slot.balloons[0])+" / 47 balloons",width,18,kSetText);
            }
            const auto& settings=state.summary->settings;
            SavesNote(std::string("Drumstick: ")+(settings.drumstick?"unlocked":"locked")+"   |   T.T.: "+
                (std::all_of(settings.tt_trials.begin(),settings.tt_trials.end(),[](bool x){return x;})?"unlocked":"locked")+
                "   |   Adventure Two: "+(settings.adventure_two?"unlocked":"locked"),width);
        }
        if(button("MAX OUT ENTIRE SAVE",false,SavesTone::Primary)){state.slot=-1;state.ask=SavesPageState::Ask::MaxOut;}
        PaddockGap(8);
        if(button("UNLOCK ALL RACERS AND MODES"))state.ask=SavesPageState::Ask::UnlockAll;
        SavesSeparator("Development tools",width);
        SavesNote("Dev Menu uses a separate copy, not this personal save. New development copies start from your saved progress. You can refresh an existing copy below; its previous progress is kept.",width);
        if(button("COPY PERSONAL SAVE TO DEVELOPMENT"))state.ask=SavesPageState::Ask::DevelopmentCopy;
    }
    SavesSeparator("Older progress and backups",width);
    SavesNote("Older mod-set saves are kept. Choose one from Backups & Recovery if it contains the progress you want to keep. Nothing is merged or deleted automatically.",width);
    if(button("OPEN BACKUPS & RECOVERY")){state.scope=3;state.ask=SavesPageState::Ask::None;}
    PaddockGap(20);
    if(button("RESET PERSONAL SAVE",false,SavesTone::Danger))state.ask=SavesPageState::Ask::Reset;
    (void)panel;
}

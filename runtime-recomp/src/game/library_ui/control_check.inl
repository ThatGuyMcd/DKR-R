// Shared real field widgets, exercised without signing into or writing to the
// account service. Only the existing authenticated disposable profile can arm this.
void LibraryControlsCheckRoute(int step,int& page) {
    if(!g_library_controls_check)return;
    if(step>=23&&step<=28) {
        page=kPageModsHacks;g_mods_page.section=g_mods_page.entry_section=0;
        g_mods_page.category=step<=25?step==25?1:0:step==26?2:3;
        g_mods_page.filters_open=step==23||step==25;
        g_mod_browsers[0].duplicates=step==24?1:0;
        g_code_state_filter=step==26?3:0;
        g_vehicle_state_filter=step==27?1:0;g_vehicle_sort=step==27?1:0;
        std::snprintf(g_vehicle_search.data(),g_vehicle_search.size(),"%s",step==28?"No matching vehicle":"");
    }
    if(step>=29)page=kPageModLibrary;
}
bool DrawLibraryControlsFieldCheck(float width) {
    if(!g_library_controls_check||g_launcher_layout_step<29)return false;
    const int step=std::min(14,g_launcher_layout_step-29);
    DrawPageHeading("MOD LIBRARY");PaddockGap(12.F);
    StoreNote("Isolated field layout check: actual shared controls; no account submission.",width);
    PaddockGap(16.F);
    struct Field {const char* key;const char* label;std::size_t maximum;bool multiline=false,review=false;};
    const std::array<std::vector<Field>,15> groups{{
        {{"installed-search","Find a mod",200},{"duplicates-search","Find a duplicate",200}},
        {{"download-search","Find a download",200}},
        {{"request-summary","Description and creator credits",4000,true},{"request-source","Original source link",1500},{"request-download","Download link",1500}},
        {{"check-reply","Reply to the library team",2400,true},{"check-sourceUrl","Updated source link",1500},{"check-requirements","Requirements",2000,true}},
        {{"comment-compose","Your comment",2000,true},{"comment-edit","Edit comment",2000,true}},
        {{"check-review-text","Review",4000,true,true},{"check-review-version","Version played",80}},
        {{"link-search","Find a Library mod",200},{"request-listing-search","Find a mod",200}},
        {{"hosted-search","Find a file",160},{"related-search","Find a mod",200}},
        {{"draft-summary","Description",4000,true},{"draft-tags","Tags, separated by commas",500},{"draft-image","Cover image link",1500}},
        {{"draft-fileName","Package filename",180},{"draft-fileUrl","Package download link",1500}},
        {{"draft-requirements","Requirements",2000,true},{"draft-install","Installation instructions",6000,true}},
        {{"check-private","Add a private note",1600,true},{"check-decision","Message to requester",2400,true}},
        {{"staff-request-search","Find a request",200},{"staff-report-search","Find a report",200},{"staff-listing-search","Find a listing or draft",200}},
        {{"account-review-search","Find a review",200},{"audit-search","Find activity",200},{"staff-access-search","Find an account",200}},
        {{"check-narrow","Version played (narrow modal)",80},{"check-narrow-note","Edit comment (narrow modal)",2000,true}}
    }};
    const float available=step==14?std::min(width,180.F):width;
    for(const auto& field:groups[step]) {
        auto& value=g_mod_store.fields[field.key];if(value.empty())value="MiXeD case preview";
        StoreField(field.key,field.label,available,field.maximum,field.multiline,field.review);
        PaddockGap(12.F);
    }
    return true;
}

// Labels stay above their input; the keyboard is attached to the input's
// right edge. Existing draft/account identity checks own accepted text.
void StoreField(const char* key,const char* label,float width,std::size_t maximum=500,
        bool multiline=false,bool review=false,bool inline_keyboard=false) {
    label=dkr::localisation::source(label).data();
    (void)inline_keyboard; // Retain existing call compatibility; every field is inline now.
    using namespace dkr::library_ui;
    width=std::max(1.F,std::min(width,ImGui::GetContentRegionAvail().x));
    auto& value=g_mod_store.fields[key];
    ImGui::PushID(key);ImGui::BeginGroup();
    const auto open_keyboard=[&] {
        const std::string name=key,identity=g_mod_store.identity;
        RequestCustomTextKeyboard(label,value,maximum+1,[name,identity](const std::string& accepted) {
            if(g_mod_store.identity==identity) {
                g_mod_store.fields[name]=accepted;
                if(name.starts_with("draft-")){g_mod_store.dirty=true;g_mod_store.previewed=false;}
            }
        });
    };
    StoreNote(label,width);PaddockGap(4.F);
    const auto at=ImGui::GetCursorScreenPos();
    const auto type=PaddockReading(14.F,false,1.45F);
    ImGui::PushFont(type.font);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,
        std::max(0.F,(ControlHeight-ImGui::GetFontSize())*.5F)});
    const float height=multiline?(review?ImGui::GetTextLineHeight()*6.F+ImGui::GetStyle().FramePadding.y*2.F:90.F):ControlHeight;
    const auto layout=text_field(width,height);
    std::vector<char> buffer(maximum+1,0);
    std::memcpy(buffer.data(),value.data(),std::min(maximum,value.size()));
    ImGui::SetNextItemWidth(layout.input_width);
    const bool changed=review&&multiline?DkrReviewInputText("##text",buffer.data(),buffer.size(),{layout.input_width,height}):
        multiline?ImGui::InputTextMultiline("##text",buffer.data(),buffer.size(),{layout.input_width,height}):
        ImGui::InputText("##text",buffer.data(),buffer.size());
    const bool controller=ImGui::IsItemFocused()&&ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)&&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
    StoreControlFocus();
    ImGui::PopStyleVar();ImGui::PopFont();
    if(changed) {
        value=buffer.data();
        if(std::string_view(key).starts_with("draft-")){g_mod_store.dirty=true;g_mod_store.previewed=false;}
    }
    ImGui::SetCursorScreenPos({at.x+layout.keyboard_x,at.y+layout.keyboard_y});
    const bool keyboard=PaddockKeyboardButton();
    if(controller||keyboard)open_keyboard();
    ImGui::SetCursorScreenPos(at);ImGui::Dummy({width,layout.height});
    ImGui::EndGroup();ImGui::PopID();PaddockGap(9.F);
}

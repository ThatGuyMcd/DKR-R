void DrawSdk2Camera(const dkr::sdk2::Panel& panel,const dkr::sdk2::Snapshot& snapshot,Sdk2PanelDraft& draft) {
    bool hidden=dkr::sdk2::hud_hidden.load();if(ImGui::Checkbox("Hide race HUD on this screen",&hidden))dkr::sdk2::hud_hidden=hidden;
    ImGui::TextWrapped("Local screenshot control. Resets when the scene changes; other peers and gameplay HUD state are unchanged.");
    if(ImGui::Button("Hide tools for screenshot",{std::min(260.F,ImGui::GetContentRegionAvail().x),38})){dkr::sdk2::tools_open=false;g_overlay_visible=false;}
    auto& settings=draft.numbers["local-view"];unsigned view=snapshot.online?snapshot.local:unsigned(std::clamp(int(settings[0]),0,3));
    if(!snapshot.online){int selected=int(view);if(ImGui::SliderInt("Local viewport",&selected,0,3))settings[0]=float(selected);view=unsigned(selected);}
    auto camera=dkr::sdk2::camera_state(view);
    ImGui::TextWrapped("LOCAL CAMERA - Temporarily takes this viewport from the normal camera or ModernCamera++. Shared racers and cameras are not edited. Returns automatically on a scene change.");
    if(!camera.valid){ImGui::TextDisabled("Enter a supported race to acquire this local camera.");return;}
    if(camera.enabled) {
        if(ImGui::Button("Return to normal camera",{260,40})){camera.enabled=false;dkr::sdk2::camera_edit(view,camera);}
    }else if(ImGui::Button("Take local camera control",{260,40}))ImGui::OpenPopup("Take camera ownership");
    if(ImGui::BeginPopupModal("Take camera ownership",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Temporarily suspend the current provider for this local viewport? Release it to restore that provider.");
        if(ImGui::Button("Take control",{160,40})){camera.enabled=true;camera.follow=false;dkr::sdk2::camera_edit(view,camera);ImGui::CloseCurrentPopup();}
        if(ImGui::GetContentRegionAvail().x>=328)ImGui::SameLine();if(ImGui::Button("Cancel",{160,40}))ImGui::CloseCurrentPopup();ImGui::EndPopup();
    }
    ImGui::TextWrapped("Right-stick/mouse look and the existing camera zoom actions move the free camera when the tools are closed. It stays within 1000 units of the local racer.");
    ImGui::BeginDisabled(!camera.enabled);
    bool changed=false;ImGui::SetNextItemWidth(std::max(100.F,ImGui::GetContentRegionAvail().x-58));
    changed|=ImGui::InputFloat3("##camera-position",camera.position,"%.2f");ImGui::SameLine();
    if(ImGui::Button("Keys",{50,0})) {
        char initial[128];std::snprintf(initial,sizeof(initial),"%.3f %.3f %.3f",camera.position[0],camera.position[1],camera.position[2]);
        RequestCustomTextKeyboard("Local camera X Y Z",initial,128,[view](const std::string& text){
            auto value=dkr::sdk2::camera_state(view);std::istringstream stream(text);for(auto& coordinate:value.position)if(!(stream>>coordinate)||!std::isfinite(coordinate))return;
            stream>>std::ws;if(stream.eof())dkr::sdk2::camera_edit(view,value);
        });
    }
    changed|=ImGui::SliderAngle("Yaw",&camera.yaw,-180,180);changed|=ImGui::SliderAngle("Pitch",&camera.pitch,-85,85);
    changed|=ImGui::SliderAngle("Field of view",&camera.fov,20,120);changed|=ImGui::SliderFloat("Travel speed",&camera.speed,1,2000,"%.0f");
    changed|=ImGui::SliderFloat("Follow smoothing",&camera.smoothing,0,30,"%.1f");
    if(ImGui::Button("Follow selected entity",{260,40})) {
        camera.follow=true;camera.enabled=true;camera.owner=panel.owner;camera.slot=draft.arguments[0];camera.generation=draft.arguments[1];changed=true;
    }
    if(ImGui::Button("Free / spectator camera",{260,40})){camera.follow=false;camera.enabled=true;changed=true;}
    auto& mark=draft.numbers["camera-bookmark"];
    if(ImGui::Button("Mark local camera position",{260,40})){std::copy_n(camera.position,3,mark.begin());mark[3]=float(camera.scene);}
    ImGui::BeginDisabled(unsigned(mark[3])!=camera.scene);
    if(ImGui::Button("Return to camera bookmark",{260,40})){std::copy_n(mark.begin(),3,camera.position);changed=true;}
    ImGui::EndDisabled();if(changed)dkr::sdk2::camera_edit(view,camera);ImGui::EndDisabled();
}

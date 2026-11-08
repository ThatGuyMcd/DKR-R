#include "launcher_themes/draw.hpp"
#include "imgui_internal.h"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace dkr::launcher_themes;
int main() {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    unsigned char* pixels=nullptr;int width=0,height=0;
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    int maximum=0;
    const auto atlas=reinterpret_cast<ImTextureID>(std::uintptr_t(1));
    for(const ImVec2 size:{ImVec2{800,600},ImVec2{1280,800},ImVec2{2560,1440}}) {
        for(int theme=0;theme<int(Theme::Count);++theme) {
            io.DisplaySize=size;io.DeltaTime=1.F/60;
            ImGui::NewFrame();auto* draw=ImGui::GetForegroundDrawList();
            const auto last_id=ImGui::GetCurrentContext()->LastItemData.ID;
            weather(draw,{0,0},size,Theme(theme),123.5,Calendar::UK);
            const auto before=draw->VtxBuffer.Size;
            corners(draw,{0,0},size,Theme(theme),atlas,Calendar::US);
            assert(draw->VtxBuffer.Size-before==(theme==0?0:8));
            for(int i=before;i<draw->VtxBuffer.Size;++i) {
                const auto& v=draw->VtxBuffer[i];
                assert(v.uv.x>=0&&v.uv.x<=1&&v.uv.y>=0&&v.uv.y<=1);
                assert(v.pos.x>=10&&v.pos.y>=10&&v.pos.x<=size.x-10&&v.pos.y<=size.y-10);
            }
            assert(last_id==ImGui::GetCurrentContext()->LastItemData.ID);
            assert(!ImGui::GetIO().WantCaptureMouse && !ImGui::GetIO().WantCaptureKeyboard);
            assert(draw->VtxBuffer.Size<30000);
            maximum=std::max(maximum,draw->VtxBuffer.Size);
            for(const auto& v:draw->VtxBuffer)assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y));
            if(theme==0)assert(draw->VtxBuffer.empty());else assert(!draw->VtxBuffer.empty());
            ImGui::Render();
        }
    }
    assert(icons_for(Theme::National,Calendar::US)==std::pair(Icon::FlagUS,Icon::BuntingUS));
    assert(icons_for(Theme::National,Calendar::Canada)==std::pair(Icon::FlagCanada,Icon::BuntingCanada));
    assert(icons_for(Theme::National,Calendar::UK)==std::pair(Icon::None,Icon::None));
    assert(icons_for(Theme::Count,Calendar::US).first==Icon::None);
    ImGui::DestroyContext();std::cout<<"All 19 native decorations at desktop/Deck sizes; no input items; maximum vertices="<<maximum<<'\n';
}

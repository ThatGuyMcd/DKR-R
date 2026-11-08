#include "imgui.h"
#include "localisation/service.hpp"
#include "library_ui/control_layout.hpp"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <iostream>

namespace {
struct {std::map<std::string,std::string> fields;std::string identity="account-a";bool dirty=false,previewed=true;} g_mod_store;
struct Type {ImFont* font;};
Type PaddockReading(float,bool,float){return {ImGui::GetIO().Fonts->Fonts[0]};}
void StoreNote(const char* text,float){ImGui::TextUnformatted(text);}
void PaddockGap(float pixels){ImGui::Dummy({0,pixels});}
ImVec2 input_min,input_max,keyboard_min,keyboard_max;
void StoreControlFocus(){input_min=ImGui::GetItemRectMin();input_max=ImGui::GetItemRectMax();}
bool PaddockKeyboardButton(){const bool pressed=ImGui::Button("Keyboard",{44,44});keyboard_min=ImGui::GetItemRectMin();keyboard_max=ImGui::GetItemRectMax();return pressed;}
bool DkrReviewInputText(const char* id,char* text,std::size_t capacity,ImVec2 size){return ImGui::InputTextMultiline(id,text,capacity,size);}
std::function<void(const std::string&)> accepted;
std::string keyboard_title,keyboard_value;std::size_t keyboard_capacity=0;
void RequestCustomTextKeyboard(const char* title,const std::string& value,std::size_t capacity,std::function<void(const std::string&)> callback) {
    keyboard_title=title;keyboard_value=value;keyboard_capacity=capacity;accepted=std::move(callback);
}
#include "library_ui/store_field.inl"
void frame(const char* key,float width,bool multiline=false,bool review=false) {
    ImGui::NewFrame();ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,700});
    ImGui::Begin("Fields",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    StoreField(key,"Field label",width,80,multiline,review);ImGui::End();ImGui::Render();
}
void click_keyboard(const char* key,float width) {
    const ImVec2 point{(keyboard_min.x+keyboard_max.x)*.5F,(keyboard_min.y+keyboard_max.y)*.5F};
    auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(0,true);frame(key,width);
    io.AddMouseButtonEvent(0,false);frame(key,width);
    assert(accepted);
}
}
int main() {
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,800};io.DeltaTime=1.F/60;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    for(float width:{240.F,340.F,815.F})for(bool multiline:{false,true})for(bool review:{false,true}) {
        frame("layout",width,multiline,review);frame("layout",width,multiline,review);
        assert(std::abs(keyboard_min.y-input_min.y)<.01F);
        assert(keyboard_min.x>=input_max.x+11.9F);
        assert(std::abs(keyboard_max.x-input_min.x-width)<.01F);
        assert(keyboard_max.y-keyboard_min.y==44.F);
    }
    frame("narrow",180,true);assert(keyboard_min.y>=input_max.y+7.9F);
    g_mod_store.fields["search"]="Initial";frame("search",500);click_keyboard("search",500);
    assert(keyboard_title=="Field label"&&keyboard_value=="Initial"&&keyboard_capacity==81);
    accepted("MiXeD case");assert(g_mod_store.fields["search"]=="MiXeD case");
    assert(!g_mod_store.dirty&&g_mod_store.previewed);
    accepted={};frame("draft-summary",500);click_keyboard("draft-summary",500);
    accepted("New draft");assert(g_mod_store.dirty&&!g_mod_store.previewed&&g_mod_store.fields["draft-summary"]=="New draft");
    accepted={};frame("comment-compose",500);click_keyboard("comment-compose",500);
    g_mod_store.identity="account-b";accepted("Wrong account");assert(g_mod_store.fields["comment-compose"].empty());
    accepted={};frame("cancelled",500);click_keyboard("cancelled",500);accepted={};assert(g_mod_store.fields["cancelled"].empty());
    ImGui::DestroyContext();
    std::cout<<"Real shared field widget: alignment, narrow layout, correct keyboard target, case, draft flags, account ownership and cancel passed\n";
}

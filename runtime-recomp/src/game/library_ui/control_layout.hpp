#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace dkr::library_ui {
inline constexpr float ControlHeight=44.F,ControlGap=12.F;
struct ToolbarLayout {
    float search_width,status_width,sort_width,status_x,status_y,sort_x,sort_y,height;
};
inline ToolbarLayout installed_toolbar(float width,float caption) {
    width=std::max(1.F,width);
    const float row=caption+ControlHeight;
    if(width>=700.F)return {width-384.F,180.F,180.F,width-372.F,0,width-180.F,0,row};
    if(width>=372.F) {
        const float half=(width-ControlGap)*.5F;
        return {width,half,half,0,row+ControlGap,half+ControlGap,row+ControlGap,2*row+ControlGap};
    }
    return {width,width,width,0,row+ControlGap,0,2*(row+ControlGap),3*row+2*ControlGap};
}
struct TextFieldLayout {float input_width,keyboard_x,keyboard_y,height;};
inline TextFieldLayout text_field(float width,float input_height) {
    width=std::max(1.F,width);
    if(width>=200.F)return {width-ControlHeight-ControlGap,width-ControlHeight,0,std::max(input_height,ControlHeight)};
    return {width,std::max(0.F,width-ControlHeight),input_height+8.F,input_height+8.F+ControlHeight};
}
inline std::string search_key(std::string_view value) {
    std::string result(value);
    for(auto& c:result)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
inline bool vehicle_matches(std::string_view name,bool enabled,std::string_view query,int state) {
    return !(state==1&&!enabled)&&!(state==2&&enabled)&&search_key(name).find(search_key(query))!=std::string::npos;
}
inline bool vehicle_before(std::string_view left,std::string_view left_id,
        std::string_view right,std::string_view right_id,int sort) {
    const auto a=search_key(left),b=search_key(right);
    return a==b?left_id<right_id:sort==1?a>b:a<b;
}
}

// Optional native software-renderer proof of the shipped cloud tint and draw code.
// Writes backgrounds only; it does not reproduce or replace the launcher UI.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "launcher_themes/draw.hpp"
#include "launcher_themes/button_pattern_ui.hpp"
#include "backends/imgui_impl_sdlrenderer2.h"
#define STB_IMAGE_IMPLEMENTATION
#include "launcher_themes/art_sdl.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc,char** argv) {
    using namespace dkr::launcher_themes;
    assert(argc==3);
    const auto directory=std::filesystem::path(argv[2]);
    std::filesystem::create_directories(directory);
    int image_width=0,image_height=0,channels=0;
    auto* original=stbi_load(argv[1],&image_width,&image_height,&channels,4);
    assert(original && image_width>0 && image_height>0);
    const std::size_t bytes=std::size_t(image_width)*std::size_t(image_height)*4;
    const std::vector<unsigned char> source(original,original+bytes);stbi_image_free(original);
    SDL_SetMainReady();assert(SDL_Init(0)==0);
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,1280,800,32,SDL_PIXELFORMAT_RGBA32);assert(surface);
    auto* renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);
    ArtTexture art(renderer);assert(art.get());
    int atlas_width=0,atlas_height=0,atlas_channels=0;
    auto* atlas=stbi_load_from_memory(ArtPng,int(sizeof(ArtPng)),&atlas_width,&atlas_height,&atlas_channels,4);
    assert(atlas&&atlas_width==ArtWidth&&atlas_height==ArtHeight);
    for(int cell=0;cell<ArtColumns*ArtRows;++cell) {
        int visible=0;
        for(int y=0;y<ArtTile;++y)for(int x=0;x<ArtTile;++x) {
            const auto alpha=atlas[((cell/ArtColumns*ArtTile+y)*ArtWidth+cell%ArtColumns*ArtTile+x)*4+3];
            visible+=alpha>16;
            if(x==0||y==0||x==ArtTile-1||y==ArtTile-1)assert(alpha==0);
        }
        assert(cell<int(Icon::Count)?visible>100:visible==0);
    }
    stbi_image_free(atlas);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,800};
    io.Fonts->TexDesiredWidth=2048;
    const int button=io.Fonts->AddCustomRectRegular(ButtonPatternWidth,ButtonPatternHeight);
    unsigned char* font_pixels=nullptr;int fw=0,fh=0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels,&fw,&fh);
    auto* button_rect=io.Fonts->GetCustomRectByIndex(button);assert(button_rect&&button_rect->IsPacked());
    fill_button_patterns(font_pixels,fw,button_rect->X,button_rect->Y);io.Fonts->TexPixelsUseColors=true;
    assert(ImGui_ImplSDLRenderer2_Init(renderer));
    for(int i=0;i<int(Theme::Count);++i) {
        auto pixels=source;assert(tint(pixels,image_width,image_height,Theme(i)));
        auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,image_width,image_height);
        assert(texture);assert(SDL_UpdateTexture(texture,nullptr,pixels.data(),image_width*4)==0);
        ImGui_ImplSDLRenderer2_NewFrame();io.DeltaTime=1.F/60;ImGui::NewFrame();
        auto* draw=ImGui::GetBackgroundDrawList();
        draw->AddImage(reinterpret_cast<ImTextureID>(texture),{0,0},{1280,800});
        weather(draw,{0,0},{1280,800},Theme(i),12,Calendar::UK);
        corners(ImGui::GetForegroundDrawList(),{0,0},{1280,800},Theme(i),reinterpret_cast<ImTextureID>(art.get()),Calendar::US);
        displayed=Theme(i);preferences.calendar=Calendar::US;
        draw=ImGui::GetForegroundDrawList();
        draw_button_pattern(draw,{360,620},{920,690},*button_rect,10,1,12);
        draw->AddRectFilled({500,636},{780,674},IM_COL32(8,27,39,245),6);
        draw->AddText({520,647},IM_COL32(255,243,201,255),Names[i]);
        ImGui::Render();SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());SDL_RenderPresent(renderer);
        const auto output=directory/(std::to_string(i)+".png");
        assert(stbi_write_png(output.string().c_str(),1280,800,4,surface->pixels,surface->pitch)!=0);
        SDL_DestroyTexture(texture);
    }
    art.release();art.release(); // Idempotent; never leave an SDL handle after renderer teardown.
    ImGui_ImplSDLRenderer2_Shutdown();ImGui::DestroyContext();SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);SDL_Quit();std::cout<<"19 native backgrounds rendered at 1280x800\n";
}

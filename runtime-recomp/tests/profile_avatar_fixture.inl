// Exercise the production avatar draw routine with deterministic renderer and
// download-cache boundaries. No API login, GPU or network is needed.
namespace avatar_fixture {
struct SDL_Renderer {
    int id;
};
struct SDL_Texture {
    SDL_Renderer* renderer;
    bool alive = true;
};
using ImTextureID = SDL_Texture*;
struct ImVec2 {
    float x, y;
};
inline constexpr int SDL_PIXELFORMAT_RGBA32 = 1, SDL_TEXTUREACCESS_STATIC = 1, SDL_BLENDMODE_BLEND = 1;
int frame = 1, initials = 0, created = 0, requests = 0, checks = 0;
bool visible = true, create_failure = false, update_failure = false;
std::vector<std::unique_ptr<SDL_Texture>> allocations;
SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, int, int, int, int) {
    if (create_failure)
        return nullptr;
    ++created;
    allocations.push_back(std::make_unique<SDL_Texture>(SDL_Texture{r}));
    return allocations.back().get();
}
int SDL_UpdateTexture(SDL_Texture*, void*, const void*, int) {
    return update_failure ? -1 : 0;
}
int SDL_SetTextureBlendMode(SDL_Texture*, int) {
    return 0;
}
void SDL_DestroyTexture(SDL_Texture* t) {
    if (!t->alive)
        throw std::runtime_error("Avatar texture destroyed twice");
    t->alive = false;
}
namespace ImGui {
bool IsRectVisible(ImVec2, ImVec2) {
    return visible;
}
int GetFrameCount() {
    return frame;
}
} // namespace ImGui
struct ImDrawList {
    std::vector<SDL_Texture*> pictures;
    void AddImageRounded(ImTextureID t, ImVec2, ImVec2, ImVec2, ImVec2, unsigned, float) {
        if (!t->alive)
            throw std::runtime_error("Avatar drew a retired texture");
        pictures.push_back(t);
    }
    void AddCircleFilled(ImVec2, float, unsigned, int) {}
    void AddCircle(ImVec2, float, unsigned, int, float) {}
};
unsigned PaddockCol(unsigned rgb, unsigned alpha = 255) {
    return rgb | alpha << 24;
}
void DrawOlAvatar(ImDrawList*, ImVec2, float, std::string_view, bool) {
    ++initials;
}
SDL_Renderer renderer{1}, second_renderer{2};
SDL_Renderer* g_mod_store_renderer = &renderer;
std::string g_discord_avatar_url;
namespace dkr {
namespace profile {
using ::dkr::profile::avatar_url;
}
namespace store {
struct Image {
    unsigned width = 2, height = 2;
    std::vector<unsigned char> rgba = std::vector<unsigned char>(16, 255);
};
struct Service {
    std::map<std::string, std::shared_ptr<const Image>> images;
    std::shared_ptr<const Image> image(const std::string& url) {
        const auto p = images.find(url);
        return p == images.end() ? nullptr : p->second;
    }
    void request_image(const std::string&) {
        ++requests;
    }
};
Service instance;
Service& service() {
    return instance;
}
} // namespace store
} // namespace dkr
#include "online_profile/avatar_draw.inl"

void verify(bool value) {
    ++checks;
    if (!value)
        throw std::runtime_error("Avatar fixture check " + std::to_string(checks) + " failed");
}
bool run() {
    const std::string url =
        "https://cdn.discordapp.com/avatars/12345678901234567/0123456789abcdef0123456789abcdef.png?size=128";
    const auto image = std::make_shared<dkr::store::Image>();
    ImDrawList draw;
    auto render = [&](const std::string& key, bool dim = false) {
        const int before = initials;
        const auto count = draw.pictures.size();
        DrawOlProfileAvatar(&draw, {0, 0}, 48, "Racer", key, dim);
        return std::pair{initials - before, draw.pictures.size() - count};
    };
    verify(render("") == std::pair{1, std::size_t(0)});
    verify(render(url) == std::pair{1, std::size_t(0)} && requests == 1);
    dkr::store::instance.images[url] = image;
    verify(render(url) == std::pair{0, std::size_t(1)} && created == 1);
    const auto original = draw.pictures.back();
    dkr::store::instance.images.clear();
    for (unsigned i = 0; i < 80; ++i) {
        ++frame;
        verify(render(url, i % 2) == std::pair{0, std::size_t(1)} && draw.pictures.back() == original);
    }
    verify(created == 1 && requests == 1 && original->alive);
    auto transparent = std::make_shared<dkr::store::Image>();
    transparent->rgba.assign(16, 0);
    dkr::store::instance.images[url] = transparent;
    verify(render(url, true) == std::pair{0, std::size_t(1)} && draw.pictures.back() == original &&
           original->alive); // Already referenced this frame.
    ++frame;
    verify(render(url, true) == std::pair{0, std::size_t(1)} && !original->alive);
    const auto replacement = draw.pictures.back();
    dkr::store::instance.images[url] = std::make_shared<dkr::store::Image>();
    create_failure = true;
    ++frame;
    verify(render(url) == std::pair{0, std::size_t(1)} && draw.pictures.back() == replacement && replacement->alive);
    create_failure = false;
    update_failure = true;
    ++frame;
    verify(render(url) == std::pair{0, std::size_t(1)} && draw.pictures.back() == replacement && replacement->alive);
    update_failure = false;
    g_mod_store_renderer = &second_renderer;
    dkr::store::instance.images.clear();
    ++frame;
    verify(render(url) == std::pair{1, std::size_t(0)} && !replacement->alive && requests == 2);
    dkr::store::instance.images[url] = image;
    create_failure = true;
    ++frame;
    verify(render(url) == std::pair{1, std::size_t(0)} && g_profile_textures.empty());
    create_failure = false;
    ++frame;
    verify(render(url) == std::pair{0, std::size_t(1)} && draw.pictures.back()->renderer == &second_renderer);
    const auto current = draw.pictures.back();
    visible = false;
    verify(render(url) == std::pair{0, std::size_t(0)});
    visible = true;
    // No entry referenced by this frame may be evicted for another picture.
    for (unsigned i = 0; i < 47; ++i) {
        const auto key = "profile-preview:" + std::to_string(i);
        dkr::store::instance.images[key] = image;
        verify(render(key) == std::pair{0, std::size_t(1)});
    }
    dkr::store::instance.images["profile-preview:overflow"] = image;
    verify(render("profile-preview:overflow") == std::pair{1, std::size_t(0)} && g_profile_textures.size() == 48 &&
           current->alive);
    for (auto* t : draw.pictures)
        if (t == current)
            verify(t->alive);
    ++frame;
    verify(render("profile-preview:overflow") == std::pair{0, std::size_t(1)} && g_profile_textures.size() == 48);
    ReleaseDiscordProfileTexture();
    verify(g_profile_textures.empty() && !g_profile_texture_renderer);
    for (const auto& t : allocations)
        verify(!t->alive);
    std::cout << checks << " avatar draw/cache checks passed\n";
    return true;
}
} // namespace avatar_fixture

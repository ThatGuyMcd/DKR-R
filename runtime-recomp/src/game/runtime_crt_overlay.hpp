#pragma once

#include <filesystem>
#include <string>

namespace RT64 {
struct Application;
}

namespace dkr::runtime::crt {

enum class ScaleMode {
    Stretch = 0,
    Tile = 1,
};

// Draws after the game image but before the normal ImGui windows. The selected
// image is uploaded once and retained so switching filters never destroys a
// resource that an in-flight present may still reference.
bool draw(RT64::Application& application, const std::filesystem::path& image_path, ScaleMode scale_mode, float strength,
          std::string& status);

// Must be called before the RT64 inspector/device is destroyed.
void release();

} // namespace dkr::runtime::crt

// Persist immutable materials/layouts, not animated vertices. Every byte on
// which a cached texture depends (including external CI palettes) is compared
// against the NEW task-owned image before reuse. No pointer-only cache keys.
struct TextureDependency {
    std::uint32_t physical;
    std::vector<std::uint8_t> bytes;
};
struct RetainedTexture {
    std::uint32_t header = 0;
    std::uint64_t checked = 0;
    std::shared_ptr<const Texture> texture;
    std::vector<TextureDependency> inputs;
};
std::vector<RetainedTexture> retained_textures;
std::uint64_t texture_frame = 0;
std::uint32_t texture_scene = 0;
std::size_t retained_texture_bytes = 0;
std::shared_ptr<const Texture> retain_texture(const Reader& r, std::uint32_t header) {
    for (auto& entry : retained_textures)
        if (entry.header == header) {
            bool matches = true;
            if (entry.checked != texture_frame)
                for (const auto& input : entry.inputs) {
                    r.offset(input.physical, input.bytes.size());
                    if (std::memcmp(r.ram.data() + input.physical, input.bytes.data(), input.bytes.size())) {
                        matches = false;
                        break;
                    }
                }
            if (matches) {
                entry.checked = texture_frame;
                ++texture_hits;
                return entry.texture;
            }
            retained_texture_bytes -= entry.texture->bytes.size();
            entry = {};
            break;
        }
    auto tex = std::make_shared<const Texture>(netplay::experimental::capture_local_texture(ram, header));
    ++texture_captures;
    RetainedTexture entry;
    entry.header = header;
    entry.checked = texture_frame;
    entry.texture = tex;
    const auto dependency = [&](std::uint32_t address, std::size_t bytes) {
        const auto physical = r.offset(address, bytes);
        if (physical >= 0x800000U || bytes > 0x800000U - physical)
            throw std::runtime_error("material cache input escapes source snapshot");
        entry.inputs.push_back({physical, {r.ram.begin() + physical, r.ram.begin() + physical + bytes}});
    };
    const auto native_bytes = r.half(header + 0x16), count = r.half(header + 0xA);
    const auto commands = r.word(header + 0xC);
    dependency(header, native_bytes);
    dependency(commands, count * 8U);
    for (unsigned c = 0; c < count; ++c)
        if ((tex->commands[c][0] >> 24) == 0xFD && tex->commands[c][1] >= native_bytes) {
            const auto begin = tex->commands[c][1];
            std::size_t end = tex->bytes.size();
            for (const auto& words : tex->commands)
                if ((words[0] >> 24) == 0xFD && words[1] > begin)
                    end = std::min(end, std::size_t(words[1]));
            if (end <= begin)
                throw std::runtime_error("material cache palette extent");
            dependency(r.word(commands + c * 8 + 4) & 0xFFFFFFU, end - begin);
        }
    // Bounded persistent storage. A large mod still renders; only optional
    // cross-frame caching is bypassed when the retained cache is full.
    if (tex->bytes.size() <= 4U * 1024U * 1024U - retained_texture_bytes) {
        auto empty =
            std::find_if(retained_textures.begin(), retained_textures.end(), [](const auto& e) { return !e.header; });
        if (empty != retained_textures.end()) {
            retained_texture_bytes += tex->bytes.size();
            *empty = std::move(entry);
        } else if (retained_textures.size() < 256) {
            retained_texture_bytes += tex->bytes.size();
            retained_textures.push_back(std::move(entry));
        }
    }
    return tex;
}

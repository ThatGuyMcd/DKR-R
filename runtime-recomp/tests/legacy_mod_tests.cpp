#include "legacy_mod_format.hpp"
#include "legacy_mod_geometry.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_mod_dependencies.hpp"
#include "legacy_audio_bank.hpp"
#include <miniz/miniz.h>
#include <chrono>
#include <thread>
#include <iostream>
using namespace dkr::mods;
namespace {
unsigned checks = 0;
void require(bool condition) {
    ++checks;
    if (!condition)
        throw Error("Test assertion failed at check " + std::to_string(checks));
}
template <class F> void rejects(F f) {
    bool caught = false;
    try {
        f();
    } catch (const Error&) {
        caught = true;
    }
    require(caught);
}
struct Temp {
    std::filesystem::path path;
    Temp() {
        const auto root = std::filesystem::current_path();
        for (unsigned i = 0; i < 1000; ++i) {
            path =
                root / ("legacy-unit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                        "-" + std::to_string(i));
            if (std::filesystem::create_directory(path))
                return;
        }
        throw Error("Could not create test fixture directory.");
    }
    ~Temp() {
        std::error_code e;
        std::filesystem::remove_all(path, e);
    }
};
Bytes zip_bytes(const std::vector<std::pair<std::string, Bytes>>& entries) {
    mz_zip_archive zip{};
    require(mz_zip_writer_init_heap(&zip, 0, 0));
    struct Close {
        mz_zip_archive& zip;
        ~Close() {
            mz_zip_writer_end(&zip);
        }
    } close{zip};
    for (const auto& [name, data] : entries)
        require(mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), 0));
    void* memory = nullptr;
    size_t size = 0;
    require(mz_zip_writer_finalize_heap_archive(&zip, &memory, &size));
    Bytes result(static_cast<std::uint8_t*>(memory), static_cast<std::uint8_t*>(memory) + size);
    mz_free(memory);
    return result;
}
void put16(Bytes& b, unsigned pos, unsigned v) {
    b.at(pos) = v >> 8;
    b.at(pos + 1) = v;
}
void put32(Bytes& b, unsigned pos, unsigned v) {
    put16(b, pos, v >> 16);
    put16(b, pos + 2, v);
}
Bytes simple_geometry() {
    // One material-less triangle, one segment, one valid batch + sentinel.
    Bytes b(256);
    put32(b, 4, 80);
    put32(b, 8, 148);
    put32(b, 0x10, 160);
    put16(b, 0x1a, 1);
    put32(b, 0x48, 256);
    put32(b, 80, 162);
    put32(b, 84, 192);
    put32(b, 92, 208);
    put32(b, 100, 232);
    put16(b, 108, 3);
    put16(b, 110, 1);
    put16(b, 112, 1);
    b[192 + 1] = 0;
    b[192 + 2] = 1;
    b[192 + 3] = 2;
    b[208] = 255;
    put32(b, 216, 0x200); // Render-only, no edge collision construction.
    put16(b, 222, 3);
    put16(b, 224, 1);
    return b;
}
void archive_tests() {
    Temp temporary;
    unsigned id = 0;
    auto read = [&](const Bytes& bytes) {
        const auto path = temporary.path / (std::to_string(++id) + ".zip");
        write_new_file(path, bytes);
        return read_patch_inputs(path);
    };
    const Bytes patch{0xd6, 0xc3, 0xc4, 0, 0};
    auto result = read(zip_bytes({{"author/course.XDELTA", patch}, {"readme.txt", {'h', 'i'}}}));
    require(result.size() == 1 && result[0].data == patch);
    require(read(zip_bytes({{"a.xdelta", patch}, {"b.xdelta", patch}})).size() == 1);
    for (const std::string name :
         {"../bad.xdelta", "a/../bad.xdelta", "/bad.xdelta", "C:\\bad.xdelta", "a:stream.xdelta", "CON.xdelta",
          "a./bad.xdelta", "a /bad.xdelta", "a//bad.xdelta", "a/./bad.xdelta", "a\\..\\bad.xdelta"})
        rejects([&] { read(zip_bytes({{name, patch}})); });
    rejects([&] { read(zip_bytes({{"A.xdelta", patch}, {"a.xdelta", patch}})); });
    rejects([&] { read(zip_bytes({{"instructions.txt", patch}})); });
    rejects([&] { read(zip_bytes({{"nested.zip", zip_bytes({{"a.xdelta", patch}})}})); });
    auto corrupt = zip_bytes({{"valid.xdelta", patch}});
    // Stored ZIP local header: payload begins after its name and extra fields.
    auto le16 = [&](unsigned p) { return corrupt[p] | (unsigned(corrupt[p + 1]) << 8); };
    corrupt[30 + le16(26) + le16(28)] ^= 1;
    rejects([&] { read(corrupt); });
    auto nul = zip_bytes({{"abc.xdelta", patch}});
    for (unsigned p = 0; p + 10 <= nul.size(); ++p)
        if (std::string_view(reinterpret_cast<const char*>(nul.data() + p), 10) == "abc.xdelta")
            nul[p + 1] = 0;
    rejects([&] { read(nul); });
    auto link = zip_bytes({{"link.xdelta", patch}});
    for (unsigned p = 0; p + 46 < link.size(); ++p)
        if (link[p] == 0x50 && link[p + 1] == 0x4b && link[p + 2] == 1 && link[p + 3] == 2) {
            // Central directory external UNIX mode: symlink.
            link[p + 40] = 0xff;
            link[p + 41] = 0xa1;
        }
    rejects([&] { read(link); });
    const auto existing = temporary.path / "untouched";
    write_new_file(existing, patch);
    rejects([&] { write_new_file(existing, {}); });
    require(read_file(existing, 64) == patch);
    rejects([&] { read_file(existing, 2); });
}
void geometry_tests() {
    const auto valid = simple_geometry();
    const auto summary = validate_geometry(valid, 24);
    require(summary.segments == 1 && summary.triangles == 1 && summary.constructed_bytes == 288);
    for (unsigned size = 0; size < valid.size(); ++size)
        if (size < 240)
            rejects([&] { validate_geometry(View(valid).first(size), 24); });
    auto mutated = valid;
    put32(mutated, 4, 0xfffffff0);
    rejects([&] { validate_geometry(mutated, 24); });
    mutated = valid;
    put16(mutated, 0x1a, 129);
    rejects([&] { validate_geometry(mutated, 24); });
    mutated = valid;
    mutated[193] = 3;
    rejects([&] { validate_geometry(mutated, 24); });
    mutated = valid;
    put16(mutated, 224, 2);
    rejects([&] { validate_geometry(mutated, 24); });
    mutated = valid;
    put32(mutated, 0x48, 0x82a00);
    rejects([&] { validate_geometry(mutated, 24); });
    mutated = valid;
    put32(mutated, 0x28, 0x7fc00000);
    rejects([&] { validate_geometry(mutated, 24); });
    rejects([&] { validate_geometry(valid, 0x82a00); });
}
Bytes packed_asset(View decoded) {
    Bytes output(decoded.size() * 2 + 128);
    const auto size = decoded.size();
    for (unsigned i = 0; i < 4; ++i)
        output[i] = static_cast<std::uint8_t>(size >> (8 * i));
    output[4] = 6;
    mz_stream stream{};
    require(mz_deflateInit2(&stream, 6, MZ_DEFLATED, -15, 9, MZ_DEFAULT_STRATEGY) == MZ_OK);
    stream.next_in = decoded.data();
    stream.avail_in = static_cast<unsigned>(decoded.size());
    stream.next_out = output.data() + 5;
    stream.avail_out = static_cast<unsigned>(output.size() - 5);
    const int result = mz_deflate(&stream, MZ_FINISH);
    const auto written = stream.total_out;
    mz_deflateEnd(&stream);
    require(result == MZ_STREAM_END);
    output.resize(5 + written);
    return output;
}
Bytes simple_model() {
    Bytes data(168);
    put32(data, 4, 96);
    put32(data, 8, 128);
    put32(data, 0x38, 144);
    put16(data, 0x24, 3);
    put16(data, 0x26, 1);
    put16(data, 0x28, 1);
    put32(data, 0x2c, 168);
    data[129] = 0;
    data[130] = 1;
    data[131] = 2;
    data[144] = 255;
    put16(data, 158, 3);
    put16(data, 160, 1);
    return data;
}
void dependency_tests() {
    Bytes sprite(16);
    put16(sprite, 0, 20);
    put16(sprite, 2, 3);
    sprite[12] = 0;
    sprite[13] = 1;
    sprite[14] = 1;
    sprite[15] = 2; // A valid invisible middle frame.
    require(inspect_sprite_textures(sprite) == std::vector<unsigned>({20, 21}));
    auto bad = sprite;
    bad[15] = 0;
    rejects([&] { inspect_sprite_textures(bad); });
    bad = sprite;
    put16(bad, 2, 500);
    rejects([&] { inspect_sprite_textures(bad); });
    bad = sprite;
    put16(bad, 0, 32767);
    rejects([&] { inspect_sprite_textures(bad); });
    bad = sprite;
    bad.resize(513);
    rejects([&] { inspect_sprite_textures(bad); });
    Bytes texture(40);
    texture[0] = texture[1] = 2;
    texture[2] = 1;
    texture[0x12] = 1;
    validate_texture_record(texture);
    require(true); // Static zero-stride texture.
    bad = texture;
    bad[2] = 7;
    rejects([&] { validate_texture_record(bad); });
    bad = texture;
    bad[0] = 0;
    rejects([&] { validate_texture_record(bad); });
    bad = texture;
    bad[0x12] = 2;
    rejects([&] { validate_texture_record(bad); });
    auto animated = texture;
    put16(animated, 0x16, 40);
    animated[0x12] = 2;
    animated.insert(animated.end(), texture.begin(), texture.end());
    put16(animated, 40 + 0x16, 40);
    validate_texture_record(animated);
    require(true);
    auto compressed = Bytes(texture.begin(), texture.begin() + 32);
    compressed[0x1d] = 1;
    const auto payload = packed_asset(texture);
    compressed.insert(compressed.end(), payload.begin(), payload.end());
    // Highly compressible payload fits the compressed-input tail allocation.
    validate_texture_record(compressed);
    require(true);
    const auto model = simple_model();
    require(inspect_model_textures(packed_asset(model)).empty());
    bad = model;
    bad[131] = 3;
    rejects([&] { inspect_model_textures(packed_asset(bad)); });
    bad = model;
    put32(bad, 4, 0xfffffffe);
    rejects([&] { inspect_model_textures(packed_asset(bad)); });
    bad = model;
    put16(bad, 160, 2);
    rejects([&] { inspect_model_textures(packed_asset(bad)); });
    bad = model;
    put32(bad, 0x2c, 100);
    rejects([&] { inspect_model_textures(packed_asset(bad)); });
    bad = model;
    put16(bad, 0x4a, 1);
    put32(bad, 0x4c, 96);
    put16(bad, 96, 1);
    rejects([&] { inspect_model_textures(packed_asset(bad)); });
    for (std::size_t size = 0; size < texture.size(); ++size)
        rejects([&] { validate_texture_record(View(texture).first(size)); });
    for (std::size_t size = 0; size < model.size(); ++size)
        rejects([&] { inspect_model_textures(packed_asset(View(model).first(size))); });
}
void process_tests(const std::filesystem::path& executable) {
    Temp temporary;
    require(run_worker(executable, temporary.path / "exit-ok", {}).outcome == WorkerOutcome::Completed);
    require(run_worker(executable, temporary.path / "exit-failure", {}).outcome == WorkerOutcome::Failed);
    require(run_worker(executable, temporary.path / "sleep", {}, std::chrono::milliseconds(100)).outcome ==
            WorkerOutcome::TimedOut);
    unsigned polls = 0;
    require(run_worker(executable, temporary.path / "sleep", [&] { return ++polls < 4; }).outcome ==
            WorkerOutcome::Cancelled);
    require(run_worker(executable, temporary.path / "sleep", [] { return false; }).outcome == WorkerOutcome::Cancelled);
    rejects([&] { run_worker(temporary.path / "absent-program", temporary.path / "request", {}); });
}
void audio_tests() {
    constexpr unsigned length = 80;
    Bytes bank(12 + length);
    put16(bank, 0, 0x5331);
    put16(bank, 2, 1);
    put32(bank, 4, 12);
    put32(bank, 8, length);
    put32(bank, 12, 68);
    put32(bank, 12 + 64, 480);
    // A real compact-MIDI note, its duration, then end-of-track. A silent
    // placeholder is no longer valid as replacement music after safety checks.
    const std::uint8_t events[]{0, 0x90, 60, 100, 60, 60, 0xff, 0x2f};
    std::copy(std::begin(events), std::end(events), bank.begin() + 12 + 68);
    const auto inspected = inspect_sequence_directory(bank);
    require(inspected.records.size() == 1 && inspected.maximum_loaded_length == length);
    require(build_sequence_bank(bank, {}, length) == bank);
    auto replacement = Bytes(bank.begin() + 12, bank.end());
    replacement[68] = 1;
    const auto replaced = build_sequence_bank(bank, {{0, replacement}}, length);
    require(inspect_sequence_directory(replaced).records[0].digest != inspected.records[0].digest);
    rejects([&] { build_sequence_bank(bank, {{1, replacement}}, length); });
    rejects([&] { build_sequence_bank(bank, {{0, replacement}}, length - 1); });
    auto silent = replacement;
    silent[68] = 0;
    silent[69] = 0xff;
    silent[70] = 0x2f;
    rejects([&] { build_sequence_bank(bank, {{0, silent}}, length); });
    auto odd = replacement;
    odd.push_back(0);
    rejects([&] { build_sequence_bank(bank, {{0, odd}}, length + 1); });
    auto bad = bank;
    put16(bad, 0, 0);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put16(bad, 2, 257);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put32(bad, 4, 8);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put32(bad, 4, 0xfffffff0);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put32(bad, 8, 0xffffffff);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put32(bad, 12, length);
    rejects([&] { inspect_sequence_directory(bad); });
    bad = bank;
    put32(bad, 12 + 64, 0);
    rejects([&] { inspect_sequence_directory(bad); });
    for (std::size_t size = 0; size < bank.size(); ++size)
        rejects([&] { inspect_sequence_directory(View(bank).first(size)); });
    // Same one-instrument bank stored at different offsets. Pointer values,
    // alignment padding and unreferenced sample bytes have no musical meaning.
    const auto control = [](unsigned shift) {
        Bytes c(120 + shift);
        put16(c, 0, 0x4231);
        put16(c, 2, 1);
        put32(c, 4, 8 + shift);
        put16(c, 8 + shift, 1);
        put32(c, 12 + shift, 32000);
        put32(c, 20 + shift, 24 + shift);
        put16(c, 38 + shift, 1);
        put32(c, 40 + shift, 44 + shift);
        put32(c, 44 + shift, 60 + shift);
        put32(c, 48 + shift, 76 + shift);
        put32(c, 52 + shift, 84 + shift);
        c[56 + shift] = 64;
        c[57 + shift] = 127;
        put32(c, 84 + shift, shift);
        put32(c, 88 + shift, 16);
        c[92 + shift] = 1;
        return c;
    };
    const auto c = control(0), moved = control(16);
    const Bytes sample(16, 42);
    Bytes moved_sample(16, 99);
    moved_sample.insert(moved_sample.end(), sample.begin(), sample.end());
    require(equivalent_music_banks(c, sample, moved, moved_sample));
    auto changed = moved_sample;
    changed[16] ^= 1;
    require(!equivalent_music_banks(c, sample, moved, changed));
    auto invalid = moved;
    invalid[16 + 76] ^= 1;
    require(!equivalent_music_banks(c, sample, invalid, moved_sample));
    invalid = moved;
    put32(invalid, 16 + 84, 0xfffffff0);
    rejects([&] { equivalent_music_banks(c, sample, invalid, moved_sample); });
    invalid = moved;
    put32(invalid, 16 + 52, 0);
    rejects([&] { equivalent_music_banks(c, sample, invalid, moved_sample); });
    invalid = moved;
    invalid[16 + 58] = 1;
    rejects([&] { equivalent_music_banks(c, sample, invalid, moved_sample); });
    invalid = moved;
    put16(invalid, 16 + 38, 1025);
    rejects([&] { equivalent_music_banks(c, sample, invalid, moved_sample); });
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2) {
        const auto instruction = std::filesystem::path(argv[1]).filename().string();
        if (instruction == "exit-ok")
            return 0;
        if (instruction == "exit-failure")
            return 9;
        if (instruction == "sleep") {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            return 0;
        }
        return 2;
    }
    try {
        require(sha256({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        const Bytes abc{'a', 'b', 'c'};
        require(sha256(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        std::vector<std::int16_t> pcm(9001);
        Bytes pcm_bytes;
        for (unsigned i = 0; i < pcm.size(); ++i) {
            pcm[i] = static_cast<std::int16_t>(i * 73);
            const auto sample = static_cast<std::uint16_t>(pcm[i]);
            pcm_bytes.push_back(static_cast<std::uint8_t>(sample));
            pcm_bytes.push_back(static_cast<std::uint8_t>(sample >> 8));
        }
        require(sha256_pcm_le(pcm) == sha256(pcm_bytes));
        require(sha256_pcm_le({}) == sha256({}));
        rejects([&] { slice(abc, 4, 0); });
        const std::string unicode_path = "author-\xc3\xa9/mod (1).xdelta";
        const auto converted = utf8_path(unicode_path).u8string();
        require(std::string(converted.begin(), converted.end()) == unicode_path);
        rejects([&] { utf8_path(std::string("bad\0path", 8)); });
        rejects([&] { slice(abc, 2, ~std::size_t{}); });
        rejects([&] { be32(abc, 0); });
        rejects([&] { verified_revision(abc); });
        rejects([&] { inflate_asset(abc, 1024); });
        rejects([&] { decode_patch(abc, abc); });
        Bytes image(4096);
        image[0] = 0x37;
        image[1] = 0x80;
        image[2] = 0x40;
        image[3] = 0x12;
        canonicalize_rom(image);
        require(be32(image, 0) == 0x80371240);
        image[0] = 0x40;
        image[1] = 0x12;
        image[2] = 0x37;
        image[3] = 0x80;
        canonicalize_rom(image);
        require(be32(image, 0) == 0x80371240);
        rejects([&] { AssetImage image(abc, "us.v77"); });
        rejects([&] { AssetImage image(abc, "unknown"); });
        archive_tests();
        geometry_tests();
        dependency_tests();
        audio_tests();
        process_tests(std::filesystem::absolute(argv[0]));
        std::cout << checks << " legacy format checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#include "virtual_pak.hpp"

#include "librecomp/helpers.hpp"
#include "recomp.h"
#include "ultramodern/ultra64.h"
#include "runtime_platform.hpp"
#include "virtual_pak_policy.hpp"
#include "save_storage.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kPakSize = 32U * 1024U;
constexpr std::uint32_t kPageSize = 256U;
constexpr std::uint32_t kDataStart = 5U * kPageSize;
constexpr std::uint32_t kDataCapacity = kPakSize - kDataStart;
constexpr std::uint32_t kDirectoryOffset = kPageSize;
constexpr std::uint32_t kDirectoryEntrySize = 64U;
constexpr std::uint32_t kMaximumFiles = 16U;
constexpr std::uint32_t kFormatVersion = 1U;
constexpr std::array<std::uint8_t, 8> kMagic{'D', 'K', 'R', 'M', 'P', 'K', '1', 0};

constexpr std::int32_t kPfsOk = 0;
constexpr std::int32_t kPfsNoPak = 1;
constexpr std::int32_t kPfsInvalid = 5;
constexpr std::int32_t kPfsBadData = 6;
constexpr std::int32_t kPfsDataFull = 7;
constexpr std::int32_t kPfsDirFull = 8;
constexpr std::int32_t kPfsExists = 9;

struct PakFile {
    bool used = false;
    std::uint16_t company_code = 0;
    std::uint32_t game_code = 0;
    std::array<std::uint8_t, 4> extension{};
    std::array<std::uint8_t, 16> name{};
    std::vector<std::uint8_t> data;
};

struct VirtualPak {
    std::mutex mutex;
    std::shared_ptr<dkr::runtime::saves::storage::Lease> storage_lease;
    bool loaded = false;
    bool corrupt = false;
    std::uint32_t generation = 0;
    std::array<PakFile, kMaximumFiles> files{};
};

std::filesystem::path g_config_directory;
std::filesystem::path g_session_directory;
std::array<VirtualPak, 4> g_paks;
std::atomic<bool> g_enabled{true};
std::atomic<bool> g_self_test_allow_all{false};

bool PortEnabled(int channel) {
    if (!g_enabled.load(std::memory_order_acquire) || channel < 0 || channel >= 4) {
        return false;
    }
    if (g_self_test_allow_all.load(std::memory_order_acquire) || channel == 0) {
        return true;
    }
    const auto status = dkr::runtime::platform::player_controller_status(
        static_cast<std::size_t>(channel));
    return status.assigned && status.connected;
}

std::uint8_t ConnectedPortMask() {
    std::array<bool, 4> assigned{};
    std::array<bool, 4> connected{};
    for (std::size_t channel = 1; channel < 4; ++channel) {
        const auto status = dkr::runtime::platform::player_controller_status(channel);
        assigned[channel] = status.assigned;
        connected[channel] = status.connected;
    }
    return dkr::runtime::pak::policy::connected_port_mask(
        g_enabled.load(std::memory_order_acquire), assigned, connected);
}

std::uint32_t AlignToPage(std::uint32_t value) {
    return (value + kPageSize - 1U) & ~(kPageSize - 1U);
}

std::filesystem::path PakPath(int channel) {
    return (g_session_directory.empty()?g_config_directory:g_session_directory) / ("controller-pak-" + std::to_string(channel + 1) + ".mpk");
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset + 0U]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

void WriteU32(std::vector<std::uint8_t>& bytes, std::size_t offset,
              std::uint32_t value) {
    bytes[offset + 0U] = static_cast<std::uint8_t>(value);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t Checksum(std::vector<std::uint8_t> bytes) {
    std::fill(bytes.begin() + 16, bytes.begin() + 20, 0U);
    std::uint32_t hash = 2166136261U;
    for (std::uint8_t value : bytes) {
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

std::uint32_t UsedBytes(const VirtualPak& pak) {
    std::uint32_t used = 0;
    for (const PakFile& file : pak.files) {
        if (file.used) {
            used += AlignToPage(static_cast<std::uint32_t>(file.data.size()));
        }
    }
    return used;
}

bool DecodePak(const std::filesystem::path& path, VirtualPak& pak) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.size() != kPakSize ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        ReadU32(bytes, 8U) != kFormatVersion ||
        ReadU32(bytes, 16U) != Checksum(bytes)) {
        return false;
    }

    std::array<PakFile, kMaximumFiles> decoded{};
    std::uint32_t total = 0;
    for (std::uint32_t index = 0; index < kMaximumFiles; ++index) {
        const std::size_t entry = kDirectoryOffset + index * kDirectoryEntrySize;
        if (ReadU32(bytes, entry) == 0U) {
            continue;
        }
        PakFile& file = decoded[index];
        file.used = true;
        file.company_code = static_cast<std::uint16_t>(ReadU32(bytes, entry + 4U));
        file.game_code = ReadU32(bytes, entry + 8U);
        const std::uint32_t size = ReadU32(bytes, entry + 12U);
        const std::uint32_t offset = ReadU32(bytes, entry + 16U);
        if (size == 0U || offset < kDataStart || offset > kPakSize ||
            size > kPakSize - offset || AlignToPage(size) > kDataCapacity - total) {
            return false;
        }
        std::copy_n(bytes.begin() + entry + 20U, file.extension.size(),
                    file.extension.begin());
        std::copy_n(bytes.begin() + entry + 24U, file.name.size(), file.name.begin());
        file.data.assign(bytes.begin() + offset, bytes.begin() + offset + size);
        total += AlignToPage(size);
    }

    pak.files = std::move(decoded);
    pak.generation = ReadU32(bytes, 12U);
    pak.corrupt = false;
    pak.loaded = true;
    return true;
}

bool ReadPakImage(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    VirtualPak decoded;
    return dkr::runtime::saves::storage::read_exact(path, kPakSize, bytes) && DecodePak(path, decoded);
}

bool SavePakLocked(int channel, VirtualPak& pak) {
    std::vector<std::uint8_t> bytes(kPakSize, 0U);
    std::copy(kMagic.begin(), kMagic.end(), bytes.begin());
    WriteU32(bytes, 8U, kFormatVersion);
    WriteU32(bytes, 12U, ++pak.generation);

    std::uint32_t data_offset = kDataStart;
    std::uint32_t count = 0;
    for (std::uint32_t index = 0; index < kMaximumFiles; ++index) {
        const PakFile& file = pak.files[index];
        if (!file.used) {
            continue;
        }
        const std::uint32_t size = static_cast<std::uint32_t>(file.data.size());
        const std::uint32_t reserved = AlignToPage(size);
        if (size == 0U || data_offset > kPakSize || reserved > kPakSize - data_offset) {
            return false;
        }
        const std::size_t entry = kDirectoryOffset + index * kDirectoryEntrySize;
        WriteU32(bytes, entry, 1U);
        WriteU32(bytes, entry + 4U, file.company_code);
        WriteU32(bytes, entry + 8U, file.game_code);
        WriteU32(bytes, entry + 12U, size);
        WriteU32(bytes, entry + 16U, data_offset);
        std::copy(file.extension.begin(), file.extension.end(), bytes.begin() + entry + 20U);
        std::copy(file.name.begin(), file.name.end(), bytes.begin() + entry + 24U);
        std::copy(file.data.begin(), file.data.end(), bytes.begin() + data_offset);
        data_offset += reserved;
        ++count;
    }
    WriteU32(bytes, 20U, count);
    WriteU32(bytes, 16U, Checksum(bytes));

    std::string error;
    if (!dkr::runtime::saves::storage::write_atomic(PakPath(channel), bytes, ReadPakImage, error)) {
        std::fprintf(stderr, "[save][pak] %s\n", error.c_str()); return false;
    }
    pak.loaded = true;
    pak.corrupt = false;
    return true;
}

std::int32_t EnsureLoaded(int channel, VirtualPak*& result) {
    if (!PortEnabled(channel)) {
        return kPfsNoPak;
    }
    VirtualPak& pak = g_paks[static_cast<std::size_t>(channel)];
    result = &pak;
    std::scoped_lock lock(pak.mutex);
    if (!pak.storage_lease) {
        std::string error;
        pak.storage_lease = dkr::runtime::saves::storage::acquire(PakPath(channel), error);
        if (!pak.storage_lease) {
            std::fprintf(stderr, "[save][pak] %s\n", error.c_str()); return kPfsBadData;
        }
    }
    if (pak.loaded) {
        return pak.corrupt ? kPfsBadData : kPfsOk;
    }
    const std::filesystem::path path = PakPath(channel);
    if (!std::filesystem::exists(path)) {
        pak.loaded = true;
        pak.corrupt = false;
        return SavePakLocked(channel, pak) ? kPfsOk : kPfsBadData;
    }
    if (DecodePak(path, pak)) {
        return kPfsOk;
    }
    if (DecodePak(path.string() + ".bak", pak)) {
        std::fprintf(stderr, "[boot][pak] recovered controller pak %d from backup\n",
                     channel + 1);
        // Do not overwrite the known-good backup with the corrupt primary
        // while promoting the recovered in-memory generation.
        // The atomic writer preserves an invalid original instead of deleting it.
        return SavePakLocked(channel, pak) ? kPfsOk : kPfsBadData;
    }
    pak.loaded = true;
    pak.corrupt = true;
    return kPfsBadData;
}

gpr Arg(std::uint8_t* rdram, recomp_context* context, int index) {
    if (index < 4) {
        return (&context->r4)[index];
    }
    return MEM_W(index * 4, context->r29);
}

int ChannelFromPfs(std::uint8_t* rdram, recomp_context* context) {
    const gpr pfs = Arg(rdram, context, 0);
    return static_cast<int>(MEM_W(8, pfs));
}

template <std::size_t Size>
std::array<std::uint8_t, Size> ReadBytes(std::uint8_t* rdram, gpr address) {
    std::array<std::uint8_t, Size> value{};
    for (std::size_t index = 0; index < Size; ++index) {
        value[index] = static_cast<std::uint8_t>(MEM_BU(index, address));
    }
    return value;
}

template <std::size_t Size>
void WriteBytes(std::uint8_t* rdram, gpr address,
                const std::array<std::uint8_t, Size>& value) {
    for (std::size_t index = 0; index < Size; ++index) {
        MEM_B(index, address) = value[index];
    }
}

void InitialisePfs(std::uint8_t* rdram, recomp_context* context, int channel) {
    const gpr pfs = Arg(rdram, context, 1);
    MEM_W(0, pfs) = 1; // PFS_INITIALIZED
    MEM_W(4, pfs) = static_cast<std::uint32_t>(Arg(rdram, context, 0));
    MEM_W(8, pfs) = channel;
    MEM_W(76, pfs) = 0x0200;
    MEM_W(80, pfs) = kMaximumFiles;
    MEM_B(102, pfs) = 0;
    MEM_B(103, pfs) = 1;
}

bool MetadataMatches(const PakFile& file, std::uint16_t company,
                     std::uint32_t game,
                     const std::array<std::uint8_t, 16>& name,
                     const std::array<std::uint8_t, 4>& extension) {
    return file.used && file.company_code == company && file.game_code == game &&
           file.name == name && file.extension == extension;
}

} // namespace

void dkr::runtime::pak::configure(const std::filesystem::path& config_directory) {
    g_config_directory = config_directory;
}

void dkr::runtime::pak::begin_session_directory(const std::filesystem::path& directory) {
    if(directory==g_session_directory)return;
    for(auto& pak:g_paks) {
        std::lock_guard lock(pak.mutex);
        pak.loaded=false;pak.corrupt=false;pak.generation=0;pak.files={};
        pak.storage_lease.reset();
    }
    g_session_directory=directory;
}

bool dkr::runtime::pak::enabled() {
    return g_enabled.load(std::memory_order_acquire);
}

void dkr::runtime::pak::set_enabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_release);
}

bool dkr::runtime::pak::self_test(const std::filesystem::path& directory,
                                  std::string& error) {
    configure(directory);
    set_enabled(true);
    struct TestPortGuard {
        TestPortGuard() { g_self_test_allow_all.store(true, std::memory_order_release); }
        ~TestPortGuard() { g_self_test_allow_all.store(false, std::memory_order_release); }
    } test_port_guard;
    for (int channel = 0; channel < 4; ++channel) {
        VirtualPak& pak = g_paks[static_cast<std::size_t>(channel)];
        {
            std::scoped_lock lock(pak.mutex);
            pak.files = {};
            pak.loaded = true;
            pak.corrupt = false;
            pak.generation = 0;
            if (!SavePakLocked(channel, pak)) {
                error = "could not create Player " +
                    std::to_string(channel + 1) + "'s empty virtual Pak";
                return false;
            }
            PakFile& file = pak.files[0];
            file.used = true;
            file.company_code = 0x3459;
            file.game_code = 0x4E445945;
            file.extension = {0, 0, 0, static_cast<std::uint8_t>(channel + 1)};
            file.name[0] = 'D';
            file.name[1] = 'K';
            file.name[2] = 'R';
            file.name[3] = static_cast<std::uint8_t>('1' + channel);
            file.data.resize(1024U);
            for (std::size_t index = 0; index < file.data.size(); ++index) {
                file.data[index] = static_cast<std::uint8_t>(
                    (index * 37U + static_cast<std::size_t>(channel)) & 0xFFU);
            }
            if (!SavePakLocked(channel, pak)) {
                error = "could not persist Player " +
                    std::to_string(channel + 1) + "'s test data";
                return false;
            }
        }
        VirtualPak decoded;
        std::error_code size_error;
        if (std::filesystem::file_size(PakPath(channel), size_error) != kPakSize ||
            size_error || !DecodePak(PakPath(channel), decoded) ||
            !decoded.files[0].used || decoded.files[0].data.size() != 1024U ||
            decoded.files[0].data[0] != static_cast<std::uint8_t>(channel)) {
            error = "Player " + std::to_string(channel + 1) +
                "'s 32 KiB round-trip validation failed";
            return false;
        }
    }

    // Create a third generation on Player 4, damage only the live file, and
    // prove that the previous complete generation is recovered atomically.
    constexpr int channel = 3;
    VirtualPak& pak = g_paks[static_cast<std::size_t>(channel)];
    {
        std::scoped_lock lock(pak.mutex);
        pak.files[0].data[0] ^= 0x5AU;
        if (!SavePakLocked(channel, pak)) {
            error = "could not create Player 4's recovery generation";
            return false;
        }
    }
    const std::filesystem::path path = PakPath(channel);
    {
        std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!output) {
            error = "could not open the test Pak for corruption recovery";
            return false;
        }
        output.seekp(128);
        const char damaged = static_cast<char>(0xA5);
        output.write(&damaged, 1);
    }
    {
        std::scoped_lock lock(pak.mutex);
        pak.loaded = false;
        pak.corrupt = false;
        pak.files = {};
    }
    VirtualPak* recovered = nullptr;
    if (EnsureLoaded(channel, recovered) != kPfsOk || recovered == nullptr) {
        error = "backup recovery rejected a valid previous generation";
        return false;
    }
    {
        std::scoped_lock lock(recovered->mutex);
        if (!recovered->files[0].used || recovered->files[0].data.size() != 1024U ||
            recovered->files[0].data[0] != static_cast<std::uint8_t>(channel)) {
            error = "backup recovery returned the wrong generation";
            return false;
        }
    }
    // Exercise the same between-session namespace switch used by offline
    // custom mods. Cached files must never leak from one profile to another.
    const auto mod_directory=directory/"mod-pak-test";
    struct RestorePakRoute {~RestorePakRoute(){dkr::runtime::pak::begin_session_directory({});}} restore_route;
    begin_session_directory(mod_directory);
    for(int port=0;port<4;++port) {
        VirtualPak* isolated=nullptr;
        if(EnsureLoaded(port,isolated)!=kPfsOk || !isolated) {error="isolated Pak could not be opened";return false;}
        std::lock_guard lock(isolated->mutex);
        if(std::any_of(isolated->files.begin(),isolated->files.end(),[](const auto& f){return f.used;})) {
            error="a normal Pak leaked into the mod namespace";return false;
        }
        isolated->files[0].used=true;isolated->files[0].data=std::vector<std::uint8_t>(256,0xA9);
        if(!SavePakLocked(port,*isolated)){error="isolated Pak write failed";return false;}
    }
    begin_session_directory({});
    for(int port=0;port<4;++port) {
        VirtualPak* original=nullptr;
        if(EnsureLoaded(port,original)!=kPfsOk || !original){error="normal Pak restore failed";return false;}
        std::lock_guard lock(original->mutex);
        if(!original->files[0].used || original->files[0].data.size()!=1024 || original->files[0].data[0]!=port) {
            error="modded Pak data replaced the normal Pak";return false;
        }
    }
    begin_session_directory(mod_directory);
    for(int port=0;port<4;++port) {
        VirtualPak* isolated=nullptr;
        if(EnsureLoaded(port,isolated)!=kPfsOk || !isolated){error="modded Pak reload failed";return false;}
        std::lock_guard lock(isolated->mutex);
        if(!isolated->files[0].used || isolated->files[0].data.size()!=256 || isolated->files[0].data[0]!=0xA9) {
            error="modded Pak did not retain its separate progress";return false;
        }
    }
    error.clear();
    return true;
}

extern "C" void osPfsIsPlug_recomp(std::uint8_t* rdram, recomp_context* context) {
    MEM_B(0, context->r5) = ConnectedPortMask();
    context->r2 = kPfsOk;
}

extern "C" int dkr_virtual_pak_preferred_status(std::uint8_t*,
                                                  recomp_context* context) {
    const int channel = static_cast<int>(context->r4);
    if (!PortEnabled(channel)) {
        return dkr::runtime::pak::policy::kUseRetailAccessoryProbe;
    }
    VirtualPak* pak = nullptr;
    const std::int32_t status = EnsureLoaded(channel, pak);
    return dkr::runtime::pak::policy::preferred_memory_pak_status(
        dkr::runtime::pak::enabled(), true,
        status == kPfsOk && pak != nullptr);
}

extern "C" void osPfsInitPak_recomp(std::uint8_t* rdram, recomp_context* context) {
    const int channel = static_cast<int>(context->r6);
    VirtualPak* pak = nullptr;
    const std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        InitialisePfs(rdram, context, channel);
    }
    context->r2 = status;
}

extern "C" void osPfsInit_recomp(std::uint8_t* rdram, recomp_context* context) {
    osPfsInitPak_recomp(rdram, context);
}

extern "C" void osPfsFreeBlocks_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        std::scoped_lock lock(pak->mutex);
        const gpr output = Arg(rdram, context, 1);
        MEM_W(0, output) = kDataCapacity - UsedBytes(*pak);
    }
    context->r2 = status;
}

extern "C" void osPfsNumFiles_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        std::scoped_lock lock(pak->mutex);
        const std::uint32_t used = static_cast<std::uint32_t>(std::count_if(
            pak->files.begin(), pak->files.end(), [](const PakFile& file) { return file.used; }));
        MEM_W(0, Arg(rdram, context, 1)) = kMaximumFiles;
        MEM_W(0, Arg(rdram, context, 2)) = used;
    }
    context->r2 = status;
}

extern "C" void osPfsFindFile_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        const auto name = ReadBytes<16>(rdram, Arg(rdram, context, 3));
        const auto extension = ReadBytes<4>(rdram, Arg(rdram, context, 4));
        const std::uint16_t company = static_cast<std::uint16_t>(Arg(rdram, context, 1));
        const std::uint32_t game = static_cast<std::uint32_t>(Arg(rdram, context, 2));
        status = kPfsInvalid;
        std::scoped_lock lock(pak->mutex);
        for (std::uint32_t index = 0; index < kMaximumFiles; ++index) {
            if (MetadataMatches(pak->files[index], company, game, name, extension)) {
                MEM_W(0, Arg(rdram, context, 5)) = index;
                status = kPfsOk;
                break;
            }
        }
    }
    context->r2 = status;
}

extern "C" void osPfsAllocateFile_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        const auto name = ReadBytes<16>(rdram, Arg(rdram, context, 3));
        const auto extension = ReadBytes<4>(rdram, Arg(rdram, context, 4));
        const std::uint16_t company = static_cast<std::uint16_t>(Arg(rdram, context, 1));
        const std::uint32_t game = static_cast<std::uint32_t>(Arg(rdram, context, 2));
        const std::int32_t requested = static_cast<std::int32_t>(Arg(rdram, context, 5));
        if (requested <= 0) {
            status = kPfsInvalid;
        } else {
            std::scoped_lock lock(pak->mutex);
            auto existing = std::find_if(pak->files.begin(), pak->files.end(),
                [&](const PakFile& file) {
                    return MetadataMatches(file, company, game, name, extension);
                });
            auto free_entry = std::find_if(pak->files.begin(), pak->files.end(),
                [](const PakFile& file) { return !file.used; });
            if (existing != pak->files.end()) {
                status = kPfsExists;
            } else if (free_entry == pak->files.end()) {
                status = kPfsDirFull;
            } else if (AlignToPage(static_cast<std::uint32_t>(requested)) >
                       kDataCapacity - UsedBytes(*pak)) {
                status = kPfsDataFull;
            } else {
                free_entry->used = true;
                free_entry->company_code = company;
                free_entry->game_code = game;
                free_entry->name = name;
                free_entry->extension = extension;
                free_entry->data.assign(static_cast<std::size_t>(requested), 0U);
                const std::uint32_t index = static_cast<std::uint32_t>(
                    std::distance(pak->files.begin(), free_entry));
                MEM_W(0, Arg(rdram, context, 6)) = index;
                status = SavePakLocked(channel, *pak) ? kPfsOk : kPfsBadData;
            }
        }
    }
    context->r2 = status;
}

extern "C" void osPfsDeleteFile_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    if (status == kPfsOk) {
        const auto name = ReadBytes<16>(rdram, Arg(rdram, context, 3));
        const auto extension = ReadBytes<4>(rdram, Arg(rdram, context, 4));
        const std::uint16_t company = static_cast<std::uint16_t>(Arg(rdram, context, 1));
        const std::uint32_t game = static_cast<std::uint32_t>(Arg(rdram, context, 2));
        status = kPfsInvalid;
        std::scoped_lock lock(pak->mutex);
        for (PakFile& file : pak->files) {
            if (MetadataMatches(file, company, game, name, extension)) {
                file = {};
                status = SavePakLocked(channel, *pak) ? kPfsOk : kPfsBadData;
                break;
            }
        }
    }
    context->r2 = status;
}

extern "C" void osPfsFileState_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    const std::int32_t index = static_cast<std::int32_t>(Arg(rdram, context, 1));
    if (status == kPfsOk) {
        std::scoped_lock lock(pak->mutex);
        if (index < 0 || index >= static_cast<std::int32_t>(kMaximumFiles) ||
            !pak->files[static_cast<std::size_t>(index)].used) {
            status = kPfsInvalid;
        } else {
            const PakFile& file = pak->files[static_cast<std::size_t>(index)];
            const gpr output = Arg(rdram, context, 2);
            MEM_W(0, output) = static_cast<std::uint32_t>(file.data.size());
            MEM_W(4, output) = file.game_code;
            MEM_H(8, output) = file.company_code;
            WriteBytes(rdram, output + 10, file.extension);
            WriteBytes(rdram, output + 14, file.name);
        }
    }
    context->r2 = status;
}

extern "C" void osPfsReadWriteFile_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    const int channel = ChannelFromPfs(rdram, context);
    std::int32_t status = EnsureLoaded(channel, pak);
    const std::int32_t index = static_cast<std::int32_t>(Arg(rdram, context, 1));
    const std::uint32_t mode = static_cast<std::uint32_t>(Arg(rdram, context, 2));
    const std::int32_t offset = static_cast<std::int32_t>(Arg(rdram, context, 3));
    const std::int32_t size = static_cast<std::int32_t>(Arg(rdram, context, 4));
    const gpr buffer = Arg(rdram, context, 5);
    if (status == kPfsOk) {
        std::scoped_lock lock(pak->mutex);
        if (index < 0 || index >= static_cast<std::int32_t>(kMaximumFiles) ||
            !pak->files[static_cast<std::size_t>(index)].used || offset < 0 || size < 0) {
            status = kPfsInvalid;
        } else {
            PakFile& file = pak->files[static_cast<std::size_t>(index)];
            if (static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(size) >
                file.data.size()) {
                status = kPfsInvalid;
            } else if (mode == 0U) {
                for (std::int32_t byte = 0; byte < size; ++byte) {
                    MEM_B(byte, buffer) = file.data[static_cast<std::size_t>(offset + byte)];
                }
            } else if (mode == 1U) {
                for (std::int32_t byte = 0; byte < size; ++byte) {
                    file.data[static_cast<std::size_t>(offset + byte)] =
                        static_cast<std::uint8_t>(MEM_BU(byte, buffer));
                }
                status = SavePakLocked(channel, *pak) ? kPfsOk : kPfsBadData;
            } else {
                status = kPfsInvalid;
            }
        }
    }
    context->r2 = status;
}

extern "C" void osPfsChecker_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    context->r2 = EnsureLoaded(ChannelFromPfs(rdram, context), pak);
}

extern "C" void osPfsRepairId_recomp(std::uint8_t* rdram, recomp_context* context) {
    VirtualPak* pak = nullptr;
    context->r2 = EnsureLoaded(ChannelFromPfs(rdram, context), pak);
}

extern "C" int dkr_virtual_pak_reformat(std::uint8_t* rdram, recomp_context* context) {
    const int channel = static_cast<int>(Arg(rdram, context, 2));
    if (!dkr::runtime::pak::enabled() || channel < 0 || channel >= 4) {
        return kPfsNoPak;
    }
    VirtualPak& pak = g_paks[static_cast<std::size_t>(channel)];
    std::scoped_lock lock(pak.mutex);
    pak.files = {};
    pak.loaded = true;
    pak.corrupt = false;
    const bool saved = SavePakLocked(channel, pak);
    if (saved) {
        const gpr pfs = Arg(rdram, context, 0);
        MEM_W(0, pfs) = 1;
        MEM_W(8, pfs) = channel;
    }
    return saved ? kPfsOk : kPfsBadData;
}

extern "C" void __osPfsSelectBank_recomp(std::uint8_t*, recomp_context* context) {
    context->r2 = dkr::runtime::pak::enabled() ? kPfsOk : kPfsNoPak;
}

extern "C" void __osContRamWrite_recomp(std::uint8_t*, recomp_context* context) {
    context->r2 = dkr::runtime::pak::enabled() ? kPfsOk : kPfsNoPak;
}

extern "C" void __osPfsGetStatus_recomp(std::uint8_t*, recomp_context* context) {
    context->r2 = dkr::runtime::pak::enabled() ? kPfsOk : kPfsNoPak;
}

extern "C" void __osGetId_recomp(std::uint8_t*, recomp_context* context) {
    context->r2 = dkr::runtime::pak::enabled() ? kPfsOk : kPfsNoPak;
}

extern "C" void __osContRamRead_recomp(std::uint8_t*, recomp_context* context) {
    context->r2 = dkr::runtime::pak::enabled() ? kPfsOk : kPfsNoPak;
}

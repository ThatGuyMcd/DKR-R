// Executes unedited, freshly generated DKR functions through the experimental
// Patch Pipeline. Only OS/scheduler endpoints are test doubles; this is not a
// full game/audio-renderer integration or an online certification.
#include "legacy_runtime_io.hpp"
#include "legacy_runtime_assets.hpp"
#include "legacy_resident_assets.hpp"
#include "legacy_audio_bank.hpp"
#include "legacy_cache_namespace.hpp"
#include "legacy_mod_dependencies.hpp"
#include <algorithm>
#include <deque>
#include <iostream>
#include <tuple>
#include <utility>

extern "C" void __amDMA(std::uint8_t*, recomp_context*);
extern "C" void tex_free(std::uint8_t*, recomp_context*);
extern "C" void dmacopy(std::uint8_t*, recomp_context*);
extern "C" void mempool_free(std::uint8_t*, recomp_context*);
extern "C" void mempool_alloc_safe(std::uint8_t*, recomp_context*);
extern "C" void asset_table_load(std::uint8_t*, recomp_context*);
extern "C" void asset_load(std::uint8_t*, recomp_context*);
extern "C" void asset_rom_offset(std::uint8_t*, recomp_context*);
extern "C" void asset_table_size(std::uint8_t*, recomp_context*);
extern "C" void asset_table_load_addr(std::uint8_t*, recomp_context*);
extern "C" void asset_table_load_zipped(std::uint8_t*, recomp_context*);
extern "C" void alSeqFileNew(std::uint8_t*, recomp_context*);
#if DKR_TEST_REVISION == 77
extern "C" void dmacopy(std::uint8_t*, recomp_context*);
constexpr auto copy_entry = dmacopy;
constexpr std::uint32_t AssetQueue = 0x80124220, AudioQueue = 0x80119af0;
constexpr std::uint32_t AudioState = 0x80119230, AudioFrame = 0x800dc680, NextDma = 0x800dc684;
constexpr std::uint32_t TextureCount = 0x80126330, TexturePointer = 0x80126328;
constexpr std::uint32_t AssetLookupPointer = 0x80124290, AssetLookupRom = 969568;
#else
extern "C" void dmacopy_v1(std::uint8_t*, recomp_context*);
constexpr auto copy_entry = dmacopy_v1;
constexpr std::uint32_t AssetQueue = 0x801247a0, AudioQueue = 0x8011a070;
constexpr std::uint32_t AudioState = 0x801197b0, AudioFrame = 0x800dcbf0, NextDma = 0x800dcbf4;
constexpr std::uint32_t TextureCount = 0x801268d0, TexturePointer = 0x801268c8;
constexpr std::uint32_t AssetLookupPointer = 0x80124830, AssetLookupRom = 970976;
#endif

namespace {
using namespace dkr::mods;
Bytes memory(0x800000), rom;
AssetBus* active_bus = nullptr;
std::shared_ptr<const AssetBus::Mount> active_mount, switch_on_allocate;
unsigned mutex_depth = 0, mutex_acquires = 0, mutex_releases = 0, allocations = 0;
bool allocation_failure = false, copy_failure = false;
std::deque<std::uint32_t> pending;
unsigned original_calls = 0, bridge_calls = 0, completed = 0, receives = 0;
std::vector<std::uint32_t> freed;
void require(bool value, const char* reason) {
    if (!value)
        throw Error(reason);
}
gpr ptr(std::uint32_t value) {
    return static_cast<gpr>(static_cast<std::int32_t>(value));
}
void put(std::uint32_t address, std::uint32_t value) {
    auto* rdram = memory.data();
    MEM_W(0, ptr(address)) = value;
}
std::uint32_t get(std::uint32_t address) {
    auto* rdram = memory.data();
    return MEM_W(0, ptr(address));
}
Bytes bytes(std::uint32_t address, std::size_t length) {
    Bytes out(length);
    const auto offset = address & 0x1fffffff;
    for (std::size_t i = 0; i < length; ++i)
        out[i] = memory[(offset + i) ^ 3];
    return out;
}
void put_bytes(std::uint32_t address, View data) {
    const auto offset = address & 0x1fffffff;
    require(offset <= memory.size() && data.size() <= memory.size() - offset, "Fixture write out of range");
    for (std::size_t i = 0; i < data.size(); ++i)
        memory[(offset + i) ^ 3] = data[i];
}
void enqueue(void*, std::uint32_t queue) {
    pending.push_back(queue);
    ++completed;
    // A production dispatcher can wake another guest reader immediately.
    // This lookup also verifies that the new bridge retained no bus mutex.
    if (active_bus)
        require(!active_bus->resolve(0x1000, 1), "Bus locked/fell through incorrectly");
}
void stock_pi(std::uint8_t* rdram, recomp_context* ctx) {
    ++original_calls;
    require(rdram == memory.data(), "Original PI received the wrong RDRAM pointer");
    require(ctx->r6 == 0, "Read direction changed at the original PI boundary");
    const auto offset = static_cast<std::uint32_t>(ctx->r7) & 0x0fffffff;
    const auto dest = static_cast<std::uint32_t>(MEM_W(0x10, ctx->r29)) & 0x1fffffff;
    const auto len = static_cast<std::uint32_t>(MEM_W(0x14, ctx->r29));
    require(offset <= rom.size() && len <= rom.size() - offset, "Stock test read out of range");
    require(dest <= memory.size() && len <= memory.size() - dest, "Stock test write out of range");
    for (std::size_t i = 0; i < len; ++i)
        memory[(dest + i) ^ 3] = rom[offset + i];
    enqueue(nullptr, MEM_W(0x18, ctx->r29));
    ctx->r2 = 0;
}
void queue(std::uint32_t address, std::uint32_t buffer, unsigned capacity) {
    for (unsigned i = 0; i < 0x18; i += 4)
        put(address + i, 0);
    put(address + 0x10, capacity);
    put(address + 0x14, buffer);
}
recomp_context context() {
    recomp_context ctx{};
    ctx.r29 = ptr(0x80700000);
    ctx.r31 = ptr(0x80001234);
    ctx.r16 = 0x121;
    ctx.r17 = 0x122;
    ctx.r18 = 0x123;
    ctx.r19 = 0x124;
    ctx.r20 = 0x125;
    ctx.r21 = 0x126;
    ctx.r22 = 0x127;
    return ctx;
}
void serialized_copy(std::uint8_t* rdram, recomp_context* ctx) {
    if (copy_failure)
        throw Error("Injected DMA failure");
    dmacopy(rdram, ctx);
}
void asset_api_test(const std::shared_ptr<const AssetBank>& stock, AssetBus& bus) {
    const auto directory = AssetDirectory::build(stock);
    const auto mount = bus.mount(directory);
    // Retail startup's resident cartridge lookup table, copied with guest byte order.
    put(AssetLookupPointer, 0x802a0000);
    for (unsigned i = 0; i < 208; i += 4)
        put(0x802a0000 + i, be32(rom, AssetLookupRom + i));
    for (bool mounted : {false, true}) {
        active_mount = mounted ? mount : nullptr;
        for (unsigned section = 0; section < 50; ++section) {
            const auto offset = directory->section_size(section) >= 2 ? 2U : 0U;
            auto ctx = context();
            ctx.r4 = section;
            ctx.r5 = offset;
            asset_rom_offset(memory.data(), &ctx);
            const auto expected = mounted ? mount->address(section, offset)
                                          : AssetLookupRom + 208 + be32(rom, AssetLookupRom + 4 + section * 4) + offset;
            require(static_cast<std::uint32_t>(ctx.r2) == expected,
                    "Asset address does not match its immutable section");
            ctx = context();
            ctx.r4 = section;
            asset_table_size(memory.data(), &ctx);
            require(ctx.r2 == directory->section_size(section), "Asset size and address use different owners");
        }
        for (const auto entry : {asset_table_load, asset_table_load_addr, asset_load}) {
            auto ctx = context();
            ctx.r4 = 22;
            ctx.r5 = ptr(0x80380000);
            ctx.r6 = 2;
            ctx.r7 = 14;
            const auto before = allocations, pi = original_calls;
            entry(memory.data(), &ctx);
            const auto expected =
                entry == asset_load ? directory->read(22, 2, 14) : directory->read(22, 0, directory->section_size(22));
            require(ctx.r29 == ptr(0x80700000) && ctx.r31 == ptr(0x80001234),
                    "Asset API corrupted caller stack/return");
            require(bytes(0x80380000, expected.size()) == expected, "Asset API copied an incoherent table or slice");
            require(allocations - before == (entry == asset_table_load ? 1U : 0U),
                    "Asset API allocation ownership changed");
            require(entry == asset_table_load ? ctx.r2 == ptr(0x80380000) : ctx.r2 == expected.size(),
                    "Asset API return differs from retail ABI");
            require(mounted ? original_calls == pi : original_calls > pi, "Asset API used the wrong PI route");
            require(!mutex_depth && pending.empty(), "Asset API left a held mutex or unconsumed DMA");
        }
    }
    // Compressed-map scratch destinations from the reported legacy failures.
    // The mounted bridge must preserve exact bytes without rounding the
    // address, consuming adjacent canaries, or bypassing the retail DMA loop.
    active_mount = mount;
    for (const auto [destination, offset, length] : {std::array<std::uint32_t, 3>{2150333300U, 48768U, 708U},
                                                     std::array<std::uint32_t, 3>{2150437172U, 6960U, 156U}}) {
        put_bytes(destination - 4, Bytes(length + 8, 0xcc));
        auto transfer = context();
        transfer.r4 = 21;
        transfer.r5 = ptr(destination);
        transfer.r6 = offset;
        transfer.r7 = length;
        asset_load(memory.data(), &transfer);
        require(bytes(destination, length) == directory->read(21, offset, length),
                "Word-aligned compressed object-map load changed bytes");
        require(bytes(destination - 4, 4) == Bytes(4, 0xcc) && bytes(destination + length, 4) == Bytes(4, 0xcc),
                "Object-map load wrote outside its exact range");
        require(pending.empty() && !mutex_depth, "Object-map load lost its completion/mutex");
    }
    // Modern packed models are not padded to an even source offset. Mirror
    // the installed Moo Moo Farm (46405 bytes) followed by Koopa Troopa Beach:
    // the second record begins odd and crosses multiple retail DMA chunks.
    const auto model_count = stock->record_count(27);
    const auto courses =
        AssetBank::append_courses(stock, {{{27, static_cast<unsigned>(model_count)}, Bytes(46405, 0x31)},
                                          {{27, static_cast<unsigned>(model_count + 1)}, Bytes(86051, 0x72)}});
    const auto course_directory = AssetDirectory::build(courses);
    active_mount = bus.mount(course_directory);
    const auto model_table = course_directory->read(26, 0, course_directory->section_size(26));
    const auto odd_source = be32(model_table, (model_count + 1) * 4);
    require((odd_source & 1) != 0, "Synthetic appended model did not exercise odd source alignment");
    for (unsigned lane = 0; lane < 4; ++lane) {
        constexpr unsigned length = 0x5003;
        const auto destination = 0x80380000U + lane;
        put_bytes(destination - 4, Bytes(length + 8, 0xcc));
        auto transfer = context();
        transfer.r4 = 27;
        transfer.r5 = ptr(destination);
        transfer.r6 = odd_source;
        transfer.r7 = length;
        const auto completions = completed, stock_calls = original_calls;
        asset_load(memory.data(), &transfer);
        require(bytes(destination, length) == Bytes(length, 0x72), "Odd-source chunked asset load changed model bytes");
        require(bytes(destination - 4, 4) == Bytes(4, 0xcc) && bytes(destination + length, 4) == Bytes(4, 0xcc),
                "Odd-source load changed adjacent bytes");
        require(completed == completions + 2 && original_calls == stock_calls && pending.empty() && !mutex_depth,
                "Odd-source load lost a chunk completion or entered real ROM DMA");
    }
    const auto override = AssetBank::derive(stock, std::string(64, 'f'), {{{27, 5}, Bytes(0x6020, 0x71)}});
    const auto other = bus.mount(AssetDirectory::build(override));
    active_mount = other;
    switch_on_allocate = mount;
    auto ctx = context();
    ctx.r4 = 26;
    asset_table_load(memory.data(), &ctx);
    require(active_mount == mount && bytes(0x80380000, other->directory()->section_size(26)) ==
                                         other->directory()->read(26, 0, other->directory()->section_size(26)),
            "An in-flight allocation switched the table's immutable owner");
    auto invalid = context();
    invalid.r4 = 27;
    invalid.r5 = ptr(0x80380000);
    invalid.r6 = directory->section_size(27) - 2;
    invalid.r7 = 8;
    const auto before_memory = memory;
    const auto pi = bridge_calls;
    bool rejected = false;
    try {
        asset_load(memory.data(), &invalid);
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected && memory == before_memory && bridge_calls == pi, "Invalid asset slice partly wrote guest memory");
    allocation_failure = true;
    ctx = context();
    ctx.r4 = 22;
    asset_table_load(memory.data(), &ctx);
    allocation_failure = false;
    require(!ctx.r2 && bridge_calls == pi, "Failed allocation still started DMA");
    copy_failure = true;
    ctx = context();
    ctx.r4 = 22;
    const auto free_count = freed.size();
    rejected = false;
    try {
        asset_table_load(memory.data(), &ctx);
    } catch (const Error&) {
        rejected = true;
    }
    copy_failure = false;
    require(rejected && freed.size() == free_count + 1 && freed.back() == 0x80380000,
            "Failed owned load leaked its allocation");
    ctx = context();
    ctx.r4 = 22;
    rejected = false;
    try {
        asset_table_load_zipped(memory.data(), &ctx);
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected, "An uncertified whole-section inflater silently used stock data");
    active_mount.reset();
    freed.clear();
    require(mutex_acquires == mutex_releases && !mutex_depth, "The v80 serialized path lost mutex balance");
    std::cout
        << "Generated asset API size/address/full/slice loads, stock fallback, immutable capture and failure bounds passed.\n";
}
void resident_test(const std::shared_ptr<const AssetBank>& stock, AssetBus& bus) {
    const auto original = ResidentBank::prepare(stock, stock, bus);
    const auto layout = resident_asset_layout(stock->revision());
    for (unsigned i = 0; i < 8; ++i) {
        const auto address = 0x80400000 + i * 0x10000;
        put(layout.table_pointers[i], address);
        put_bytes(address, original->tables()[i]);
    }
    for (const auto& cache : layout.caches) {
        put(cache.count_address, 0);
        put(cache.pointer_address, 0);
    }
    // Independently exercise retail's sequence relocation, rather than using
    // the new producer to manufacture its own expected stock state.
    const auto sequences = stock->record(39, 5);
    const auto count = be16(sequences, 2);
    const auto audio_offsets = stock->stock_section(38);
    put_bytes(0x80460000, sequences.first(4 + count * 8));
    auto seqctx = context();
    seqctx.r4 = ptr(0x80460000);
    seqctx.r5 = stock->stock_section_rom_offset(39) + be32(audio_offsets, 16);
    alSeqFileNew(memory.data(), &seqctx);
    require(bytes(0x80460000, 4 + count * 8) == original->tables()[6],
            "Stock sequence pointers differ from retail relocation");
    ResidentAssetState state(original);
    AssetBank::Overrides changes;
    // Synthetic size changes make stale resident offset use observable. These
    // records are not decoded/rendered and are not certified playable assets.
    for (const unsigned section : {2U, 4U, 12U, 29U, 32U}) {
        unsigned id = 0;
        while (stock->record(section, id).empty())
            ++id;
        const auto old = stock->record(section, id);
        Bytes longer(old.begin(), old.end());
        longer.resize(longer.size() + 8, 0);
        changes.emplace(AssetKey{section, id}, std::move(longer));
    }
    const auto song_directory = inspect_sequence_directory(sequences);
    const auto& record = song_directory.records.at(10);
    const auto old_song = sequences.subspan(record.offset, record.length);
    Bytes song(old_song.begin(), old_song.end());
    song.back() ^= 1;
    changes.emplace(AssetKey{39, 5},
                    build_sequence_bank(sequences, {{10, std::move(song)}}, song_directory.maximum_loaded_length));
    const auto changed_bank = AssetBank::derive(stock, std::string(64, '9'), std::move(changes));
    const auto changed = ResidentBank::prepare(stock, changed_bank, bus);
    auto plan = state.prepare(memory, changed);
    const auto original_memory = memory;
    auto other_guest = memory;
    require(state.commit(other_guest, std::move(plan)) == ResidentAssetState::Commit::Stale &&
                other_guest == original_memory,
            "Resident plan crossed into another guest's RDRAM");
    auto lease = state.acquire();
    require(!lease.route() && state.commit(memory, std::move(plan)) == ResidentAssetState::Commit::Busy &&
                memory == original_memory,
            "Resident transition published underneath an active asset read");
    lease = {};
    require(state.commit(memory, std::move(plan)) == ResidentAssetState::Commit::Published,
            "Resident table publication failed");
    require(state.commit(memory, std::move(plan)) == ResidentAssetState::Commit::Stale,
            "Resident transaction replay succeeded");
    for (unsigned i = 0; i < 8; ++i)
        require(bytes(0x80400000 + i * 0x10000, changed->tables()[i].size()) == changed->tables()[i],
                "Resident publication mixed asset owners");
    auto active = state.acquire();
    require(active.route() == changed->route(), "Resident tables and active route disagree");
    active = {};
    // Music's offset-minus-section-base convention must still select the song
    // from the same bank as the retained sequence-length table.
    for (unsigned id = 0; id < count; ++id) {
        const auto address = get(0x80460004 + id * 8), length = get(0x80470000 + id * 4);
        const auto read = bus.resolve(address, length);
        require(bool(read), "Relocated song missed its mounted owner");
        const auto offset = address - changed->route()->address(39);
        require(read->bytes() == changed->route()->directory()->read(39, offset, length),
                "Song address/size pair selects different bytes");
    }
    auto stale = state.prepare(memory, original);
    put(0x80400000, get(0x80400000) ^ 1);
    const auto edited = memory;
    require(state.commit(memory, std::move(stale)) == ResidentAssetState::Commit::Stale && memory == edited,
            "Stale resident plan partly changed state");
    put_bytes(0x80400000, changed->tables()[0]);
    auto cancelled = state.prepare(memory, original);
    state.cancel();
    const auto before_cancel = memory;
    require(state.commit(memory, std::move(cancelled)) == ResidentAssetState::Commit::Stale && memory == before_cancel,
            "Cancelled resident plan published");
    auto restore = state.prepare(memory, original);
    require(state.commit(memory, std::move(restore)) == ResidentAssetState::Commit::Published,
            "Resident stock restoration failed");
    require(memory == original_memory && !state.acquire().route(),
            "Restoring stock did not restore all original resident bytes and routing");
    // A forged resident pointer must not alias a cache/global or adjacent table.
    const auto saved = get(layout.table_pointers[1]);
    put(layout.table_pointers[1], get(layout.table_pointers[0]));
    bool rejected = false;
    try {
        state.prepare(memory, changed);
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected, "Overlapping resident tables were accepted");
    put(layout.table_pointers[1], saved);
    std::cout
        << "Resident lookup/music transaction, original relocation, borrower drain, stale/cancel/replay and exact stock restoration passed.\n";
}
void copy(std::uint32_t address, View expected, unsigned stock_count) {
    const auto before = original_calls, done = completed, received = receives, bridges = bridge_calls;
    auto ctx = context();
    ctx.r4 = ptr(address);
    ctx.r5 = ptr(0x80300000);
    ctx.r6 = expected.size();
    copy_entry(memory.data(), &ctx);
    const auto chunks = (expected.size() + 0x4fff) / 0x5000;
    require(original_calls - before == stock_count, "Wrong stock/virtual routing");
    require(completed - done == chunks && receives - received == chunks && bridge_calls - bridges == chunks,
            "PI completion/receive count differs from the actual DMA chunks");
    require(pending.empty(), "DMA left a duplicate completion");
    require(ctx.r29 == ptr(0x80700000) && ctx.r31 == ptr(0x80001234), "Caller SP/RA corrupted");
    require(ctx.r16 == 0x121 && ctx.r17 == 0x122 && ctx.r18 == 0x123 && ctx.r19 == 0x124 && ctx.r20 == 0x125 &&
                ctx.r21 == 0x126 && ctx.r22 == 0x127,
            "Saved registers corrupted");
    const auto actual = bytes(0x80300000, expected.size());
    require(std::equal(actual.begin(), actual.end(), expected.begin(), expected.end()),
            "Generated DMA copied wrong bytes");
}
std::uint32_t audio(std::uint32_t address, unsigned length) {
    auto ctx = context();
    ctx.r4 = ptr(address);
    ctx.r5 = length;
    ctx.r6 = ptr(AudioState);
    __amDMA(memory.data(), &ctx);
    require(ctx.r29 == ptr(0x80700000) && ctx.r31 == ptr(0x80001234) && ctx.r16 == 0x121,
            "Generated audio DMA did not restore the caller");
    return static_cast<std::uint32_t>(ctx.r2);
}
void cache_test(const std::shared_ptr<const AssetBank>& stock, AssetBus& bus) {
    unsigned sprite = 0, model = 0;
    std::vector<unsigned> sprite_textures, model_textures;
    for (; sprite < stock->record_count(12); ++sprite) {
        sprite_textures = inspect_sprite_textures(stock->record(12, sprite));
        if (!sprite_textures.empty())
            break;
    }
    for (; model < stock->record_count(29); ++model) {
        model_textures = inspect_model_textures(stock->record(29, model));
        if (!model_textures.empty())
            break;
    }
    require(!sprite_textures.empty() && !model_textures.empty(), "No cache dependency fixtures found");
    const auto texture2 = sprite_textures.front(), texture3 = model_textures.front();
    // Synthetic fingerprints only; these edited bytes are never loaded by a
    // texture decoder or presented as a playable/custom asset.
    AssetBank::Overrides overrides;
    // Independent decomp asset-section mapping: sprites -> 4 (2D), models -> 2 (3D).
    for (const auto key : {AssetKey{4, texture2}, AssetKey{2, texture3}}) {
        const auto original = stock->record(key.first, key.second);
        Bytes changed(original.begin(), original.end());
        changed.back() ^= 1;
        overrides.emplace(key, std::move(changed));
    }
    const auto a_bank = AssetBank::derive(stock, std::string(64, 'a'), overrides);
    for (auto& [key, data] : overrides)
        data.back() ^= 2;
    const auto b_bank = AssetBank::derive(stock, std::string(64, 'b'), std::move(overrides));
    auto content = [&](const auto& bank) {
        return std::make_shared<CacheContent>(bank, bus.mount(AssetDirectory::build(bank)));
    };
    const auto original = content(stock), a = content(a_bank), b = content(b_bank);
    const auto single3d = stock->record(2, texture3);
    Bytes changed3d(single3d.begin(), single3d.end());
    changed3d.back() ^= 1;
    const auto only3d =
        content(AssetBank::derive(stock, std::string(64, 'd'), {{{2, texture3}, std::move(changed3d)}}));
    require(original->identity(CacheKind::Model, model) != only3d->identity(CacheKind::Model, model) &&
                original->identity(CacheKind::Sprite, sprite) == only3d->identity(CacheKind::Sprite, sprite),
            "3D texture changes did not stay in the model/3D cache namespace");
    const auto single2d = stock->record(4, texture2);
    Bytes changed2d(single2d.begin(), single2d.end());
    changed2d.back() ^= 1;
    const auto only2d =
        content(AssetBank::derive(stock, std::string(64, 'e'), {{{4, texture2}, std::move(changed2d)}}));
    require(original->identity(CacheKind::Sprite, sprite) != only2d->identity(CacheKind::Sprite, sprite) &&
                original->identity(CacheKind::Model, model) == only2d->identity(CacheKind::Model, model),
            "2D texture changes did not stay in the sprite/2D cache namespace");
    require(original->identity(CacheKind::Model, model) != a->identity(CacheKind::Model, model),
            "An unchanged model concealed its changed child texture");
    require(original->identity(CacheKind::Sprite, sprite) != a->identity(CacheKind::Sprite, sprite),
            "An unchanged sprite concealed its changed child texture");
    const std::array tables{GuestCacheTable{CacheKind::Texture, TextureCount, TexturePointer},
                            GuestCacheTable{CacheKind::Sprite, 0x80290000, 0x80290004},
                            GuestCacheTable{CacheKind::Model, 0x80290008, 0x8029000c}};
    put(TextureCount, 2);
    put(TexturePointer, 0x80400000);
    put(0x80290000, 1);
    put(0x80290004, 0x80401000);
    put(0x80290008, 1);
    put(0x8029000c, 0x80402000);
    put(0x80400000, texture3 | 0x8000);
    put(0x80400004, 0x80500000);
    put(0x80400008, 0xffffffff);
    put(0x8040000c, 0xffffffff);
    put(0x80401000, sprite);
    put(0x80401004, 0x80501000);
    put(0x80402000, model);
    put(0x80402004, 0x80502000);
    memory[(0x500000 + 5) ^ 3] = 2;
    CacheNamespace space;
    auto to_a = space.prepare(memory, tables, original, a);
    auto other_guest = memory;
    require(!space.apply(other_guest, std::move(to_a)) && other_guest == memory,
            "Cache plan crossed into another guest's RDRAM");
    require(get(0x80400000) == (texture3 | 0x8000), "Preparing a namespace already modified RDRAM");
    require(space.apply(memory, std::move(to_a)), "First cache transition failed");
    require(!space.apply(memory, std::move(to_a)), "Replaying an applied cache plan succeeded");
    require(get(0x80400000) == CacheNamespace::Hidden && get(0x80401000) == CacheNamespace::Hidden &&
                get(0x80402000) == CacheNamespace::Hidden,
            "Changed dependencies reused stock cache IDs");
    require(get(0x80400004) == 0x80500000 && memory[(0x500000 + 5) ^ 3] == 2 && freed.empty(),
            "Retagging damaged a retained pointer/refcount");
    // Model a new retail texture load using its ordinary ID in an empty slot.
    put(0x80400008, texture3 | 0x8000);
    put(0x8040000c, 0x80503000);
    memory[(0x503000 + 5) ^ 3] = 2;
    auto stale = space.prepare(memory, tables, a, b);
    put(TextureCount, 3);
    const auto after_external_change = memory;
    require(!space.apply(memory, std::move(stale)) && memory == after_external_change,
            "Stale cache transaction partially changed the guest");
    put(TextureCount, 2);
    auto to_b = space.prepare(memory, tables, a, b);
    require(space.apply(memory, std::move(to_b)), "Second cache transition failed");
    require(get(0x80400000) == CacheNamespace::Hidden && get(0x80400008) == (CacheNamespace::Hidden | 1),
            "Different custom/stock texture owners collided");
    auto to_stock = space.prepare(memory, tables, b, original);
    require(space.apply(memory, std::move(to_stock)), "Stock cache restoration failed");
    require(get(0x80400000) == (texture3 | 0x8000) && get(0x80401000) == sprite && get(0x80402000) == model &&
                get(0x80400008) == (CacheNamespace::Hidden | 1),
            "Cache restoration used the wrong owner");
    // Run the actual regenerated retail free function, not an imitation of its
    // ID/pointer/refcount logic. Only mempool_free itself is a recording double.
    auto free_ctx = context();
    free_ctx.r4 = ptr(0x80503000);
    tex_free(memory.data(), &free_ctx);
    require(freed.empty() && memory[(0x503000 + 5) ^ 3] == 1 && get(0x80400008) == (CacheNamespace::Hidden | 1),
            "Retained hidden texture freed while still referenced");
    free_ctx = context();
    free_ctx.r4 = ptr(0x80503000);
    tex_free(memory.data(), &free_ctx);
    require(freed == std::vector<std::uint32_t>{0x80503000} && get(0x80400008) == 0xffffffff &&
                get(0x8040000c) == 0xffffffff,
            "Retail pointer-based free could not retire a namespaced texture");
    auto cleanup = space.prepare(memory, tables, original, original);
    require(space.apply(memory, std::move(cleanup)), "Freed cache metadata was not retired");
    auto forged = memory;
    put(0x80400008, CacheNamespace::Hidden | 1);
    put(0x8040000c, 0x80503000);
    bool rejected = false;
    try {
        space.prepare(memory, tables, original, a);
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected, "An unowned hidden cache entry was silently adopted");
    memory = std::move(forged);
    std::cout
        << "Resident texture/sprite/model IDs, dependent identities, stale plans, pointer preservation and retail texture-free checks passed.\n";
}
} // namespace

extern "C" void osInvalDCache_recomp(std::uint8_t*, recomp_context*) {}
extern "C" void osVirtualToPhysical_recomp(std::uint8_t*, recomp_context* ctx) {
    ctx->r2 = ctx->r4 & 0x1fffffff;
}
extern "C" void mempool_free(std::uint8_t*, recomp_context* ctx) {
    freed.push_back(static_cast<std::uint32_t>(ctx->r4));
}
extern "C" void mempool_alloc_safe(std::uint8_t*, recomp_context* ctx) {
    require(ctx->r4 <= 0x80000 && static_cast<std::uint32_t>(ctx->r5) == 0x7f7f7fff, "Unexpected guest allocation/tag");
    ++allocations;
    ctx->r2 = allocation_failure ? 0 : ptr(0x80380000);
    if (switch_on_allocate)
        active_mount = std::exchange(switch_on_allocate, {});
}
extern "C" void byteswap32(std::uint8_t*, recomp_context*) {
    throw dkr::mods::Error("Unqualified whole-section swap entered");
}
extern "C" void gzip_inflate(std::uint8_t*, recomp_context*) {
    throw dkr::mods::Error("Unqualified whole-section inflate entered");
}
extern "C" void dkr_v11_asset_mutex_acquire(std::uint8_t*, recomp_context* ctx) {
    require(!mutex_depth, "Re-entered the retail v80 DMA mutex");
    ++mutex_depth;
    ++mutex_acquires;
    ctx->r2 = 0;
}
extern "C" void dkr_v11_asset_mutex_release(std::uint8_t*, recomp_context* ctx) {
    require(mutex_depth == 1, "Released an unowned v80 DMA mutex");
    --mutex_depth;
    ++mutex_releases;
    ctx->r2 = 0;
}
extern "C" int dkr_legacy_asset_api(std::uint8_t* rdram, recomp_context* ctx, unsigned operation) {
    require(rdram == memory.data(), "Asset API received the wrong memory");
    return dkr::mods::dispatch_asset_api(static_cast<dkr::mods::AssetOperation>(operation), active_mount, memory, *ctx,
                                         {mempool_alloc_safe, mempool_free, serialized_copy});
}
extern "C" void osRecvMesg_recomp(std::uint8_t* rdram, recomp_context* ctx) {
    require(!pending.empty() && pending.front() == static_cast<std::uint32_t>(ctx->r4),
            "DKR would wait forever: missing/wrong DMA completion queue");
    require(ctx->r6 == 1, "Asset DMA unexpectedly stopped using the blocking receive");
    pending.pop_front();
    ++receives;
    if (ctx->r5)
        MEM_W(0, ctx->r5) = 0;
    ctx->r2 = 0;
}
extern "C" void dkr_legacy_pi_start_dma(std::uint8_t* rdram, recomp_context* ctx) {
    require(rdram == memory.data(), "Bridge received the wrong memory");
    ++bridge_calls;
    dkr::mods::dispatch_pi_dma(active_bus, memory, *ctx, stock_pi, {nullptr, enqueue});
}

int main(int argc, char** argv) try {
    require(argc == 2, "Supply the private owned ROM for this revision");
    rom = read_file(utf8_path(argv[1]), MaxImage);
    const auto source_hash = sha256(rom);
    const auto stock = AssetBank::stock(rom);
    require(stock->revision() == "us.v" + std::to_string(DKR_TEST_REVISION), "Wrong pipeline revision fixture");
    queue(AssetQueue, 0x80280000, 1);
    queue(AudioQueue, 0x80280100, 32);
    // Zero/odd/aligned/final-short-chunk reads execute the retail loop itself.
    for (auto n : {0U, 1U, 8U, 0x5000U, 0x5003U, 0xa010U})
        copy(0x200000, View(rom).subspan(0x200000, n), (n + 0x4fff) / 0x5000);
    AssetBus bus;
    active_bus = &bus;
    const auto directory = AssetDirectory::build(stock);
    const auto mount = bus.mount(directory);
    for (auto n : {0U, 1U, 8U, 0x5000U, 0x5003U, 0xa010U}) {
        const auto expected = directory->read(39, 0, n);
        copy(mount->address(39), expected, 0);
    }
    // Existing stock calls still bypass the bus even while custom IO exists.
    copy(0x200002, View(rom).subspan(0x200002, 0x5003), 2);
    // Change the SAME carrier record, retaining both banks to test late reads.
    Bytes replacement(0x6008, 0x5c);
    const auto custom = AssetBank::derive(stock, std::string(64, 'c'), {{{27, 5}, replacement}});
    const auto custom_dir = AssetDirectory::build(custom);
    const auto custom_mount = bus.mount(custom_dir);
    const auto table = custom_dir->read(26, 0, custom_dir->section_size(26));
    copy(custom_mount->address(27, be32(table, 5 * 4)), replacement, 0);
    copy(mount->address(27), directory->read(27, 0, 0x6008), 0);
    // Audio starts with three real guest DMA-cache nodes/free-list links.
    put(AudioState + 4, 0);
    put(AudioState + 8, 0x80200000);
    put(AudioFrame, 1);
    put(NextDma, 0);
    for (unsigned i = 0; i < 3; ++i) {
        const auto node = 0x80200000 + i * 0x20;
        put(node, i < 2 ? node + 0x20 : 0);
        put(node + 4, i ? node - 0x20 : 0);
        put(node + 8, 0);
        put(node + 12, 0);
        put(node + 16, 0x80201000 + i * 0x1000);
    }
    const auto before = bridge_calls;
    const auto audio_a = audio(mount->address(39) + 1, 32);
    require(audio_a == 0x201001 && bridge_calls == before + 1 && pending.size() == 1 && pending.front() == AudioQueue,
            "Audio miss lost its odd-address adjustment or completion");
    pending.clear();
    require(bytes(0x80201000, 1024) == directory->read(39, 0, 1024), "Audio sample DMA bytes differ");
    require(audio(mount->address(39) + 3, 16) == 0x201003 && bridge_calls == before + 1,
            "Audio cache hit did not retain its original owner/address");
    const auto audio_b = audio(custom_mount->address(39) + 1, 32);
    require(audio_b == 0x202001 && bridge_calls == before + 2 && pending.size() == 1,
            "Two content banks aliased the same audio cache entry");
    pending.clear();
    require(audio(mount->address(39) + 3, 16) == 0x201003 && bridge_calls == before + 2,
            "Returning to retained bank A used bank B's cached sample");
    // Invalid tagged pointers must not reach stock ROM masking or enqueue a
    // fake completion. This test catches the error; game-session containment
    // is deliberately a later, separately verified integration requirement.
    auto ctx = context();
    ctx.r6 = 0;
    ctx.r7 = AssetBus::End;
    put(0x80700010, 0x80300000);
    put(0x80700014, 8);
    put(0x80700018, AssetQueue);
    const auto calls = original_calls, done = completed;
    bool rejected = false;
    try {
        dispatch_pi_dma(active_bus, memory, ctx, stock_pi, {nullptr, enqueue});
    } catch (const Error&) {
        rejected = true;
    }
    require(rejected && original_calls == calls && completed == done, "Invalid virtual address escaped into stock DMA");
    cache_test(stock, bus);
    asset_api_test(stock, bus);
    resident_test(stock, bus);
    require(sha256(read_file(utf8_path(argv[1]), MaxImage)) == source_hash, "Owned ROM changed");
    std::cout
        << "v" << DKR_TEST_REVISION
        << ": generated asset/audio DMA, delay slots, chunk completion, cache A/B/A, stock fallback and ROM preservation passed.\n";
    std::cout
        << "OS completion endpoint is a test double; full scene/cache ownership and renderer playback remain pending.\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}

#include "legacy_mod_format.hpp"
#include "legacy_mod_geometry.hpp"
#include "legacy_mod_dependencies.hpp"
#include <miniz/miniz.h>
#include <mbedtls/sha256.h>
#include <json/json.hpp>
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <map>
#include <set>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace dkr::mods {
std::filesystem::path private_storage_path(const std::filesystem::path& path) {
    if(path.empty())throw Error("A private storage path is required.");
    auto normalized=std::filesystem::absolute(path).lexically_normal().make_preferred();
#ifdef _WIN32
    const auto text=normalized.native();
    if(text.starts_with(L"\\\\?\\"))return normalized;
    if(text.starts_with(L"\\\\"))return std::filesystem::path(L"\\\\?\\UNC\\"+text.substr(2));
    return std::filesystem::path(L"\\\\?\\"+text);
#else
    return normalized;
#endif
}
View slice(View bytes, std::size_t offset, std::size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw Error("Asset offset or size is outside its containing record.");
    return bytes.subspan(offset, size);
}
std::uint16_t be16(View bytes, std::size_t pos) {
    const auto s = slice(bytes, pos, 2);
    return static_cast<std::uint16_t>((s[0] << 8) | s[1]);
}
std::uint32_t be32(View bytes, std::size_t pos) {
    const auto s = slice(bytes, pos, 4);
    return (std::uint32_t(s[0]) << 24) | (std::uint32_t(s[1]) << 16) |
           (std::uint32_t(s[2]) << 8) | s[3];
}
namespace {
std::string hex_digest(const std::array<unsigned char,32>& hash) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (auto byte : hash) {
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
}
std::string sha256(View bytes) {
    std::array<unsigned char,32> hash{};
    if(mbedtls_sha256(bytes.data(),bytes.size(),hash.data(),0)!=0)
        throw Error("Content identity could not be calculated.");
    return hex_digest(hash);
}
std::string sha256_pcm_le(std::span<const std::int16_t> samples) {
    struct Hash {
        mbedtls_sha256_context value;
        Hash(){mbedtls_sha256_init(&value);}
        ~Hash(){mbedtls_sha256_free(&value);}
    } hash;
    if(mbedtls_sha256_starts(&hash.value,0)!=0)throw Error("Music identity could not be calculated.");
    std::array<unsigned char,4096> chunk{};
    while(!samples.empty()) {
        const auto count=std::min(samples.size(),chunk.size()/2);
        for(std::size_t i=0;i<count;++i) {
            const auto value=static_cast<std::uint16_t>(samples[i]);
            chunk[2*i]=static_cast<unsigned char>(value);chunk[2*i+1]=static_cast<unsigned char>(value>>8);
        }
        if(mbedtls_sha256_update(&hash.value,chunk.data(),count*2)!=0)throw Error("Music identity could not be calculated.");
        samples=samples.subspan(count);
    }
    std::array<unsigned char,32> result{};
    if(mbedtls_sha256_finish(&hash.value,result.data())!=0)throw Error("Music identity could not be calculated.");
    return hex_digest(result);
}
Bytes read_file(const std::filesystem::path& path, std::size_t limit) {
    std::ifstream file(private_storage_path(path), std::ios::binary | std::ios::ate);
    if (!file) throw Error("The selected file could not be opened.");
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > limit)
        throw Error("The file exceeds the legacy import size limit.");
    Bytes result(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!result.empty() && !file.read(reinterpret_cast<char*>(result.data()), result.size()))
        throw Error("The file changed or could not be read completely.");
    return result;
}
void write_new_file(const std::filesystem::path& path, View bytes) {
#if defined(_WIN32)
    const auto normalized=private_storage_path(path);
    const auto file=CreateFileW(normalized.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file==INVALID_HANDLE_VALUE) throw Error("Could not create a new private import file.");
    DWORD written=0;
    const bool okay=bytes.size()<=MAXDWORD &&
        WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
        written==bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
#else
    const int file=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(file<0) throw Error("Could not create a new private import file.");
    std::size_t written=0;
    while(written<bytes.size()) {
        const auto count=write(file,bytes.data()+written,bytes.size()-written);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) break;
        written+=static_cast<std::size_t>(count);
    }
    const bool okay=written==bytes.size() && fsync(file)==0;
    close(file);
#endif
    if(!okay) throw Error("The private import file could not be written completely.");
}
void canonicalize_rom(Bytes& bytes) {
    if (bytes.size() < 0x1000 || bytes.size() > MaxImage || bytes.size() % 4)
        throw Error("Invalid Nintendo 64 image length.");
    switch (be32(bytes, 0)) {
    case 0x80371240: return;
    case 0x37804012:
        for (std::size_t i = 0; i < bytes.size(); i += 2) std::swap(bytes[i], bytes[i+1]);
        return;
    case 0x40123780:
        for (std::size_t i = 0; i < bytes.size(); i += 4) {
            std::swap(bytes[i], bytes[i+3]); std::swap(bytes[i+1], bytes[i+2]);
        }
        return;
    default: throw Error("The patch did not produce a recognized Nintendo 64 image.");
    }
}
std::string verified_revision(View bytes) {
    if (bytes.size() != 12 * MiB) throw Error("Import requires an original supported Game Pak.");
    const auto digest = sha256(bytes);
    if (digest == "dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876") return "us.v77";
    if (digest == "7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf") return "us.v80";
    throw Error("The source is not an original supported Game Pak. Import US v 1.0 or v 1.1 first.");
}
std::string revision_fingerprint(std::string_view revision) {
    if(revision=="us.v77")return "dcf54c82a58f6b38603b5865e90c3baabfe553a4d141a1ea2b282170a2e98876";
    if(revision=="us.v80")return "7de1a8fb2a9558cfc3d9ad4497df698c1e89cf7095ac1531557df2af40ba8bcf";
    throw Error("No native asset adapter exists for this Game Pak revision.");
}
Bytes inflate_asset(View bytes, std::size_t maximum) {
    slice(bytes, 0, 6);
    const std::uint32_t size = std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8) |
        (std::uint32_t(bytes[2]) << 16) | (std::uint32_t(bytes[3]) << 24);
    if (!size || size > maximum || bytes[4] > 9)
        throw Error("Compressed asset exceeds its engine allocation limit.");
    Bytes out(size);
    mz_stream stream{};
    if (mz_inflateInit2(&stream, -15) != MZ_OK) throw Error("Could not initialize asset decoder.");
    stream.next_in = bytes.data() + 5;
    stream.avail_in = static_cast<unsigned>(bytes.size() - 5);
    stream.next_out = out.data();
    stream.avail_out = size;
    const auto status = mz_inflate(&stream, MZ_FINISH);
    const auto written = stream.total_out;
    mz_inflateEnd(&stream);
    if (status != MZ_STREAM_END || written != size)
        throw Error("Compressed asset is truncated or has an incorrect decoded size.");
    return out;
}
AssetImage::AssetImage(View rom, const std::string& source_revision) {
    if (source_revision == "us.v77") lut_start = 969568;
    else if (source_revision == "us.v80") lut_start = 970976;
    else throw Error("This asset-layout revision has no reviewed adapter.");
    if(rom.size()>MaxImage)throw Error("Asset image exceeds its size limit.");
    const auto load=[&](std::size_t at) {
        if(be32(rom,at)!=sections.size() || be32(rom,at+4)!=0)throw Error("Not an asset directory.");
        for(unsigned i=0;i<sections.size();++i) {
            const auto a=be32(rom,at+4+4*i),b=be32(rom,at+8+4*i);
            if(b<a)throw Error("Asset section offsets are not monotonic.");
            sections[i]=slice(rom,at+208+std::size_t(a),b-a);
        }
        for(unsigned s:{0U,2U,4U,6U,8U,10U,12U,15U,21U,23U,25U,27U,29U,32U,34U,37U,39U,41U,43U,49U})records(s);
        if(records(34).size()<178 || records(23).size()<50 || records(39).size()<8)
            throw Error("Asset directory is missing native root tables.");
    };
    try {load(lut_start);return;}catch(const Error&){}
    // Relocating the directory is normal for rebuilt hacks. Discover only a
    // unique, fully bounded 50-section directory; never execute target code
    // or accept a candidate merely because it starts with the number 50.
    unsigned candidates=0;std::uint32_t found=0;
    for(std::size_t at=0x1000;at+208<=rom.size() && at<2*MiB;at+=4) {
        if(be32(rom,at)!=50 || be32(rom,at+4)!=0)continue;
        try {load(at);found=static_cast<std::uint32_t>(at);++candidates;}catch(const Error&){}
        if(candidates>1)throw Error("Patch contains ambiguous asset directories.");
    }
    if(candidates!=1)throw Error("No structurally valid native asset directory was found in the patch.");
    lut_start=found;
    const std::size_t data = lut_start + 208;
    for (unsigned i = 0; i < sections.size(); ++i) {
        const auto begin = be32(rom, lut_start + 4 + 4*i);
        const auto end = be32(rom, lut_start + 8 + 4*i);
        if (end < begin) throw Error("Asset section offsets are not monotonic.");
        sections[i] = slice(rom, data + std::size_t(begin), end - begin);
    }
}
namespace {
int table_for(unsigned section) {
    switch (section) {
    case 0: case 2: case 4: case 6: case 8: case 10: case 12: case 15: return section+1;
    case 21: case 23: case 25: case 27: case 29: case 32: case 34: case 37:
    case 39: case 41: case 43: case 49: return section-1;
    default: return -1;
    }
}
bool same(View a, View b) { return std::ranges::equal(a, b); }
bool compressed(unsigned section) { return section == 21 || section == 27 || section == 29 || section == 32; }
std::string clean_name(View name, unsigned carrier) {
    std::string result;
    for (auto ch : name) {
        if (!ch) break;
        if (result.size() == 160) break;
        if (ch >= 32 && ch < 127) result += static_cast<char>(ch);
        else result += '?';
    }
    return result.empty() ? "Custom Track " + std::to_string(carrier) : result;
}
void validate_map(View record) {
    const auto map = inflate_asset(record, 0x3000);
    const auto length = be32(map, 0);
    slice(map, 16, length);
    unsigned count = 0;
    for (std::size_t pos = 16; pos < 16 + length;) {
        slice(map, pos, 8);
        const unsigned size = map[pos+1] & 0x7f;
        if (size < 8 || size > 16 + length - pos || ++count > 512)
            throw Error("Object map contains an invalid entry or too many objects.");
        pos += size;
    }
}
}
std::vector<View> AssetImage::records(unsigned section) const {
    if (section >= sections.size()) throw Error("Invalid asset section.");
    const int table_id = table_for(section);
    if (table_id < 0) return {sections[section]};
    const auto table = sections[table_id];
    std::vector<std::uint32_t> offsets;
    if (section == 39) offsets.push_back(0);
    const unsigned stride = section == 49 ? 8 : 4;
    const unsigned start = (section == 8 || section == 49) ? 4 : 0;
    bool terminated = false;
    for (std::size_t pos = start; pos + 4 <= table.size(); pos += stride) {
        auto value = be32(table, pos);
        if (value == 0xffffffff) { terminated = true; break; }
        if (section == 6) value &= 0x7fffffff;
        if (section == 15) {
            if (value > MaxImage/4) throw Error("MISC table offset overflows.");
            value *= 4;
        }
        if (value > sections[section].size() || (!offsets.empty() && value < offsets.back()))
            throw Error("Invalid asset record offset table.");
        offsets.push_back(value);
        if (offsets.size() > 65536) throw Error("Too many asset records.");
    }
    if (!terminated || offsets.empty()) throw Error("Asset offset table has no bounded terminator.");
    std::vector<View> result;
    for (std::size_t i = 1; i < offsets.size(); ++i)
        result.push_back(slice(sections[section], offsets[i-1], offsets[i] - offsets[i-1]));
    return result;
}
View AssetImage::record(unsigned section, unsigned id) const {
    const auto entries = records(section);
    if (id >= entries.size()) throw Error("Track references an absent asset record.");
    return entries[id];
}
Analysis analyze(View base, View target, const std::string& patch_digest, bool behaviour_enabled) {
    Analysis result;
    result.source_revision = verified_revision(base);
    result.patch_digest = patch_digest;
    result.target_digest = sha256(target);
    const AssetImage before(base, result.source_revision), after(target, result.source_revision);
    result.asset_digest = sha256(target.subspan(after.lut_start));
    const bool bigboo = result.target_digest == "ad0de627d2549da242e54e819c31902eab22428b0b5640724333aaf3c799fa66";
    const bool community = result.target_digest == "56f24d91cd3d098446cc3a88c51c88a301e0b2c4a435cb866a69d66eef8ca1ff";
    const bool haunter = result.target_digest == "92c016c905b08d4063ccf77a46fac229530d2267accda0dbbf14d87e54607102" ||
        result.target_digest == "4fe181d105b69e527d7c175da565be146667308262ed1fff3ee304339649c20c";
    const bool yooka=result.target_digest=="a79844c8713382a7be1724990c075cfe362817c255a634291701b12dda9a5a9a";
    if (bigboo) result.profile = "sixtyfour-big-boo-2018";
    if (community) result.profile = "community-track-pack";
    if (haunter) result.profile = "sixtyfour-haunter-2019";
    if (yooka) result.profile = "yooka-relocated-assets";
    // Retain executable evidence independently of asset identity. A rebuilt
    // image may relocate constants/functions; a byte count alone is NOT proof
    // that it adds an ability. Only exact reviewed edits acquire permission.
    Bytes evidence;std::set<std::size_t> covered;
    if(after.lut_start==before.lut_start) {
        for(unsigned i=0;i<BehaviourSites;++i) {
            const auto& site=behaviour_sites()[i];
            const auto pc=result.source_revision=="us.v77"?site.pc77:site.pc80;
            const auto word=result.source_revision=="us.v77"?site.original77:site.original80;
            if(!pc)continue;
            const auto offset=std::size_t(pc-0x80000400U)+0x1000;
            if(be32(base,offset)!=word)throw Error("The reviewed executable source signature changed.");
            if(be32(target,offset)==site.replacement) {
                result.behaviour.instructions[i]=site.replacement;
                for(unsigned b=0;b<4;++b)covered.insert(offset+b);
            }
        }
    }
    for(std::size_t i=0;i<before.lut_start;++i) {
        if(i>=0x10 && i<0x18)continue; // Header checksums are neither code nor behaviour.
        if(i<target.size() && base[i]==target[i])continue;
        ++result.behaviour.changed_bytes;
        if(!covered.contains(i))++result.behaviour.unresolved_bytes;
        for(unsigned b=0;b<4;++b)evidence.push_back(static_cast<std::uint8_t>(i>>(24-8*b)));
        evidence.push_back(base[i]);evidence.push_back(i<target.size()?target[i]:0);
    }
    if(result.behaviour.changed_bytes) {
        result.behaviour.abi=BehaviourAbi;result.behaviour.evidence=sha256(evidence);
        if(behaviour_enabled && result.source_revision=="us.v77") {
            result.behaviour.native_profile=linked_behaviour_profile(slice(target,0,after.lut_start));
            if(result.behaviour.native_profile) {
                result.behaviour.unresolved_bytes=0;
                result.behaviour.instructions.fill(0);
            }
        }
    }
    result.notes.push_back(behaviour_summary(result.behaviour));
    const unsigned data_sections[] = {0,2,4,6,8,10,12,15,17,18,19,21,23,25,27,29,30,32,34,35,37,39,41,43,44,47,49};
    std::set<std::pair<unsigned,unsigned>> changed;
    for (auto section : data_sections) {
        const auto a = before.records(section), b = after.records(section);
        for (unsigned id = 0; id < b.size(); ++id) {
            if (id < a.size() && same(a[id], b[id])) continue;
            if (compressed(section)) {
                // Sub-24-byte entries in these legacy packs are deleted/dummy
                // records. They are evidence only, never playable roots.
                if (b[id].size() < 24) continue;
                const auto decoded = inflate_asset(b[id], 4*MiB);
                if (id < a.size() && a[id].size() >= 24 &&
                    same(decoded, inflate_asset(a[id], 4*MiB))) continue;
            }
            changed.emplace(section,id);
            result.changed_records.emplace_back(section,id);
        }
    }
    if (haunter || yooka) {
        CharacterRoot character;
        character.name=haunter?"Haunter":"Yooka";
        character.base_character=haunter?8:3;character.portrait=haunter?130:126;
        character.headers=haunter?std::array<unsigned,3>{1,11,21}:std::array<unsigned,3>{5,15,25};
        const auto identity="dkr-character-review-v1:"+result.asset_digest+":"+std::to_string(character.base_character);
        character.content_id=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
        result.character_roots.push_back(character);
        result.characters.push_back(character.name+(haunter?" (T.T.-derived; car, hovercraft and plane)":" (Conker-derived; car, hovercraft and plane)"));
        result.notes.push_back("Character dependencies require additive runtime preparation; original racers remain unchanged.");
    }
    if(!haunter && !yooka) {
        constexpr unsigned roots[]{2,3,4,5,6,7,8,9,1,0};
        constexpr unsigned portraits[]{122,123,124,126,125,127,128,129,130,131};
        const auto skin_changed=[&](unsigned texture) {
            if(!changed.contains({2,texture}))return false;
            if(texture>=before.records(2).size())return true;
            const auto decode=[](View record) {slice(record,0,32);return record[0x1d]?inflate_asset(record.subspan(32),4*MiB):Bytes(record.begin(),record.end());};
            const auto a=decode(before.record(2,texture)),b=decode(after.record(2,texture));
            if(a.size()<32 || b.size()<32)return true;
            if(a[0]!=b[0] || a[1]!=b[1] || a[2]!=b[2])return true;
            constexpr unsigned bits[]{32,16,8,4,16,8,4};
            if((a[2]&15)>=7)return true;
            const auto pixels=(std::size_t(a[0])*a[1]*bits[a[2]&15]+7)/8;
            return !same(slice(a,32,pixels),slice(b,32,pixels));
        };
        const auto visual_changed=[&](unsigned header) {
            const auto h=after.record(34,header),old=before.record(34,header);
            if(!same(h,old)) {
                if(!behaviour_enabled)return false;
                try{if(object_parameter_fields(old,h))return true;}catch(const Error&){return false;}
            }
            if(h.size()<0x78 || h[0x53]!=0 || !h[0x55])return false;
            const auto model=be32(h,be32(h,0x10));
            const auto a=inflate_asset(before.record(29,model),4*MiB),b=inflate_asset(after.record(29,model),4*MiB);
            if(be16(a,0x24)!=be16(b,0x24))return true;
            for(unsigned i=0;i<be16(a,0x24);++i)
                if(!same(slice(a,be32(a,4)+i*10,6),slice(b,be32(b,4)+i*10,6)))return true;
            const auto material_count=be16(b,0x22);
            for(unsigned i=0;i<material_count;++i) {
                const auto texture=be32(b,be32(b,0)+8*i);
                // Player-sign materials are not character skins.
                if(texture!=1400 && skin_changed(texture))return true;
            }
            return false;
        };
        for(unsigned c=0;c<10;++c) {
            // A primary racing model, not incidental low-LOD/exporter bytes,
            // identifies the donor. All three vehicle graphs are then owned.
            bool stats_changed=false;
            for(unsigned stat=0;stat<3;++stat) {
                const auto a=before.record(15,9+stat),b=after.record(15,9+stat);
                if(a.size()==b.size() && a.size()>=40 && be32(a,4*c)!=be32(b,4*c))stats_changed=true;
            }
            if(!visual_changed(roots[c]) && !visual_changed(roots[c]+10) && !visual_changed(roots[c]+20) && !(behaviour_enabled && stats_changed))continue;
            CharacterRoot root;root.base_character=c;root.portrait=portraits[c];
            root.name="Custom Racer "+std::to_string(c+1);
            root.headers={roots[c],roots[c]+10,roots[c]+20};
            const auto identity="dkr-character-review-v1:"+result.asset_digest+":"+std::to_string(c);
            root.content_id=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
            result.character_roots.push_back(root);result.characters.push_back(root.name);
        }
    }
    if(result.profile.empty())result.profile="native-asset-graph-v1";
    const auto headers = after.records(23);
    const auto names = after.records(25);
    const auto geometry = after.records(27);
    for (unsigned id = 0; id < headers.size(); ++id) {
        const auto header = headers[id];
        slice(header,0,0xc4);
        const auto geo = be16(header,0x34);
        const auto objects = be16(header,0x36);
        const auto setup = be16(header,0xba);
        const auto type = header[0x4c];
        if (type != 0 && (type & 0x40) == 0) continue;
        if (geo >= geometry.size() || geometry[geo].size() < 24) continue;
        if (!changed.contains({27,geo}) && !changed.contains({21,setup}) &&
            !changed.contains({21,objects}) && !changed.contains({23,id})) continue;
        Root root;
        root.carrier=id; root.geometry=geo; root.object_map=setup; root.collectables=objects;
        root.race_type=type; root.vehicles=header[0x4e] & 7;
        root.name=id < names.size() ? clean_name(names[id],id) : "Custom Track " + std::to_string(id);
        if (bigboo && id == 5) { root.name="Big Boo's Haunt"; root.vehicles &= ~4U; }
        try {
            validate_geometry(inflate_asset(geometry[geo],0x82a00),geometry[geo].size());
            if (setup != 0xffff) validate_map(after.record(21,setup));
            if (objects != 0xffff) validate_map(after.record(21,objects));
        } catch (const Error& e) { root.blockers.push_back(e.what()); }
        const std::string identity = "dkr-legacy-track-v1:" + result.asset_digest + ":" + std::to_string(id);
        root.content_id = sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
        result.tracks.push_back(std::move(root));
    }
    // Authored intent metadata, NOT an admission whitelist: unknown patches
    // continue through the structural graph detector above. These early hacks
    // also delete unrelated assets to reclaim ROM space or modify donor skins.
    const auto identify_character=[&](unsigned donor,const char* name) {
        std::erase_if(result.character_roots,[&](const auto& c){return c.base_character!=donor;});
        result.characters.clear();
        for(auto& c:result.character_roots){c.name=name;result.characters.push_back(c.name);}
    };
    if(result.target_digest=="66356578e2cbb63bfc57ab305271825e216cc0e6c1824531ceecd5be2103145b")identify_character(8,"Rexy");
    if(result.target_digest=="0e17d7970dc9cd4b66f212b2099b8e950c6651bb7814f61c2b13c01df2186ea1") {
        identify_character(5,"007 Bond (car only)");
        result.notes.push_back("Author's early car-only Banjo replacement. Stage, hovercraft, plane and missing LOD assets retain the native donor appearance; no missing artwork is fabricated.");
    }
    if(result.target_digest=="8255db47c99a75feba3fd3d523c2229928a6b81f35f4f2b06281a228989607d8") {
        identify_character(8,"Link");
        result.notes.push_back("The author does not support plane AI on these four courses. Native gameplay and non-track sound effects are retained.");
        for(auto& t:result.tracks)t.vehicles&=~4U;
    }
    if(result.target_digest=="c16f3ef859d5e03a4f1437a92ceb778ac75fd5b629b86749db18a7d11117d883") {
        std::erase_if(result.tracks,[](const auto& t){return t.carrier!=3 && t.carrier!=5 && t.carrier!=7 && t.carrier!=29;});
        result.notes.push_back("Four authored GoldenEye courses; collateral edits/deletions of other courses are excluded. Non-track sound effects remain native.");
    }
    if(result.target_digest=="000a04797625d14960eb54f5d7692eaf2d2732e559c3a8f6f28f2ebdad877a2d") {
        std::erase_if(result.tracks,[](const auto& t){return t.carrier!=4;});
        for(auto& t:result.tracks)t.vehicles&=~4U;
        result.notes.push_back("Rainbow Road supports car and hovercraft. Its collateral Greenwood Village edits and non-track sound effects are excluded.");
    }
    if(result.target_digest=="4d4110b99653832dc810f175524e5d0bf4a9aecb5e07ea11adf506025068198b")identify_character(9,"Mario");
    if(result.target_digest=="737ca0cd721188d9ff32721eb8d8f7bd47e10f07fbab908a0ce8115537f06a6b")identify_character(9,"Dixie Kong");
    // The ten title-name literals precede '?' / CREDITS.
    // Decode the complete anchored table, not a lone matching word somewhere
    // in rebuilt code. Ambiguous/structurally different layouts stay unnamed.
    const auto title_names=[](View rom,std::size_t end)->std::vector<std::string> {
        const std::string anchor="CREDITS";std::vector<std::string> found;
        for(std::size_t at=0;at+32<=end;++at) {
            if(!std::equal(anchor.begin(),anchor.end(),rom.begin()+at) || rom[at+7]!=0)continue;
            auto p=at;std::vector<std::string> reverse;
            for(unsigned n=0;n<11;++n) {
                while(p && rom[p-1]==0)--p;
                const auto last=p;while(p && rom[p-1]!=0 && last-p<=32)--p;
                if(!p || last-p>32 || last==p)break;
                std::string name(rom.begin()+p,rom.begin()+last);
                if(std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c>126;}))break;
                reverse.push_back(std::move(name));
            }
            if(reverse.size()!=11 || reverse.front()!="?")continue;
            reverse.erase(reverse.begin());std::reverse(reverse.begin(),reverse.end());
            constexpr const char* stock[]{"TIMBER","BUMPER","CONKER","TIPTUP","DRUMSTICK","PIPSY","BANJO","KRUNCH","WIZPIG","DIDDY"};
            unsigned unchanged=0;for(unsigned i=0;i<10;++i)unchanged+=reverse[i]==stock[i];
            if(unchanged<7)continue;
            if(!found.empty())return {};found=std::move(reverse);
        }
        return found;
    };
    const auto native_names=title_names(base,before.lut_start),custom_names=title_names(target,after.lut_start);
    constexpr unsigned name_index[]{7,1,3,2,4,6,5,0,10,9};
    if(behaviour_enabled && native_names.size()==10 && custom_names.size()==10)for(auto& root:result.character_roots) {
        const auto i=name_index[root.base_character];
        if(i<10 && custom_names[i]!=native_names[i])root.name=custom_names[i];
    }
    result.characters.clear();
    for(const auto& root:result.character_roots)result.characters.push_back(root.name);
    if(behaviour_enabled) {
        for(auto& root:result.tracks) {
            root.behaviour=result.behaviour;
            const auto dependencies=inspect_track_dependencies(before,after,root);
            for(const auto& dependency:dependencies.records)if(dependency.section==34 && dependency.changed)try {
                const auto fields=object_parameter_fields(before.record(34,dependency.id),after.record(34,dependency.id));
                if(fields && !root.behaviour.abi) {root.behaviour.abi=BehaviourAbi;root.behaviour.evidence=sha256(evidence);}
                root.behaviour.header_fields|=fields;
            }catch(const Error& e){root.blockers.push_back(e.what());}
            const auto behaviour_id=behaviour_identity(root.behaviour);
            if(!behaviour_id.empty()) {
                const auto identity="dkr-track-behaviour-1:"+root.content_id+":"+behaviour_id;
                root.content_id=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));
            }
        }
        for(auto& root:result.character_roots)root.behaviour=result.behaviour;
    }
    // MISC table values are copied only for the identified custom donor.
    // Never overlay the shared ten-racer table: two mods can share a donor.
    if(behaviour_enabled)for(auto& root:result.character_roots)for(unsigned stat=0;stat<3;++stat) {
        const auto old=before.record(15,9+stat),replacement=after.record(15,9+stat);
        if(old.size()!=replacement.size() || old.size()<40) {
            root.blockers.push_back("Racer parameter table requires a native layout adapter.");break;
        }
        const auto at=root.base_character*4;
        if(be32(old,at)==be32(replacement,at))continue;
        if(!root.behaviour.abi) {
            root.behaviour.abi=BehaviourAbi;
            root.behaviour.evidence=sha256(evidence);
        }
        root.behaviour.stat_mask|=1U<<stat;root.behaviour.stat_bits[stat]=be32(replacement,at);
        try{validate_behaviour(root.behaviour);}catch(const Error& e){root.blockers.push_back(e.what());}
    }
    if(behaviour_enabled)for(auto& root:result.character_roots) {
        constexpr unsigned stage_headers[]{169,171,172,170,173,175,176,177,40,174};
        for(auto id:{root.headers[0],root.headers[1],root.headers[2],stage_headers[root.base_character]})try {
            const auto fields=object_parameter_fields(before.record(34,id),after.record(34,id));
            if(fields && !root.behaviour.abi) {root.behaviour.abi=BehaviourAbi;root.behaviour.evidence=sha256(evidence);}
            root.behaviour.header_fields|=fields;
        }catch(const Error& e){root.blockers.push_back(e.what());}
    }
    if (bigboo) result.notes.push_back("Includes custom music, projectile model and HUD textures; Greenwood Village deletion is excluded.");
    for(const auto& [section,id]:result.changed_records) {
        if(section==47)result.notes.push_back("GLOBAL_DATA record "+std::to_string(id)+" changed: whole-game state/layout changes are not transplanted into an additive mod.");
        if(section==15 && (id<9 || id>11))result.notes.push_back("MISC record "+std::to_string(id)+" changed: this parameter needs an additional ownership/behaviour adapter.");
    }
    if (result.tracks.empty() && result.character_roots.empty()) result.blockers.push_back("No identifiable track or character asset roots were found.");
    if (result.profile.empty()) result.notes.push_back("Structurally detected content; package intent and dependency closure need compatibility review.");
    return result;
}
std::string analysis_json(const Analysis& a) {
    using nlohmann::json;
    json out = {{"schema",Schema},{"source_revision",a.source_revision},{"patch_sha256",a.patch_digest},
        {"target_sha256",a.target_digest},{"asset_sha256",a.asset_digest},{"profile",a.profile},
        {"notes",a.notes},{"blockers",a.blockers},{"characters",a.characters},
        {"changed_records",a.changed_records},{"tracks",json::array()},{"character_roots",json::array()}};
    out["behaviour_contract"]=json::parse(encode_behaviour(a.behaviour));
    out["behaviour_identity"]=behaviour_identity(a.behaviour);
    for(const auto& c:a.character_roots) out["character_roots"].push_back({
        {"id",c.content_id},{"name",c.name},{"base_character",c.base_character},
        {"portrait",c.portrait},{"headers",c.headers},{"blockers",c.blockers},
        {"runtime_certified",false},{"behaviour",behaviour_summary(c.behaviour)},
        {"behaviour_identity",behaviour_identity(c.behaviour)},
        {"behaviour_contract",json::parse(encode_behaviour(c.behaviour))}});
    for (const auto& t : a.tracks) out["tracks"].push_back({{"id",t.content_id},{"name",t.name},
        {"carrier",t.carrier},{"geometry",t.geometry},{"object_map",t.object_map},
        {"collectables",t.collectables},{"race_type",t.race_type},{"vehicles",t.vehicles},
        {"blockers",t.blockers},{"behaviour",behaviour_summary(t.behaviour)},
        {"behaviour_identity",behaviour_identity(t.behaviour)},
        {"behaviour_contract",json::parse(encode_behaviour(t.behaviour))}});
    return out.dump(2);
}
} // namespace dkr::mods

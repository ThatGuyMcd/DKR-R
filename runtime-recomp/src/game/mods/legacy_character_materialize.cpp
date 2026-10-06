#include "legacy_character_materialize.hpp"
#include "legacy_mod_dependencies.hpp"
#include "legacy_asset_capacity.hpp"
#include <miniz/miniz.h>
#include <algorithm>
#include <functional>
#include <set>

namespace dkr::mods {
namespace {
constexpr unsigned stage_headers[]{169,171,172,170,173,175,176,177,40,174};
void put16(Bytes& bytes,std::size_t at,unsigned n) {bytes.at(at)=n>>8;bytes.at(at+1)=n;}
void put32(Bytes& bytes,std::size_t at,unsigned n) {put16(bytes,at,n>>16);put16(bytes,at+2,n);}
void append16(Bytes& bytes,unsigned n) {
    if(n>=32768) throw Error("Character namespace exceeds signed native asset IDs.");
    bytes.push_back(n>>8);bytes.push_back(n);
}
Bytes pack_model(View raw) {
    if(raw.empty() || raw.size()>4*MiB)throw Error("Character model exceeds its decoded budget.");
    Bytes result(5+mz_compressBound(static_cast<mz_ulong>(raw.size())));
    const auto size=static_cast<unsigned>(raw.size());
    for(unsigned i=0;i<4;++i)result[i]=size>>(i*8);
    result[4]=9;
    mz_stream stream{};
    if(mz_deflateInit2(&stream,9,MZ_DEFLATED,-15,8,MZ_DEFAULT_STRATEGY)!=MZ_OK)
        throw Error("Could not initialize character model encoder.");
    stream.next_in=raw.data();stream.avail_in=size;
    stream.next_out=result.data()+5;stream.avail_out=static_cast<unsigned>(result.size()-5);
    const auto status=mz_deflate(&stream,MZ_FINISH);
    const auto written=stream.total_out;
    mz_deflateEnd(&stream);
    if(status!=MZ_STREAM_END)throw Error("Character model encoding did not complete.");
    result.resize((5+written+15)&~std::size_t(15),0);
    if(inflate_asset(result,4*MiB)!=Bytes(raw.begin(),raw.end()))throw Error("Character model encoder round-trip failed.");
    return result;
}
Bytes align_animation_dma(View packed) {
    const auto raw=inflate_asset(packed,4*MiB);
    // object_animate/object_model_init place compressed input at the tail of
    // an aligned allocation: base + decodedSize + 0x80 - packedSize. Some
    // reviewed legacy exporters omit trailing compressed-stream padding.
    // Pad only our appended copy; the inflater ignores bytes after deflate's
    // end marker, so every decoded keyframe stays byte-for-byte identical.
    if(raw.size()%8)throw Error("Character animation needs a reviewed decoded-alignment adapter.");
    const auto size=(packed.size()+7)&~std::size_t(7);
    if(size>raw.size()+0x80)throw Error("Aligned character animation exceeds its native allocation.");
    Bytes result(packed.begin(),packed.end());result.resize(size,0);
    if(inflate_asset(result,4*MiB)!=raw)throw Error("Character animation padding changed decoded data.");
    return result;
}
std::vector<unsigned> header_ids(View bytes,unsigned pointer_field,unsigned count) {
    if(count>127)throw Error("Character header has a negative array count.");
    const auto offset=be32(bytes,pointer_field);
    if(count && (offset<0x78 || offset%4))throw Error("Character header array is invalid.");
    slice(bytes,offset,4*count);
    std::vector<unsigned> ids;
    for(unsigned i=0;i<count;++i)ids.push_back(be32(bytes,offset+i*4));
    return ids;
}
void validate_stage(const PreparedCharacter& c,const std::function<View(unsigned,unsigned)>& stock) {
    if(c.root.base_character>=10 || c.stage_header!=stage_headers[c.root.base_character])
        throw Error("Character selection actor does not match its reviewed base.");
    const auto& header=c.records.at({34,c.stage_header});
    const auto ids=header_ids(header,0x10,header[0x55]);
    if(header[0x53]!=0 || ids.size()!=1)throw Error("Selection actor requires one animated 3D model.");
    const auto model=ids.front();
    const auto raw=inflate_asset(c.records.at({29,model}),4*MiB);
    const auto original=inflate_asset(stock(29,model),4*MiB);
    if(c.model_animations.at(model).size()<2 || be16(raw,0x22)<4 || be16(original,0x22)<4)
        throw Error("Selection actor is missing idle/selected animations or player-sign materials.");
    for(unsigned i=0;i<4;++i) {
        const auto texture=be32(raw,be32(raw,0)+i*8);
        const auto original_texture=be32(original,be32(original,0)+i*8);
        const auto& a=c.records.at({2,texture});const auto b=stock(2,original_texture);
        const auto da=a[0x1d]?inflate_asset(View(a).subspan(32),4*MiB):a;
        const auto db=b[0x1d]?inflate_asset(b.subspan(32),4*MiB):Bytes(b.begin(),b.end());
        constexpr unsigned bpp[]{32,16,8,4,16,8,4};
        const auto format=da.at(2)&15;
        const auto pixels=(std::size_t(da.at(0))*da.at(1)*bpp[format]+7)/8;
        if(texture!=original_texture || da.size()<32 || db.size()<32 || da[0]!=db[0] || da[1]!=db[1] || da[2]!=db[2] ||
           !std::ranges::equal(slice(da,32,pixels),slice(db,32,pixels)))
            throw Error("Selection actor's player-sign material "+std::to_string(i)+" ("+std::to_string(texture)+"/"+std::to_string(original_texture)+") needs a reviewed adapter.");
    }
    unsigned signs=0;
    for(unsigned i=0;i<be16(raw,0x28);++i)if(slice(raw,be32(raw,0x38)+12*i,12)[0]<4)++signs;
    if(!signs)throw Error("Selection actor has no player-sign batches.");
}
}
void validate_character_animation(View packed,unsigned vertices) {
    const auto raw=inflate_asset(packed,4*MiB);
    const auto frames=be32(raw,0);
    // Native animFrame uses the first s16 XYZ keyframe and subsequent s8 XYZ
    // deltas, with 12 bytes of transform data per keyframe. One-keyframe
    // static LOD animations occur in the supplied Haunter fixture. This
    // validates source bounds, not certification of native animation playback.
    if(!vertices || vertices>=32768 || !frames || frames>32767 || packed.size()>raw.size()+0x80)
        throw Error("Character animation has an invalid frame/vertex allocation (vertices="+std::to_string(vertices)+", frames="+std::to_string(frames)+").");
    // obj_animate (v77 0x80062044 onward) reads the next s8 frame even
    // when animLength=1 clamps the tick back to zero. Such legacy static
    // animations must physically contain that second frame too.
    const auto stored_frames=std::max(2U,frames);
    const std::uint64_t required=4ULL+12ULL*stored_frames+6ULL*vertices+3ULL*vertices*(stored_frames-1);
    if(required>raw.size())throw Error("Character animation stream is truncated for its model's animated vertices.");
}
bool character_portrait_changed(View replacement,View original) {
    validate_texture_record(replacement);validate_texture_record(original);
    const auto frames=be16(replacement,0x12)>>8;
    if(frames!=(be16(original,0x12)>>8))return true;
    const auto decoded_a=replacement[0x1d]?inflate_asset(replacement.subspan(32),4*MiB):Bytes{};
    const auto decoded_b=original[0x1d]?inflate_asset(original.subspan(32),4*MiB):Bytes{};
    const View a=replacement[0x1d]?View(decoded_a):replacement;
    const View b=original[0x1d]?View(decoded_b):original;
    constexpr unsigned bpp[]{32,16,8,4,16,8,4};
    std::size_t at=0,bt=0;
    for(unsigned frame=0;frame<frames;++frame) {
        const auto ah=slice(a,at,32),bh=slice(b,bt,32);
        const auto format=ah[2]&15;
        if(ah[0]!=bh[0] || ah[1]!=bh[1] || format!=(bh[2]&15))return true;
        const auto bytes=(std::size_t(ah[0])*ah[1]*bpp[format]+7)/8;
        if(!std::ranges::equal(slice(a,at+32,bytes),slice(b,bt+32,bytes)))return true;
        if(frame+1<frames){at+=be16(ah,0x16);bt+=be16(bh,0x16);}
    }
    return false;
}
PreparedCharacter prepare_character(View original,View target,std::string patch_digest,unsigned base_character) {
    const auto analysis=analyze(original,target,patch_digest);
    if(!analysis.blockers.empty())throw Error(analysis.blockers.front());
    const auto found=std::find_if(analysis.character_roots.begin(),analysis.character_roots.end(),
        [&](const auto& root){return root.base_character==base_character;});
    if(found==analysis.character_roots.end() || !found->blockers.empty())
        throw Error("This patch has no reviewed character asset profile for that racer.");
    const AssetImage stock(original,analysis.source_revision),image(target,analysis.source_revision);
    PreparedCharacter result;result.root=*found;result.source_revision=analysis.source_revision;
    result.base_fingerprint=sha256(original);
    std::set<unsigned> active_headers;
    std::size_t total=0;
    std::function<void(unsigned,unsigned)> visit=[&](unsigned section,unsigned id) {
        if(section==34 && active_headers.contains(id))throw Error("Character attachments contain a header cycle.");
        if(result.records.contains({section,id}))return;
        if(result.records.size()>=1024)throw Error("Character dependency graph exceeds its bounded capacity.");
        const auto bytes=image.record(section,id);
        if(bytes.empty() || bytes.size()>16*MiB-total)throw Error("Character dependency bytes exceed the preparation budget.");
        total+=bytes.size();result.records.emplace(std::pair{section,id},Bytes(bytes.begin(),bytes.end()));
        if(section==2 || section==4) {validate_texture_record(bytes);return;}
        if(section==12) {
            for(auto texture:inspect_sprite_textures(bytes))visit(4,texture);
        } else if(section==29) {
            for(auto texture:inspect_model_textures(bytes))visit(2,texture);
            const auto raw=inflate_asset(bytes,4*MiB);
            const auto first=be16(image.sections[30],id*2),end=be16(image.sections[30],id*2+2);
            if(first>end || end-first>128 || end>=32768)throw Error("Character animation range is invalid.");
            auto& animations=result.model_animations[id];
            for(unsigned i=first;i<end;++i) {
                validate_character_animation(image.record(32,i),be16(raw,0x4a));
                visit(32,i);animations.push_back(i);
            }
        } else if(section==34) {
            slice(bytes,0,0x78);
            // First reviewed fixtures retain native object behaviour/header
            // semantics. Custom pointer layouts, particles or code need their
            // own adapter, not permissive transplantation into native memory.
            if(!std::ranges::equal(bytes,stock.record(34,id)) || (bytes[0x53]!=0 && bytes[0x53]!=2))
                throw Error("Character header "+std::to_string(id)+" (type="+std::to_string(bytes[0x53])+") requires a reviewed behaviour adapter.");
            active_headers.insert(id);
            for(auto model:header_ids(bytes,0x10,bytes[0x55]))if(model!=0xffffffff)visit(bytes[0x53]==0?29:12,model);
            for(auto header:header_ids(bytes,0x14,bytes[0x56]))if(header!=0xffffffff)visit(34,header);
            active_headers.erase(id);
        } else if(section!=32)throw Error("Unowned character asset dependency.");
    };
    for(auto header:result.root.headers)visit(34,header);
    visit(4,result.root.portrait);
    result.stage_header=stage_headers[base_character];visit(34,result.stage_header);
    validate_stage(result,[&](unsigned s,unsigned i){return stock.record(s,i);});
    result.audio=prepare_character_audio(image.record(39,2),image.record(39,3),image.record(39,7),base_character);
    result.race_audio=prepare_character_race_audio(image.record(39,2),image.record(39,3),image.record(39,7),base_character);
    validate_character_audio(result.audio);
    std::string canonical="dkr-character-assets-v2:"+result.source_revision+":"+std::to_string(base_character);
    canonical+=":"+character_audio_identity(result.audio);
    for(const auto& [key,bytes]:result.records) {
        const auto normalized=(key.first==29 || key.first==32)?inflate_asset(bytes,4*MiB):bytes;
        canonical+=":"+std::to_string(key.first)+","+std::to_string(key.second)+","+sha256(normalized);
    }
    for(const auto& [model,animations]:result.model_animations) {
        canonical+=":model,"+std::to_string(model);
        for(auto animation:animations)canonical+=","+std::to_string(animation);
    }
    result.root.content_id=sha256(View(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
    return result;
}
CharacterNamespace allocate_characters(std::shared_ptr<const AssetBank> stock,std::vector<PreparedCharacter> characters) {
    if(!stock || !stock->digest().empty() || stock->augmented() || characters.empty() || characters.size()>MaxEnabledCharacters)
        throw Error("Character boot namespace requires an original bank and a representable character library.");
    CharacterNamespace result;result.base_fingerprint=stock->fingerprint();
    std::sort(characters.begin(),characters.end(),[](const auto& a,const auto& b){return a.root.content_id<b.root.content_id;});
    std::array<unsigned,50> next{};
    for(unsigned s=0;s<50;++s)next[s]=static_cast<unsigned>(stock->record_count(s));
    const auto original_ids=stock->stock_section(30);
    result.animation_ids=Bytes(original_ids.begin(),original_ids.begin()+(next[29]+1)*2);
    std::string previous;
    std::map<std::pair<unsigned,std::string>,unsigned> shared_textures;
    std::set<std::string> presentation_resources;
    std::size_t presentation_bytes=0;
    const auto retain=[&](std::string key,std::size_t bytes) {
        if(!presentation_resources.insert(std::move(key)).second)return;
        if(bytes>MaxCharacterPresentationBytes-presentation_bytes)
            throw Error("Enabled characters exceed the safe retained portrait/audio memory budget. Reduce this collection or use lighter assets; installed mods are retained.");
        presentation_bytes+=bytes;
    };
    for(const auto& character:characters) {
        if(validate_prepared_character(character,*stock)!=character.root.content_id)
            throw Error("Prepared character content identity does not match its dependencies.");
        // All character dependencies are copied into an append-only namespace.
        // Validation below proves native header/stage compatibility against the
        // selected revision; original source IDs never address target textures.
        if(character.base_fingerprint!=revision_fingerprint(character.source_revision) || character.root.content_id.size()!=64)
            throw Error("Character assets have invalid original Game Pak provenance.");
        if(character.root.content_id==previous)continue;
        previous=character.root.content_id;
        const auto& portrait=character.records.at({4,character.root.portrait});
        const auto decoded_portrait=portrait[0x1d]?inflate_asset(View(portrait).subspan(32),4*MiB):Bytes{};
        // Include display-list/palette/alignment overhead, not just packed PNG
        // or ROM bytes. Banks stay resident for previously queued audio.
        retain("portrait:"+sha256(portrait),2*(decoded_portrait.empty()?portrait.size():decoded_portrait.size())+1024);
        retain("select:"+sha256(character.audio.control)+sha256(character.audio.samples),character.audio.control.size()+64);
        if(!character.race_audio.control.empty())retain("race:"+sha256(character.race_audio.control)+sha256(character.race_audio.samples),character.race_audio.control.size()+64);
        std::map<std::pair<unsigned,unsigned>,unsigned> remap;
        std::set<AssetKey> reused;
        for(const auto& [key,bytes]:character.records)if(key.first!=32) {
            if(key.first!=2 && key.first!=4 && key.first!=12 && key.first!=29 && key.first!=34)throw Error("Unowned additive character section.");
            // Sprite frame ranges must remain contiguous. Only 3D textures
            // are interned here; each 2D frame sequence retains its own IDs.
            if(key.first==2) {
                const auto hash=std::pair{key.first,sha256(bytes)};
                if(const auto found=shared_textures.find(hash);found!=shared_textures.end()) {remap[key]=found->second;reused.insert(key);continue;}
                shared_textures.emplace(hash,next[key.first]);
            }
            if(next[key.first]>=32767)throw Error("Character asset namespace is full.");
            remap[key]=next[key.first]++;
        }
        for(const auto& [key,bytes]:character.records) {
            if(key.first==32 || reused.contains(key))continue; // Animation ranges are owned by each copied model.
            Bytes copy=bytes;
            if(key.first==12) {
                const auto textures=inspect_sprite_textures(bytes);
                if(textures.empty())throw Error("Character sprite has no texture frames.");
                const auto first=remap.at({4,textures[0]});
                for(unsigned n=0;n<textures.size();++n)
                    if(remap.at({4,textures[n]})!=first+n)throw Error("Character sprite's relocated frame range is not contiguous.");
                put16(copy,0,first);
            } else if(key.first==29) {
                copy=inflate_asset(bytes,4*MiB);
                const auto offset=be32(copy,0);
                const auto count=be16(copy,0x22);
                for(unsigned i=0;i<count;++i)put32(copy,offset+8*i,remap.at({2,be32(copy,offset+8*i)}));
                copy=pack_model(copy);inspect_model_textures(copy);
                for(auto animation:character.model_animations.at(key.second)) {
                    const auto& packed=character.records.at({32,animation});
                    result.additions.emplace(std::pair{32,next[32]++},align_animation_dma(packed));
                }
                append16(result.animation_ids,next[32]);
            } else if(key.first==34) {
                for(auto [field,count]:{std::pair{0x10U,unsigned(copy[0x55])},std::pair{0x14U,unsigned(copy[0x56])}}) {
                    const auto offset=be32(copy,field);
                    for(unsigned i=0;i<count;++i) {
                        const auto id=be32(copy,offset+4*i);
                        if(id!=0xffffffff)put32(copy,offset+4*i,remap.at({field==0x10?(copy[0x53]==0?29U:12U):34U,id}));
                    }
                }
            }
            result.additions.emplace(std::pair{key.first,remap.at(key)},std::move(copy));
        }
        AllocatedCharacter slot{character.root.content_id,character.root.name,character.root.base_character,
            remap.at({4,character.root.portrait}),{}};
        for(unsigned i=0;i<3;++i)slot.headers[i]=remap.at({34,character.root.headers[i]});
        slot.stage_header=remap.at({34,character.stage_header});
        slot.stage_source_header=character.stage_header;
        slot.audio=character.audio;
        slot.race_audio=character.race_audio;
        // Presentation provenance is computed from verified records, not a mod
        // name or a donor guess. Never change content IDs or isolated-save paths.
        slot.custom_portrait=character_portrait_changed(portrait,stock->record(4,character.root.portrait));
        slot.portrait_identity=sha256(portrait);
        slot.selection_audio_identity=sha256(character.audio.control)+sha256(character.audio.samples);
        slot.race_audio_identity=sha256(character.race_audio.control)+sha256(character.race_audio.samples);
        if(!slot.race_audio.control.empty())validate_character_race_audio(slot.race_audio);
        result.characters.push_back(std::move(slot));
    }
    // AssetBank owns the final table/count/size checks, even for callers that
    // construct a preparation result directly rather than using the worker.
    std::string roster_ids;for(const auto& c:result.characters)roster_ids+=c.id;
    result.roster_identity=sha256(View(reinterpret_cast<const std::uint8_t*>(roster_ids.data()),roster_ids.size()));
    const auto composite=result.apply(stock);
    const auto caps=legacy_asset_cache_capacities(true,static_cast<unsigned>(composite->record_count(4)),static_cast<unsigned>(composite->record_count(2)),
        static_cast<unsigned>(composite->record_count(12)),static_cast<unsigned>(composite->record_count(29)));
    auto metadata=std::size_t(caps[0])*8+std::size_t(caps[1])*8+std::size_t(caps[2])*8+std::size_t(caps[3])*4;
    // Native initialization retains these expanded lookup tables as well as
    // the cache arrays. Account for their additional guest bytes, not only
    // presentation resources, before admitting a large collection.
    for(unsigned section:{2U,4U,12U,29U,34U}) {
        const auto table=section<29?section+1:section-1;
        // AssetDirectory emits zero + one end offset per record + sentinel.
        const auto expanded=(composite->record_count(section)+2)*4,original=stock->stock_section(table).size();
        if(expanded>original)metadata+=expanded-original;
    }
    if(result.animation_ids.size()>original_ids.size())metadata+=result.animation_ids.size()-original_ids.size();
    if(metadata>MaxCharacterPresentationBytes-presentation_bytes)
        throw Error("Enabled characters exceed the safe portrait/audio/cache memory budget. Reduce this collection or use lighter assets; installed mods are retained.");
    return result;
}
std::string validate_prepared_character(const PreparedCharacter& value,const AssetBank& stock) {
    constexpr unsigned roots[]{2,3,4,5,6,7,8,9,1,0};
    const auto base=value.root.base_character;
    if(base>=10 || !stock.digest().empty() || stock.augmented() ||
       value.base_fingerprint!=revision_fingerprint(value.source_revision) ||
       value.records.empty() || value.records.size()>1024 || !value.root.blockers.empty())
        throw Error("Invalid prepared character provenance or dependency budget.");
    for(unsigned vehicle=0;vehicle<3;++vehicle)
        if(value.root.headers[vehicle]!=roots[base]+vehicle*10)throw Error("Character vehicle root does not match its inherited behaviour.");
    std::set<AssetKey> reached;std::set<unsigned> active,models;
    std::size_t bytes=0;
    const auto record=[&](unsigned section,unsigned id)->View {
        const auto found=value.records.find({section,id});
        if(found==value.records.end())throw Error("A prepared character dependency is missing.");
        return found->second;
    };
    std::function<void(unsigned,unsigned)> visit=[&](unsigned section,unsigned id) {
        if(section==34 && active.contains(id))throw Error("Prepared attachment cycle.");
        if(!reached.insert({section,id}).second)return;
        const auto data=record(section,id);
        if(data.empty() || data.size()>16*MiB-bytes)throw Error("Prepared character exceeds its byte budget.");
        bytes+=data.size();
        if(section==2 || section==4)validate_texture_record(data);
        else if(section==12)for(auto texture:inspect_sprite_textures(data))visit(4,texture);
        else if(section==29) {
            models.insert(id);
            for(auto texture:inspect_model_textures(data))visit(2,texture);
            const auto raw=inflate_asset(data,4*MiB);
            const auto found=value.model_animations.find(id);
            if(found==value.model_animations.end() || found->second.size()>128)throw Error("Prepared model animation ranges are invalid.");
            for(auto animation:found->second) {
                validate_character_animation(record(32,animation),be16(raw,0x4a));visit(32,animation);
            }
        } else if(section==34) {
            slice(data,0,0x78);
            if(id>=stock.record_count(34) || !std::ranges::equal(data,stock.record(34,id)) || (data[0x53]!=0 && data[0x53]!=2))
                throw Error("Prepared object behaviour/header is not supported.");
            active.insert(id);
            for(auto model:header_ids(data,0x10,data[0x55]))if(model!=0xffffffff)visit(data[0x53]==0?29:12,model);
            for(auto header:header_ids(data,0x14,data[0x56]))if(header!=0xffffffff)visit(34,header);
            active.erase(id);
        } else if(section!=32)throw Error("Prepared character contains an unowned asset section.");
    };
    for(auto header:value.root.headers)visit(34,header);
    visit(4,value.root.portrait);
    visit(34,value.stage_header);
    validate_stage(value,[&](unsigned s,unsigned i){return stock.record(s,i);});
    validate_character_audio(value.audio);
    if(reached.size()!=value.records.size() || models.size()!=value.model_animations.size())
        throw Error("Prepared character contains unrelated resources or animation ranges.");
    std::string canonical="dkr-character-assets-v2:"+value.source_revision+":"+std::to_string(base);
    canonical+=":"+character_audio_identity(value.audio);
    for(const auto& [key,data]:value.records) {
        const auto normalized=(key.first==29 || key.first==32)?inflate_asset(data,4*MiB):data;
        canonical+=":"+std::to_string(key.first)+","+std::to_string(key.second)+","+sha256(normalized);
    }
    for(const auto& [model,animations]:value.model_animations) {
        canonical+=":model,"+std::to_string(model);
        for(auto animation:animations)canonical+=","+std::to_string(animation);
    }
    return sha256(View(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size()));
}
std::shared_ptr<const AssetBank> CharacterNamespace::apply(std::shared_ptr<const AssetBank> bank) const {
    if(!bank || bank->base_fingerprint()!=base_fingerprint)throw Error("Character namespace does not match this bank's original Game Pak.");
    return AssetBank::augment(std::move(bank),additions,animation_ids);
}
} // namespace dkr::mods

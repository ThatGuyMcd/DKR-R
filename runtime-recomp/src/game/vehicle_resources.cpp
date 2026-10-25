#include "vehicle_resources.hpp"
#include "vehicle_seat_alignment.hpp"
#include "vehicle_base_hashes.hpp"
#include "vehicle_character_masks.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <set>

namespace dkr::mods::vehicles {
namespace {
// Reviewed batch ownership in the VERIFIED US asset bank. No material-name
// heuristics are applied to user models. Recognize complete original geometry
// independently of its UVs, or use a separately reviewed digest-bound mask for
// rebuilt exports. Unknown ownership retains Vanilla, never guessed driver cuts.
// Mixed driver/car batches below are refined per triangle, especially T.T.'s
// gloves and Tiptup/Bumper's shared underside material.
const std::map<unsigned,std::vector<unsigned>> VehicleBatches{
    {43,{19,20,21,22,23,24,25,26,28}},{44,{9,21,22,23,24,25,26,27,29}},
    {45,{5,16,17,18,19,20,21,22}},{46,{0,11,12,13,14,15}},{47,{3,11,12}},
    {31,{11,12,24,25,26,27,28,29,30,31}},{32,{1,18,19,20,21,22,23,24,25}},
    {33,{1,18,19,20,21,22,23,24}},{34,{1,16,17,18,19,20}},{35,{1,4,10}},
    {37,{17,18,19,20,21,22,23,24,26,27}},{38,{14,15,16,17,18,19,20,22,23}},
    {39,{7,13,14,15,16,17,18,20}},{40,{0,6,11,12,13,14}},{41,{0,6,10}},
    {55,{14,15,19,20,21,22,23,24,25,26}},{56,{1,17,18,19,20,21,22,23,24}},
    {57,{1,16,17,18,19,20,21,22}},{58,{1,15,16,17,18,19}},{59,{0,13,14}},
    {49,{22,23,28,29,30,31,32,33,34,35}},{50,{11,27,28,29,30,31,32,33,34}},
    {51,{4,17,21,22,23,25,26,27}},{52,{4,17,18,20,21,22}},{53,{2,14,15}},
    {61,{22,23,24,25,26,27,28,29,30,31}},{62,{14,21,22,23,24,25,26,27,28}},
    {63,{16,18,19,20,21,22,23,24}},{64,{5,14,15,16,17,18}},{65,{0,2,12}},
    {67,{4,5,29,30,31,32,33,34,35,36}},{68,{2,23,24,25,26,27,28,29,30}},
    {69,{0,10,17,18,19,20,21,22}},{70,{0,13,14,15,16,17}},{71,{0,12,13}},
    {73,{0,1,16,17,18,19,20,21,22,23,24}},{75,{0,1,9,16,17,18,19,20,21}},
    {76,{3,10,11,12,13,14}},{77,{3,10,11}},
    {25,{0,1,17,18,19,20,21,22,23,24}},{26,{0,14,15,16,17,18,19,20,21,22,23}},
    {27,{0,13,14,15,16,17,18,19,20,21}},{28,{0,1,10,11,12,13,14,15,16}},{29,{0,8,9}},
    {19,{18,19,20,21,22,23,24,25,28,29}},{20,{17,18,19,20,21,22,23,24,27}},
    {21,{15,16,17,18,19,20,21,24}},{22,{13,14,15,16,17,18}},{23,{10,11,12}}
};
[[noreturn]] void bad(const std::string& s){throw Error("Custom vehicle resources: "+s);}
struct Model {
    Bytes raw;
    unsigned vertices=0,triangles=0,batches=0;
    std::vector<std::string> faces;
    std::vector<std::string> geometry;
    std::vector<unsigned> owners;
};
Model model(const AssetBank& bank,unsigned id) {
    Model out;out.raw=inflate_asset(bank.record(29,id),4*MiB);const View raw=out.raw;slice(raw,0,0x80);
    out.vertices=be16(raw,0x24);out.triangles=be16(raw,0x26);out.batches=be16(raw,0x28);
    const unsigned nt=be16(raw,0x22),vp=be32(raw,4),tp=be32(raw,8),bp=be32(raw,0x38),textures=be32(raw,0);
    if(!out.vertices||out.vertices>8192||out.triangles>8192||out.batches>1024||nt>255)bad("driver model exceeds its profile limits.");
    slice(raw,vp,out.vertices*10);slice(raw,tp,out.triangles*16);slice(raw,bp,(out.batches+1)*12);slice(raw,textures,nt*8);
    out.faces.resize(out.triangles);out.geometry.resize(out.triangles);out.owners.resize(out.triangles,UINT32_MAX);
    for(unsigned b=0;b<out.batches;++b) {
        const unsigned at=bp+b*12,vs=be16(raw,at+2),ve=be16(raw,at+14),ts=be16(raw,at+4),te=be16(raw,at+16),texture=unsigned(raw[at]);
        if(vs>ve||ve>out.vertices||ts>te||te>out.triangles||(texture!=255&&texture>=nt))bad("driver model has an invalid batch.");
        for(unsigned t=ts;t<te;++t) {
            if(out.owners[t]!=UINT32_MAX)bad("driver model triangles have overlapping owners.");
            out.owners[t]=b;Bytes signature;
            const auto at=tp+t*16;signature.push_back(raw[at]&0x40U); // cull policy, never mutable collision flags
            std::array<std::array<std::uint8_t,6>,3> positions{};
            for(unsigned corner=0;corner<3;++corner) {
                const auto index=raw[at+1+corner];if(index>=ve-vs)bad("driver model indexes outside its batch.");
                const auto vertex=slice(raw,vp+(vs+index)*10,6),uv=slice(raw,at+4+corner*4,4);
                signature.insert(signature.end(),vertex.begin(),vertex.end());signature.insert(signature.end(),uv.begin(),uv.end());
                std::copy(vertex.begin(),vertex.end(),positions[corner].begin());
            }
            out.faces[t]=sha256(signature);
            std::sort(positions.begin(),positions.end());Bytes geometry;
            for(const auto& p:positions)geometry.insert(geometry.end(),p.begin(),p.end());
            out.geometry[t]=sha256(geometry);
        }
    }
    if(std::ranges::any_of(out.owners,[](unsigned b){return b==UINT32_MAX;}))bad("driver model has unowned triangles.");
    return out;
}
std::string ownership_digest(const Model& m) {
    // Asset namespace allocation changes ONLY the numeric texture IDs in the
    // native model. Exclude those relocations, not geometry, UVs, animation map,
    // material layout, counts, batch ownership, flags or other model bytes.
    auto normalized=m.raw;const auto tx=be32(normalized,0);const auto count=be16(normalized,0x22);
    for(unsigned i=0;i<count;++i)std::fill_n(normalized.begin()+tx+i*8,4,0);
    return sha256(normalized);
}
std::vector<std::uint8_t> mask(const Model& m,unsigned id) {
    const auto found=VehicleBatches.find(id);if(found==VehicleBatches.end())bad("unreviewed base car model.");
    std::vector<std::uint8_t> result(m.triangles);
    for(const auto batch:found->second)if(batch>=m.batches)bad("base car batch profile changed.");
    for(unsigned i=0;i<m.triangles;++i)result[i]=std::ranges::find(found->second,m.owners[i])!=found->second.end();
    const auto preserve=[&](unsigned first,unsigned end){if(end>result.size())bad("base car triangle profile changed.");std::fill(result.begin()+first,result.begin()+end,0);};
    if(id==26){preserve(151,157);preserve(159,165);}
    if(id==27){preserve(111,116);preserve(118,123);}
    if(id==28)preserve(93,99);
    if(id==40)preserve(15,21);
    // Bumper's low-detail cap shares the same underside batch as the spoiler.
    if(id==35)preserve(11,13);
    // Tiptup's low-detail shell keeps the front portion of this mixed batch.
    if(id==41)preserve(18,22);
    if(std::count(result.begin(),result.end(),std::uint8_t(1))<8)bad("base car profile contains too few vehicle faces.");
    return result;
}
std::vector<unsigned> models(const AssetBank& bank,unsigned header) {
    const auto h=bank.record(34,header);slice(h,0,0x78);
    const unsigned n=h[0x55],at=be32(h,0x10);
    if(h[0x53]||!n||n>32)bad("a custom character has no compatible car LOD list.");
    slice(h,at,n*4);std::vector<unsigned> ids;
    for(unsigned i=0;i<n;++i)ids.push_back(be32(h,at+i*4));return ids;
}
Image thumbnail(const Image& source) {
    Image out{64,64,Bytes(64*64*4,0)};
    const float factor=std::min(64.F/source.width,64.F/source.height);
    const auto w=std::max(1U,unsigned(source.width*factor)),h=std::max(1U,unsigned(source.height*factor));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
        const auto sx=std::min(source.width-1,unsigned(float(x)/factor)),sy=std::min(source.height-1,unsigned(float(y)/factor));
        std::copy_n(source.rgba.begin()+(sy*source.width+sx)*4,4,out.rgba.begin()+(((64-h)/2+y)*64+(64-w)/2+x)*4);
    }
    return out;
}
Image portrait_tile(const Image& source,unsigned half) {
    // The original vehicle portrait is two 64x32 RGBA16 tiles. A single
    // 64x64 tile exceeds TMEM and repeats/corrupts the image's lower half.
    Image tile{64,32,Bytes(64*32*4)};
    std::copy_n(source.rgba.begin()+half*64*32*4,tile.rgba.size(),tile.rgba.begin());return tile;
}
} // namespace
const DriverProfile* Resources::driver(unsigned header)const {
    const auto it=std::find_if(drivers.begin(),drivers.end(),[&](const auto& d){return d.header==header;});return it==drivers.end()?nullptr:&*it;
}
const DriverProfile* Resources::character(std::string_view id,unsigned base)const {
    const auto it=std::find_if(drivers.begin(),drivers.end(),[&](const auto& d){return d.character_id==id&&d.base==base;});return it==drivers.end()?nullptr:&*it;
}
std::shared_ptr<const AssetBank> Resources::apply(std::shared_ptr<const AssetBank> bank)const {return AssetBank::append_artwork(std::move(bank),artwork);}
std::shared_ptr<const Resources> prepare_resources(std::shared_ptr<const AssetBank> original,std::shared_ptr<const AssetBank> boot,
        std::shared_ptr<const CharacterNamespace> characters,Frozen frozen) {
    if(!original||!boot||original->base_fingerprint()!=boot->base_fingerprint())bad("car resources require the admitted original Game Pak.");
    auto out=std::make_shared<Resources>();out->library=std::move(frozen);if(out->library.packages.empty())return out;
    constexpr unsigned headers[]{2,3,4,5,6,7,8,9,1,0};
    constexpr const char* names[]{"Krunch","Bumper","Tiptup","Conker","Timber","Banjo","Drumstick","Pipsy","T.T.","Diddy"};
    std::array<std::array<Model,5>,10> originals;
    std::array<std::array<std::vector<std::uint8_t>,5>,10> masks;
    for(unsigned base=0;base<10;++base) {
        const auto list=models(*original,headers[base]);
        if(list.size()<5)bad("the selected native driver has an unreviewed LOD layout.");
        DriverProfile p;p.base=base;p.header=headers[base];p.name=names[base];p.native_scale=std::bit_cast<float>(be32(original->record(34,p.header),0x0c));
        if(!std::isfinite(p.native_scale)||p.native_scale<=0||p.native_scale>16)bad("invalid base car scale.");
        for(unsigned lod=0;lod<5;++lod) {
            auto& m=originals[base][lod];m=model(*original,list[lod]);
            const auto known=BaseModelHashes.find(list[lod]);
            if(known==BaseModelHashes.end()||sha256(m.raw)!=known->second)
                bad("the selected native driver model variant has not been reviewed for safe car replacement.");
            masks[base][lod]=mask(m,list[lod]);
            p.lods[lod]={list[lod],m.vertices,m.triangles,masks[base][lod],sha256(m.raw)};
        }
        out->drivers.push_back(std::move(p));
    }
    if(characters)for(const auto& character:characters->characters) {
        DriverProfile p;p.base=character.base_character;p.header=character.headers[0];p.character_id=character.id;p.name=character.name;
        if(p.base>=10)bad("invalid inherited driver.");
        try {
            p.native_scale=std::bit_cast<float>(be32(boot->record(34,p.header),0x0c));
            if(!std::isfinite(p.native_scale)||p.native_scale<=0||p.native_scale>16)bad("invalid custom driver scale.");
            const auto list=models(*boot,p.header);
            for(unsigned lod=0;lod<5;++lod) {
                const auto model_id=list[std::min<std::size_t>(lod,list.size()-1)];
                const auto candidate=model(*boot,model_id);
                std::map<std::string,std::vector<unsigned>> available;
                for(unsigned i=0;i<candidate.triangles;++i)available[candidate.faces[i]].push_back(i);
                std::vector<std::uint8_t> removed(candidate.triangles,0);
                bool matched=false;
                // Individually reviewed re-exported car geometry: masks are
                // immutable, relocatable and exact to this complete model.
                if(const auto reviewed=ReviewedCharacterMasks.find(ownership_digest(candidate));reviewed!=ReviewedCharacterMasks.end()) {
                    const auto& r=reviewed->second;
                    if(r.vertices!=candidate.vertices||r.triangles!=candidate.triangles)bad("reviewed character model counts changed.");
                    for(const auto& [first,end]:r.ranges) {
                        if(first>=end||end>removed.size())bad("reviewed character mask exceeds its model.");
                        for(unsigned i=first;i<end;++i){if(removed[i])bad("reviewed character mask overlaps.");removed[i]=1;}
                    }
                    // A driver-only distant proxy legitimately has no chassis.
                    if(std::ranges::all_of(removed,[](auto v){return v!=0;}))bad("reviewed mask does not retain the driver.");
                    matched=true;
                }
                // Some valid character exports reuse one detailed model in
                // several LOD slots. Match explicit geometry ownership against
                // every reviewed donor LOD, not the slot number or material.
                for(unsigned donor_lod=0;donor_lod<5&&!matched;++donor_lod) {
                    const auto& source=originals[p.base][donor_lod];const auto& source_mask=masks[p.base][donor_lod];
                    std::map<std::string,unsigned> required;
                    for(unsigned i=0;i<source.triangles;++i)if(source_mask[i])++required[source.faces[i]];
                    bool exact=true;
                    for(const auto& [signature,count]:required) {
                        const auto it=available.find(signature);if(it==available.end()||it->second.size()!=count){exact=false;break;}
                    }
                    if(!exact)continue;
                    for(const auto& [signature,count]:required)for(const auto index:available.at(signature))removed[index]=1;
                    matched=true;
                }
                // Retextured/reordered stock geometry is still the same car.
                // Some exports use another driver's chassis with inherited
                // behaviour. Require a COMPLETE unambiguous donor, never a
                // percentage threshold or blanket static-vertex suppression.
                if(!matched) {
                    std::map<std::string,std::vector<unsigned>> geometry;
                    for(unsigned i=0;i<candidate.triangles;++i)geometry[candidate.geometry[i]].push_back(i);
                    for(unsigned base=0;base<10&&!matched;++base)for(unsigned donor_lod=0;donor_lod<5&&!matched;++donor_lod) {
                        const auto& source=originals[base][donor_lod];const auto& owned=masks[base][donor_lod];
                        std::map<std::string,unsigned> required;std::set<std::string> driver;
                        for(unsigned i=0;i<source.triangles;++i)if(owned[i])++required[source.geometry[i]];else driver.insert(source.geometry[i]);
                        bool exact=true;
                        for(const auto& [signature,count]:required) {
                            const auto it=geometry.find(signature);
                            if(driver.contains(signature)||it==geometry.end()||it->second.size()!=count){exact=false;break;}
                        }
                        if(!exact)continue;
                        for(const auto& [signature,count]:required)for(const auto i:geometry.at(signature))removed[i]=1;
                        matched=true;
                    }
                }
                if(!matched)throw Error("Car/driver ownership is not yet recognized at LOD "+std::to_string(lod)+". Original vehicle retained.");
                p.lods[lod]={model_id,candidate.vertices,candidate.triangles,std::move(removed),sha256(candidate.raw)};
            }
        }catch(const std::exception& e){p.issue=e.what();}
        std::fprintf(stderr,"[custom-vehicles][character] name=%s id=%s header=%u base=%u status=%s%s%s\n",
            p.name.c_str(),p.character_id.c_str(),p.header,p.base,p.compatible()?"ready":"vanilla-only",
            p.compatible()?"":" reason=",p.issue.c_str());
        out->drivers.push_back(std::move(p));
    }
    unsigned next=unsigned(boot->record_count(4));
    for(const auto& p:out->library.packages) {
        if(next>32765)bad("the portrait namespace is full.");
        std::size_t bytes=0;
        float radius=0,pivot_radius=0;
        for(const auto& lod:p->lods)for(const auto& mesh:lod.meshes)for(const auto& source:mesh.vertices) {
            const auto v=dkr::runtime::vehicles::seating::source_point(*p,source);
            radius=std::max(radius,std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z));
        }
        for(unsigned w=0;w<4;++w)if(!p->wheels[w].empty()) {
            const auto v=dkr::runtime::vehicles::seating::source_point(*p,p->pivots[w]);
            pivot_radius=std::max(pivot_radius,std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z));
        }
        const auto units=std::min(64.F,30000.F/std::max(1.F,(radius+2*pivot_radius)*p->scale));
        if(!std::isfinite(units)||units<=0)bad("car vertex precision cannot be represented safely.");
        out->vertex_units.push_back(units);
        std::vector<MaterialPixels> pixels;
        for(const auto& t:p->textures) {
            const auto size=std::size_t(t.image.width)*t.image.height*2;
            if(t.image.width>128||t.image.height>128||size>4096)bad("car textures must fit a native 4 KiB material tile (for example 32x32 or 64x32).");
            // RGBA32 stores RG in the lower 2 KiB bank and BA in the upper
            // bank. Account for each padded row, not just the texel count.
            // All supplied native-4kib-rgba32-safe exports fit unchanged.
            // Keep the old RGBA16 path for larger already-supported tiles;
            // never resize images, expand TMEM limits or change menu artwork.
            const auto bank_row=(std::size_t(t.image.width)*2+7)&~std::size_t(7);
            const bool rgba32=bank_row*t.image.height<=2048;
            if(rgba32)pixels.push_back({t.image.rgba,true});
            else {
                auto encoded=native_texture(t.image);
                pixels.push_back({Bytes(encoded.begin()+32,encoded.begin()+32+size),false});
            }
            bytes+=pixels.back().bytes.size();
        }
        if(bytes>256*1024)bad("a car exceeds its per-racer texture budget.");
        out->texture_pixels.push_back(std::move(pixels));
        const auto portrait=thumbnail(p->portrait);out->portraits.push_back(next);
        for(unsigned half=0;half<2;++half)
            out->artwork.emplace(AssetKey{4,next++},native_texture(portrait_tile(portrait,half)));
    }
    const auto ready=std::count_if(out->drivers.begin(),out->drivers.end(),[](const auto& d){return d.compatible();});
    std::fprintf(stderr,"[custom-vehicles][compatibility] reviewed-character-ownership=v1 ready=%zu total=%zu; all-five-lods; no material-name cuts\n",std::size_t(ready),out->drivers.size());
    std::string identity="dkr-car-resources-v5-source-colour-fixed-seats:"+boot->fingerprint()+":"+out->library.identity;
    for(const auto& d:out->drivers) {
        identity+=':'+std::to_string(d.header)+":"+d.character_id+":"+d.issue+":"+std::to_string(std::bit_cast<std::uint32_t>(d.native_scale));
        if(d.compatible())for(const auto& lod:d.lods)identity+=':'+lod.identity+":"+sha256(lod.hide);
    }
    for(const auto& [key,value]:out->artwork)identity+=':'+std::to_string(key.second)+":"+sha256(value);
    out->identity=sha256(View(reinterpret_cast<const std::uint8_t*>(identity.data()),identity.size()));return out;
}
} // namespace dkr::mods::vehicles

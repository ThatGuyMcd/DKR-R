#include "local_scenery.hpp"
#include "experimental_performance.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr std::uint32_t kRamBytes=0x800000U;
struct Reader {
    std::span<const std::uint8_t> ram;
    std::uint32_t physical(std::uint32_t p,std::size_t bytes,unsigned alignment=1) const {
        // Asset structures use KSEG0; embedded display lists use physical DMA.
        if(p>=0x80000000U&&p<0x80800000U)p-=0x80000000U;
        if(p>=kRamBytes||bytes>kRamBytes-p||(p&(alignment-1)))throw std::runtime_error("local scenery span");
        return p;
    }
    std::uint32_t word(std::uint32_t p) const {
        p=physical(p,4,4);std::uint32_t v;std::memcpy(&v,ram.data()+p,4);return v;
    }
    std::uint16_t half(std::uint32_t p) const {
        p=physical(p,2,2);std::uint16_t v;std::memcpy(&v,ram.data()+(p^2U),2);return v;
    }
    std::uint8_t byte(std::uint32_t p) const {return ram[physical(p,1)^3U];}
    float scalar(std::uint32_t p) const {const auto v=std::bit_cast<float>(word(p));if(!std::isfinite(v))throw std::runtime_error("local scenery transform");return v;}
    std::array<std::uint32_t,2> command(std::uint32_t p) const {return {word(p),word(p+4)};}
};
bool texture_opcode(std::uint8_t op) {
    // Only texture uploads/tiles and their syncs. No framebuffer, depth image,
    // full sync, branches, native calls or unrestricted command interpreter.
    return op==0xE6||op==0xE7||op==0xE8||op==0xF0||op==0xF2||
           op==0xF3||op==0xF4||op==0xF5||op==0xFD;
}
void validate_upload(const LocalSceneryTexture& tex) {
    struct Tile { unsigned size=0,format=0,line=0; };
    std::array<Tile,8> tiles{};
    std::size_t image=0;
    unsigned size=0,width=0;
    bool have_image=false;
    for(const auto& c:tex.commands) {
        const auto op=c[0]>>24;
        const auto tile=(c[1]>>24)&7U;
        if(op==0xFD) {
            image=c[1];size=(c[0]>>19)&3U;width=(c[0]&4095U)+1;
            have_image=true;
        } else if(op==0xF5) {
            tiles[tile]={(c[0]>>19)&3U,(c[0]>>21)&7U,(c[0]>>9)&511U};
        } else if(op==0xF0||op==0xF3||op==0xF4) {
            if(!have_image)throw std::runtime_error("local scenery upload without image");
            const std::size_t uls=(c[0]>>12)&4095U,ult=c[0]&4095U;
            const std::size_t lrs=(c[1]>>12)&4095U,lrt=c[1]&4095U;
            const auto& t=tiles[tile];
            if(t.format>4||(t.size!=size&&op!=0xF0))throw std::runtime_error("local scenery upload format");
            std::size_t start=0,extent=0;
            if(op==0xF3) {
                if(uls>lrs||lrs>=2048)throw std::runtime_error("local scenery block bounds");
                start=((uls<<size)>>1)+((std::size_t(width)<<size)>>1)*ult;
                extent=(((lrs-uls)>>(4-t.size))+1)*8;
            } else {
                if(uls>lrs||ult>lrt)throw std::runtime_error("local scenery tile bounds");
                const auto stride=(std::size_t(width)<<size)>>1;
                start=((uls>>2)<<size>>1)+stride*(ult>>2);
                const auto rows=1+(lrt>>2)-(ult>>2);
                const auto words=op==0xF0?1+(lrs>>2)-(uls>>2):(((lrs>>2)-(uls>>2))>>(4-t.size))+1;
                extent=(rows-1)*stride+words*(op==0xF0?2:8);
                if(op==0xF0&&(size!=2||t.size!=0))throw std::runtime_error("local scenery palette format");
            }
            if(image<32||image>tex.bytes.size()||start>tex.bytes.size()-image||
               extent>tex.bytes.size()-image-start)
                throw std::runtime_error("local scenery upload outside owned texture");
        }
    }
}
LocalScenerySprite read_sprite(const Reader& r,std::uint32_t object,std::uint32_t material_table) {
    r.physical(object,0x6C,4);
    const auto header=r.word(object+0x40);r.physical(header,0x60,4);
    // BHV_SCENERY, MODEL_TYPE_SPRITE, NO_MULTIPLAYER only. Do not accidentally
    // cache collected balloons, weapons, cutscene actors or other freed objects.
    if(r.half(object+0x48)!=0||r.byte(header+0x53)!=1||!(r.half(header+0x30)&0x40U))
        throw std::runtime_error("not removed retail scenery");
    const auto model=r.byte(object+0x3A),models=r.byte(header+0x55);
    if(!models||models>32||model>=models)throw std::runtime_error("local scenery model index");
    const auto instances=r.word(object+0x68);r.physical(instances,models*4,4);
    const auto sprite=r.word(instances+model*4);r.physical(sprite,12,4);
    const auto frames=r.half(sprite),textures=r.half(sprite+2);
    const auto texture_table=r.word(sprite+8);
    if(!frames||frames>32||!textures||textures>128)throw std::runtime_error("local scenery asset count");
    r.physical(sprite+12,frames*4,4);r.physical(texture_table,textures*4,4);
    LocalScenerySprite out;
    out.identity=r.half(object+0x4A);
    for(unsigned i=0;i<3;++i)out.position[i]=r.scalar(object+12+i*4);
    out.scale=r.scalar(object+8);
    if(out.scale<=0||out.scale>128)throw std::runtime_error("local scenery scale");
    out.roll=std::int16_t(r.half(object+4));out.draw_distance=std::int16_t(r.half(header+0x4E));
    out.segment=std::int16_t(r.half(object+0x2E));
    if(out.segment<-1||out.segment>=127)throw std::runtime_error("local scenery segment index");
    out.transform_flags=r.half(object+6);
    out.animation=r.half(object+0x18);out.flags=r.half(sprite+6);
    // The common table is 16 groups of four entries, NOT 128 entries.
    // Clamp/wrap flags are texture state, never material-table indices.
    if(out.flags&~0x3FU)throw std::runtime_error("local scenery material flags");
    auto flags=(out.flags|2U|8U|(out.transform_flags&0xCU))&0x3FU;
    if(flags&4U)flags|=1U; // Retail anti-aliasing for translucent materials.
    for(unsigned i=0;i<2;++i) {
        out.material[i]=r.command(material_table+flags*16+i*8);
        if((out.material[i][0]>>24)!=(i==0?0xFCU:0xEFU))throw std::runtime_error("local scenery material");
        out.faded_material[i]=r.command(material_table+(flags|5U)*16+i*8);
        if((out.faded_material[i][0]>>24)!=(i==0?0xFCU:0xEFU))throw std::runtime_error("local scenery fade material");
    }
    const auto shading=r.word(object+0x54);
    if(shading) {
        r.physical(shading,8,4);
        const auto intensity=std::uint32_t(std::clamp(r.scalar(shading)*255.0F,0.0F,255.0F));
        out.primitive=(intensity<<24)|(intensity<<16)|(intensity<<8)|255U;
        out.environment=0;
        for(unsigned i=0;i<4;++i)out.environment=(out.environment<<8)|r.byte(shading+4+i);
    }
    std::vector<std::uint32_t> upload_addresses;
    out.textures.reserve(textures);upload_addresses.reserve(textures);
    for(unsigned i=0;i<textures;++i) {
        const auto p=r.word(texture_table+i*4);r.physical(p,32,8);
        // textureSize includes the 32-byte header. Require a bounded loaded,
        // uncompressed frame; the texture command count is independently checked.
        const auto bytes=r.half(p+0x16),count=r.half(p+0xA);
        const auto commands=r.word(p+0xC);
        if(bytes<32||bytes>32768||!count||count>32)throw std::runtime_error("local scenery texture bounds");
        const auto source=r.physical(p,bytes,8);r.physical(commands,count*8,8);
        LocalSceneryTexture tex;tex.bytes.assign(r.ram.begin()+source,r.ram.begin()+source+bytes);
        tex.commands.reserve(count);
        for(unsigned c=0;c<count;++c) {
            auto command=r.command(commands+c*8);const auto op=std::uint8_t(command[0]>>24);
            if(!texture_opcode(op))throw std::runtime_error("local scenery texture opcode");
            if(op==0xFD) {
                const auto image=r.physical(command[1],1);
                if(image>=source+32&&image<source+bytes)command[1]=image-source;
                else {
                    // Retail CI textures keep palettes in gCiPalettes, not in
                    // this texture's allocation. Own only the bounded TLUT
                    // used by this FD; never admit arbitrary external images.
                    if(((command[0]>>21)&7U)!=0||((command[0]>>19)&3U)!=2||
                       (command[0]&4095U)!=0)
                        throw std::runtime_error("local scenery external image");
                    std::size_t palette_bytes=0;
                    for(unsigned next=c+1;next<count;++next) {
                        const auto load=r.command(commands+next*8);
                        const auto next_op=load[0]>>24;
                        if(next_op==0xFD)break;
                        if(next_op==0xF3||next_op==0xF4)
                            throw std::runtime_error("local scenery external non-palette load");
                        if(next_op==0xF0) {
                            const auto lrs=(load[1]>>12)&4095U;
                            if((load[0]&0xFFFFFFU)||(load[1]&4095U)||(lrs&3U))
                                throw std::runtime_error("local scenery palette rectangle");
                            palette_bytes=std::max(palette_bytes,std::size_t((lrs>>2)+1)*2);
                        }
                    }
                    if(!palette_bytes||palette_bytes>512)
                        throw std::runtime_error("local scenery palette budget");
                    // Word-swapped lanes and TMEM loads may read the padded
                    // final word. Keep its complete aligned source span too.
                    palette_bytes=(palette_bytes+7U)&~std::size_t(7U);
                    const auto palette=r.physical(image,palette_bytes,8);
                    const auto owned=(tex.bytes.size()+7U)&~std::size_t(7U);
                    tex.bytes.resize(owned);
                    tex.bytes.insert(tex.bytes.end(),r.ram.begin()+palette,r.ram.begin()+palette+palette_bytes);
                    command[1]=std::uint32_t(owned);
                }
            }
            tex.commands.push_back(command);
        }
        validate_upload(tex);
        upload_addresses.push_back(r.physical(commands,count*8,8));
        out.textures.push_back(std::make_shared<const LocalSceneryTexture>(std::move(tex)));
    }
    out.frames.resize(frames);
    for(unsigned f=0;f<frames;++f) {
        auto dl=r.word(sprite+12+f*4);
        std::array<LocalSceneryVertex,32> vertices{};std::array<bool,32> valid{};
        int texture=-1;bool ended=false;
        for(unsigned n=0;n<512;++n,dl+=8) {
            const auto command=r.command(dl);const auto op=std::uint8_t(command[0]>>24);
            if(op==0xB8){ended=true;break;}
            if(op==0xE7)continue;
            if(op==0x07) {
                const auto it=std::find(upload_addresses.begin(),upload_addresses.end(),r.physical(command[1],8,8));
                if(it==upload_addresses.end())throw std::runtime_error("local scenery upload reference");
                texture=int(it-upload_addresses.begin());
                if(((command[0]>>16)&255U)!=out.textures[texture]->commands.size())throw std::runtime_error("local scenery upload count");
                continue;
            }
            if(op==0x04) {
                const auto count=((command[0]>>19)&31U)+1;
                const auto dst=1U+((command[0]>>9)&31U);
                if(!(command[0]&0x10000U)||dst+count>32)throw std::runtime_error("local scenery vertex batch");
                const auto source=r.physical(command[1],count*10,2);
                for(unsigned v=0;v<count;++v) {
                    auto& vertex=vertices[dst+v];const auto p=source+v*10;
                    vertex.x=std::int16_t(r.half(p));vertex.y=std::int16_t(r.half(p+2));vertex.z=std::int16_t(r.half(p+4));
                    for(unsigned color=0;color<4;++color)vertex.color[color]=r.byte(p+6+color);
                    valid[dst+v]=true;
                }
                continue;
            }
            if(op!=0x05||texture<0||((command[0]>>20)&15U)!=1U||out.frames[f].tiles.size()>=128)
                throw std::runtime_error("local scenery frame opcode");
            const auto p=r.physical(command[1],32,2);
            LocalSceneryTile tile;tile.texture=std::uint16_t(texture);
            for(unsigned triangle=0;triangle<2;++triangle)for(unsigned corner=0;corner<3;++corner) {
                const auto index=r.byte(p+triangle*16+1+corner);
                if(index>=32||!valid[index])throw std::runtime_error("local scenery triangle index");
                auto& vertex=tile.vertices[triangle*3+corner];vertex=vertices[index];
                vertex.s=std::int16_t(r.half(p+triangle*16+4+corner*4));
                vertex.t=std::int16_t(r.half(p+triangle*16+6+corner*4));
            }
            out.frames[f].tiles.push_back(tile);
        }
        if(!ended||out.frames[f].tiles.empty())throw std::runtime_error("local scenery unterminated frame");
    }
    return out;
}
std::size_t payload_size(const LocalScenerySprite& sprite) {
    std::size_t bytes=sizeof(sprite)+sprite.textures.size()*sizeof(sprite.textures[0])+
        sprite.frames.size()*sizeof(LocalSceneryFrame);
    // Shared textures are accounted once when admitted into the scene below.
    for(const auto& f:sprite.frames)bytes+=f.tiles.size()*sizeof(LocalSceneryTile);
    return bytes;
}
}
void LocalSceneryCapture::reset(std::uint32_t scene) noexcept {
    scene_.reset();rejected_=0;last_failure_.fill(0);
    try {scene_=std::make_shared<LocalSceneryScene>();scene_->scene=scene;scene_->sprites.reserve(32);}catch(...){}
}
bool LocalSceneryCapture::capture(std::span<const std::uint8_t> ram,std::uint32_t object,std::uint32_t table) noexcept {
    performance::Scope timing(performance::Stage::SceneryCapture);
    try {
        if(!scene_||ram.size()<kRamBytes||scene_->sprites.size()>=kMaxSprites)throw std::runtime_error("local scenery scene budget");
        auto sprite=read_sprite({ram},object,table);
        auto bytes=payload_size(sprite);
        for(std::size_t index=0;index<sprite.textures.size();++index) {
            auto& texture=sprite.textures[index];
            std::shared_ptr<const LocalSceneryTexture> existing;
            for(std::size_t prior=0;prior<index;++prior) {
                const auto& source=sprite.textures[prior];
                if(source->bytes==texture->bytes&&source->commands==texture->commands) {existing=source;break;}
            }
            for(const auto& prior:scene_->sprites) {
                if(existing)break;
                for(const auto& source:prior.textures)
                    if(source->bytes==texture->bytes&&source->commands==texture->commands) {existing=source;break;}
                if(existing)break;
            }
            if(existing)texture=std::move(existing);
            else bytes+=sizeof(LocalSceneryTexture)+texture->bytes.size()+
                texture->commands.size()*sizeof(texture->commands[0]);
        }
        if(bytes>kMaxPayloadBytes-scene_->payload_bytes)throw std::runtime_error("local scenery payload budget");
        // Construction is exclusive. If an older immutable frame exists,
        // copy-on-write preserves it rather than mutating its retained assets.
        if(scene_.use_count()!=1)scene_=std::make_shared<LocalSceneryScene>(*scene_);
        scene_->sprites.push_back(std::move(sprite));scene_->payload_bytes+=bytes;return true;
    }catch(const std::exception& error){
        ++rejected_;std::strncpy(last_failure_.data(),error.what(),last_failure_.size()-1);
        last_failure_.back()=0;return false;
    }catch(...){
        ++rejected_;std::strncpy(last_failure_.data(),"local scenery capture failed",last_failure_.size()-1);
        last_failure_.back()=0;return false;
    }
}
std::shared_ptr<const LocalSceneryScene> LocalSceneryCapture::freeze(std::uint32_t scene) const noexcept {
    return scene_&&scene_->scene==scene ? scene_:nullptr;
}
}

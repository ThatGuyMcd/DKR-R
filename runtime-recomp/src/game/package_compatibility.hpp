#pragma once
#include "vehicle_package.hpp"
#include "magic_code_package.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>

namespace dkr::mods::vehicles::original_textures {
[[noreturn]] inline void fail(const std::string& why){throw Error("Custom vehicle: "+why);}

inline void seating_contract(const Json& manifest,Package& car) {
    if(!manifest.contains("seating_contract"))return;
    const auto& c=manifest.at("seating_contract");
    if(!c.is_object() || c.value("id","")!="dkrkart.per-base-driver" ||
       !c.contains("revision") || !c["revision"].is_number_integer() || c["revision"]!=1)
        fail("unsupported explicit seating contract revision.");
    if(!car.per_driver())fail("the seating contract requires per-base-driver positions.");
    const auto& coords=c.at("coordinate_system");const auto& units=c.at("units");
    if(coords.value("handedness","")!="right" || coords.value("up","")!="+Y" || coords.value("forward","")!="-Z" ||
       units.value("driver_vertices","")!="unscaled native DKR model units, before ObjectHeader scale" ||
       units.value("driver_anchors","")!="same unscaled native DKR model units" ||
       units.value("driver_seat_positions","")!="Studio placement units" ||
       !units.at("native_model_to_studio").is_number() || units.at("native_model_to_studio")!=1.0 ||
       units.at("object_header_scale_applied")!=false)
        fail("unsupported seating coordinate units.");
    const auto& driver=c.at("driver");
    if(driver.value("position_source","")!="driver_seat_positions[base_slot]" ||
       driver.value("anchor_source","")!="driver_anchors[base_slot]" ||
       driver.value("rotation_source","")!="settings.driver_yaw" ||
       driver.value("rotation_units","")!="degrees" || driver.value("rotation_axis","")!="+Y" ||
       driver.value("rotation_pivot","")!="driver_anchors[base_slot]")fail("unsupported explicit driver placement.");
    for(const char* field:{"vehicle_scale_applies","vehicle_yaw_applies","vehicle_translation_applies",
        "legacy_settings_driver_applies","legacy_forward_20_applies","legacy_diddy_correction_applies"})
        if(driver.at(field)!=false)fail("the seating contract requests an unsupported driver transform.");
    const auto& table=c.at("anchor_table");
    if(table.value("definition","")!="kart-fixed-base-reference-v1" || table.value("read_values_from","")!="driver_anchors")
        fail("unsupported explicit anchor table.");
    const auto canonical=table.at("canonical_json").get<std::string>();
    if(canonical.empty() || canonical.size()>8192)fail("invalid anchor table encoding.");
    const View bytes(reinterpret_cast<const std::uint8_t*>(canonical.data()),canonical.size());
    if(table.at("sha256")!=sha256(bytes) || code::parse_document(bytes,8192)!=manifest.at("driver_anchors"))
        fail("the seating contract anchor table does not match its hash and values.");
    // Preserve the existing Package ABI. For PerBaseDriverV1 only, an explicit
    // authored anchor flag selects revisioned RAW native units. The old shared-
    // seat calibrated-anchor flag remains distinct through car.per_driver().
    car.authored_anchors=true;
}

inline void effect_contract(const Json& manifest) {
    if(!manifest.contains("effect_anchors"))return;
    const auto& effects=manifest.at("effect_anchors");
    if(!effects.contains("revision") || !effects["revision"].is_number_integer() || effects["revision"]!=1)fail("unsupported effect anchor revision.");
    const auto& boost=effects.at("boost");
    if(!boost.at("enabled").is_boolean())fail("invalid boost anchor setting.");
    if(boost["enabled"].get<bool>())fail("custom boost anchors are not supported yet; export with the native boost position.");
    // Disabled means the native effect position, as declared by the exporter.
}

inline constexpr unsigned Interior=30, MaxTiles=32, MaxMeshes=64, MaxVertices=8192, MaxTriangles=4096;
using Point=std::array<double,5>; // source XYZ and pixel-space ST
struct Cell {unsigned tile=0;double low=0,high=0,origin=0;};
struct Axis {
    unsigned size;bool repeat;
    bool split()const{return size>32;}
    unsigned count()const{return split()?(size+Interior-1)/Interior:1;}
    unsigned start(unsigned i)const{return split()?i*Interior:0;}
    unsigned content(unsigned i)const{return split()?std::min(Interior,size-start(i)):size;}
    unsigned gutter()const{return split()?1:0;}
    unsigned stored(unsigned i)const{return content(i)+2*gutter();}
    std::vector<Cell> cells(double low,double high)const {
        const double inf=std::numeric_limits<double>::infinity();
        if(!split())return {{0,-inf,inf,0}};
        const auto intersects=[&](double a,double b){return low==high ? low>=a && low<b : high>a && low<b;};
        std::vector<Cell> out;
        if(!repeat) {
            for(unsigned i=0;i<count();++i) {
                const double a=i?double(start(i)):-inf,b=i+1<count()?double(start(i)+content(i)):inf;
                if(intersects(a,b))out.push_back({i,a,b,double(start(i))});
            }
        } else {
            const auto first=int(std::floor(low/size));
            const auto last=low==high?first:int(std::ceil(high/size))-1;
            if(last<first || last-first>256)fail("original texture repetition exceeds the preparation budget.");
            for(int period=first;period<=last;++period)for(unsigned i=0;i<count();++i) {
                const double a=double(period)*size+start(i),b=a+content(i);
                if(intersects(a,b))out.push_back({i,a,b,a});
            }
        }
        if(out.empty())fail("original texture coordinates could not be tiled safely.");
        return out;
    }
    unsigned pixel(int p)const {
        if(!repeat)return unsigned(std::clamp(p,0,int(size)-1));
        const int n=p%int(size);return unsigned(n<0?n+int(size):n);
    }
};
inline Point intersection(Point a,Point b,unsigned axis,double edge) {
    // Canonical endpoint order gives adjacent faces the same shared cut.
    if(b<a)std::swap(a,b);
    const auto delta=b[axis]-a[axis];
    if(delta==0)return a;
    const auto t=std::clamp((edge-a[axis])/delta,0.0,1.0);
    Point p{};for(unsigned i=0;i<5;++i)p[i]=a[i]+(b[i]-a[i])*t;
    p[axis]=edge;return p;
}
inline std::vector<Point> clip(std::vector<Point> input,unsigned axis,double edge,bool greater) {
    if(input.empty() || !std::isfinite(edge))return input;
    std::vector<Point> out;out.reserve(input.size()+1);
    auto a=input.back();auto in_a=greater?a[axis]>=edge:a[axis]<=edge;
    for(const auto& b:input) {
        const bool in_b=greater?b[axis]>=edge:b[axis]<=edge;
        if(in_a!=in_b)out.push_back(intersection(a,b,axis,edge));
        if(in_b)out.push_back(b);
        a=b;in_a=in_b;
    }
    out.erase(std::unique(out.begin(),out.end()),out.end());
    if(out.size()>1 && out.front()==out.back())out.pop_back();
    return out;
}
inline bool drawable(const Point& a,const Point& b,const Point& c) {
    double norm=0;
    for(unsigned i=0;i<3;++i){const auto j=(i+1)%3,k=(i+2)%3;
        const auto cross=(b[j]-a[j])*(c[k]-a[k])-(b[k]-a[k])*(c[j]-a[j]);norm+=cross*cross;}
    return norm>1e-24;
}
inline void prepare(Package& car) {
    const auto& source_textures=car.textures;const auto source_count=source_textures.size();
    std::vector<Texture> tiles;std::vector<std::vector<unsigned>> mappings;
    for(const auto& t:source_textures) {
        const Axis sx{t.image.width,t.repeat_s},sy{t.image.height,t.repeat_t};
        if(!sx.size || !sy.size || t.image.rgba.size()!=std::size_t(sx.size)*sy.size*4)fail("invalid original-resolution image storage.");
        if(sx.count()*sy.count()>MaxTiles-tiles.size())fail("original textures require more than 32 safe material tiles. Split or simplify the car materials.");
        std::vector<unsigned> map;
        for(unsigned y=0;y<sy.count();++y)for(unsigned x=0;x<sx.count();++x) {
            Texture tile;tile.repeat_s=!sx.split()&&t.repeat_s;tile.repeat_t=!sy.split()&&t.repeat_t;
            tile.image={sx.stored(x),sy.stored(y),{}};tile.image.rgba.resize(std::size_t(tile.image.width)*tile.image.height*4);
            for(unsigned v=0;v<tile.image.height;++v)for(unsigned u=0;u<tile.image.width;++u) {
                const auto px=sx.pixel(int(sx.start(x)+u)-int(sx.gutter()));
                const auto py=sy.pixel(int(sy.start(y)+v)-int(sy.gutter()));
                std::copy_n(t.image.rgba.data()+(std::size_t(py)*sx.size+px)*4,4,
                    tile.image.rgba.data()+(std::size_t(v)*tile.image.width+u)*4);
            }
            map.push_back(unsigned(tiles.size()));tiles.push_back(std::move(tile));
        }
        mappings.push_back(std::move(map));
    }
    std::array<Lod,6> prepared;
    for(unsigned level=0;level<6;++level) {
        auto& out=prepared[level];unsigned vertices=0,visits=0;
        for(const auto& source:car.lods[level].meshes) {
            if(source.texture<0) {out.meshes.push_back(source);vertices+=unsigned(source.vertices.size());out.triangles+=unsigned(source.faces.size());continue;}
            const auto original=unsigned(source.texture);
            if(original>=source_textures.size())fail("invalid original-resolution material reference.");
            const auto& t=source_textures[original];const Axis sx{t.image.width,t.repeat_s},sy{t.image.height,t.repeat_t};
            if(!sx.split()&&!sy.split()) {auto mesh=source;mesh.texture=int(mappings[original][0]);out.meshes.push_back(std::move(mesh));vertices+=unsigned(source.vertices.size());out.triangles+=unsigned(source.faces.size());continue;}
            struct Group {Mesh mesh;std::map<std::array<std::uint32_t,5>,unsigned> vertices;};
            std::map<unsigned,Group> groups;
            for(const auto& face:source.faces) {
                std::vector<Point> polygon;
                double min_s=std::numeric_limits<double>::infinity(),min_t=min_s,max_s=-min_s,max_t=-min_s;
                for(const auto index:face) {
                    const auto& p=source.vertices.at(index);const auto& uv=source.uv.at(index);
                    Point v{p.x,p.y,p.z,double(uv.x)*sx.size,double(uv.y)*sy.size};polygon.push_back(v);
                    min_s=std::min(min_s,v[3]);max_s=std::max(max_s,v[3]);min_t=std::min(min_t,v[4]);max_t=std::max(max_t,v[4]);
                }
                const auto xs=sx.cells(min_s,max_s),ys=sy.cells(min_t,max_t);
                if(xs.size()*ys.size()>32768U-visits)fail("original texture clipping exceeds the preparation budget.");
                visits+=unsigned(xs.size()*ys.size());
                for(const auto& cy:ys)for(const auto& cx:xs) {
                    auto cut=clip(clip(clip(clip(polygon,3,cx.low,true),3,cx.high,false),4,cy.low,true),4,cy.high,false);
                    if(cut.size()<3)continue;
                    const auto tile=mappings[original].at(cy.tile*sx.count()+cx.tile);
                    for(std::size_t i=1;i+1<cut.size();++i) {
                        if(!drawable(cut[0],cut[i],cut[i+1]))continue;
                        auto [at,created]=groups.try_emplace(tile);auto& group=at->second;
                        if(created){group.mesh.id=source.id;group.mesh.name=source.name;group.mesh.colour=source.colour;
                            group.mesh.double_sided=source.double_sided;group.mesh.texture=int(tile);}
                        std::array<unsigned,3> triangle{};unsigned corner=0;
                        for(const auto* p:{&cut[0],&cut[i],&cut[i+1]}) {
                            const std::array<float,5> value{float((*p)[0]),float((*p)[1]),float((*p)[2]),
                                float(((*p)[3]-cx.origin+sx.gutter())/tiles[tile].image.width),
                                float(((*p)[4]-cy.origin+sy.gutter())/tiles[tile].image.height)};
                            std::array<std::uint32_t,5> key{};for(unsigned n=0;n<5;++n)key[n]=std::bit_cast<std::uint32_t>(value[n]==0?0.F:value[n]);
                            auto [v,added]=group.vertices.try_emplace(key,unsigned(group.mesh.vertices.size()));
                            if(added){group.mesh.vertices.push_back({value[0],value[1],value[2]});group.mesh.uv.push_back({value[3],value[4]});
                                if(++vertices>MaxVertices)fail("original texture clipping exceeds 8192 vertices in one LOD.");}
                            triangle[corner++]=v->second;
                        }
                        if(triangle[0]==triangle[1] || triangle[1]==triangle[2] || triangle[2]==triangle[0])continue;
                        group.mesh.faces.push_back(triangle);
                        if(++out.triangles>MaxTriangles)fail("original texture clipping exceeds 4096 triangles in one LOD.");
                    }
                }
            }
            if(out.meshes.size()+groups.size()>MaxMeshes)fail("original texture clipping exceeds 64 draw groups in one LOD.");
            for(auto& [tile,group]:groups)if(!group.mesh.faces.empty())out.meshes.push_back(std::move(group.mesh));
        }
        if(out.meshes.empty() || out.meshes.size()>MaxMeshes || vertices>MaxVertices || out.triangles>MaxTriangles)
            fail("original-resolution geometry exceeds the safe draw budget.");
        for(const auto& wheel:car.wheels)if(!wheel.empty() && std::none_of(out.meshes.begin(),out.meshes.end(),[&](const auto& m){return m.id==wheel;}))
            fail("original texture preparation lost an assigned wheel.");
    }
    // Publish only after all six LODs fit. Authored pivots were computed before
    // splitting; each piece keeps its wheel ID and shares the original pivot.
    car.textures=std::move(tiles);car.lods=std::move(prepared);
    static std::atomic<unsigned> reports{0};
    if(reports.fetch_add(1,std::memory_order_relaxed)<8)
        std::fprintf(stderr,"[custom-vehicles][original-textures] source-materials=%zu safe-tiles=%zu lod0-triangles=%u; original RGBA8 pixels retained; 4 KiB limits unchanged\n",source_count,car.textures.size(),car.lods[0].triangles);
}
}

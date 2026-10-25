#include "vehicle_package.hpp"
#include "magic_code_package.hpp"
#include "package_compatibility.hpp"
#include <miniz/miniz.h>
#include <stb/stb_image.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cctype>
#include <limits>
#include <set>

namespace dkr::mods::vehicles {
namespace {
[[noreturn]] void bad(const std::string& why) { throw Error("Custom vehicle: " + why); }
std::string lower(std::string s) {
    for(auto& c:s)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    return s;
}
bool safe_name(std::string_view s) {
    if(s.empty()||s.size()>240||s.front()=='/'||s.back()=='/'||s.find('\\')!=s.npos)return false;
    for(unsigned char c:s)if(c<32||c==127||c==':'||c=='<'||c=='>'||c=='|'||c=='?'||c=='*'||c=='"')return false;
    for(std::size_t at=0;at<s.size();) {
        const auto end=s.find('/',at);const auto p=s.substr(at,end==s.npos?s.size()-at:end-at);
        if(p.empty()||p=="."||p==".."||p.back()=='.'||p.back()==' ')return false;
        const auto stem=lower(std::string(p.substr(0,p.find('.'))));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
           (stem.size()==4&&(stem.starts_with("com")||stem.starts_with("lpt"))&&stem[3]>='0'&&stem[3]<='9'))return false;
        if(end==s.npos)break;at=end+1;
    }
    return true;
}
struct Archive {
    std::map<std::string,Bytes> files;
    explicit Archive(View bytes,unsigned maximum=256) {
        if(bytes.empty()||bytes.size()>MaxPackageBytes)bad("archive exceeds the 32 MiB limit.");
        mz_zip_archive zip{};
        if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))bad("the archive is unreadable.");
        struct End { mz_zip_archive& z;~End(){mz_zip_reader_end(&z);} } end{zip};
        const auto count=mz_zip_reader_get_num_files(&zip);
        if(!count||count>maximum)bad("too many files in the archive.");
        std::size_t total=0;std::set<std::string> names;
        for(unsigned i=0;i<count;++i) {
            mz_zip_archive_file_stat stat{};
            if(!mz_zip_reader_file_stat(&zip,i,&stat))bad("invalid archive directory.");
            const auto size=mz_zip_reader_get_filename(&zip,i,nullptr,0);
            if(size<2||size>242)bad("invalid archive filename.");
            std::string name(size,'\0');
            if(mz_zip_reader_get_filename(&zip,i,name.data(),size)!=size||name.find('\0')!=size-1)bad("ambiguous archive filename.");
            name.pop_back();const bool directory=mz_zip_reader_is_file_a_directory(&zip,i)!=0;
            if(directory&&name.back()=='/')name.pop_back();
            const auto mode=(stat.m_external_attr>>16)&0170000U;
            if(!safe_name(name)||(mode&&mode!=0100000U&&mode!=0040000U)||(stat.m_bit_flag&1))bad("unsafe or encrypted archive entry.");
            if(!names.insert(lower(name)).second)bad("duplicate archive filenames.");
            if(directory)continue;
            if(stat.m_uncomp_size>MaxExpandedBytes-total||stat.m_uncomp_size>MaxPackageBytes)bad("expanded archive exceeds its limit.");
            total+=std::size_t(stat.m_uncomp_size);Bytes data(std::size_t(stat.m_uncomp_size));
            if(!mz_zip_reader_extract_to_mem(&zip,i,data.data(),data.size(),0))bad("an archive entry is incomplete or has an invalid checksum.");
            files.emplace(std::move(name),std::move(data));
        }
    }
    const Bytes& require(const std::string& name)const {
        const auto it=files.find(name);if(it==files.end())bad("missing " + name);return it->second;
    }
};
float number(const Json& j,float limit=32767.F) {
    if(!j.is_number())bad("a model coordinate is not a number.");
    const double d=j.get<double>();
    if(!std::isfinite(d)||std::abs(d)>limit)bad("a model coordinate is outside its safe range.");
    return float(d);
}
unsigned integer(const Json& j,unsigned limit) {
    if(!j.is_number_integer()||j.get<std::int64_t>()<0||j.get<std::uint64_t>()>limit)bad("an index is outside its safe range.");
    return j.get<unsigned>();
}
Vec3 vector3(const Json& j,float limit=32767.F) {
    if(!j.is_array()||j.size()!=3)bad("expected an XYZ coordinate.");
    return {number(j[0],limit),number(j[1],limit),number(j[2],limit)};
}
std::string string(const Json& j,std::size_t limit) {
    if(!j.is_string())bad("invalid text metadata.");
    auto s=j.get<std::string>();if(s.empty()||s.size()>limit)bad("invalid text length.");
    for(unsigned char c:s)if(c<32||c==127)bad("control characters are not allowed in names.");
    return s;
}
Bytes base64(const std::string& s) {
    constexpr std::string_view prefix="data:image/png;base64,";
    if(!s.starts_with(prefix)||s.size()>8*MiB)bad("textures must be embedded PNG images.");
    const auto encoded=std::string_view(s).substr(prefix.size());
    if(encoded.empty()||encoded.size()%4)bad("invalid embedded PNG encoding.");
    Bytes out;out.reserve(encoded.size()/4*3);
    const auto digit=[](char c)->int {
        if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;
    };
    for(std::size_t i=0;i<encoded.size();i+=4) {
        const int a=digit(encoded[i]),b=digit(encoded[i+1]);
        const bool p2=encoded[i+2]=='=',p3=encoded[i+3]=='=';
        const int c=p2?0:digit(encoded[i+2]),d=p3?0:digit(encoded[i+3]);
        if(a<0||b<0||c<0||d<0||(p2&&!p3)||((p2||p3)&&i+4!=encoded.size())||(p2&&(b&15))||(p3&&!p2&&(c&3)))bad("invalid embedded PNG padding.");
        out.push_back(std::uint8_t((a<<2)|(b>>4)));
        if(!p2)out.push_back(std::uint8_t((b<<4)|(c>>2)));
        if(!p3)out.push_back(std::uint8_t((c<<6)|d));
    }
    return out;
}
Image image(View bytes,unsigned maximum,std::size_t& budget) {
    static constexpr std::uint8_t signature[]{137,80,78,71,13,10,26,10};
    if(bytes.size()<33||bytes.size()>8*MiB||!std::equal(std::begin(signature),std::end(signature),bytes.begin()))bad("invalid PNG image.");
    int w=0,h=0,n=0;
    if(!stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n)||w<=0||h<=0||unsigned(w)>maximum||unsigned(h)>maximum)bad("PNG dimensions exceed the vehicle image limit.");
    const auto size=std::size_t(w)*unsigned(h)*4;
    if(size>16*MiB-budget)bad("vehicle textures exceed their decoded memory budget.");
    auto* decoded=stbi_load_from_memory(bytes.data(),int(bytes.size()),&w,&h,&n,4);
    if(!decoded)bad("could not decode a PNG image.");
    Image result{unsigned(w),unsigned(h),Bytes(decoded,decoded+size)};
    stbi_image_free(decoded);budget+=size;return result;
}
Json document(View bytes) {return code::parse_document(bytes,MaxPackageBytes);}
Lod geometry(const Json& j,std::size_t textures,float scale) {
    if(j.value("schema",0)!=1||!j.contains("meshes")||!j["meshes"].is_array()||j["meshes"].empty()||j["meshes"].size()>64)bad("unsupported mesh layout.");
    Lod out;std::set<std::string> ids;std::size_t vertices=0;
    for(const auto& item:j["meshes"]) {
        Mesh m;m.id=string(item.at("id"),96);m.name=string(item.at("name"),160);
        if(!ids.insert(m.id).second)bad("duplicate mesh identities.");
        const auto& v=item.at("vertices");const auto& uv=item.at("uv");const auto& f=item.at("faces");
        if(!v.is_array()||v.empty()||v.size()>8192-vertices||!uv.is_array()||uv.size()!=v.size()||!f.is_array()||f.empty()||f.size()>4096-out.triangles)bad("invalid mesh size, UV count or triangle count.");
        vertices+=v.size();out.triangles+=unsigned(f.size());
        for(const auto& p:v)m.vertices.push_back(vector3(p,32700.F/scale));
        for(const auto& p:uv) {
            if(!p.is_array()||p.size()!=2)bad("invalid texture coordinates.");
            m.uv.push_back({number(p[0],128.F),number(p[1],128.F)});
        }
        for(const auto& t:f) {
            if(!t.is_array()||t.size()!=3)bad("only triangular faces are supported.");
            std::array<unsigned,3> face{};for(unsigned i=0;i<3;++i)face[i]=integer(t[i],unsigned(v.size()-1));
            if(face[0]==face[1]||face[1]==face[2]||face[2]==face[0])bad("a triangle repeats a vertex.");
            m.faces.push_back(face);
        }
        m.texture=item.value("texture",-1);
        if(m.texture < -1 || (m.texture>=0&&std::size_t(m.texture)>=textures))bad("a mesh references a missing texture.");
        if(item.contains("color")) {
            const auto& c=item["color"];if(!c.is_array()||c.size()!=4)bad("invalid mesh colour.");
            for(unsigned i=0;i<4;++i)m.colour[i]=std::uint8_t(integer(c[i],255));
        }
        m.double_sided=item.value("double_sided",false);
        if(item.value("primitive",0)!=0)bad("unsupported mesh primitive.");
        out.meshes.push_back(std::move(m));
    }
    return out;
}
} // namespace
bool valid_digest(std::string_view s) {
    return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
Type type_for_extension(std::string_view value) {
    const auto s=lower(std::string(value));
    if(s==".dkrkart")return Type::Car;if(s==".dkrhover")return Type::Hovercraft;if(s==".dkrplane")return Type::Plane;
    bad("choose a .dkrkart car package or a ZIP containing one.");
}
const char* extension(Type t){switch(t){case Type::Car:return ".dkrkart";case Type::Hovercraft:return ".dkrhover";case Type::Plane:return ".dkrplane";}bad("invalid vehicle class.");}
const char* type_name(Type t){switch(t){case Type::Car:return "Car";case Type::Hovercraft:return "Hovercraft";case Type::Plane:return "Plane";}bad("invalid vehicle class.");}
bool supported(Type t){return t==Type::Car;}
std::vector<Input> read_inputs(const std::filesystem::path& path) {
    const auto ext=lower(path.extension().string());auto bytes=read_file(path,MaxPackageBytes);
    const auto filename=path.filename().u8string();const std::string name(filename.begin(),filename.end());
    if(ext!=".zip")return {{name,std::move(bytes),type_for_extension(ext)}};
    Archive zip(bytes);std::vector<Input> out;std::set<std::string> digests;
    for(const auto& [member,data]:zip.files) {
        const auto dot=member.rfind('.');const auto suffix=dot==member.npos?std::string{}:lower(member.substr(dot));
        if(suffix!=".dkrkart"&&suffix!=".dkrhover"&&suffix!=".dkrplane")continue;
        if(digests.insert(sha256(data)).second)out.push_back({member.substr(member.find_last_of('/')+1),data,type_for_extension(suffix)});
        if(out.size()>16)bad("import up to 16 vehicle packages at once.");
    }
    if(out.empty())bad("this ZIP contains no vehicle package. Nested ZIPs are not unpacked.");
    return out;
}
static std::shared_ptr<const Package> inspect_impl(View bytes,std::string source_name,Type type) {
    if(!supported(type))bad(std::string(type_name(type))+" packages are reserved for a later update. This playtest supports cars.");
    Archive zip(bytes);auto p=std::make_shared<Package>();p->type=type;
    p->digest=sha256(bytes);p->id="vehicle:"+p->digest;p->source.assign(bytes.begin(),bytes.end());p->source_name=std::move(source_name);
    const auto manifest=document(zip.require("kart.json"));
    if(manifest.value("format","")!="DKR Character Studio Kart"||manifest.value("version",0)!=1)bad("unsupported Studio kart format.");
    for(const auto* field:{"vehicle_type","vehicle_class","type"})if(manifest.contains(field)) {
        const auto t=lower(string(manifest[field],32));
        if(t!="car"&&t!="kart"&&t!="dkrkart")bad("the manifest vehicle class conflicts with its .dkrkart extension.");
    }
    p->name=manifest.contains("name")?string(manifest["name"],160):p->source_name;
    const auto slash=p->name.find_last_of("/\\");if(slash!=p->name.npos)p->name.erase(0,slash+1);
    if(lower(p->name).ends_with(".dkrkart"))p->name.resize(p->name.size()-8);
    if(p->name.empty())bad("the car needs a name.");
    const auto& settings=manifest.at("settings");
    p->scale=number(settings.at("scale"),4096.F);if(p->scale<0.001F)bad("invalid model scale.");
    p->yaw=number(settings.at("yaw"),3600.F);p->driver_yaw=number(settings.value("driver_yaw",Json(0)),3600.F);
    p->driver_offset=vector3(settings.value("driver",Json::array({0,0,0})),1024.F);
    constexpr const char* drivers[]{"KRUNCH","BUMPER","TIPTUP","CONKER","TIMBER","BANJO","DRUMSTICK","PIPSY","TT","DIDDY"};
    const bool authored=manifest.contains("driver_position_mode")||manifest.contains("driver_seat_positions")||
        settings.contains("driver_seat_positions")||manifest.value("driver_anchor_kind","")=="kart-fixed-base-reference-v1";
    if(authored) {
        if(manifest.value("driver_position_mode","")!="per-base-driver"||
           manifest.value("driver_anchor_kind","")!="kart-fixed-base-reference-v1")
            bad("this per-driver seating convention is not supported.");
        p->seating=SeatingMode::PerBaseDriverV1;
        const auto& positions=manifest.at("driver_seat_positions");
        const auto& anchors=manifest.at("driver_anchors");
        if(!positions.is_object()||positions.size()!=10||!anchors.is_object()||anchors.size()!=10)
            bad("per-driver seating needs positions and anchors for all ten base drivers.");
        for(unsigned i=0;i<10;++i) {
            if(!positions.contains(drivers[i])||!anchors.contains(drivers[i]))
                bad(std::string("missing seat or anchor for ")+drivers[i]+".");
            p->driver_seats[i]=vector3(positions.at(drivers[i]),1024.F);
            p->driver_anchors[i]=vector3(anchors.at(drivers[i]),1024.F);
        }
        if(settings.contains("driver_seat_positions")&&settings["driver_seat_positions"]!=positions)
            bad("the two driver seat tables disagree. Export the car again.");
        if(settings.contains("seat")||p->driver_offset.x!=0||p->driver_offset.y!=0||p->driver_offset.z!=0)
            bad("per-driver seats cannot be combined with a legacy shared seat or driver offset.");
        const auto& transform=manifest.at("kart_transform");
        if(!transform.is_object()||transform.value("order","")!="subtract origin, scale, rotate around +Y, translate"||
           transform.value("units","")!="Y-up Studio units; forward -Z; yaw in degrees"||
           !transform.contains("driver_positions_inherit_transform")||
           !transform["driver_positions_inherit_transform"].is_boolean()||
           transform["driver_positions_inherit_transform"].get<bool>())
            bad("unsupported kart transform. Driver seats must stay independent of the kart transform.");
        p->origin=vector3(transform.at("origin"),32700.F/p->scale);
        p->kart_offset=vector3(transform.at("translation"),1024.F);
        if(transform.at("scale")!=settings.at("scale")||transform.at("yaw_degrees")!=settings.at("yaw")||
           manifest.at("geometry_origin")!=transform.at("origin")||settings.at("kart_offset")!=transform.at("translation"))
            bad("the kart transform settings disagree. Export the car again.");
        p->authored_anchors=true;
    }else {
        if(manifest.contains("kart_transform")||settings.contains("kart_offset"))
            bad("the kart transform needs an explicit per-driver seating convention.");
        p->seat=vector3(settings.at("seat"),1024.F);
    }
    if(manifest.value("forward_axis","")!="-Z")bad("the exporter must use the agreed Y-up, forward -Z convention.");
    const auto& policy=manifest.at("vanilla_vehicle_policy");
    if(policy.value("when_custom_kart_active","")!="hide_geometry_and_wheel_sprites"||policy.value("without_custom_kart","")!="use_existing_character_vehicle")bad("unsupported native vehicle visibility policy.");
    const std::set<std::string> required_hidden{"chassis","steering_wheel","rear_spoiler","wheel_sprites","vehicle_attachments"};
    const std::set<std::string> required_kept{"driver_geometry","driver_animations","driver_voice","physics","collision"};
    const auto hidden=policy.at("hidden_native_parts").get<std::set<std::string>>(),kept=policy.at("preserve").get<std::set<std::string>>();
    if(hidden!=required_hidden||kept!=required_kept)bad("the package requests an unsupported driver/vehicle policy.");
    // Adapter defaults for the older neutral-base export. The newer manifest
    // can override all ten anchors, never just a partial driver subset.
    // Native enum order is NOT the workshop's display order.
    if(!authored)p->driver_anchors={Vec3{0,24,0},Vec3{0,24,-9.5F},Vec3{0,9.5F,-4.75F},Vec3{0,9.5F,-4.75F},Vec3{0,14.5F,-4.75F},
                       Vec3{0,4.75F,4.75F},Vec3{0,14.5F,-4.75F},Vec3{0,4.75F,-4.75F},Vec3{0,20,0},Vec3{0,4.75F,-19.25F}};
    if(!authored&&manifest.contains("driver_anchors")) {
        if(manifest.value("driver_anchor_kind","")!="calibrated-base-pelvis-v1"||!manifest["driver_anchors"].is_object()||manifest["driver_anchors"].size()!=10)bad("unsupported driver anchor table.");
        for(unsigned i=0;i<10;++i)p->driver_anchors[i]=vector3(manifest["driver_anchors"].at(drivers[i]),1024.F);
        p->authored_anchors=true;
    }
    constexpr const char* roles[]{"front_left","front_right","rear_left","rear_right"};
    std::set<std::string> wheel_ids;
    const auto& wheels=settings.at("wheels");
    if(!wheels.is_object()||wheels.size()!=4)bad("wheel assignments need the four named wheel roles; unused roles may be empty.");
    for(unsigned i=0;i<4;++i) {
        const auto& value=wheels.at(roles[i]);
        if(authored&&value.is_string()&&value.get_ref<const std::string&>().empty())continue;
        p->wheels[i]=string(value,96);
        if(!wheel_ids.insert(p->wheels[i]).second)bad("a wheel mesh is assigned more than once.");
    }
    const auto axle=string(settings.at("axle"),1);if(axle!="X"&&axle!="Y"&&axle!="Z")bad("unsupported wheel axle.");p->axle=unsigned(axle=="X"?0:axle=="Y"?1:2);
    const auto original=document(zip.require("geometry.json"));
    if(authored&&original.at("origin")!=manifest.at("kart_transform").at("origin"))
        bad("the geometry origin does not match the kart transform.");
    const auto profile=manifest.value("texture_profile",std::string{});
    const bool original_resolution=profile=="original-resolution-v1";
    if(!profile.empty()&&profile!="native-4kib-rgba32-safe-v1"&&!original_resolution)bad("unsupported car texture profile.");
    original_textures::seating_contract(manifest,*p);
    original_textures::effect_contract(manifest);
    const auto& images=original.at("textures");if(!images.is_array()||images.size()>32)bad("too many car textures.");
    std::size_t image_budget=0;
    for(const auto& t:images) {
        Texture texture;texture.image=image(base64(string(t.at("image"),8*MiB)),256,image_budget);
        if(integer(t.at("width"),256)!=texture.image.width||integer(t.at("height"),256)!=texture.image.height)bad("texture metadata does not match its PNG.");
        const auto& wrap=t.at("wrap");
        for(const auto* axis:{"wrap-s","wrap-t"}) {const auto mode=wrap.value(axis,"Wrap");if(mode!="Wrap"&&mode!="Clamp")bad("unsupported texture wrapping mode.");}
        texture.repeat_s=wrap.value("wrap-s","Wrap")=="Wrap";texture.repeat_t=wrap.value("wrap-t","Wrap")=="Wrap";
        const auto texels=std::size_t(texture.image.width)*texture.image.height;
        if(!original_resolution&&texels*2>4096)bad("a car texture exceeds the native 4 KiB material limit. Export smaller material textures.");
        if(profile=="native-4kib-rgba32-safe-v1"&&texels*4>4096)bad("a car texture exceeds its declared RGBA32-safe 4 KiB profile.");
        if((texture.repeat_s&&!std::has_single_bit(texture.image.width))||
           (texture.repeat_t&&!std::has_single_bit(texture.image.height)))bad("repeating car textures need power-of-two dimensions.");
        if(t.value("flipped",false))for(unsigned y=0;y<texture.image.height/2;++y)for(unsigned x=0;x<texture.image.width*4;++x)
            std::swap(texture.image.rgba[y*texture.image.width*4+x],texture.image.rgba[(texture.image.height-1-y)*texture.image.width*4+x]);
        p->textures.push_back(std::move(texture));
    }
    const auto full=geometry(original,p->textures.size(),p->scale);
    const auto& lods=manifest.at("lods");if(!lods.is_array()||lods.size()!=6)bad("all six car LODs are required.");
    for(unsigned lod=0;lod<6;++lod) {
        const auto file="lods/lod"+std::to_string(lod)+".json";
        if(integer(lods[lod].at("level"),5)!=lod||lods[lod].at("file").get<std::string>()!=file)bad("invalid LOD file mapping.");
        const auto doc=document(zip.require(file));
        p->lods[lod]=geometry(doc,p->textures.size(),p->scale);
        if(integer(lods[lod].at("triangles"),4096)!=p->lods[lod].triangles)bad("LOD triangle metadata does not match its geometry.");
        for(const auto& id:p->wheels)if(!id.empty()&&std::none_of(p->lods[lod].meshes.begin(),p->lods[lod].meshes.end(),[&](const auto& m){return m.id==id;}))bad("a wheel assignment is missing from an LOD.");
        if(authored&&doc.contains("origin")&&doc["origin"]!=original.at("origin"))bad("an LOD has a different geometry origin.");
        // Material indices have a single owner: the root geometry. Exported
        // LODs may repeat them but must not substitute a different image.
        if(doc.contains("textures")&&doc["textures"]!=images)bad("LOD textures differ from the root material table.");
    }
    const auto& pivots=settings.value("wheel_pivots",Json::object());
    for(const auto& lod:p->lods)for(const auto& mesh:lod.meshes)if(mesh.texture>=0) {
        const auto& texture=p->textures.at(unsigned(mesh.texture));
        for(const auto& uv:mesh.uv)if(std::abs(uv.x)*texture.image.width*32.F>32700.F||std::abs(uv.y)*texture.image.height*32.F>32700.F)
            bad("car UV coordinates exceed the native material range. Reduce the UV repeat count.");
    }
    if(!pivots.is_object()||pivots.size()>4)bad("invalid wheel pivot table.");
    for(const auto& [key,value]:pivots.items()) {
        if(wheel_ids.contains(key))continue;
        const auto role=std::find(std::begin(roles),std::end(roles),key);
        if(role==std::end(roles)||p->wheels[unsigned(role-std::begin(roles))].empty())bad("a wheel pivot has no assigned wheel.");
    }
    for(unsigned w=0;w<4;++w) {
        if(p->wheels[w].empty())continue;
        if(pivots.contains(p->wheels[w]))p->pivots[w]=vector3(pivots[p->wheels[w]],32700.F/p->scale);
        else if(pivots.contains(roles[w]))p->pivots[w]=vector3(pivots[roles[w]],32700.F/p->scale);
        else {
            const auto it=std::find_if(p->lods[0].meshes.begin(),p->lods[0].meshes.end(),[&](const auto& m){return m.id==p->wheels[w];});
            auto lo=it->vertices.front(),hi=lo;
            for(const auto& v:it->vertices){lo.x=std::min(lo.x,v.x);lo.y=std::min(lo.y,v.y);lo.z=std::min(lo.z,v.z);hi.x=std::max(hi.x,v.x);hi.y=std::max(hi.y,v.y);hi.z=std::max(hi.z,v.z);}
            p->pivots[w]={(lo.x+hi.x)*.5F,(lo.y+hi.y)*.5F,(lo.z+hi.z)*.5F};
        }
    }
    const auto portrait=manifest.at("portrait").at("file").get<std::string>();
    if(portrait!="portrait.png")bad("unsupported portrait path.");
    p->portrait=image(zip.require(portrait),512,image_budget);
    if(original_resolution)original_textures::prepare(*p);
    return p;
}
std::shared_ptr<const Package> inspect(View bytes,std::string source_name,Type type) {
    try {return inspect_impl(bytes,std::move(source_name),type);}
    catch(const Json::exception&) {
        bad("the package has a missing or invalid model setting. Export it again with the current Studio version.");
    }
}
Bytes native_texture(const Image& image) {
    if(!image.width||!image.height||image.width>255||image.height>255||image.rgba.size()!=std::size_t(image.width)*image.height*4)bad("image cannot be represented by a native texture.");
    Bytes out(32+image.width*image.height*2,0);out[0]=std::uint8_t(image.width);out[1]=std::uint8_t(image.height);out[2]=1;
    out[0x12]=1; // one frame, matching native texture frame-count convention
    const auto size=unsigned((out.size()+7)&~std::size_t(7));out.resize(size,0);out[0x16]=std::uint8_t(size>>8);out[0x17]=std::uint8_t(size);
    for(std::size_t i=0;i<std::size_t(image.width)*image.height;++i) {
        const auto* c=image.rgba.data()+i*4;
        const unsigned n=(unsigned(c[0]>>3)<<11)|(unsigned(c[1]>>3)<<6)|(unsigned(c[2]>>3)<<1)|(c[3]>=128);
        out[32+i*2]=std::uint8_t(n>>8);out[33+i*2]=std::uint8_t(n);
    }
    return out;
}
} // namespace dkr::mods::vehicles

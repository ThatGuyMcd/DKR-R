#include "online_mod_bundle.hpp"
#include <json/json.hpp>
#include <algorithm>
#include <cctype>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace dkr::mods::online {
namespace {
using nlohmann::json;
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("Online mod preparation cancelled.");}
json document(View bytes) {
    std::vector<std::set<std::string>> keys;
    return json::parse(bytes.begin(),bytes.end(),[&](int depth,json::parse_event_t event,json& value){
        if(depth>12)throw Error("Mod metadata nesting exceeds its limit.");
        if(event==json::parse_event_t::object_start)keys.emplace_back();
        if(event==json::parse_event_t::key && (keys.empty() || !keys.back().insert(value.get<std::string>()).second))
            throw Error("Duplicate Track Lab manifest field.");
        if(event==json::parse_event_t::object_end)keys.pop_back();return true;});
}
void put32(Bytes& out,std::uint32_t value) {for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(24-8*i)));}
bool safe_path(std::string_view name) {
    if(name.empty() || name.size()>240 || name.front()=='/' || name.back()=='/' || name.find('\\')!=name.npos || name.find(':')!=name.npos)
        return false;
    for(unsigned char c:name)if(c<32 || c==127 || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|')return false;
    std::size_t at=0;
    while(at<name.size()) {
        const auto end=name.find('/',at);const auto part=name.substr(at,end==name.npos?name.size()-at:end-at);
        if(part.empty() || part=="." || part==".." || part.back()=='.' || part.back()==' ')return false;
        std::string base(part.substr(0,part.find('.')));
        std::transform(base.begin(),base.end(),base.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});
        if(base=="CON" || base=="PRN" || base=="AUX" || base=="NUL" ||
           (base.size()==4 && (base.starts_with("COM") || base.starts_with("LPT")) && base[3]>='0' && base[3]<='9'))return false;
        if(end==name.npos)break;at=end+1;
    }
    return true;
}
std::set<std::string> declared_files(View manifest) {
    const auto doc=document(manifest);
    const auto schema=doc.at("schemaVersion").get<unsigned>();
    if((schema!=1 && schema!=2) || (schema==2)!=doc.contains("music") || !doc.at("adds").is_array() ||
       doc.at("adds").empty() || doc.at("adds").size()>MaxEntries-2)throw Error("Unsupported Track Lab manifest.");
    std::set<std::string> files{"manifest.json"},folded{"manifest.json"};
    const auto add=[&](const json& entry) {
        const auto name=entry.at("file").get<std::string>();
        if(!safe_path(name) || name=="manifest.json")throw Error("Track Lab names an unsafe payload path.");
        if(files.contains(name))return;
        auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!folded.insert(lower).second)throw Error("Track Lab payload paths collide across platforms.");
        files.insert(name);
    };
    for(const auto& entry:doc.at("adds"))add(entry);
    if(doc.contains("music"))add(doc.at("music"));
    return files;
}
}
void check_storage(const std::filesystem::path& input,bool directory) {
    auto path=std::filesystem::absolute(input).lexically_normal();
#ifdef _WIN32
    // Iterate normal drive/UNC components, but perform each OS query using
    // the extended-length form. Treating the "?" in \\?\ as a directory, or
    // querying an unprefixed deep staging path, rejects valid private jobs.
    const auto text=path.native();
    if(text.starts_with(L"\\\\?\\UNC\\"))path=std::filesystem::path(L"\\\\"+text.substr(8));
    else if(text.starts_with(L"\\\\?\\"))path=std::filesystem::path(text.substr(4));
#endif
    auto part=path.root_path();
    for(const auto& name:path.relative_path()) {
        part/=name;
        const auto queried=private_storage_path(part);
        if(std::filesystem::is_symlink(queried))throw Error("Online mod storage must not contain links.");
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(queried.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))
            throw Error("Online mod storage is missing or contains a reparse point.");
#endif
    }
    const auto queried=private_storage_path(path);
    if(directory?!std::filesystem::is_directory(queried):!std::filesystem::is_regular_file(queried))
        throw Error("Online mod storage has an invalid file type.");
}
Bytes pack_track_lab(const std::filesystem::path& input_directory,std::stop_token stop) {
    const auto directory=private_storage_path(input_directory);
    check_storage(directory,true);check_storage(directory/"manifest.json",false);
    const auto manifest=read_file(directory/"manifest.json",MiB);const auto files=declared_files(manifest);
    Bytes out{'D','K','R','P',1,0,0,0};put32(out,static_cast<std::uint32_t>(files.size()));
    for(const auto& name:files) {
        cancelled(stop);const auto path=private_storage_path(directory/utf8_path(name));check_storage(path,false);
        const auto overhead=8+name.size();
        const auto size=std::filesystem::file_size(path);
        if(overhead>MaxStaged-out.size() || size>MaxStaged-out.size()-overhead)throw Error("Track Lab package exceeds its online byte budget.");
        const auto data=name=="manifest.json"?manifest:read_file(path,static_cast<std::size_t>(size));
        if(data.size()!=size)throw Error("Track Lab changed during export. Re-export and try hosting again.");
        if(overhead>MaxStaged-out.size() || data.size()>MaxStaged-out.size()-overhead)throw Error("Track Lab package exceeds its online byte budget.");
        put32(out,static_cast<std::uint32_t>(name.size()));put32(out,static_cast<std::uint32_t>(data.size()));
        out.insert(out.end(),name.begin(),name.end());out.insert(out.end(),data.begin(),data.end());
    }
    return out;
}
void unpack_track_lab(View bytes,const std::filesystem::path& input_destination,std::stop_token stop) {
    const auto destination=private_storage_path(input_destination);
    constexpr std::array<std::uint8_t,8> magic{'D','K','R','P',1,0,0,0};
    if(bytes.size()<12 || bytes.size()>MaxStaged || !std::equal(magic.begin(),magic.end(),bytes.begin()))
        throw Error("Unsupported Track Lab network package.");
    const auto count=be32(bytes,8);if(!count || count>MaxEntries)throw Error("Track Lab package exceeds its file limit.");
    std::map<std::string,View> files;std::set<std::string> folded;std::size_t at=12;
    for(unsigned i=0;i<count;++i) {
        cancelled(stop);const auto header=slice(bytes,at,8);at+=8;
        const auto length=be32(header,0),size=be32(header,4);const auto name_bytes=slice(bytes,at,length);at+=length;
        const std::string name(name_bytes.begin(),name_bytes.end());if(!safe_path(name))throw Error("Unsafe Track Lab network path.");
        auto lower=name;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(!folded.insert(lower).second)throw Error("Duplicate Track Lab network path.");
        files.emplace(name,slice(bytes,at,size));at+=size;
    }
    if(at!=bytes.size() || !files.contains("manifest.json") || files.at("manifest.json").size()>MiB)throw Error("Incomplete Track Lab network package.");
    const auto declared=declared_files(files.at("manifest.json"));std::set<std::string> actual;
    for(const auto& [name,data]:files)actual.insert(name);
    if(declared!=actual)throw Error("Track Lab network package includes missing or undeclared files.");
    // Validate the complete index before writing. Never overwrite existing data.
    if(std::filesystem::exists(destination))throw Error("Track Lab network staging already exists.");
    check_storage(destination.parent_path(),true);std::filesystem::create_directory(destination);check_storage(destination,true);
    for(const auto& [name,data]:files) {
        cancelled(stop);const auto target=private_storage_path(destination/utf8_path(name));
        std::filesystem::create_directories(target.parent_path());check_storage(target.parent_path(),true);
        write_new_file(target,data);
    }
}
}

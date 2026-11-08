#pragma once
#include "mods/online_mod_cache.hpp"
#include "policy.hpp"
#include <random>

namespace dkr::lobby_stability {
// Repair only the named private download. Never follow a symlink, replace an
// installed mod or discard unrelated payloads. Retain the damaged file for QA.
inline bool verify_or_repair_download(const std::filesystem::path& input_root,const char* folder,
                            const dkr::mods::online::Payload& expected,std::stop_token stop) {
    using namespace dkr::mods;
    using namespace dkr::mods::online;
    if(stop.stop_requested())throw Error("Online cache repair cancelled.");
    if(!valid_digest(expected.digest))throw Error("Invalid payload repair identity.");
    const auto root=private_storage_path(input_root),parent=root/folder,path=parent/expected.digest;
    if(!std::filesystem::exists(path))return false;
    check_storage(root,true);check_storage(parent,true);check_storage(path,false);
    if(std::filesystem::canonical(parent).parent_path()!=std::filesystem::canonical(root))throw Error("Private download repair escaped its cache.");
    const auto size=std::filesystem::file_size(path);
    if(std::string_view(folder)=="partials"&&size<expected.size)return false;
    if(size==expected.size&&sha256(read_file(path,static_cast<std::size_t>(expected.size)))==expected.digest)return true;
    const auto quarantine=root/"quarantined-downloads";std::filesystem::create_directory(quarantine);check_storage(quarantine,true);
    std::random_device random;
    for(unsigned n=0;n<5;++n) {
        const auto reservation=quarantine/(expected.digest+"-"+std::to_string(random())+"-"+std::to_string(random()));
        if(!std::filesystem::create_directory(reservation))continue;
        check_storage(reservation,true);std::filesystem::rename(path,reservation/expected.digest);
        std::fprintf(stderr,"[online-mods][download-repair] payload=%s source=%s preserved-old-file=1\n",expected.digest.c_str(),folder);return false;
    }
    throw Error("Could not reserve a safe private download repair.");
}
}

#include "legacy_mod_stage.hpp"
#include "legacy_mod_process.hpp"
#include "legacy_track_prepare_job.hpp"
#include "online_mod_prepare.hpp"
#include <json/json.hpp>
#include <fstream>
#include <iostream>

namespace {
int self_test() {
    using namespace dkr::mods;
    try {
        constrain_import_worker();
        const Bytes abc{'a','b','c'};
        if(sha256(abc)!="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") return 1;
        bool rejected=false;try{decode_patch(abc,abc);}catch(const Error&){rejected=true;}
        if(!rejected)return 1;
        rejected=false;try{inflate_asset(abc,1024);}catch(const Error&){rejected=true;}
        if(!rejected)return 1;
        std::cout<<"Legacy importer startup/hash/invalid-input self-test passed. No ROM or profile loaded.\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
int execute(const std::filesystem::path& input_request) {
    using namespace dkr::mods;
    // Keep directory creation and the progress stream long-path-safe even
    // when a launcher sends an ordinary (unprefixed) Windows request path.
    auto request=input_request;
    try {
        constrain_import_worker();
        request=private_storage_path(input_request);
        const auto bytes=read_file(request,64*1024);
        const auto job=nlohmann::json::parse(bytes.begin(),bytes.end());
        if(job.at("schema")!=Schema) throw Error("Unsupported importer request version.");
        if(job.value("operation",std::string{})=="online-capabilities") {
            const auto capabilities=nlohmann::json({{"schema",Schema},{"online_prepare",1}}).dump();
            write_new_file(request.parent_path()/"capabilities.json",
                View(reinterpret_cast<const std::uint8_t*>(capabilities.data()),capabilities.size()));
            return 0;
        }
        const auto source=utf8_path(job.at("source").get<std::string>());
        const auto destination=request.parent_path()/"content";
        std::vector<std::filesystem::path> roms;
        if(!job.at("roms").is_array() || job.at("roms").size()>8) throw Error("Invalid Game Pak list.");
        for(const auto& rom : job.at("roms")) roms.push_back(utf8_path(rom.get<std::string>()));
        // Events contain only fixed stage labels, never source paths or ROM data.
        const auto events=request.parent_path()/"progress.jsonl";
        write_new_file(events,{});
        std::ofstream stream(events,std::ios::app|std::ios::binary);
        if(!stream) throw Error("Could not open the private progress stream.");
        const auto operation=job.value("operation",std::string("import"));
        unsigned event_count=0;
        const auto report=[&](const Progress& progress) {
            if(++event_count>(operation=="online-prepare"?4096U:512U)) throw Error("Import progress budget exceeded.");
            stream<<nlohmann::json({{"patch",progress.patch},{"count",progress.count},
                                   {"stage",progress.stage}}).dump()<<'\n';
            stream.flush();return bool(stream);
        };
        if(operation=="import")stage_import(source,roms,destination,report);
        else if(operation=="prepare-tracks")prepare_imported_tracks(source,roms,destination,report);
        else if(operation=="prepare-characters")prepare_imported_characters(source,roms,destination,report);
        else if(operation=="online-prepare") {
            const auto manifest_path=utf8_path(job.at("manifest").get<std::string>());
            const auto manifest=online::decode(read_file(manifest_path,online::MaxManifestBytes));
            online::prepare_library(manifest,source,roms,destination,report);
        } else throw Error("Unsupported legacy worker operation.");
        return 0;
    } catch(const std::exception& error) {
        // Diagnostic is private to this transaction. Failures must never turn
        // a half-staged package into a catalog entry.
        try {
            const auto value=nlohmann::json({{"error",std::string(error.what()).substr(0,1024)}}).dump();
            write_new_file(request.parent_path()/"failure.json",
                View(reinterpret_cast<const std::uint8_t*>(value.data()),value.size()));
        } catch(...) {}
        return 1;
    }
}
}
#if defined(_WIN32)
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    if(std::wstring_view(argv[1])==L"--self-test")return self_test();
    if(std::wstring_view(argv[1])==L"--online-capability") {std::cout<<"DKR-R online-prepare protocol 1\n";return 0;}
    return execute(std::filesystem::path(argv[1]));
}
#else
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    if(std::string_view(argv[1])=="--self-test")return self_test();
    if(std::string_view(argv[1])=="--online-capability") {std::cout<<"DKR-R online-prepare protocol 1\n";return 0;}
    return execute(std::filesystem::path(argv[1]));
}
#endif

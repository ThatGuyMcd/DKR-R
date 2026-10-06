#include "owned_game.hpp"
#include <iostream>
#include <stdexcept>
#include <fstream>
bool dkr_experimental_v77_dkr_probe_owned_adapter_check(std::span<const std::uint8_t>,std::span<const std::uint8_t>,unsigned);
#if DKR_OWNED_EXPECT_V80
bool dkr_experimental_v80_dkr_probe_owned_adapter_check(std::span<const std::uint8_t>,std::span<const std::uint8_t>,unsigned);
#endif
int main(int argc,char** argv) {
    using namespace dkr::runtime::netplay::experimental;
    using dkr::runtime::rom::Revision;
    unsigned checks=0;
    const auto check=[&](bool ok){if(!ok)throw std::runtime_error("Owned revision registry failed");++checks;};
    check(owned_adapter_available(Revision::UsV77));
    check(owned_adapter_available(Revision::UsV80)==bool(DKR_OWNED_EXPECT_V80));
    check(!owned_adapter_available(Revision::Unsupported));
    check(owned_adapter_identity(Revision::UsV77).size()==64);
    check(owned_adapter_identity(Revision::Unsupported).empty());
    if(DKR_OWNED_EXPECT_V80) {
        check(owned_adapter_identity(Revision::UsV80).size()==64);
        check(owned_adapter_identity(Revision::UsV77)!=owned_adapter_identity(Revision::UsV80));
    }
    PresentationMailbox box;std::string error;
    for(auto revision:{Revision::Unsupported,Revision::UsV77,Revision::UsV80}) {
        check(!make_owned_game_for_revision(revision,{},{},91,box,{},error));
        check(!error.empty());
    }
    check(!make_owned_game({},{},91,box,{},error));
    if(argc==5) {
        const auto read=[](const char* path) {
            std::ifstream stream(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
            if(!stream || stream.tellg()<=0 || stream.tellg()>32*1024*1024)throw std::runtime_error("Private fixture size invalid");
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stream.tellg()));stream.seekg(0);
            if(!stream.read(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Private fixture unreadable");
            return bytes;
        };
        const auto fixture77=read(argv[1]),rom77=read(argv[2]),fixture80=read(argv[3]),rom80=read(argv[4]);
        const auto sink=[](ConfirmedAudio,std::string&){return true;};
        check(!make_owned_game_for_revision(Revision::UsV77,fixture80,rom77,91,box,sink,error));
        check(!make_owned_game_for_revision(Revision::UsV80,fixture77,rom80,91,box,sink,error));
        check(!make_owned_game_for_revision(Revision::UsV77,fixture77,rom80,91,box,sink,error));
        check(dkr_experimental_v77_dkr_probe_owned_adapter_check(fixture77,rom77,24));
#if DKR_OWNED_EXPECT_V80
        check(dkr_experimental_v80_dkr_probe_owned_adapter_check(fixture80,rom80,24));
#endif
        // Keep both revision capsules alive and alternate execution. Their
        // private ROM/input/audio bridges must not leak into the other engine.
        std::array<PresentationMailbox,2> boxes;
        for(auto& mailbox:boxes)check(mailbox.begin_epoch(91));
        auto first=make_owned_game_for_revision(Revision::UsV77,fixture77,rom77,91,boxes[0],sink,error);
        auto second=make_owned_game_for_revision(Revision::UsV80,fixture80,rom80,91,boxes[1],sink,error);
        check(bool(first) && bool(second));
        std::array<OwnedGame*,2> worlds{first.get(),second.get()};
        std::array<std::vector<std::uint8_t>,2> baseline;
        for(unsigned i=0;i<2;++i){baseline[i].resize(worlds[i]->contract().state_bytes);check(worlds[i]->capture(baseline[i],error));}
        std::array<std::vector<std::uint8_t>,2> after;
        for(unsigned pass=0;pass<2;++pass) {
            for(unsigned frame=0;frame<6;++frame)for(auto* world:worlds) {
                TickOutput output;check(world->tick(frame,{},output,error) && !output.scene_boundary);
            }
            for(unsigned i=0;i<2;++i) {
                std::vector<std::uint8_t> state(baseline[i].size());check(worlds[i]->capture(state,error));
                if(!pass)after[i]=state;else check(state==after[i]);
                check(worlds[i]->restore(baseline[i],error));
            }
        }
        std::cout<<"Both production revision engines: real gameplay corrections, exact state/PCM and interleaved replay isolation passed.\n";
    } else if(argc!=1)throw std::runtime_error("Expected zero or four private fixture paths");
    std::cout<<checks<<" production owned revision dispatch checks passed.\n";
}

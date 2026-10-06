#include "owned_game.hpp"
#include "experimental_checkpoint_hash.hpp"

namespace dkr::runtime::netplay::experimental {
// Symbols are produced by the Patch Pipeline's private forced-include map.
// The common interface is the same; no pointers/state from one engine enter
// the other. Each engine still checks its exact ROM and bootstrap revision.
#define DKR_DECLARE_OWNED_FACTORY(revision) \
std::unique_ptr<OwnedGame> dkr_experimental_v##revision##_make_owned_game( \
    std::span<const std::uint8_t>,std::span<const std::uint8_t>,std::uint64_t, \
    PresentationMailbox&,ConfirmedAudioSink,std::string&,bool,std::uint32_t, \
    std::span<const std::uint8_t>,OwnedBootOptions)
#if DKR_OWNED_HAS_V77
DKR_DECLARE_OWNED_FACTORY(77);
#endif
#if DKR_OWNED_HAS_V80
DKR_DECLARE_OWNED_FACTORY(80);
#endif
#undef DKR_DECLARE_OWNED_FACTORY

bool owned_adapter_available(rom::Revision revision) {
    switch(revision) {
#if DKR_OWNED_HAS_V77
    case rom::Revision::UsV77: return true;
#endif
#if DKR_OWNED_HAS_V80
    case rom::Revision::UsV80: return true;
#endif
    default: return false;
    }
}
std::string_view owned_adapter_identity(rom::Revision revision) {
    switch(revision) {
#if DKR_OWNED_HAS_V77
    case rom::Revision::UsV77: return DKR_OWNED_BUILD_ID_V77;
#endif
#if DKR_OWNED_HAS_V80
    case rom::Revision::UsV80: return DKR_OWNED_BUILD_ID_V80;
#endif
    default: return {};
    }
}
std::unique_ptr<OwnedGame> make_owned_game_for_revision(rom::Revision revision,
    std::span<const std::uint8_t> bootstrap,std::span<const std::uint8_t> rom,
    std::uint64_t epoch,PresentationMailbox& box,ConfirmedAudioSink sink,
    std::string& error,bool title,std::uint32_t codes,
    std::span<const std::uint8_t> save,OwnedBootOptions boot) {
    if(!owned_adapter_available(revision) || rom.size()!=rom::kRetailRomSize ||
       checkpoint_hash(rom)!=(revision==rom::Revision::UsV77?rom::kUsV77Xxh3:rom::kUsV80Xxh3)) {
        error="The accepted online revision does not match a linked owned replay engine and canonical Game Pak.";
        return {};
    }
    switch(revision) {
#if DKR_OWNED_HAS_V77
    case rom::Revision::UsV77: return dkr_experimental_v77_make_owned_game(bootstrap,rom,epoch,box,std::move(sink),error,title,codes,save,std::move(boot));
#endif
#if DKR_OWNED_HAS_V80
    case rom::Revision::UsV80: return dkr_experimental_v80_make_owned_game(bootstrap,rom,epoch,box,std::move(sink),error,title,codes,save,std::move(boot));
#endif
    default: return {};
    }
}
std::unique_ptr<OwnedGame> make_owned_game(std::span<const std::uint8_t> bootstrap,
    std::span<const std::uint8_t> rom,std::uint64_t epoch,PresentationMailbox& box,
    ConfirmedAudioSink sink,std::string& error,bool title,std::uint32_t codes,
    std::span<const std::uint8_t> save,OwnedBootOptions boot) {
    auto revision=rom::Revision::Unsupported;
    if(rom.size()==rom::kRetailRomSize) {
        const auto hash=checkpoint_hash(rom);
        if(hash==rom::kUsV77Xxh3)revision=rom::Revision::UsV77;
        if(hash==rom::kUsV80Xxh3)revision=rom::Revision::UsV80;
    }
    return make_owned_game_for_revision(revision,bootstrap,rom,epoch,box,std::move(sink),error,title,codes,save,std::move(boot));
}
}

#include "mods/online_game_pak.hpp"
#include "mods/online_mod_bundle.hpp"
#include "mods/online_preparation_notice.hpp"
#include "custom_tracks.hpp"
#include <iostream>
#include <stdexcept>
int main() {
    using namespace dkr::mods::online;
    using namespace dkr::runtime::rom;
    unsigned checks=0;
    const auto check=[&](bool value){if(!value)throw std::runtime_error("Online Game Pak boundary failed");++checks;};
    for(auto revision:{Revision::UsV77,Revision::UsV80}) {
        for(auto order:{ByteOrder::BigEndian,ByteOrder::ByteSwapped16,ByteOrder::LittleEndian32}) {
            Identity identity;identity.revision=revision;identity.byte_order=order;
            const auto asset=asset_revision(identity.revision);
            auto bundle=export_legacy({},std::string(asset));
            check(canonical(bundle.manifest).revision==asset);
            check(matches_game_pak(identity,bundle.manifest.revision));
            check(!matches_game_pak(identity,revision==Revision::UsV77?"us.v80":"us.v77"));
            check(!matches_game_pak(identity,revision==Revision::UsV77?"dkr.us.v77":"dkr.us.v80"));
            identity.error=InspectionError::UnsupportedRevision;
            check(!matches_game_pak(identity,asset));
        }
    }
    check(asset_revision(Revision::Unsupported).empty());
    check(!matches_game_pak(Identity{},{}));
    const std::vector<std::filesystem::path> imports{"v77", "v80"};
    unsigned constructed=0;
    const auto inspect=[](const auto& path){Identity i;i.revision=path=="v77"?Revision::UsV77:Revision::UsV80;return i;};
    for(auto revision:{Revision::UsV77,Revision::UsV80}) {
        const auto selected=with_matching_game_pak(imports,asset_revision(revision),{},inspect,
            [&](const auto& path){++constructed;return path;});
        check(selected==(revision==Revision::UsV77?"v77":"v80"));
    }
    check(constructed==2);
    for(bool cancelled:{false,true}) {
        std::stop_source stop;if(cancelled)stop.request_stop();
        bool refused=false;
        try{(void)with_matching_game_pak(imports,"dkr.us.v77",stop.get_token(),inspect,
            [&](const auto& path){++constructed;return path;});}catch(const dkr::mods::Error&){refused=true;}
        check(refused && constructed==2);
    }
    PreparationNotice notice;
    notice.progress("Preparing exact course revision");
    check(notice.fail("Missing source Game Pak","US v1.1","Experimental rollback"));
    notice.progress({}); // Idle/cancelled worker did not erase the UI notice.
    check(notice.visible() && notice.stage=="Preparing exact course revision");
    check(!notice.fail("Missing source Game Pak","US v1.1","Experimental rollback"));
    notice.clear();check(!notice.visible() && notice.stage.empty());
    std::array<std::vector<std::int32_t>, 7> tables;
    for(auto& table:tables)table={0,-1};
    // Large character rosters extend the texture table before Track Lab
    // preparation. Use its exact length, not the raw retail pointer bound.
    tables[4].assign(6002,0);tables[4].back()=-1;
    check(dkr::runtime::custom_tracks::prepare_tracks({},tables).courses.empty());
    tables[4].assign(32768,0);tables[4].back()=-1;
    check(dkr::runtime::custom_tracks::prepare_tracks({},tables).courses.empty());
    tables[4].insert(tables[4].end()-1,0);
    bool over_capacity=false;
    try{(void)dkr::runtime::custom_tracks::prepare_tracks({},tables);}
    catch(const std::runtime_error&){over_capacity=true;}
    check(over_capacity);
    std::cout<<checks<<" production online Game Pak boundary checks passed.\n";
}

#include "legacy_checkpoint.hpp"
#include "legacy_character_roster.hpp"
#include "legacy_character_menu.hpp"
#include "legacy_character_stage.hpp"
#include "legacy_character_menu_render.hpp"
#include "legacy_character_presentation.hpp"
#include "legacy_track_menu_adapter.hpp"
#include <bit>
#include <cmath>
#include <set>

namespace dkr::mods {
namespace {
// Addresses identify bytes in the captured guest RAM, never native objects.
// Restore still belongs to the exclusive CPU owner and the SAME RAM image.
std::uint32_t guest_pointer(CheckpointReader& in,bool optional=false,unsigned alignment=4) {
    const auto value=in.u32();
    if(optional && !value)return 0;
    if(value<0x80000000U || value>0x807ffffcU || value%alignment)
        throw Error("Mod sidecar has an invalid guest address.");
    return value;
}
void schema(CheckpointReader& in,unsigned version=1){if(in.u32()!=version)throw Error("Unsupported guest mod sidecar schema.");}
std::string roster_identity(const CharacterNamespace& assets) {
    if(!assets.roster_identity.empty())return assets.roster_identity;
    std::string ids;for(const auto& c:assets.characters)ids+=c.id;
    return sha256(View(reinterpret_cast<const std::uint8_t*>(ids.data()),ids.size()));
}
void identities(CheckpointWriter& out,const CharacterNamespace& assets) {
    out.u32(static_cast<std::uint32_t>(assets.characters.size()));
    out.text(roster_identity(assets),64);
}
void identities(CheckpointReader& in,const CharacterNamespace& assets) {
    if(in.bounded(CustomCharacterLimit)!=assets.characters.size())throw Error("Mod sidecar belongs to a different character namespace.");
    if(in.text(64)!=roster_identity(assets))throw Error("Mod sidecar character order/identity differs.");
}
std::string character(CheckpointReader& in,const CharacterNamespace& assets) {
    auto id=in.text(64);
    if(!id.empty() && std::ranges::none_of(assets.characters,[&](const auto& c){return c.id==id;}))
        throw Error("Mod sidecar refers to an unadmitted character.");
    return id;
}
void pointers(CheckpointWriter& out,const std::vector<std::uint32_t>& values) {
    out.u32(static_cast<std::uint32_t>(values.size()));for(auto p:values)out.u32(p);
}
std::vector<std::uint32_t> pointers(CheckpointReader& in,unsigned limit,bool optional) {
    std::vector<std::uint32_t> values;const auto count=in.bounded(limit);values.reserve(count);
    for(unsigned i=0;i<count;++i)values.push_back(guest_pointer(in,optional));return values;
}
}
Bytes CharacterRoster::checkpoint()const {
    CheckpointWriter out;out.u32(2);identities(out,*assets_);
    for(const auto& id:requested_)out.text(id,64);for(const auto& id:humans_)out.text(id,64);
    for(const auto& id:active_)out.text(id,64);for(bool value:ai_)out.flag(value);
    out.flag(allow_duplicates_);return std::move(out).finish();
}
CharacterRoster CharacterRoster::stage_checkpoint(View bytes)const {
    CheckpointReader in(bytes);schema(in,2);identities(in,*assets_);auto next=*this;
    for(auto& id:next.requested_)id=character(in,*assets_);for(auto& id:next.humans_)id=character(in,*assets_);
    for(auto& id:next.active_)id=character(in,*assets_);for(auto& value:next.ai_)value=in.flag();
    next.allow_duplicates_=in.flag();in.end();return next;
}
Bytes CharacterMenuAdapter::checkpoint()const {
    CheckpointWriter out;out.u32(2);identities(out,*assets_);
    const auto put=[&](auto value){out.flag(value.has_value());if(value)out.u32(static_cast<std::uint32_t>(*value));};
    for(auto value:custom_)put(value);for(auto value:displayed_)put(value);
    out.u32(owner_);out.flag(duplicates_);out.flag(drumstick_);out.flag(tt_);return std::move(out).finish();
}
CharacterMenuAdapter CharacterMenuAdapter::stage_checkpoint(View bytes)const {
    CheckpointReader in(bytes);schema(in,2);identities(in,*assets_);auto next=*this;
    const auto get=[&]()->std::optional<std::size_t>{if(!in.flag())return {};return in.bounded(static_cast<std::uint32_t>(entries().size()-1));};
    for(auto& value:next.custom_)value=get();for(auto& value:next.displayed_)value=get();
    next.owner_=in.bounded(3);next.duplicates_=in.flag();next.drumstick_=in.flag();next.tt_=in.flag();in.end();
    std::set<std::size_t> visible;
    for(auto value:next.displayed_)if(value && !visible.insert(*value).second)throw Error("Mod sidecar duplicates a visible character slot.");
    if(visible.size()!=std::min<std::size_t>(CustomStageSlots,entries().size()))throw Error("Mod sidecar loses a visible character slot.");
    for(auto value:next.custom_)if(value && !visible.contains(*value))throw Error("Mod sidecar selection is absent from its stage.");
    return next;
}
Bytes CharacterStage::checkpoint()const {
    // Native callbacks must have unwound before any owner checkpoint. A
    // pending stack entry is not a continuation and cannot be replayed safely.
    if(pending_entry_ || pending_header_)throw Error("Cannot checkpoint a nested custom actor spawn.");
    CheckpointWriter out;out.u32(1);for(auto f:fields_)out.u32(f);
    out.u32(static_cast<std::uint32_t>(actors_.size()));
    for(const auto& a:actors_) {
        out.u32(a.object);out.u32(static_cast<std::uint32_t>(a.entry));out.u32(a.slot);out.u32(a.sign_ticks);out.u32(a.sign_index);
        out.u32(static_cast<std::uint32_t>(a.sign_batches.size()));for(auto b:a.sign_batches)out.u32(b);
    }
    pointers(out,retiring_);return std::move(out).finish();
}
CharacterStage CharacterStage::stage_checkpoint(View bytes,std::size_t characters)const {
    if(!characters || characters>CustomCharacterLimit)throw Error("Invalid admitted stage namespace.");
    CheckpointReader in(bytes);schema(in);CharacterStage next;
    bool zero=false,nonzero=false;
    for(auto& f:next.fields_){f=guest_pointer(in,true,1);if(f)nonzero=true;else zero=true;}
    if(zero && nonzero)throw Error("Mod sidecar has an incomplete stage field ABI.");
    const auto count=in.bounded(CustomStageSlots);std::set<unsigned> slots;std::set<std::uint32_t> objects;
    for(unsigned i=0;i<count;++i) {
        Actor a;a.object=guest_pointer(in);a.entry=in.bounded(static_cast<std::uint32_t>(characters-1));a.slot=in.bounded(CustomStageSlots-1);
        a.sign_ticks=in.bounded(15);a.sign_index=in.bounded(3);const auto batches=in.bounded(4096);
        if(zero || !batches || !slots.insert(a.slot).second || !objects.insert(a.object).second)
            throw Error("Mod sidecar has an invalid custom stage actor.");
        std::set<unsigned> unique;
        for(unsigned b=0;b<batches;++b){const auto value=in.bounded(4095);if(!unique.insert(value).second)throw Error("Mod sidecar duplicates a sign batch.");a.sign_batches.push_back(value);}
        next.actors_.push_back(std::move(a));
    }
    next.retiring_=pointers(in,200,false);
    for(auto p:next.retiring_)if(!objects.insert(p).second)throw Error("Mod sidecar duplicates a retired stage actor.");
    if(zero && !next.retiring_.empty())throw Error("Uninitialized stage owns retired actors.");
    in.end();return next;
}
Bytes CharacterMenuRenderer::checkpoint()const {
    CheckpointWriter out;out.u32(1);out.u32(scratch_);pointers(out,portraits_);return std::move(out).finish();
}
CharacterMenuRenderer CharacterMenuRenderer::stage_checkpoint(View bytes,std::size_t characters)const {
    if(!characters || characters>CustomCharacterLimit)throw Error("Invalid menu drawing namespace.");
    CheckpointReader in(bytes);schema(in);CharacterMenuRenderer next;
    next.scratch_=guest_pointer(in,true);next.portraits_=pointers(in,static_cast<unsigned>(characters),false);
    if(!next.scratch_ && !next.portraits_.empty())throw Error("Custom portrait resources have no menu allocation.");
    in.end();return next;
}
Bytes CharacterPresentation::checkpoint()const {
    // HUD bindings describe a transient draw invocation. They MUST be gone
    // at the full-tick checkpoint boundary, never resurrected during replay.
    if(!hud_bindings_.empty())throw Error("Cannot checkpoint an outstanding custom HUD draw.");
    CheckpointWriter out;out.u32(2);out.flag(ready_);pointers(out,portrait_cells_);pointers(out,banks_);pointers(out,selection_banks_);
    for(auto p:cinematic_cells_)out.u32(p);return std::move(out).finish();
}
CharacterPresentation CharacterPresentation::stage_checkpoint(View bytes,std::size_t characters)const {
    if(!characters || characters>CustomCharacterLimit)throw Error("Invalid presentation namespace.");
    CheckpointReader in(bytes);schema(in,2);CharacterPresentation next;next.ready_=in.flag();
    next.portrait_cells_=pointers(in,static_cast<unsigned>(characters),true);
    next.banks_=pointers(in,static_cast<unsigned>(characters),true);
    next.selection_banks_=pointers(in,static_cast<unsigned>(characters),true);
    for(auto& p:next.cinematic_cells_)p=guest_pointer(in,true);in.end();
    if((next.ready_ && (next.portrait_cells_.size()!=characters || next.banks_.size()!=characters || next.selection_banks_.size()!=characters)) ||
       (!next.ready_ && (!next.portrait_cells_.empty() || !next.banks_.empty() || !next.selection_banks_.empty())))
        throw Error("Custom presentation resources are only partially initialized.");
    for(auto p:next.portrait_cells_)if(p && (p<0x80000010U || p>0x807ffff4U))throw Error("Custom portrait cell is outside its allocation.");
    for(auto p:next.cinematic_cells_)if(p && std::ranges::find(next.portrait_cells_,p)==next.portrait_cells_.end())throw Error("Cinematic portrait refers to an unprepared resource.");
    for(unsigned i=0;i<next.banks_.size();++i)if(next.banks_[i] && !next.portrait_cells_[i])throw Error("Race voice has no retained fallback handle.");
    return next;
}
Bytes TrackMenuAdapter::checkpoint()const {
    if(masked_ || background_y_ || candidate_)throw Error("Cannot checkpoint an outstanding custom track input/draw/load invocation.");
    CheckpointWriter out;out.u32(1);out.u32(static_cast<std::uint32_t>(tracks_.size()));
    for(const auto& t:tracks_)out.text(t.content_id,64);
    out.u32(name_address_);out.flag(allow_races_);out.flag(in_tracks_);
    const auto cursor=[&](const auto& value){out.flag(value.has_value());if(value){out.u32(static_cast<std::uint32_t>(value->row));out.u32(static_cast<std::uint32_t>(value->column));}};
    const auto scene=[&](const auto& value){out.flag(value.has_value());if(value){out.text(value->id,64);out.u32(value->carrier);}};
    cursor(move_);cursor(restore_);scene(pending_);scene(race_);out.text(preview_id_,64);return std::move(out).finish();
}
TrackMenuAdapter TrackMenuAdapter::stage_checkpoint(View bytes)const {
    CheckpointReader in(bytes);schema(in);
    if(in.bounded(512)!=tracks_.size())throw Error("Track sidecar belongs to a different custom catalogue.");
    for(const auto& t:tracks_)if(in.text(64)!=t.content_id)throw Error("Track sidecar order/identity differs.");
    auto next=*this;next.name_address_=guest_pointer(in,true,8);next.allow_races_=in.flag();next.in_tracks_=in.flag();
    if(next.allow_races_!=allow_races_)throw Error("Track sidecar changes the admitted race policy.");
    if(next.name_address_ && names_.size()>0x80800000U-next.name_address_)throw Error("Track name allocation is outside guest RAM.");
    const auto cursor=[&]()->std::optional<TrackCursor>{if(!in.flag())return {};const auto row=in.bounded(5U+static_cast<unsigned>((tracks_.size()+3)/4)-1);const auto column=in.bounded(3);return TrackCursor{static_cast<int>(row),static_cast<int>(column)};};
    const auto scene=[&]()->std::optional<TrackSceneChoice>{if(!in.flag())return {};TrackSceneChoice value{in.text(64),in.u32()};
        if(std::ranges::none_of(tracks_,[&](const auto& t){return t.content_id==value.id && t.carrier==value.carrier;}))
            throw Error("Track sidecar refers to an unadmitted course.");return value;};
    next.move_=cursor();next.restore_=cursor();next.pending_=scene();next.race_=scene();next.preview_id_=in.text(64);in.end();
    if(!next.preview_id_.empty() && std::ranges::none_of(tracks_,[&](const auto& t){return t.content_id==next.preview_id_;}))
        throw Error("Track sidecar preview is not admitted.");
    next.masked_=false;next.saved_x_=next.saved_y_=0;next.saved_buttons_=0;next.candidate_.reset();next.background_y_.reset();return next;
}
}

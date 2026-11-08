// Shadow metadata belongs to this renderer, scene and viewport. Reuse the
// established canonical/rigid-shadow renderer, not arbitrary vertex matching.
namespace {
std::uint64_t new_camera_shadow_history() {
    // A renderer/session restart can reuse the same scene/object addresses.
    // Allocate an owner lifetime in a host-local namespace, never game state.
    static std::atomic<std::uint64_t> next{1};
    const auto serial=next.fetch_add(1,std::memory_order_relaxed);
    return serial<0x0100000000000000ULL ? 0xC600000000000000ULL|serial : 0ULL;
}
}
void RenderPass::capture_shadows(const Range& range) {
    shadow_markers_.clear();
    const auto valid=[](std::uint32_t p,std::uint32_t size) {
        return p>=0x80000000U&&p<=0x80800000U-size;
    };
    const auto half=[&](std::uint32_t p) {
        std::uint16_t v=0; if(valid(p,2)&&!(p&1))std::memcpy(&v,decoder_.data()+((p&0xFFFFFFU)^2U),2);return v;
    };
    const auto byte=[&](std::uint32_t p) {
        return valid(p,1)?decoder_[(p&0xFFFFFFU)^3U]:std::uint8_t(0);
    };
    const auto number=[&](std::uint32_t p) {
        const auto bits=word(decoder_,p);float v;std::memcpy(&v,&bits,4);return v;
    };
    for(unsigned i=0;i<shadows_.count;++i) {
        const auto& source=shadows_.entries[i];
        if(source.begin<0x80800000U||source.end<source.begin||source.end>0x80900000U||
           (source.begin&7)||(source.end&7)||!valid(source.object,0x68)||!valid(source.shadow,0x10))
            throw mods::Error("Camera shadow observation is invalid.");
        const auto object=source.object,header=word(decoder_,object+0x40);
        const auto kind=half(object+0x48),asset=half(object+0x4A);
        const auto start=std::int16_t(half(source.shadow+8)),end=std::int16_t(half(source.shadow+10));
        ShadowMarker marker{};marker.address=source.begin&0xFFFFFFU;marker.begin=true;
        marker.batches=std::uint8_t(start>=0&&end>=start?
            std::min<int>(end-start,presentation::kMaximumCanonicalShadowBatches):0);
        auto owner=std::find_if(shadow_owners_.begin(),shadow_owners_.end(),[&](const ShadowOwner& o) {
            return o.object==object&&o.header==header&&o.asset==asset&&o.kind==kind&&
                o.view==range.view&&o.continuity==shadow_continuity_[range.view];
        });
        if(owner==shadow_owners_.end()&&shadow_owners_.size()<512U) {
            shadow_owners_.push_back({object,header,asset,kind,range.view,shadow_continuity_[range.view],new_camera_shadow_history()});
            owner=shadow_owners_.end()-1;
        }
        if(owner!=shadow_owners_.end()&&owner->history&&!range.pose.cut) {
            marker.identity=presentation::normalise_identity(std::uint32_t(owner->history)^std::uint32_t(owner->history>>32)^0x43534844U);
            marker.history=owner->history;
        }
        int vehicle=-1;
        if(kind==1) {
            const auto racer=word(decoder_,object+0x64);
            if(valid(racer,0x1D8)) {
                vehicle=byte(racer+0x1D6);
                if(vehicle>2)vehicle=byte(racer+0x1D7);
                if(vehicle>2)vehicle=-1;
            }
        }
        const auto texture=word(decoder_,source.shadow+4);
        marker.policy=presentation::rigid_shadow_owner_policy(kind,std::int8_t(vehicle),byte(texture),asset&presentation::kObjectAssetIdMask);
        marker.motion={{number(object+12),number(object+16),number(object+20)},std::int16_t(half(object+2)),true};
        marker.motion.valid=std::isfinite(marker.motion.position.x)&&std::isfinite(marker.motion.position.y)&&
            std::isfinite(marker.motion.position.z);
        shadow_markers_.push_back(marker);
        marker.address=source.end&0xFFFFFFU;marker.begin=false;shadow_markers_.push_back(marker);
    }
    std::stable_sort(shadow_markers_.begin(),shadow_markers_.end(),[](const auto& a,const auto& b){return a.address<b.address;});
}
std::span<const RenderPass::ShadowMarker> RenderPass::shadows_at(std::uint32_t physical)const {
    if(!drawing_||physical<0x800000U||physical>=0x900000U)return {};
    const auto first=std::lower_bound(shadow_markers_.begin(),shadow_markers_.end(),physical,
        [](const ShadowMarker& marker,std::uint32_t address){return marker.address<address;});
    auto last=first;while(last!=shadow_markers_.end()&&last->address==physical)++last;
    return {first,std::size_t(last-first)};
}

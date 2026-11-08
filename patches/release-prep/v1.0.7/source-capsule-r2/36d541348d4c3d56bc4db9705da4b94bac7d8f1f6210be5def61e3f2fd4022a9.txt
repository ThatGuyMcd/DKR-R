// Owner-thread observation of ADDED courses only. The original 65 headers and
// original 47 retail record pairs are never overwritten by this companion.
mods::Bytes course_records(std::uint8_t* rdram,const recomp_context& context,bool restore) {
    using namespace saves::records;
    const auto courses=authored_courses();if(courses.empty())return {};
    const auto payload=guest_payload();if(!payload||!payload->get_settings)return {};
    auto call=context;payload->get_settings(rdram,&call);const auto settings=std::uint32_t(call.r2);
    const auto valid=[](std::uint32_t p,unsigned length,unsigned alignment){return p>=0x80000000U&&p<0x80800000U&&length<=0x80800000U-p&&!(p&(alignment-1));};
    if(!valid(settings,0x48,4))return {};
    auto word=[&](std::uint32_t p){return std::uint32_t(MEM_W(0,static_cast<gpr>(static_cast<std::int32_t>(p))));};
    auto half=[&](std::uint32_t p)->std::int16_t&{return MEM_H(0,static_cast<gpr>(static_cast<std::int32_t>(p)));};
    Map remembered;
    if(restore&&!saves::records::decode(saves::records::seed(),remembered))throw mods::Error("Invalid custom-record launch seed.");
    Map snapshot;bool ready=false;
    for(const auto& course:courses) {
        if(course.level_id<65||course.level_id>=128)continue;
        const auto key=mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(course.id.data()),course.id.size()));
        for(unsigned vehicle=0;vehicle<3;++vehicle)if(course.vehicles&(1U<<vehicle)) {
            std::uint32_t p[4];bool good=true;
            for(unsigned kind=0;kind<4;++kind) {
                const auto table=word(settings+0x18+kind*12+vehicle*4);
                good=good&&valid(table,128*2,2);p[kind]=table+unsigned(course.level_id)*2;
            }
            if(!good)continue;
            ready=true;
            if(restore)if(auto it=remembered.find({key,vehicle});it!=remembered.end()) {
                const auto& r=it->second;half(p[0])=std::int16_t(r.lap_name);half(p[1])=std::int16_t(r.lap);
                half(p[2])=std::int16_t(r.course_name);half(p[3])=std::int16_t(r.course);
            }
            Record r{key,vehicle,std::uint16_t(half(p[1])),std::uint16_t(half(p[0])),std::uint16_t(half(p[3])),std::uint16_t(half(p[2]))};
            if(r.lap||r.course)snapshot.emplace(std::pair{key,vehicle},r);
        }
    }
    return ready?encode(snapshot):Bytes{};
}

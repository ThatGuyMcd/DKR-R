// Owner-thread access to the SAME revision-mapped gSettingsPtr that the native
// get_settings routine returns. Never execute guest code from this observer:
// owned capture/initial installation occur outside an active guest call fence.
mods::Bytes course_records(std::uint8_t* rdram,const recomp_context&,bool restore) {
    using namespace saves::records;
    const auto courses=authored_courses();if(courses.empty())return {};
    const auto payload=guest_payload();if(!payload)return {};
    auto revision=payload->revision;
    if(current_world().private_owner) {
        const auto assets=current_world().session.load();
        if(!assets||!assets->boot_bank())throw mods::Error("Custom records lost their admitted asset owner.");
        const auto id=assets->boot_bank()->revision();
        if(id!="us.v77"&&id!="us.v80")throw mods::Error("Custom records have an unsupported owned asset revision.");
        revision=id=="us.v80"?rom::Revision::UsV80:rom::Revision::UsV77;
    }
    if(revision!=rom::Revision::UsV77&&revision!=rom::Revision::UsV80)
        throw mods::Error("Custom records have no supported ROM revision.");
    const auto valid=[](std::uint32_t p,unsigned bytes,unsigned alignment) {
        return p>=0x80000000U&&p<0x80800000U&&bytes<=0x80800000U-p&&!(p&(alignment-1));
    };
    const auto word=[&](std::uint32_t p) {
        if(!valid(p,4,4))throw mods::Error("Custom record pointer is outside owned RDRAM.");
        std::uint32_t value;std::memcpy(&value,rdram+(p-0x80000000U),4);return value;
    };
    const auto read_half=[&](std::uint32_t p) {
        if(!valid(p,2,2))throw mods::Error("Custom record value is outside owned RDRAM.");
        std::uint16_t value;std::memcpy(&value,rdram+((p-0x80000000U)^2U),2);return value;
    };
    const auto write_half=[&](std::uint32_t p,std::uint16_t value) {
        if(!restore||!valid(p,2,2))throw mods::Error("Custom record restoration crossed its owned span.");
        std::memcpy(rdram+((p-0x80000000U)^2U),&value,2);
    };
    if(!rdram)return {};
    const auto settings=word(revision==rom::Revision::UsV80?0x80123A90U:0x80123510U);
    if(!valid(settings,0x48,4))return {};
    Map remembered;
    if(restore&&!decode(seed(),remembered))throw mods::Error("Invalid custom-record launch seed.");
    // Validate ALL destinations before writing any initial records. Capture
    // only reads; original course slots 0..64 can never become destinations.
    struct Row {std::string key;unsigned vehicle;std::array<std::uint32_t,4> p;};
    std::vector<Row> rows;rows.reserve(courses.size()*3);
    for(const auto& course:courses) {
        if(course.level_id<65||course.level_id>=128)continue;
        const auto key=mods::sha256(mods::View(reinterpret_cast<const std::uint8_t*>(course.id.data()),course.id.size()));
        for(unsigned vehicle=0;vehicle<3;++vehicle)if(course.vehicles&(1U<<vehicle)) {
            Row row{key,vehicle,{}};bool good=true;
            for(unsigned kind=0;kind<4;++kind) {
                const auto table=word(settings+0x18+kind*12+vehicle*4);
                if(!valid(table,128*2,2)){good=false;break;}
                row.p[kind]=table+unsigned(course.level_id)*2;
            }
            if(!good){if(restore)return {};continue;}
            rows.push_back(std::move(row));
        }
    }
    if(rows.empty())return {};
    Map snapshot;
    for(const auto& row:rows) {
        const auto& p=row.p;
        if(restore)if(const auto it=remembered.find({row.key,row.vehicle});it!=remembered.end()) {
            const auto& r=it->second;write_half(p[0],r.lap_name);write_half(p[1],r.lap);
            write_half(p[2],r.course_name);write_half(p[3],r.course);
        }
        Record r{row.key,row.vehicle,read_half(p[1]),read_half(p[0]),read_half(p[3]),read_half(p[2])};
        if(r.lap||r.course)snapshot.emplace(std::pair{row.key,row.vehicle},r);
    }
    return encode(snapshot);
}

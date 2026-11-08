namespace sdk2 {
constexpr unsigned Document=40000,DocumentData=40032,DocumentCapacity=16384;
std::vector<Json> documents(const Package& package) {
    std::vector<Json> out;if(!package.resources.contains("tools-ui"))return out;
    const auto ui=parse_document(package.resources.at("tools-ui"),256*1024);
    for(const auto& page:ui.at("pages"))for(const auto& field:page.at("fields"))if(field.at("type")=="document")out.push_back(field);
    return out;
}
}
std::string GameWorld::sdk2_course_identity()const {
    if(sdk2_current_course_){const auto id=sdk2_current_course_();if(!id.empty())return "legacy:"+id;}
    if(!native_bus_)return {};
    const auto level=unsigned(native_bus_->load(symbol("gMapId"),4));
    for(const auto& course:sdk2_courses_)if(course.carrier==level&&!course.id.starts_with("legacy:"))return course.id;
    return linked().revision+":level:"+std::to_string(level);
}
int GameWorld::sdk2_document(std::size_t index,Memory& bus,std::string_view name,Registers& c,Budget& budget) {
    const auto& owner=linked().owners[index];if(!owner.package->modules.contains("documents2"))return -5;
    const auto a=std::uint32_t(c.integer[4]),b=std::uint32_t(c.integer[5]),d=std::uint32_t(c.integer[6]);const auto key=guest_text(bus,a);
    const auto fields=sdk2::documents(*owner.package);
    const auto field=std::find_if(fields.begin(),fields.end(),[&](const auto& item){return item.at("document")==key;});
    if(field==fields.end())return -5;
    if(name=="dkr2_document_publish") {
        if(d>12000||d>budget.instructions)return -6;Bytes payload(d);bus.read(b,payload);budget.instructions-=d;
        try {
            auto value=parse_document(payload,12000);Json wrapper={{"schema",2},{"package",owner.package->id},{"digest",owner.package->digest},
                {"revision",linked().revision},{"course",sdk2_course_identity()},{"kind",key},{"payload",std::move(value)}};
            if(wrapper.dump().size()>16384)return -6;if(!online_||sdk2_frame_agreed_)dkr::sdk2::document(unsigned(index),key,std::move(wrapper));return 0;
        }catch(const std::exception&){return -1;}
    }
    const auto at=owner.services+sdk2::Document;
    if(!bus.load(at+20,4)||bus.load(at+16,4)!=unsigned(field-fields.begin()))return 1;
    const auto bytes=unsigned(bus.load(at,4));const auto capacity=std::uint32_t(c.integer[7]);
    if(name=="dkr2_document_read") {
        if(b>bytes||capacity>bytes-b||capacity>4096)return -1;
        std::array<std::uint8_t,4096> copy{};bus.read(owner.services+sdk2::DocumentData+b,std::span(copy).first(capacity));bus.write(d,std::span(copy).first(capacity));return int(capacity);
    }
    if(name=="dkr2_document_size")return int(bytes);
    if(name=="dkr2_document_accept") {
        if(sdk2_phase_!=2)return -3;bus.store(at+20,4,0);return 0;
    }
    return -5;
}
bool GameWorld::sdk2_document_command(const dkr::sdk2::Command& command,std::size_t index,Memory& bus) {
    const auto action=command.words[2];if(action<65532)return false;
    const auto& owner=linked().owners[index];const auto at=owner.services+sdk2::Document;
    const auto reject=[&](const char* why){bus.fill(at,32);dkr::sdk2::notice(why);};
    if(!owner.package->modules.contains("documents2")||command.words[15]!=bus.load(owner.services+sdk2::Header+4,4)){reject("Document import target changed. Import it again in the intended scene.");return true;}
    if(action==65534) {
        const auto fields=sdk2::documents(*owner.package);
        if(command.words[3]<1||command.words[3]>sdk2::DocumentCapacity||command.words[5]>=fields.size()){reject("Document transfer exceeds its schema or byte budget.");return true;}
        bus.fill(at,32);bus.store(at,4,command.words[3]);bus.store(at+4,4,command.words[4]);bus.store(at+16,4,command.words[5]);bus.store(at+24,4,1);return true;
    }
    if(!bus.load(at+24,4)){reject("No document upload is active.");return true;}
    const unsigned length=unsigned(bus.load(at,4)),received=unsigned(bus.load(at+12,4));
    if(action==65533) {
        const unsigned offset=command.words[3],bytes=command.words[4];
        if(bytes>40||offset!=received||offset>length||bytes>length-offset){reject("Document upload arrived out of order.");return true;}
        for(unsigned i=0;i<bytes;++i)bus.store(owner.services+sdk2::DocumentData+offset+i,1,(command.words[5+i/4]>>(24-(i%4)*8))&255U);
        bus.store(at+12,4,offset+bytes);return true;
    }
    if(received!=length){reject("Document transfer is incomplete.");return true;}
    Bytes bytes(length);bus.read(owner.services+sdk2::DocumentData,bytes);std::uint32_t hash=2166136261U;for(auto c:bytes){hash^=c;hash*=16777619U;}
    if(hash!=bus.load(at+4,4)){reject("Document checksum did not match.");return true;}
    try {
        const auto value=parse_document(bytes,sdk2::DocumentCapacity);const auto fields=sdk2::documents(*owner.package);const auto field=unsigned(bus.load(at+16,4));
        if(value.at("schema")!=2||value.at("package")!=owner.package->id||value.at("digest")!=owner.package->digest||value.at("revision")!=linked().revision||
           value.at("course")!=sdk2_course_identity()||value.at("kind")!=fields.at(field).at("document")||!value.contains("payload")){reject("This document belongs to another mod edition, course, revision or schema.");return true;}
        const auto payload=value.at("payload").dump();bus.write(owner.services+sdk2::DocumentData,View(reinterpret_cast<const std::uint8_t*>(payload.data()),payload.size()));
        bus.store(at,4,payload.size());bus.store(at+20,4,1);bus.store(at+24,4,0);
        auto invoke=command;invoke.words[2]=fields[field].at("import_action").get<unsigned>();
        bus.store(owner.services+20480,4,1);for(unsigned i=0;i<16;++i)bus.store(owner.services+20496+i*4,4,invoke.words[i]);
    }catch(const std::exception&){reject("The document is not a valid SDK 2 scene patch.");}
    return true;
}

// Immutable metadata only, prepared once outside VM callbacks. No source paths,
// ROM bytes, textures or mutable launcher selection are exported to a code mod.
static std::string sdk2_asset_inventory(const std::shared_ptr<const mods::RuntimeSession>& session) {
    if(!session)return {};using Json=mods::code::Json;const auto& bank=*session->boot_bank();Json output={{"schema",2},{"banks",Json::object()},{"characters",Json::array()}};
    for(const auto [name,section]:std::array<std::pair<const char*,unsigned>,6>{{{"models",29},{"textures-3d",2},{"textures-2d",4},{"sprites",12},{"object-templates",34},{"level-models",27}}})
        output["banks"][name]={{"section",section},{"count",bank.record_count(section)},{"bank",bank.fingerprint()},{"scope","admitted-boot-namespace"}};
    if(const auto names=session->characters())for(const auto& item:names->characters) {
        Json character={{"id",item.id},{"name",item.name},{"donor",item.base_character},{"portrait",item.portrait},{"headers",item.headers},{"stage_header",item.stage_header},{"models",Json::array()}};
        for(auto header_id:item.headers) {
            const auto header=bank.record(34,header_id);if(header.size()<0x78)throw mods::Error("SDK inventory found a truncated admitted character header.");
            const auto first=mods::be32(header,0x10);const unsigned count=header[0x55];if(count>127||first>header.size()||count>(header.size()-first)/4)throw mods::Error("SDK inventory found an invalid character model list.");
            Json models=Json::array();for(unsigned i=0;i<count;++i){const auto id=mods::be32(header,first+i*4);if(id!=UINT32_MAX)models.push_back(id);}character["models"].push_back(std::move(models));
        }output["characters"].push_back(std::move(character));
    }
    const auto bytes=output.dump();if(bytes.size()>2*mods::MiB)throw mods::Error("SDK asset catalogue exceeds its metadata limit.");return bytes;
}

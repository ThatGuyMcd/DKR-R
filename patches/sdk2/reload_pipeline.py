"""Instruction-only offline reload; migration cannot touch native game RAM."""
def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name=='magic_code_package.hpp':
        one('    bool development=false;', '    bool development=false;\n    bool sdk2_reload=false;\n    unsigned sdk2_state_schema=0;')
    elif name=='magic_code_package.cpp':
        one('    sdk2_schema(*p);', '''    if(meta.contains("live_reload")) {
        if(p->api!=2||!p->development||meta.at("live_reload")!="instruction-only"||!meta.contains("state_schema")||
           !meta.at("state_schema").is_number_unsigned()||meta.at("state_schema").get<std::uint64_t>()<1||meta.at("state_schema").get<std::uint64_t>()>UINT32_MAX)
            bad("Live reload requires API 2 development, instruction-only and a nonzero state_schema");
        p->sdk2_reload=true;p->sdk2_state_schema=meta.at("state_schema").get<unsigned>();
    }
    sdk2_schema(*p);''')
    elif name=='magic_code_world.hpp':
        one('    bool idle()const', '''    struct Rebind {std::shared_ptr<const LinkedSet> linked;std::unique_ptr<Memory> bus;int migration=-1;};
    std::shared_ptr<const LinkedSet> image()const{return linked_;}
    std::unique_ptr<Rebind> prepare_rebind(std::shared_ptr<const LinkedSet>,int migration=-1)const;
    void exchange(Rebind&)noexcept;
    void owner_event(unsigned,std::string_view,Budget&);
    bool idle()const''')
        one('    unsigned active_=0;', '    unsigned active_=0;\n    int migration_owner_=-1;')
    elif name=='magic_code_link.cpp':
        one('event.event_name=="on_tool"&&p.api!=2','(event.event_name=="on_tool"||event.event_name=="on_cleanup"||event.event_name=="on_reload")&&p.api!=2')
    elif name=='magic_code_world.cpp':
        one('void World::gateway(std::uint32_t address,Registers& c,Budget& budget) {', '''void World::gateway(std::uint32_t address,Registers& c,Budget& budget) {
    if(migration_owner_>=0) {
        const auto found=linked_->imports.find(address);
        if(found==linked_->imports.end()||found->second.owner!=unsigned(migration_owner_)||found->second.event||found->second.dependency!="*"||
           !core_policy().services.contains(found->second.name))bad("reload migration permits only this owner's core services");
    }''')
        one('void World::detach() {', '''std::unique_ptr<World::Rebind> World::prepare_rebind(std::shared_ptr<const LinkedSet> image,int migration)const {
    if(!idle()||ram_.empty()||!image||image->resident_bytes!=linked_->resident_bytes)bad("reload needs a retired equal-size world");
    auto regions=image->regions;
    if(migration>=0)for(auto& r:regions)if(r.writable) {
        const auto& owner=image->owners.at(unsigned(migration));r.writable=r.address>=owner.begin&&r.address+std::uint64_t(r.size)<=owner.end;
    }
    regions.push_back({0x80000000U,8*unsigned(MiB),migration<0,false});
    auto result=std::make_unique<Rebind>();result->linked=std::move(image);result->migration=migration;
    result->bus=std::make_unique<Memory>(ram_,std::move(regions));return result;
}
void World::exchange(Rebind& value)noexcept {
    linked_.swap(value.linked);bus_.swap(value.bus);std::swap(migration_owner_,value.migration);
    for(const auto& r:linked_->regions)if(r.executable) {
        const auto image=std::find_if(linked_->sections.begin(),linked_->sections.end(),[&](const auto& s){return r.address>=s.address&&r.address-s.address<s.bytes.size();});
        // All extents/alignment were admitted by link(), before the swap.
        std::memcpy(ram_.data()+r.address-0x80000000U,image->word_swapped.data()+r.address-image->address,r.size);
    }
}
void World::owner_event(unsigned owner,std::string_view name,Budget& budget) {
    const auto found=linked_->events.find("*:"+std::string(name));if(found==linked_->events.end())return;
    for(auto address:found->second)if(linked_->function_owners.at(address)==owner){Registers c;call(address,c,budget);}
}
void World::detach() {''')
    elif name=='magic_code_game_world.hpp':
        one('    unsigned sdk2_pulse();', '    unsigned sdk2_pulse();\n    void sdk2_reload();')
    elif name=='magic_code_game_world.cpp':
        one('#include "sdk2_overrides.inl"','#include "sdk2_overrides.inl"\n#include "sdk2_reload.inl"')
        one('    p.modules.emplace("overrides2",2);','    p.modules.emplace("reload2",2);p.events.insert({"on_cleanup","on_reload"});\n    p.modules.emplace("overrides2",2);')
    return text

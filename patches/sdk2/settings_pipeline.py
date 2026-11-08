"""Explicit opt-in cross-mod live settings, preserving API 1 frozen semantics."""
def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name=='magic_code_package.hpp':
        one('    bool sdk2_reload=false;', '    std::set<std::string> sdk2_live_options;\n    bool sdk2_reload=false;')
    elif name=='magic_code_package.cpp':
        one('    sdk2_schema(*p);', '''    if(meta.contains("live_options")) {
        if(p->api!=2||!p->modules.contains("live-settings2")||!meta.at("live_options").is_array()||meta.at("live_options").size()>128)bad("Live options require API 2/live-settings2 and a bounded list");
        for(const auto& id:meta.at("live_options")) {
            if(!id.is_string()||!valid_id(id.get<std::string>())||!p->sdk2_live_options.insert(id.get<std::string>()).second)bad("Invalid or duplicate live option ID");
            const auto option=std::find_if(p->options.begin(),p->options.end(),[&](const auto& item){return item.at("id")==id;});
            if(option==p->options.end()||(option->at("type")!="Number"&&option->at("type")!="Enum"))bad("Only explicitly reviewed numeric/enum options can be live");
        }
    }
    sdk2_schema(*p);''')
    elif name=='magic_code_world.hpp':
        one('    void owner_event(unsigned,std::string_view,Budget&);','    void owner_event(unsigned,std::string_view,Budget&,const Registers& args={});')
    elif name=='magic_code_world.cpp':
        one('void World::owner_event(unsigned owner,std::string_view name,Budget& budget) {','void World::owner_event(unsigned owner,std::string_view name,Budget& budget,const Registers& args) {')
        one('if(linked_->function_owners.at(address)==owner){Registers c;call(address,c,budget);}','if(linked_->function_owners.at(address)==owner){auto c=args;call(address,c,budget);}')
    elif name=='magic_code_link.cpp':
        one('event.event_name=="on_reload")&&p.api!=2','event.event_name=="on_reload"||event.event_name=="on_live_setting")&&p.api!=2')
    elif name=='magic_code_game_world.hpp':
        one('    int sdk2_tune(Memory&,std::size_t,unsigned);','    int sdk2_tune(Memory&,std::size_t,unsigned);\n    int sdk2_setting_apply(Memory&,std::size_t,unsigned,unsigned,std::uint32_t,Budget&);')
    elif name=='magic_code_game_world.cpp':
        one('#include "sdk2_reload.inl"','#include "sdk2_reload.inl"\n#include "sdk2_settings.inl"')
        one('    p.modules.emplace("reload2",2);','    p.services.insert("dkr2_setting_apply");p.events.insert("on_live_setting");\n    p.modules.emplace("reload2",2);')
    return text

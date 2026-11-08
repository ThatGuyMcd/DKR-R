"""Freeze logical resources in launch admission, never from a live inspector."""
FILES=('src/game/mods/legacy_mod_launch.cpp','src/game/mods/online_mod_profile.cpp',
       'src/game/mods/magic_code_trophy_catalog.cpp','src/game/mods/magic_code_trophy_catalog.hpp',
       'src/game/mods/online_mod_runtime.cpp')


def transform(name,text,once):
    if name.endswith(('legacy_mod_launch.cpp','online_mod_profile.cpp')):
        text=once(text,'return p->modules.contains("trophies")||!p->tracks.empty();',
                    'return (p->api==2&&p->modules.contains("catalogues2"))||p->modules.contains("trophies")||!p->tracks.empty();')
        if name.endswith('legacy_mod_launch.cpp'):
            text=once(text,'                auto courses=code::trophy::installed_catalogue(', '                code::Json sdk2_inventory;\n                auto courses=code::trophy::installed_catalogue(')
            text=once(text,'labs,stop,false);','labs,stop,false,&sdk2_inventory);')
            text=once(text,'code::trophy::rebind(*rebound,courses,online::asset_revision(dkr::runtime::rom::inspect(rom).revision));','code::trophy::rebind(*rebound,courses,online::asset_revision(dkr::runtime::rom::inspect(rom).revision),sdk2_inventory);')
        else:
            text=once(text,'            const auto courses=with_matching_game_pak(', '            code::Json sdk2_inventory;\n            const auto courses=with_matching_game_pak(')
            text=once(text,'code::trophy::installed_catalogue(path,library,labs,stop);','code::trophy::installed_catalogue(path,library,labs,stop,true,&sdk2_inventory);')
            text=once(text,'code::trophy::rebind(frozen,courses,bundle.manifest.revision);','code::trophy::rebind(frozen,courses,bundle.manifest.revision,sdk2_inventory);')
        return text
    if name.endswith('magic_code_trophy_catalog.hpp'):
        text=once(text,'std::stop_token,bool canonical_order=true);','std::stop_token,bool canonical_order=true,Json* sdk2_inventory=nullptr);')
        text=once(text,'void rebind(FrozenSet&,const std::vector<Course>&,std::string_view revision);','void rebind(FrozenSet&,const std::vector<Course>&,std::string_view revision,const Json& inventory=Json::object());')
        text=once(text,'void verify_configuration(const FrozenSet&,const std::vector<Course>&);','void verify_configuration(const FrozenSet&,const std::vector<Course>&);\nJson sdk2_inventory(const AssetBank&,Json characters);\nvoid verify_sdk2_inventory(const FrozenSet&,const AssetBank&,const CharacterNamespace*);')
        return text
    if name.endswith('online_mod_runtime.cpp'):
        return once(text,'        code::trophy::verify_configuration(*out->code_,', '        code::trophy::verify_sdk2_inventory(*out->code_,*out->stock_,out->characters_.get());\n        code::trophy::verify_configuration(*out->code_,')
    if name.endswith('magic_code_trophy_catalog.cpp'):
        text=once(text,'void rebind(FrozenSet& code,const std::vector<Course>& courses,std::string_view revision) {', '''void rebind(FrozenSet& code,const std::vector<Course>& courses,std::string_view revision,const Json& inventory) {
    Json catalogue=Json::array();for(const auto& course:courses)catalogue.push_back(course_document(course));
    for(const auto& p:code.ordered)if(p->api==2&&p->modules.contains("catalogues2")) {
        auto& data=code.data[p->id];if(data.is_null())data=Json::object();
        data["_sdk2_catalogue"]={{"schema",2},{"courses",catalogue},{"inventory",inventory}};
        (void)structured_data(data);
    }''')
        text=once(text,'void verify_configuration(const FrozenSet& code,const std::vector<Course>& courses) {', '''void verify_configuration(const FrozenSet& code,const std::vector<Course>& courses) {
    Json catalogue=Json::array();for(const auto& course:courses)catalogue.push_back(course_document(course));
    for(const auto& p:code.ordered)if(p->api==2&&p->modules.contains("catalogues2")) {
        const auto& data=code.data.at(p->id);
        if(!data.contains("_sdk2_catalogue")||data.at("_sdk2_catalogue").at("schema")!=2||data.at("_sdk2_catalogue").at("courses")!=catalogue)
            throw Error(p->name+": frozen SDK 2 resource catalogue differs from the admitted game assets.");
    }''')
        text=once(text,'std::vector<Course> installed_catalogue(','''Json sdk2_inventory(const AssetBank& stock,Json characters) {
    std::sort(characters.begin(),characters.end(),[](const auto& a,const auto& b){return a.at("id").template get<std::string>()<b.at("id").template get<std::string>();});
    Json banks=Json::object();for(const auto [name,section]:std::array<std::pair<const char*,unsigned>,6>{{{"models",29},{"textures-3d",2},{"textures-2d",4},{"sprites",12},{"object-templates",34},{"level-models",27}}})
        banks[name]={{"section",section},{"count",stock.record_count(section)},{"bank",stock.base_fingerprint()},{"scope","native-base"}};
    return {{"schema",2},{"banks",banks},{"characters",characters}};
}
void verify_sdk2_inventory(const FrozenSet& code,const AssetBank& stock,const CharacterNamespace* names) {
    Json characters=Json::array();if(names)for(const auto& item:names->characters)characters.push_back({{"id",item.id},{"name",item.name},{"donor",item.base_character}});
    const auto actual=sdk2_inventory(stock,std::move(characters));
    for(const auto& package:code.ordered)if(package->api==2&&package->modules.contains("catalogues2")) {
        const auto& expected=code.data.at(package->id).at("_sdk2_catalogue").at("inventory");
        if(expected.at("schema")!=2||expected.at("banks")!=actual.at("banks")||expected.at("characters").size()!=actual.at("characters").size())throw Error("SDK resource inventory differs from admitted native assets.");
        for(unsigned i=0;i<actual.at("characters").size();++i)if(expected.at("characters")[i].at("id")!=actual.at("characters")[i].at("id")||expected.at("characters")[i].at("donor")!=actual.at("characters")[i].at("donor"))throw Error("SDK character catalogue differs from the admitted character namespace.");
    }
}
std::vector<Course> installed_catalogue(''')
        text=once(text,'std::stop_token stop,bool canonical_order) {','std::stop_token stop,bool canonical_order,Json* inventory) {')
        text=once(text,'\n    if(stop.stop_requested())throw Error("Cup catalogue cancelled.");', '''
    if(inventory) {
        Json characters=Json::array();for(const auto& item:TrackCatalog::selected_items(legacy,stock->revision(),TrackCatalog::Kind::Character))characters.push_back({{"id",item.id},{"name",item.name},{"donor",item.base_character}});
        *inventory=sdk2_inventory(*stock,std::move(characters));
    }
    if(stop.stop_requested())throw Error("Cup catalogue cancelled.");''')
        return text
    raise ValueError('Unreviewed catalogue input: '+name)

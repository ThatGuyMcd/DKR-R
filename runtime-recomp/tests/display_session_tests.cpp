#include "display_settings/policy.hpp"
#include "session_content/policy.hpp"
#include "launcher_themes/regional_sources.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <iostream>

int main() {
    namespace display=dkr::display;
    using display::Aspect;
    // Explicit supersampling is independent of the physical monitor.
    auto four_k=display::resolve({Aspect::Wide,2160,1},1920,1080);
    assert((four_k.render==display::Size{3840,2160}));
    assert((four_k.output==display::Size{1920,1080}));
    for(unsigned ratio=0;ratio<display::Ratios.size();++ratio) {
        display::Settings s{Aspect(ratio),0,1};
        for(auto size:{display::Size{1920,1080},display::Size{1280,800},display::Size{720,1280},display::Size{0,0}}) {
            const auto fitted=display::resolve(s,size.width,size.height);
            assert(fitted.render.height<=std::max(1U,size.height));
            if(size.width&&size.height)assert(fitted.render.width<=size.width+1);
            const auto options=display::choices(s,size.width,size.height);
            assert(!options.empty());
            for(auto option:options) {
                assert(option.size.width<=display::DimensionLimit&&option.size.height<=display::DimensionLimit);
                assert(std::abs(double(option.size.width)/option.size.height-fitted.aspect)<.005);
            }
        }
    }
    assert((display::resolve({Aspect::Original,0,1},1920,1080).render==display::Size{1440,1080}));
    assert((display::resolve({Aspect::Deck,800,1},1920,1080).render==display::Size{1280,800}));
    assert((display::resolve({Aspect::Window,0,1},1280,800).render==display::Size{1280,800}));
    assert((display::resolve({Aspect::Window,0,1},1920,1080).render==display::Size{1920,1080}));
    for(int resolution=0;resolution<=2;++resolution)for(int aspect=0;aspect<2;++aspect)for(int sample:{1,2,4,8}) {
        display::loaded=false;display::migrate(resolution,aspect,sample);
        const auto migrated=display::resolve(display::preference,1920,1080);
        assert(migrated.render.height==unsigned(resolution==2?1200:(resolution==1?480:240)*sample));
        assert(migrated.downsample==sample&&display::legacy_matches(resolution,aspect,sample));
        const auto before=display::preference;display::migrate(0,0,1);assert(before==display::preference);
    }
    display::load("display_aspect",int(Aspect::Wide));display::load("display_height",2160);
    display::load("display_legacy_downsample",1);std::ostringstream saved;display::save(saved);
    assert(saved.str()=="display_aspect=4\ndisplay_height=2160\ndisplay_legacy_downsample=1\n");
    display::load("display_aspect",-10);display::load("display_height",100000);display::load("display_legacy_downsample",0);
    assert((display::preference==display::Settings{}));assert(!display::load("other",4));
    display::device_limit=4096;
    const auto capped=display::resolve({Aspect::Wide,4320,1},1920,1080);
    assert(capped.render.width<=4096&&capped.render.height<=4096);
    assert((display::resolve({Aspect::Wide,2160,1},1920,1080).render==display::Size{3840,2160}));
    display::device_limit=display::DimensionLimit;
    namespace content=dkr::session_content;
    content::next_enabled=false;
    {content::Preparation frozen(true);assert(content::preparing());
     {content::Preparation nested(false);assert(!content::preparing());}assert(content::preparing());
     bool separate=false;std::thread reader([&]{separate=!content::preparing();});reader.join();assert(separate);}
    assert(!content::preparing());
    {content::Session session(true,false);content::next_enabled=false;
     assert(content::local_assets_allowed()&&content::textures_allowed());}
    {content::Session session(false,false);content::next_enabled=true;
     assert(!content::local_assets_allowed()&&!content::textures_allowed());}
    {content::Session session(true,true);assert(!content::local_assets_allowed()&&content::textures_allowed());}
    assert(!content::running_active&&content::next_enabled&&content::local_assets_allowed());
    // Generic Gaming Mode locales cannot hide the configured desktop territory.
    using namespace dkr::launcher_themes;
    const auto plasma=parse_regional("[Translations]\nLANG=de_DE\n[Formats]\nLANG=\"en_GB.UTF-8\"\n",true);
    RegionalValues generic{{"LC_ALL","C.UTF-8"},{"LANG","C"}};
    assert(resolve_country(plasma,generic,{}).country=="GB");
    assert(resolve_country({},generic,{{"LANG","fr_FR.UTF-8"}}).country=="FR");
    assert(resolve_country({},{{"LANG","en_CA"}},{ {"LANG","en_GB"}}).country=="GB");
    assert(resolve_country({},{{"LC_TIME","de_DE"}},{ {"LANG","en_GB"}}).country=="DE");
    assert(resolve_country({},generic,{}).country.empty());
    assert(parse_regional(std::string(65537,'a'),false).empty());
    assert(parse_regional("[Translations]\nLANG=en_GB",true).empty());
    assert(regional_country(parse_regional("LC_ALL=C\nLC_TIME='en_AU.UTF-8'\n",false))=="AU");
    const auto temp=std::filesystem::temp_directory_path()/("dkr-regional-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {std::ofstream file(temp,std::ios::binary);file<<std::string(65537,'x');}
    assert(read_regional(temp,false).empty());
    {std::ofstream file(temp,std::ios::binary);file<<"LANG=en_GB.UTF-8\n";}
    assert(regional_country(read_regional(temp,false))=="GB");std::filesystem::remove(temp);
    std::cout<<"Display, frozen session content, and regional fallback checks passed\n";
}

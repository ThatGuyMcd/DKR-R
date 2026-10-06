#include "legacy_track_menu_adapter.hpp"
#include <bit>
#include <iostream>

using namespace dkr::mods;
namespace {
unsigned checks=0;
void check(bool result,const char* message){++checks;if(!result)throw Error(message);}
template<class F>void rejects(F fn){bool caught=false;try{fn();}catch(const Error&){caught=true;}check(caught,"Expected a bounded adapter error");}
struct Fixture {
    Bytes memory=Bytes(MiB,0);
    TrackMenuFields fields{};
    TrackMenuAdapter adapter;
    Fixture(bool races=true,std::vector<Root> courses=roots()):adapter(std::move(courses),races) {
        for(unsigned i=0;i<fields.size();++i)fields[i]=0x80001000+i*256;
        put(MenuField::FutureFunLand,65535,0,2);put(MenuField::Height,240);put(MenuField::HalfHeight,120);
        put(MenuField::PreviewCarrier,5);put(MenuField::LoadedCarrier,5);
        for(unsigned r=0;r<5;++r)for(unsigned c=0;c<6;++c)put(MenuField::StockIDs,5+r*6+c,(r*6+c)*2,2);
        event(15,15);event(0);adapter.install_names(memory,0x80010000);event(1);
    }
    static std::vector<Root> roots() {
        std::vector<Root> result(5);
        for(unsigned i=0;i<result.size();++i) {
            result[i].content_id=std::string(64,'a'+i);result[i].name="CUSTOM COURSE "+std::to_string(i);
            result[i].carrier=5;result[i].vehicles=i==0?1:7;
        }return result;
    }
    void put(MenuField field,std::uint32_t value,unsigned offset=0,unsigned size=4) {
        const auto at=(fields[static_cast<unsigned>(field)]&0x1fffffff)+offset;
        for(unsigned i=0;i<size;++i)memory[(at+i)^3]=static_cast<std::uint8_t>(value>>((size-i-1)*8));
    }
    std::uint32_t get(MenuField field,unsigned offset=0,unsigned size=4)const {
        const auto at=(fields[static_cast<unsigned>(field)]&0x1fffffff)+offset;std::uint32_t v=0;
        for(unsigned i=0;i<size;++i)v=(v<<8)|memory[(at+i)^3];return v;
    }
    void real(MenuField field,float value){put(field,std::bit_cast<std::uint32_t>(value));}
    float real(MenuField field)const{return std::bit_cast<float>(get(field));}
    TrackMenuEffect event(unsigned event,std::uint32_t arg=0,std::uint32_t ret=0){return adapter.apply(event,memory,fields,arg,ret);}
    void select(int row,int col){put(MenuField::CursorY,row);put(MenuField::CursorX,col);}
    void input(int x,int y) {
        const auto previous_x=get(MenuField::CursorX),previous_y=get(MenuField::CursorY);
        put(MenuField::Buttons,0,16);put(MenuField::StickX,x,8,2);put(MenuField::StickY,y,8,2);
        check(!event(2).navigation_sound,"Custom move pinged before committing selection");
        check(get(MenuField::StickX,8,2)==0 && get(MenuField::StickY,8,2)==0,"Custom navigation leaked into retail array indexing");
        const auto result=event(3);
        check(result.navigation_sound==(previous_x!=get(MenuField::CursorX) || previous_y!=get(MenuField::CursorY)),"Navigation ping must occur exactly for successful custom moves");
        check(!event(3).navigation_sound,"Custom navigation ping was repeated");
        check(get(MenuField::StickX,8,2)==static_cast<std::uint16_t>(x) &&
            get(MenuField::StickY,8,2)==static_cast<std::uint16_t>(y),"Input was not restored");
    }
};
void run() {
    Fixture f;const auto stock=f.memory;
    const auto boot_sidecar=f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(boot_sidecar).checkpoint()==boot_sidecar,"Initial Track Select sidecar differs.");
    for(std::size_t cut=0;cut<boot_sidecar.size();++cut)rejects([&]{f.adapter.stage_checkpoint(View(boot_sidecar).first(cut));});
    check(!f.event(2).navigation_sound && !f.event(3).navigation_sound,"Stock navigation received a second ping");
    check(f.memory==stock,"Unrelated stock input changed guest memory");
    check(f.get(MenuField::CursorX)==0 && f.get(MenuField::CursorY)==0,"Fresh native entry did not retain Dino Domain");
    f.select(2,1);f.event(15,15);f.event(0);f.event(1);
    check(f.get(MenuField::CursorX)==1 && f.get(MenuField::CursorY)==2,"Original remembered stock selection was replaced");
    f.select(3,0);f.input(0,-1);check(f.get(MenuField::CursorY)==4,"Custom rows are not reachable below stock");
    check(f.real(MenuField::TargetY)==-960 && f.get(MenuField::PreviewCarrier)==5,"Custom navigation did not update preview/target");
    f.put(MenuField::Opacity,32);f.event(5);check(f.get(MenuField::LoadedCarrier)==0xffffffff,"Same-carrier custom preview was not reloaded");
    f.event(6,5);f.event(7,0,1);f.put(MenuField::BackgroundBusy,1);f.put(MenuField::LoadedCarrier,5);
    const auto pending_sidecar=f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(pending_sidecar).checkpoint()==pending_sidecar,"Confirmed preview identity lost during sidecar restore.");
    f.input(1,0);check(f.get(MenuField::CursorX)==1,"Second same-carrier track cannot be selected");
    auto effect=f.event(8,5);check(effect.scene && effect.scene->id==std::string(64,'a'),"Rapid navigation replaced the accepted loader identity");
    check(!f.event(8,5).scene,"A preview request was consumed twice");
    f.event(5);check(f.get(MenuField::LoadedCarrier)==0xffffffff,"New logical selection reused the old preview");
    f.put(MenuField::Buttons,0x9000,16);f.event(2);check(f.get(MenuField::Buttons,16)==0,"Confirm accepted an unfinished preview");f.event(3);
    check(f.get(MenuField::Buttons,16)==0x9000,"Confirm mask escaped its input scope");
    f.put(MenuField::BackgroundBusy,0);f.event(6,5);f.event(7,0,1);f.put(MenuField::LoadedCarrier,5);
    f.event(6,5);f.event(7,0,0);effect=f.event(8,5);
    check(effect.scene && effect.scene->id==std::string(64,'b'),"Rejected loader request changed the outstanding preview");
    f.event(2);check(f.get(MenuField::Buttons,16)==0x9000,"Finished custom preview cannot be confirmed");f.event(3);
    f.real(MenuField::X,320);f.real(MenuField::Y,-960);f.put(MenuField::SelectedX,1);f.put(MenuField::SelectedY,4);
    f.put(MenuField::Opacity,10);f.event(4);
    const unsigned middle=4*16;
    check(f.get(MenuField::Details,middle+12,1)==1 && f.get(MenuField::Details,middle+15,1)==4,"Custom course does not use a native ordinary wooden frame");
    check(f.get(MenuField::Details,middle+8,2)==0 && f.get(MenuField::Details,middle+10,2)==0,"Custom frame does not follow native scroll coordinates");
    check(f.get(MenuField::Details,middle+13,1)==80 && (f.get(MenuField::Details,middle+14,1)&128),"Native fade/live viewport flags are wrong");
    check(f.get(MenuField::Details,middle)==0x80010000,"Custom row header is not guest-owned");
    f.event(16);check(f.real(MenuField::Y)==0,"Custom row accesses out-of-range background data");
    rejects([&]{f.adapter.checkpoint();});f.event(17);
    check(f.real(MenuField::Y)==-960,"Background scope changed the viewport scroll");
    effect=f.event(9,5);check(effect.override_return && effect.return_value==f.get(MenuField::Details,middle+4),"Custom setup name differs from the frame title");
    check(!f.event(9,6).override_return,"Custom name replaced another stock level");
    check(f.event(11,5).return_value==7,"Custom vehicle capabilities not supplied");
    f.select(4,0);check(f.event(11,5).return_value==1 && f.event(10,5).return_value==0,"Unsupported aircraft enabled for a car-only custom course");
    f.put(MenuField::VoiceDelay,30);f.event(13);check(f.get(MenuField::VoiceDelay)==0,"T.T. speaks the stock carrier's name");
    f.event(12,2);effect=f.event(14,5);check(effect.scene && effect.scene->id==std::string(64,'a'),"Race launch lost custom identity");
    const auto race_sidecar=f.adapter.checkpoint();
    check(f.adapter.stage_checkpoint(race_sidecar).checkpoint()==race_sidecar,"Race/restart custom identity lost during restore.");
    check(f.event(14,5).scene.has_value(),"Custom restart reverted to stock");
    f.event(15,17);effect=f.event(14,5);
    check(effect.scene && effect.scene->id==std::string(64,'a'),"Results retry lost the custom course identity");
    f.event(15,0);check(!f.event(14,5).scene,"Leaving the custom race left a sticky carrier override");
    for(unsigned destination:{3U,6U,15U,17U}) {
        Fixture transition;transition.select(4,0);transition.event(12,2);transition.event(14,5);
        transition.event(15,destination);
        check(transition.event(14,5).scene.has_value()==(destination==17),"Custom race identity survived the wrong menu transition");
    }
    Fixture wrong_course;wrong_course.select(4,0);wrong_course.event(12,2);wrong_course.event(14,5);
    wrong_course.event(15,17);check(!wrong_course.event(14,6).scene,"Results bound another course to the custom track");
    check(!wrong_course.event(14,5).scene,"Mismatched load did not clear the old custom race identity");
    Fixture restore;restore.select(5,0);restore.event(0);check(restore.get(MenuField::CursorY)==0,"Custom row reached original init's stock array access");
    restore.event(1);check(restore.get(MenuField::CursorY)==5 && restore.real(MenuField::Y)==-1200,"Return to menu lost the logical custom selection");
    Fixture title_return;title_return.select(5,0);title_return.put(MenuField::TitleLoaded,1);
    title_return.event(0);
    check(title_return.get(MenuField::TitleLoaded)==1,"Adapter consumed the native title reset flag");
    check(title_return.get(MenuField::CursorX)==0 && title_return.get(MenuField::CursorY)==0,"Old custom row reached stock initialization after title");
    // Native menu_track_select_init consumes this flag and selects Dino Domain.
    title_return.put(MenuField::TitleLoaded,0);title_return.event(1);
    check(title_return.get(MenuField::CursorX)==0 && title_return.get(MenuField::CursorY)==0,"Custom selection overrode native title-entry Dino Domain reset");
    Fixture original;original.select(4,0);original.event(6,5);original.event(7,0,1);original.event(8,5);
    original.select(0,0);original.event(5);check(original.get(MenuField::LoadedCarrier)==0xffffffff,"Stock carrier reused a custom preview");
    original.event(6,5);original.event(7,0,1);effect=original.event(8,5);check(effect.scene && effect.scene->id.empty(),"Stock selection did not explicitly restore original assets");
    Fixture guarded(false);guarded.select(4,0);guarded.event(6,5);guarded.event(7,0,1);guarded.event(8,5);
    guarded.put(MenuField::Buttons,0x9000,16);guarded.event(2);check(guarded.get(MenuField::Buttons,16)==0,"Unqualified custom race passed its activation gate");guarded.event(3);
    rejects([&]{guarded.event(12,2);});
    Fixture quiet;quiet.select(4,0);quiet.input(0,0);quiet.input(-1,0);
    quiet.put(MenuField::Delay,1);quiet.input(1,0);
    check(quiet.get(MenuField::CursorX)==0,"Delayed navigation changed selection");
    quiet.put(MenuField::Delay,0);quiet.put(MenuField::Buttons,0x4000,16);quiet.put(MenuField::StickX,1,8,2);
    quiet.event(2);check(!quiet.event(3).navigation_sound,"Back confirmation played a navigation ping");
    for(unsigned unlocked: {0U,65535U}) {
        Fixture navigation;navigation.put(MenuField::FutureFunLand,unlocked,0,2);
        const unsigned rows=unlocked==65535?4:5;TrackMenuLayout grid(rows,5);
        for(unsigned i=0;i<5;++i)for(auto direction:std::array<std::pair<int,int>,4>{{{-1,0},{1,0},{0,1},{0,-1}}}) {
            auto start=grid.custom_cursor(i);navigation.select(start.row,start.column);navigation.input(direction.first,direction.second);
            const auto end=grid.move(start,direction.first,direction.second);
            check(navigation.get(MenuField::CursorY)==static_cast<unsigned>(end.row) && navigation.get(MenuField::CursorX)==static_cast<unsigned>(end.column),"Native adapter and logical grid disagree");
        }
    }
    Fixture bad;auto fields=bad.fields;fields[0]=0xffffffff;
    rejects([&]{bad.adapter.apply(2,bad.memory,fields);});rejects([&]{bad.event(18);});
    rejects([&]{bad.adapter.install_names(bad.memory,0x80010000);});
    bad.select(4,0);bad.event(6,5);bad.event(7,0,1);rejects([&]{bad.event(8,6);});

    // An appended .dkrmap level shares the legacy category but keeps its own
    // level ID through background preview, confirmation, retry and return.
    auto mixed=Fixture::roots();
    mixed[1].carrier=65;mixed[1].name="AUTHORED COURSE";
    Fixture authored(true,mixed);
    authored.select(4,0);authored.input(1,0);
    check(authored.get(MenuField::PreviewCarrier)==65,"Authored course lost its appended level ID");
    authored.event(6,65);authored.event(7,0,1);
    effect=authored.event(8,65);
    check(effect.scene && effect.scene->carrier==65 && effect.scene->id==mixed[1].content_id,
          "Authored preview did not retain its identity");
    authored.put(MenuField::LoadedCarrier,65);authored.put(MenuField::Buttons,0x9000,16);
    authored.event(2);check(authored.get(MenuField::Buttons,16)==0x9000,"Ready authored race cannot be confirmed");authored.event(3);
    authored.event(12,2);effect=authored.event(14,65);
    check(effect.scene && effect.scene->carrier==65,"Authored race lost its selection");
    authored.event(15,17);effect=authored.event(14,65);
    check(effect.scene && effect.scene->carrier==65,"Authored retry lost its selection");
    authored.event(0);authored.event(1);authored.input(-1,0);
    check(authored.get(MenuField::PreviewCarrier)==5,"Cannot navigate back to a legacy course");
}
}
int main(){try{run();std::cout<<checks<<" native custom-menu adapter checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

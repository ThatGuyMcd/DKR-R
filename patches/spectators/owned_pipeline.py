"""Transactional observer admission in isolated, pipeline-owned build copies."""
from pathlib import Path
import re

AUTH=Path(__file__).resolve().parent
def once(text,old,new):
 if text.count(old)!=1:raise ValueError('Owned spectator anchor changed: '+old[:130])
 return text.replace(old,new,1)
def snippet(name):return (AUTH/'native'/name).read_text(encoding='utf-8')
def header(text,name,origin):
 text='#include <utility>\n'+text
 text=once(text,'private:',snippet('admit_'+name+'.inl')+'private:')
 def adjacent(m):
  p=origin.parent/m[1]
  return '#include "'+p.resolve().as_posix()+'"' if p.is_file() else m[0]
 return re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
def driver(text):
 text='#include "owned_observer.hpp"\n'+text
 text=once(text,'    bool restore(std::span<const std::uint8_t> input, std::string& error) override {',
  snippet('admit_component.inl')+'''    bool restore(std::span<const std::uint8_t> input,std::string& error) override {return restore_state(input,error,nullptr);}
    bool restore_state(std::span<const std::uint8_t> input,std::string& error,const ConfirmedAdmission* admission) {''')
 text=once(text,'dkr_probe_input_state pads{}; auto staged_save=save_;auto staged_paks=paks_;auto staged_magic=magic_;',
  'dkr_probe_input_state pads{}; auto staged_save=admission?admission->save:save_;auto staged_paks=admission?admission->paks:paks_;auto staged_magic=admission?admission->magic:magic_;')
 text=once(text,'            if(!valid_audio(audio)) return false;','            if(!valid_audio(audio,admission?0:confirmed_count_)) return false;')
 text=once(text,'    bool valid_audio(const ComponentAudio& audio) const {','    bool valid_audio(const ComponentAudio& audio,std::uint32_t minimum=UINT32_MAX) const {\n        if(minimum==UINT32_MAX)minimum=confirmed_count_;')
 text=once(text,'audio.next_frame<confirmed_count_ || audio.next_frame==UINT32_MAX','audio.next_frame<minimum || audio.next_frame==UINT32_MAX')
 text=once(text,'auto observed=dkr::runtime::achievements::capture(ram_.data(),ram_.size(),DKR_PROBE_REVISION);',
  'auto observed=dkr::runtime::achievements::capture(ram_.data(),ram_.size(),DKR_PROBE_REVISION);')
 # No observations from an observer may reach achievement persistence. The
 # gameplay checkpoint and all save/Pak journals remain identical to racers.
 text=once(text,'achievement_frames_.insert_or_assign(frame,observed);',
  'if(!observer_read_only_)achievement_frames_.insert_or_assign(frame,observed);')
 text=once(text,'if(scene_sessions_)dkr::runtime::achievements::submit(observation->second,epoch,frame);',
  'if(scene_sessions_&&!observer_read_only_)dkr::runtime::achievements::submit(observation->second,epoch,frame);')
 text=once(text,'    std::uint32_t confirmed_count_=0;',
  '    bool observer_read_only_=false,observer_epoch_advanced_=false;\n    std::optional<dkr::runtime::spectators::Cursor> observer_retired_;\n    std::uint32_t confirmed_count_=0;')
 text=once(text,'        world_.configure_owned_output(box,std::move(sink),true);',
 '''        world_.configure_owned_output(box,std::move(sink),true);
        using namespace dkr::runtime::spectators;
        register_owned_observer(static_cast<OwnedGame*>(this),{this,
            [](void* p,Cursor at,std::string& e){auto& self=*static_cast<OwnedAdapter*>(p);return self.check(e)?self.world_.retire_observer(at,e):Work::Failed;},
            [](void* p,const Baseline& b,std::string& e){auto& self=*static_cast<OwnedAdapter*>(p);return self.check(e)&&self.world_.admit_observer(b,e);},
            [](void* p){return static_cast<OwnedAdapter*>(p)->world_.observer_magic_actions();},
            [](void* p,bool value){static_cast<OwnedAdapter*>(p)->world_.observer_read_only(value);}});''')
 text=once(text,'    ~OwnedAdapter() {dkr_probe_rom(nullptr,0);}',
  '    ~OwnedAdapter() {dkr::runtime::spectators::unregister_owned_observer(static_cast<OwnedGame*>(this));dkr_probe_rom(nullptr,0);}')
 return text

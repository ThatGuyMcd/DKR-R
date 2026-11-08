"""Compose local achievements into isolated build copies. Source inputs are read-only."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import sys
import tomllib
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent
ROOT=AUTH.parents[1]
sys.path.insert(0,str(ROOT/'patches/custom-vehicles'))
from guest_pipeline import body

def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Achievement anchor changed: '+old[:140])
    return text.replace(old,new,1)
def symbols():
    fields={'settings':'gSettingsPtr','mode':'gGameMode','menu':'gCurrentMenuId','tracks':'gIsInTracksMode','adventure_two':'gIsInAdventureTwo','paused':'gIsPaused','slot':'gSaveFileIndex','map':'gMapId','trial':'gIsTimeTrial','header':'gCurrentLevelHeader2','racers':'gRacersByPort','tt_time':'gTTGhostTimeToBeat','staff_ghost':'gTimeTrialStaffGhost','tt_vehicle':'gTimeTrialVehicle','trophy_world':'gTrophyRaceWorldId','trophy_round':'gTrophyRaceRound'}
    result='#pragma once\nnamespace dkr::runtime::achievements {\nstruct Symbols {'+''.join('std::uint32_t '+key+';' for key in fields)+'};\n'
    for rev in (77,80):
        doc=tomllib.loads((ROOT/f'modding/reference-us-v{rev}/data.toml').read_text(encoding='utf-8'))
        found={s['name']:s['vram'] for section in doc['section'] for s in section.get('symbols',[])}
        result+='inline constexpr Symbols symbols'+str(rev)+'{'+','.join(hex(found[value])+'U' for value in fields.values())+'};\n'
    return result+'}\n'
def ui(text):
    text=once(text,'#include "runtime_ui.hpp"','#include "runtime_ui.hpp"\n#include "achievements.hpp"\n#include <cfloat>\n#include <ctime>')
    text=once(text,'constexpr int kMenuPageCount = 9;','constexpr int kPageAchievements = 9;\nconstexpr int kMenuPageCount = 10;')
    text=once(text,'kSidebarOrder{0,5,6,1,2,3,4,8,7,9,10}','kSidebarOrder{0,5,6,1,2,3,4,8,9,7,10,11}')
    text=once(text,'#include "texture_dump_ui.inl"','#include "texture_dump_ui.inl"\n#include "achievement_ui.inl"')
    for name in ('LauncherSidebarButton','SidebarButton'):
        pattern=rf'(?<![A-Za-z_]){name}\("TEXTURES",\s*kPageTextures,[^;]+;'
        matches=list(re.finditer(pattern,text))
        if len(matches)!=1:raise ValueError('Expected one '+name+' Textures entry')
        call=matches[0].group(0);text=once(text,call,call+'\n        '+call.replace('"TEXTURES"','"ACHIEVEMENTS"').replace('kPageTextures','kPageAchievements'))
    text=once(text,'        DrawTexturesPage(content_width);','        DrawTexturesPage(content_width);\n    } else if (g_overlay_page == kPageAchievements) {\n        DrawAchievementsPage(content_width);')
    text=once(text,'            DrawTexturesPage(right_inner_width);','            DrawTexturesPage(right_inner_width);\n        } else if (page == kPageAchievements) {\n            DrawAchievementsPage(right_inner_width);')
    text=once(text,'    g_config_directory = config_directory;','    g_config_directory = config_directory;\n    dkr::runtime::achievements::configure(config_directory);')
    text=once(text,'    ReleaseTextureDumpTextures();','    ReleaseAchievementAudio();\n    ReleaseTextureDumpTextures();')
    text=once(text,'    const bool show_mipmap_loading = RT64::mipLoadingVisible();','    const bool show_achievement_toast=AchievementToastVisible();\n    const bool show_mipmap_loading = RT64::mipLoadingVisible();')
    text=once(text,'        !show_overlay && !show_fps && !show_crt && !show_online_error &&','        !show_achievement_toast && !show_overlay && !show_fps && !show_crt && !show_online_error &&')
    text=once(text,'    inspector->endFrame();','    DrawPinnedAchievement();\n    DrawAchievementToast();\n    inspector->endFrame();')
    text=once(text,'        ImGui::Render();','        DrawAchievementToast();\n        ImGui::Render();')
    return text
def runtime(text):
    text=once(text,'#include "runtime_legacy_mods.hpp"','#include "runtime_legacy_mods.hpp"\n#include "achievements.hpp"')
    anchor='        if(name=="dkr_shared_records_capture") {'
    text=once(text,anchor,'''        if(name=="dkr_achievements_context") {
            arity(1);const auto assets=session.load();
            result=owns_level(static_cast<std::int32_t>(args[0])) ||
                (assets&&!assets->current_content().empty()) || !state_->selected_cup.empty();
        }
        else if(name=="dkr_shared_records_capture") {'''.replace('!state_->selected_cup.empty()','!selected_trophy().empty()'))
    text+='''
extern "C" void dkr_achievements_observe(std::uint8_t* ram,unsigned revision) noexcept {
    using namespace dkr::runtime;
    if(achievements::online_session())return;
    try {
        auto sample=achievements::capture(ram,legacy::guest_memory_size(),revision);
        const auto assets=legacy::session.load();
        sample.custom=sample.level>=65||legacy::owns_level(sample.level)||
            (assets&&!assets->current_content().empty())||!legacy::selected_trophy().empty();
        achievements::submit(sample);
    }catch(...){std::fprintf(stderr,"[achievements] Read-only observer unavailable at this boundary\\n");}
}
'''
    return text
def driver(text):
    text=once(text,'#include "resident_music.hpp"','#include "resident_music.hpp"\n#include "achievements.hpp"\n#include <map>')
    # Projections are host-local observations, not guest state, checkpoints or
    # wire effects. Replay replaces the same frame; only commit consumes one.
    text=once(text,'    std::vector<std::uint8_t> ram_;','    std::map<std::uint32_t,dkr::runtime::achievements::Sample> achievement_frames_;\n    std::vector<std::uint8_t> ram_;')
    anchor='''        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame, std::span<const std::uint8_t> effects,'''
    text=once(text,anchor,'''        if(authored_cpu_&&!sdk2_idle) {
            auto observed=dkr::runtime::achievements::capture(ram_.data(),ram_.size(),DKR_PROBE_REVISION);
            observed.custom=observed.level>=65;
#if DKR_PROBE_HAS_MOD_SERVICES
            if(mods_) {std::uint64_t custom=0;const std::uint64_t level=std::uint32_t(observed.level);
                if(!mods_->dispatch("dkr_achievements_context",ram_.data(),&context_,&level,1,nullptr,0,custom))observed.custom=observed.custom||bool(custom);}
#endif
            achievement_frames_.insert_or_assign(frame,observed);
            if(achievement_frames_.size()>1024)achievement_frames_.erase(achievement_frames_.begin());
        }
        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame, std::span<const std::uint8_t> effects,''')
    anchor='''        ++confirmed_count_;
#if DKR_PROBE_HAS_MOD_SERVICES'''
    text=once(text,anchor,'''        ++confirmed_count_;
        if(const auto observation=achievement_frames_.find(frame);observation!=achievement_frames_.end()) {
            if(scene_sessions_)dkr::runtime::achievements::submit(observation->second,epoch,frame);
            achievement_frames_.erase(achievement_frames_.begin(),std::next(observation));
        }
#if DKR_PROBE_HAS_MOD_SERVICES''')
    # A scene starts a new frame sequence. Never let an old projection survive
    # a scene/epoch transition or be accepted after checkpoint admission.
    text=once(text,'        scenery_.reset(native_.presentation.scene);','        scenery_.reset(native_.presentation.scene);\n        achievement_frames_.clear();')
    return text
def main(args):
    if args.output.resolve().is_relative_to(ROOT):raise ValueError('Achievements need an isolated build outside the checkout')
    args.output.mkdir(parents=True,exist_ok=True)
    for file in (AUTH/'native').iterdir():
        if file.is_file():(args.output/file.name).write_bytes(file.read_bytes())
    (args.output/'achievement_symbols.hpp').write_text(symbols(),encoding='utf-8',newline='\n')
    swaps=[];guest_counts={77:0,80:0}
    for row in json.loads(args.inputs.read_text(encoding='utf-8')):
        path=Path(row['path']);original=path.read_bytes();text=original.decode().replace('\r\n','\n');name=path.name
        if row.get('guest'):
            span=body(text,'main_game_loop')
            if not span:continue
            rev=80 if row['target'].endswith('80') else 77;guest_counts[rev]+=1
            start,end=span;part=text[start:end]
            part,count=re.subn(r'(?m)^    return;$',f'    extern void dkr_achievements_observe(uint8_t*,unsigned);\n    dkr_achievements_observe(rdram,{rev});\n    return;',part)
            if count!=1:raise ValueError('Unreviewed main_game_loop return boundary')
            text=text[:start]+part+text[end:];next_path=args.output/('guest'+str(rev))/name
        else:
            if name=='runtime_ui.cpp':text=ui(text)
            elif name=='runtime_legacy_mods.cpp':text=runtime(text)
            elif name=='probe_driver.cpp':text=driver(text)
            elif name=='game_main.cpp':
                text=once(text,'#include "runtime_ui.hpp"','#include "runtime_ui.hpp"\n#include "achievements.hpp"')
                anchor='            const auto code_set=online_mods?online_mods->code():prepared_mods->code;'
                text=once(text,anchor,anchor+'\n            dkr::runtime::achievements::begin(online_save.active,0,code_set&&!code_set->ordered.empty(),code_set&&std::any_of(code_set->ordered.begin(),code_set->ordered.end(),[](const auto& p){return p->development;}));')
            elif name=='experimental_runtime.cpp':
                text='#include "achievements.hpp"\n'+text
                text=once(text,'    LobbyRoute route(lobby,*launch);const auto local=route.owner(initial.local_slot);','    LobbyRoute route(lobby,*launch);const auto local=route.owner(initial.local_slot);\n    dkr::runtime::achievements::begin(true,local,bool(mods),launch->development);')
            else:raise ValueError('Unexpected achievement input '+str(path))
            next_path=args.output/name
        # Moving a TU must not change quote-include lookup. In particular the
        # shared-save header must not be mixed with the older SDK capsule copy.
        def adjacent(match):
            candidate=path.parent/match.group(1)
            return '#include "'+candidate.resolve().as_posix()+'"' if candidate.is_file() else match.group(0)
        text=re.sub(r'^#include "([^"\n]+)"',adjacent,text,flags=re.M)
        next_path.parent.mkdir(parents=True,exist_ok=True);next_path.write_text(text,encoding='utf-8',newline='\n')
        swaps.append({**row,'new':str(next_path.resolve()),'input_sha256':hashlib.sha256(original).hexdigest(),'output_sha256':hashlib.sha256(text.encode()).hexdigest()})
    if guest_counts!={77:1,80:1}:raise ValueError('Missing an original revision frame observer: '+str(guest_counts))
    (args.output/'achievement-swaps.json').write_text(json.dumps(swaps,indent=2)+'\n',encoding='utf-8')
    print('All 94 local achievements staged over the accepted texture/vehicle build; source inputs unchanged.')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);main(p.parse_args())

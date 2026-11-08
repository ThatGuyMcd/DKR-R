"""Reviewed additive command lane for Experimental Rollback only.

A command travels WITH a final host sample, shares its duplicate/conflict/ACK
and repair rules, and is never predicted from a preceding command. The
rollback driver corrects a predicted empty command when its actual arrives.
"""
FILES=('src/game/netplay/experimental_network.hpp','src/game/netplay/experimental_network.cpp',
       'src/game/netplay/experimental_owner_inputs.hpp','src/game/netplay/experimental_owner_inputs.cpp',
       'src/game/netplay/experimental_rollback.hpp','src/game/netplay/experimental_rollback.cpp',
       'src/game/netplay/experimental_timeline.hpp','src/game/netplay/experimental_timeline.cpp',
       'src/game/netplay/experimental_session.hpp','src/game/netplay/experimental_session.cpp',
       'src/game/netplay/experimental_runtime.cpp','tests/replay_probe/probe_driver.cpp')


def transform(name,text,once):
    def one(a,b):
        nonlocal text
        text=once(text,a,b)
    if name.endswith('experimental_network.hpp'):
        one('kLanes = 2 + 4 * 2;', 'kLanes = 3 + 4 * 2;')
    elif name.endswith('experimental_network.cpp'):
        one('#include "monocypher.h"','#include "monocypher.h"\n#include "../mods/sdk2_bridge.hpp"')
        one('plain[5]>1','plain[5]>2')
        one('    } else {\n        const auto result=session_.receive_control', '''    } else if(plain[5]==2) {
        if(!dkr::sdk2::network_receive(peer,payload))++statistics_.rejected;
    } else {
        const auto result=session_.receive_control''')
        one('        const unsigned players=session_.player_count(),local=session_.local_owner();', '''        const unsigned players=session_.player_count(),local=session_.local_owner();
        dkr::sdk2::network_epoch(session_.epoch(),local,players);''')
        one('            const auto current=session_.control_for', '''            const auto tools=dkr::sdk2::network_packet(peer);
            if(!tools.empty()&&!queue(peer,kLanes-1,2,tools,TransportTrafficClass::Control,now))return false;
            const auto current=session_.control_for''')
    elif name.endswith('experimental_owner_inputs.hpp'):
        one('#include "netplay_types.hpp"','#include "netplay_types.hpp"\n#include "../mods/sdk2_command.hpp"')
        one('kOwnerInputMaximumBytes = 52+4*kOwnerInputBatch;', 'kOwnerInputMaximumBytes = 53+68*kOwnerInputBatch;')
        one('    std::array<PackedInput, kOwnerInputBatch> inputs{};', '    std::array<PackedInput, kOwnerInputBatch> inputs{};\n    std::array<dkr::sdk2::Command,kOwnerInputBatch> commands{};')
        one('std::uint32_t frame; PackedInput input;','std::uint32_t frame; PackedInput input; dkr::sdk2::Command command{};')
        one('publish(std::uint32_t frame, PackedInput input);','publish(std::uint32_t frame, PackedInput input,dkr::sdk2::Command command={});')
        one('    bool failed() const', '    dkr::sdk2::Command command(std::uint8_t owner,std::uint32_t frame)const;\n    bool failed() const')
        one('PackedInput input{}; bool valid = false;', 'PackedInput input{}; bool valid = false; dkr::sdk2::Command command{};')
        one('void insert(std::uint8_t owner, std::uint32_t frame, PackedInput input);','void insert(std::uint8_t owner, std::uint32_t frame, PackedInput input,dkr::sdk2::Command command={});')
    elif name.endswith('experimental_owner_inputs.cpp'):
        one('constexpr unsigned kHeader = 52;', 'constexpr unsigned kHeader = 53;')
        one('    return true;\n}\nvoid put(', '''    for(unsigned i=0;i<p.count;++i)if(!p.commands[i].valid()||(p.owner!=0&&!p.commands[i].empty()))return false;
    return true;
}
void put(''')
        one("result{'D','K','R','X',2,p.players,p.owner,p.count}","result{'D','K','R','X',3,p.players,p.owner,p.count}")
        one('result.reserve(kHeader + p.count * 4);','result.reserve(kOwnerInputMaximumBytes);')
        one('    for (unsigned i = 0; i < p.count; ++i) {\n        put(result, p.inputs[i].buttons, 2);', '''    std::uint8_t mask=0;for(unsigned i=0;i<p.count;++i)if(!p.commands[i].empty())mask|=std::uint8_t(1U<<i);
    result.push_back(mask);
    for (unsigned i = 0; i < p.count; ++i) {
        put(result, p.inputs[i].buttons, 2);''')
        one('        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_y));', '''        result.push_back(std::bit_cast<std::uint8_t>(p.inputs[i].stick_y));
        if(mask&(1U<<i))for(auto word:p.commands[i].words)put(result,word,4);''')
        one("bytes[3] != 'X' || bytes[4] != 2 || bytes[7] > kOwnerInputBatch ||\n        bytes.size() != kHeader + bytes[7] * 4U)","bytes[3] != 'X' || bytes[4] != 3 || bytes[7] > kOwnerInputBatch ||\n        bytes.size()>kOwnerInputMaximumBytes || (bytes[52]>>bytes[7]))")
        one('    for (unsigned i = 0; i < p.count; ++i) {\n        p.inputs[i] = {std::uint16_t(get(bytes, kHeader + i * 4, 2)),\n            std::bit_cast<std::int8_t>(bytes[kHeader + i * 4 + 2]),\n            std::bit_cast<std::int8_t>(bytes[kHeader + i * 4 + 3])};\n    }', '''    unsigned cursor=kHeader;
    for(unsigned i=0;i<p.count;++i) {
        if(bytes.size()-cursor<4)return {};
        p.inputs[i]={std::uint16_t(get(bytes,cursor,2)),std::bit_cast<std::int8_t>(bytes[cursor+2]),std::bit_cast<std::int8_t>(bytes[cursor+3])};cursor+=4;
        if(bytes[52]&(1U<<i)) {
            if(bytes.size()-cursor<64)return {};
            for(auto& word:p.commands[i].words){word=std::uint32_t(get(bytes,cursor,4));cursor+=4;}
        }
    }
    if(cursor!=bytes.size())return {};''')
        one('bool OwnerInputHistory::within_window(', '''dkr::sdk2::Command OwnerInputHistory::command(std::uint8_t owner,std::uint32_t frame)const {
    if(owner>=players_)return {};const auto& value=samples_[owner][frame%kHistory];
    return value.valid&&value.frame==frame?value.command:dkr::sdk2::Command{};
}
bool OwnerInputHistory::within_window(''')
        one('void OwnerInputHistory::insert(std::uint8_t owner, std::uint32_t frame, PackedInput input) {', 'void OwnerInputHistory::insert(std::uint8_t owner, std::uint32_t frame, PackedInput input,dkr::sdk2::Command command) {')
        one('= {frame, input, true};','= {frame, input, true,command};')
        one('OwnerInputResult OwnerInputHistory::publish(std::uint32_t frame, PackedInput input) {', 'OwnerInputResult OwnerInputHistory::publish(std::uint32_t frame, PackedInput input,dkr::sdk2::Command command) {')
        one('    if (!epoch_ || conflict_) return OwnerInputResult::Rejected;', '    if (!epoch_ || conflict_ || !command.valid() || (local_!=0&&!command.empty())) return OwnerInputResult::Rejected;')
        one('if (*prior == input) return OwnerInputResult::Duplicate;', 'if (*prior == input && this->command(local_,frame)==command) return OwnerInputResult::Duplicate;')
        one('insert(local_, frame, input);','insert(local_, frame, input,command);')
        one('prior && *prior != p.inputs[i]', 'prior && (*prior != p.inputs[i]||command(p.owner,frame)!=p.commands[i])')
        one('insert(p.owner, frame, p.inputs[i]); received.push_back({p.owner, frame, p.inputs[i]});', 'insert(p.owner, frame, p.inputs[i],p.commands[i]); received.push_back({p.owner, frame, p.inputs[i],p.commands[i]});')
        one('            p.inputs[p.count++] = *sample;', '            p.commands[p.count]=command(owner,first+i);p.inputs[p.count++] = *sample;')
    elif name.endswith('experimental_rollback.hpp'):
        one('#include "netplay_types.hpp"','#include "netplay_types.hpp"\n#include "../mods/sdk2_command.hpp"')
        one('    virtual bool tick(std::uint32_t frame, const FrameInputs& inputs,', '''    virtual bool development_command(std::uint64_t,std::uint32_t,const dkr::sdk2::Command& command,std::string& error) {
        if(command.empty())return true;error="This simulation has no development command adapter.";return false;
    }
    virtual bool tick(std::uint32_t frame, const FrameInputs& inputs,''')
        one('std::uint32_t frame, PackedInput input);','std::uint32_t frame, PackedInput input,dkr::sdk2::Command command={});')
        one('        FrameInputs actual{};', '        dkr::sdk2::Command actual_command{},used_command{};\n        FrameInputs actual{};')
    elif name.endswith('experimental_rollback.cpp'):
        one('std::uint32_t frame, PackedInput input) {','std::uint32_t frame, PackedInput input,dkr::sdk2::Command command) {')
        one('    if (owner >= configuration_.players ||','    if (!command.valid()||(owner!=0&&!command.empty()))return InputResult::OutOfRange;\n    if (owner >= configuration_.players ||')
        one('if (existing->actual[owner] == input) return InputResult::Duplicate;', 'if (existing->actual[owner] == input && (owner!=0||existing->actual_command==command)) return InputResult::Duplicate;')
        one('    value.actual[owner] = input;', '    value.actual[owner] = input;\n    if(owner==0)value.actual_command=command;')
        one('    if (value.simulated && (value.dependencies&mask) && value.used[owner] != input) {', '    if (value.simulated && (((value.dependencies&mask)&&value.used[owner]!=input)||(owner==0&&value.used_command!=command))) {')
        one('        if(!corrected)break;', '        if(frame->used_command!=frame->actual_command)corrected=false;\n        if(!corrected)break;')
        one('        frame.used = predict(frame);','        frame.used = predict(frame);\n        frame.used_command=(frame.actual_mask&1)?frame.actual_command:dkr::sdk2::Command{};')
        one('        if ((simulation_.requires_confirmed_tick() || resource_frame_==frame.number) && !agreed)', '        if ((simulation_.requires_confirmed_tick() || resource_frame_==frame.number || !frame.actual_command.empty()) && !agreed)')
        one('         tick_ok=simulation_.tick(frame.number, frame.used, frame.output, detail);}', '''         tick_ok=simulation_.development_command(configuration_.epoch,frame.number,frame.used_command,detail)&&
             simulation_.tick(frame.number, frame.used, frame.output, detail);}''')
    elif name.endswith('experimental_timeline.hpp'):
        one('sample_local(PackedInput input,bool live_lead=false);','sample_local(PackedInput input,bool live_lead=false,dkr::sdk2::Command command={});')
        one('PackedInput input{}; bool valid = false;', 'PackedInput input{}; bool valid = false; dkr::sdk2::Command command{};')
        one('stage(std::uint8_t owner, std::uint32_t frame,PackedInput input);','stage(std::uint8_t owner, std::uint32_t frame,PackedInput input,dkr::sdk2::Command command={});') if 'stage(std::uint8_t owner, std::uint32_t frame,PackedInput input);' in text else one('stage(std::uint8_t owner, std::uint32_t frame, PackedInput input);','stage(std::uint8_t owner, std::uint32_t frame, PackedInput input,dkr::sdk2::Command command={});')
    elif name.endswith('experimental_timeline.cpp'):
        one('Timeline::sample_local(PackedInput input,bool live_lead) {', 'Timeline::sample_local(PackedInput input,bool live_lead,dkr::sdk2::Command command) {')
        one('inputs_.publish(frame,input);','inputs_.publish(frame,input,command);')
        one('!stage(configuration_.local_owner,frame,input)', '!stage(configuration_.local_owner,frame,input,command)')
        one('Timeline::stage(std::uint8_t owner,std::uint32_t frame,PackedInput input) {','Timeline::stage(std::uint8_t owner,std::uint32_t frame,PackedInput input,dkr::sdk2::Command command) {')
        one('pending.input!=input)', 'pending.input!=input||pending.command!=command)')
        one('pending={frame,input,true};','pending={frame,input,true,command};')
        one('stage(actual.owner,actual.frame,actual.input)', 'stage(actual.owner,actual.frame,actual.input,actual.command)')
        one('owner,pending.frame,pending.input);','owner,pending.frame,pending.input,pending.command);')
    elif name.endswith('experimental_session.hpp'):
        one('sample_local(PackedInput input,bool live_lead=false);','sample_local(PackedInput input,bool live_lead=false,dkr::sdk2::Command command={});')
    elif name.endswith('experimental_session.cpp'):
        one('Session::sample_local(PackedInput input,bool live_lead) {','Session::sample_local(PackedInput input,bool live_lead,dkr::sdk2::Command command) {')
        one('timeline_.sample_local(input,live_lead)', 'timeline_.sample_local(input,live_lead,command)')
    elif name.endswith('experimental_runtime.cpp'):
        one('    SimulationContract contract() const override {return game_->contract();}', '''    bool development_command(std::uint64_t epoch,std::uint32_t frame,const dkr::sdk2::Command& command,std::string& error)override {
        return game_->development_command(epoch,frame,command,error);
    }
    SimulationContract contract() const override {return game_->contract();}''')
    elif name.endswith('probe_driver.cpp'):
        one('            code_->trophy_selector([this]{return mods_?mods_->selected_trophy():std::string{};});', '            code_->trophy_selector([this]{return mods_?mods_->selected_trophy():std::string{};});\n            code_->sdk2_current_course([this]{return mods_?mods_->current_course():std::string{};});\n            if(code_->sdk2_wants_assets())code_->sdk2_assets(mods_?mods_->asset_inventory():std::string{});')
        one('    bool tick(std::uint32_t frame, const FrameInputs& inputs, TickOutput& output, std::string& error) override {', '''    bool development_command(std::uint64_t epoch,std::uint32_t frame,const dkr::sdk2::Command& command,std::string& error)override {
        if(command.empty()){if(code_)code_->sdk2_command({});return true;}
        if(!code_){error="No admitted code world for this development command.";return false;}
        try{code_->sdk2_command(command);return true;}catch(const std::exception& e){error=e.what();return false;}
    }
    bool tick(std::uint32_t frame, const FrameInputs& inputs, TickOutput& output, std::string& error) override {''')
    else:raise ValueError('Unreviewed command-lane source: '+name)
    return text

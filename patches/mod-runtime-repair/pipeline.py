"""Counted repair composition over read-only final pipeline inputs. No execution."""
from pathlib import Path
import argparse
import hashlib
import json
import sys

sys.dont_write_bytecode = True
AUTH = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def put(path, data):
    if isinstance(data,str):data=data.encode('utf-8')
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.exists() or path.read_bytes()!=data:path.write_bytes(data)


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Repair anchor changed: ' + old[:160])
    return text.replace(old, new)


def transform(name, text):
    if name.startswith('magic_code_') and name.endswith('.cpp'):
        text = once(text, '\nnamespace dkr::mods::code {', '\n#include "magic_code_execution_policy.hpp"\n\nnamespace dkr::mods::code {')
    if name == 'magic_code_vm.cpp':
        text = once(text, '        --budget.instructions;', '        --budget.instructions;execution::instruction();')
        return once(text, '                if(!budget.native_calls)fault("native call budget exceeded");\n                --budget.native_calls;\n', '')
    if name == 'magic_code_world.cpp':
        begin = text.index('    const auto before_instructions=budget.instructions;')
        end = text.index('    if(depth>=8)', begin)
        text = text[:begin] + '''    execution::Transaction transaction(this,budget,"callback");
    auto& work_budget=transaction.budget();
    execution::OwnerScope execution_owner(owner,package.id,
        package.api==2?package.sdk2_instructions:200000,
        package.api==2?package.sdk2_native_calls:128);
    const auto before=execution_owner.work().counts;
    const auto started=std::chrono::steady_clock::now();
    struct Costs {
        Metric& metric;const execution::OwnerWork& work;execution::Counts before;
        bool outer;std::chrono::steady_clock::time_point started;
        ~Costs(){++metric.callbacks;if(!outer)return;
            metric.instructions+=work.counts.instructions-before.instructions;
            metric.native_calls+=work.counts.native-before.native;
            metric.nanoseconds+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();}
    } costs{metrics_[owner],execution_owner.work(),before,depth==0,started};
''' + text[end:]
        text = once(text, 'execute(bus,c,function,budget,', 'execute(bus,c,function,work_budget,')
        text = once(text, 'throw Error(std::string(e.what())+context+location);', 'throw Error(std::string(e.what())+context+location+execution::diagnostic());')
        for signature, label in (
            ('void World::initialize(Budget& budget) {', 'on_boot'),
            ('void World::owner_event(unsigned owner,std::string_view name,Budget& budget,const Registers& args) {', 'owner-event'),
            ('void World::event(std::string_view owner,std::string_view name,const Registers& arguments,Budget& budget,const ArgumentStack* native_arguments) {', 'event'),
            ('bool World::enter(std::uint32_t function,Registers& c,Budget& budget,const ArgumentStack* native_arguments) {', 'entry-hooks'),
            ('void World::leave(std::uint32_t function,const Registers& c,Budget& budget,const ArgumentStack* native_arguments) {', 'exit-hooks')):
            phase = 'execution::PhaseScope phase_scope(execution::event_phase(name));\n    ' if label in ('event','owner-event') else ''
            if label=='on_boot':phase='execution::PhaseScope phase_scope(execution::Phase::Loading);\n    '
            name = 'name' if label in ('event','owner-event') else '"'+label+'"'
            text = once(text, signature, signature+'\n    '+phase+'execution::Transaction transaction(this,budget,'+name+');')
        text = once(text, '    if(import.event){', '    execution::gateway(import.dependency=="*"&&!import.event);\n    if(import.event){')
        return text
    if name == 'magic_code_link.cpp':
        text = once(text, 'unsigned instruction_requests=0,native_requests=0,simulation_owners=0,tuning_owners=0;', 'unsigned simulation_owners=0,tuning_owners=0;')
        text = once(text, '        instruction_requests+=p->sdk2_instructions;native_requests+=p->sdk2_native_calls;\n', '')
        return once(text, '    if(instruction_requests>2000000||native_requests>1024)bad("SDK 2 work profiles exceed the portable session budget");\n', '')
    if name == 'magic_code_package.cpp':
        return once(text, 'Json value={{"api",sdk2?2:ApiVersion},{"execution",sdk2?2:ExecutionVersion},', 'Json value={{"work-policy",std::string(execution::Policy)},{"api",sdk2?2:ApiVersion},{"execution",sdk2?2:ExecutionVersion},')
    if name == 'magic_code_game_world.cpp':
        reset='if(nested_==0)budget_=sdk2_budget_;'
        if text.count(reset)!=3:raise ValueError('Native re-entry work reset anchors changed')
        text=text.replace(reset,'if(nested_==0&&!execution::active(&world_))budget_=sdk2_budget_;')
        text = once(text, 'void GameWorld::native(std::uint32_t address,Registers& registers,Budget&,bool curated)', 'void GameWorld::native(std::uint32_t address,Registers& registers,Budget& budget,bool curated)')
        text = once(text, '    ++native_depth_;struct Depth', '    execution::native(budget);\n    ++native_depth_;struct Depth')
        text = once(text, 'void GameWorld::initialize(){std::lock_guard lock(mutex_);Budget budget=sdk2_budget_;world_.initialize(budget);}', 'void GameWorld::initialize(){std::lock_guard lock(mutex_);Budget budget=sdk2_budget_;execution::PhaseScope phase(execution::Phase::Loading);world_.initialize(budget);}')
        text = once(text, '        const auto lifecycle=leaving?entry->after_event:entry->before_event;', '        execution::PhaseScope work_phase(execution::function_phase(function_name));\n        const auto lifecycle=leaving?entry->after_event:entry->before_event;')
        text = once(text, '        auto registers=read(*context);\n        const auto outgoing=', '        execution::Transaction work_transaction(&world_,budget_,"game-hooks");\n        auto registers=read(*context);\n        const auto outgoing=')
        # Classification does not change SDK capability/phase permissions.
        text = once(text, '    world_.event("*",Events[event],args,budget_,outgoing?&*outgoing:nullptr);', '    execution::PhaseScope work_phase(event==DKR_CODE_EVENT_FRAME||event==DKR_CODE_EVENT_FRAME_END?execution::Phase::Simulation:execution::event_phase(Events[event]));\n    world_.event("*",Events[event],args,budget_,outgoing?&*outgoing:nullptr);')
        return text
    if name == 'sdk2_operations.inl':
        return once(text, '        if(entry==table_.entries.end()||!budget.native_calls)throw Error("SDK operation exhausted its native-call budget.");\n        --budget.native_calls;Registers call;', '        if(entry==table_.entries.end())throw Error("SDK operation has no native dispatcher.");\n        Registers call;')
    if name == 'sdk2_simulation.inl':
        return once(text,'    budget_=sdk2_budget_;world_.event("*","on_tool",Registers{},budget_);',
            '    if(!execution::active(&world_))budget_=sdk2_budget_;world_.event("*","on_tool",Registers{},budget_);')
    if name == 'presentation_identity.cpp':
        anchor = '    if (!integrity.accept(ValidRange(command_address, 8U) &&'
        # Removing only a consecutive matching empty scope is semantics-neutral:
        # no guest command occurs between these two markers. In particular do
        # not merge geometry/viewport markers or non-adjacent/scoped transforms.
        replacement = '''    const bool metadata_valid=ValidRange(command_address,8U)&&
        valid_presentation_marker(kind,mode,variant)&&
        (kind!=PresentationMarkerKind::HudWidget||hud_transform.valid());
    const bool empty_scope_kind=kind==PresentationMarkerKind::HudPass||
        kind==PresentationMarkerKind::HudWidget||kind==PresentationMarkerKind::HudRect||
        is_aspect_marker(kind);
    if(metadata_valid&&empty_scope_kind&&mode==0&&!markers.empty()&&
       markers.back().kind==kind&&markers.back().mode==1) {
        markers.pop_back();return true; // empty-scope-compaction=v1
    }
    if (!integrity.accept(ValidRange(command_address, 8U) &&'''
        return once(text, anchor, replacement)
    if name == 'runtime_hud_layout.cpp':
        text = once(text, 'extern "C" void dkr_hud_asset_load_failed(std::uint8_t*, recomp_context* context)', 'extern "C" void dkr_hud_asset_load_failed(std::uint8_t* rdram, recomp_context* context)')
        anchor = '    reported[sprite] = true;'
        text = once(text, anchor, anchor+'''
    const bool v80=dkr::runtime::revision_addresses::CurrentHud==dkr::runtime::revision_addresses::kUsV80.CurrentHud;
    const auto read=[&](std::uint32_t address){return static_cast<std::uint32_t>(MEM_W(0,RdramAddress(address)));};
    std::fprintf(stderr,"[hud][resource-context] sprite=%u layout=%u allocated-layout=%u dl=0x%08X tri=0x%08X; guest cursors and resource retries preserved\\n",
        sprite,read(v80?0x80124368U:0x80123DE8U),read(v80?0x80123A8CU:0x8012350CU),
        read(v80?0x80121778U:0x801211F8U),read(v80?0x80123558U:0x80122FD8U));
''')
        # Keep the diagnostics independent of ROM-address guesses: derive the
        # two moving-cursor addresses from SDK symbols in stage(), below.
        return text+'''
extern "C" void dkr_render_heap_sync_notice(std::uint8_t*,recomp_context* context) {
    static std::atomic<unsigned> reports{0};
    if(reports.fetch_add(1)>=16)return;
    std::fprintf(stderr,"[render][heap-layout] before=%d requested=%d allocated=%d; post-mod-init-track=v1; before track generation\\n",
        int(context->r4),int(context->r5),int(context->r6));
}
'''
    if name == 'f3ddkr_rt64.cpp':
        # Decoding remains strict. This bounded context identifies the earliest
        # malformed producer command without a second snapshot/GPU flush.
        helper = '''
void ReportMalformedInput(RT64::State* state,RT64::DisplayList* command,std::uint64_t task) {
    static unsigned reports=0;if(reports++>=8||!state||!state->RDRAM||!command)return;
    const auto address=reinterpret_cast<std::uintptr_t>(command)-reinterpret_cast<std::uintptr_t>(state->RDRAM);
    if(address>kRDRAMSize-8)return;
    std::fprintf(stderr,"[render][invalid-input] task=%llu dl=0x%06X words=(%08X,%08X); immutable task snapshot; command validation retained\\n",
        static_cast<unsigned long long>(task),unsigned(address),command->w0,command->w1);
}
'''
        text = once(text, 'std::uint32_t g_logged_counted_errors = 0;', 'std::uint32_t g_logged_counted_errors = 0;\n'+helper)
        text = once(text, '    if (handler == nullptr || handler == &Dispatch) {',
            '    if (handler == nullptr || handler == &Dispatch) {\n        ReportMalformedInput(state,command,active_->data_->task_count);')
        return once(text, '            RejectTask(display_list);\n            return;\n        }\n    }\n\n    const bool canonical_shadow', '            ReportMalformedInput(state,*display_list,data.task_count);\n            RejectTask(display_list);\n            return;\n        }\n    }\n\n    const bool canonical_shadow')
    raise ValueError('Unreviewed native repair input: '+name)


def stage(args):
    records=json.loads(args.inputs.read_text())
    out=args.output.resolve()
    if any(Path(row['path']).resolve().is_relative_to(out) for row in records):
        raise ValueError('A repair input overlaps its stage')
    out.mkdir(parents=True,exist_ok=True)
    receipt={'schema':1,'policy':'per-owner-work-v2','inputs':{},'outputs':{},'authored':{}}
    # Forward headers to their canonical read-only files: copying a pragma-once
    # header creates a second physical definition when an adjacent vehicle/HUD
    # header also includes the canonical spelling. Inlines stay build-local so
    # the reviewed internal native-call edit is selected by quote lookup.
    source=args.sdk/'mods'
    for path in sorted(source.iterdir()):
        if path.suffix in ('.hpp','.h','.inl'):
            target=out/'native/mods'/path.name
            target.parent.mkdir(parents=True,exist_ok=True)
            if path.suffix=='.inl':put(target,path.read_bytes())
            else:put(target,'#pragma once\n#include "'+path.resolve().as_posix()+'"\n')
            receipt['inputs'][str(path.resolve())]=sha(path)
            receipt['outputs'][target.relative_to(out).as_posix()]=sha(target)
    for path in (AUTH/'native').iterdir():
        target=out/('native/game' if path.suffix=='.cpp' else 'native/mods')/path.name
        target.parent.mkdir(parents=True,exist_ok=True)
        put(target,path.read_bytes())
        receipt['authored'][path.relative_to(AUTH).as_posix()]=sha(path)
        receipt['outputs'][target.relative_to(out).as_posix()]=sha(target)
    for row in records:
        path=Path(row['path']).resolve(strict=True)
        relative=Path('native/mods' if path.name.startswith('magic_code_') else 'native/game')/path.name
        target=out/relative;target.parent.mkdir(parents=True,exist_ok=True)
        text=transform(path.name,path.read_text())
        if path.name=='runtime_hud_layout.cpp':
            import tomllib
            for revision in (77,80):
                symbols=tomllib.loads((args.reference/f'reference-us-v{revision}/data.toml').read_text())
                addresses={s['name']:s['vram'] for sec in symbols['section'] for s in sec.get('symbols',[])}
                # Replaced below after validating the exact exported symbol map.
                expected={77:{'gViewportLayout':0x80123DE8,'gGameCurrTriList':0x80122FD8},80:{'gViewportLayout':0x80124368,'gGameCurrTriList':0x80123558}}[revision]
                for name,old in expected.items():
                    text=text.replace(f'0x{old:08X}U',f'0x{addresses[name]:08X}U')
        put(target,text)
        receipt['inputs'][str(path)]=sha(path)
        receipt['outputs'][relative.as_posix()]=sha(target)
        row['new']=str(target)
    # Internal SDK native operations route through the same native() charge.
    for filename in ('sdk2_operations.inl','sdk2_simulation.inl'):
        path=source/filename
        target=out/'native/mods'/filename
        put(target,transform(path.name,path.read_text()))
        receipt['outputs'][target.relative_to(out).as_posix()]=sha(target)
    import tomllib
    addresses=[]
    for revision in (77,80):
        path=args.reference/f'reference-us-v{revision}/data.toml'
        symbols=tomllib.loads(path.read_text())
        table={s['name']:s['vram'] for section in symbols['section'] for s in section.get('symbols',[])}
        addresses.append(f'inline constexpr std::uint32_t Pool{revision}=0x{table["gMemoryPools"]:08X}U;')
        receipt['inputs'][str(path.resolve())]=sha(path)
    path=out/'native/game/render_heap_addresses.hpp'
    put(path,'#pragma once\n#include <cstdint>\nnamespace dkr::runtime::render_heap_addresses {\n'+'\n'.join(addresses)+'\n}\n')
    receipt['outputs'][path.relative_to(out).as_posix()]=sha(path)
    for path in sorted(AUTH.iterdir()):
        if path.is_file():receipt['authored'][path.name]=sha(path)
    for path,value in receipt['inputs'].items():
        if sha(Path(path))!=value:raise ValueError('Read-only source input changed')
    put(out/'native-swaps.json',json.dumps(records,indent=2)+'\n')
    put(out/'repair-receipt.json',json.dumps(receipt,indent=2)+'\n')
    print('Per-owner work, post-mod drawing heaps, empty HUD scopes composed; no game/mod/test execution.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('inputs','output','sdk','reference'):parser.add_argument('--'+name,type=Path,required=True)
    stage(parser.parse_args())

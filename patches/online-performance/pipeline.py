"""Compose bounded setup and owned-menu repairs above the accepted v1.0.11 chain."""
from pathlib import Path
import argparse,hashlib,json,re,sys
sys.dont_write_bytecode=True
AUTH=Path(__file__).resolve().parent;ROOT=AUTH.parents[1]
VERSION='1.0.11-online-playtest.1'
NAMES={('DKRExperimentalOwnedGameV77','probe_native.c'),('DKRExperimentalOwnedGameV80','probe_native.c'),
 ('DKRUpdateInstall','update_process.cpp'),('DKRPortGame','game_main.cpp'),('DKRPortGame','party_runtime.cpp'),
 ('DKRNetplayCore','direct_session.cpp'),('DKRNetplayCore','quick_join_transport.cpp'),
 ('DKRLegacyModCore','online_mod_cache.cpp'),('DKRLegacyImportLibrary','online_mod_sync.cpp'),
 ('DKRLegacyModLaunch','online_mod_runtime.cpp')}
def sha(data):return hashlib.sha256(data).hexdigest()
def once(text,old,new):
    if text.count(old)!=1:raise ValueError('Online performance anchor differs: '+old[:140])
    return text.replace(old,new,1)
def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    if not path.is_file() or path.read_bytes()!=data:path.write_bytes(data)
def function(text,marker):
    start=text.index(marker);brace=text.index('{',start);depth=1;end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def compose(rows,out):
    if len(rows)!=len(NAMES) or {(r['target'],Path(r['path']).name) for r in rows}!=NAMES:
        raise ValueError('Online performance source closure differs')
    files={p.name:p.read_bytes() for p in (AUTH/'native').glob('*')};swaps=[]
    header=lambda n:'#include "'+(out/n).as_posix()+'"\n'
    for row in rows:
        source=Path(row['path']);raw=source.read_bytes();text=raw.decode().replace('\r\n','\n');name=source.name
        if name=='probe_native.c':
            text=header('menu_notifications.h')+text
            text=once(text,'DKR_PROBE_HAS_FULL_SCENES && event<=17','DKR_PROBE_HAS_FULL_SCENES && dkr_menu_notification(event)')
            text=text.replace('complete 18-event reviewed','reviewed stock and vehicle-notification')
            marker='    dkr_probe_block(name);'
            start=text.index('int dkr_probe_native_fields(')
            pre=text[:start];tail=text[start:]
            tail=once(tail,marker,'    if(strcmp(name,"dkr_legacy_track_menu")==0)fprintf(stderr,"[rollback][menu-refused] event=%u full-scenes=%u offline=%u canonical=%u\\n",event,unsigned_placeholder,offline,canonical_presentation);\n'+marker)
            tail=tail.replace('unsigned_placeholder','(unsigned)DKR_PROBE_HAS_FULL_SCENES')
            text=pre+tail
            files[row['target']+'/menu_fixture.c']=(header('menu_notifications.h')+(AUTH/'tests/menu_fixture_prefix.c').read_text()+
                function(text,'int dkr_probe_native_fields(')+'\n'+(AUTH/'tests/menu_fixture_main.c').read_text()).encode()
            files[row['target']+'/menu_before_fixture.c']=((AUTH/'tests/menu_fixture_prefix.c').read_text()+
                function(raw.decode(),'int dkr_probe_native_fields(')+'\n'+(AUTH/'tests/menu_fixture_main.c').read_text()).encode()
        elif name=='update_process.cpp':
            text=once(text,'command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW','command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW')
        elif name=='game_main.cpp':
            text=header('runtime_log.hpp')+text
            text=once(text,'const std::filesystem::path current = log_directory / "runtime.log";\n    if (!dkr::runtime::support::history::rotate(log_directory)) return false;',
                'const bool rotated=dkr::runtime::support::history::rotate(log_directory);\n'
                '    const std::filesystem::path current = rotated ? log_directory / "runtime.log" : dkr::online_performance::fallback_log(log_directory);\n    if(current.empty())return false;')
            text=once(text,'    std::setvbuf(stderr, nullptr, _IONBF, 0);\n#if defined(__ANDROID__) || defined(DKR_IOS)',
                '    dkr::online_performance::protect_log(stderr);\n    std::setvbuf(stderr, nullptr, _IONBF, 0);\n'
                '    if(!rotated){dkr::runtime::support::history::prune(log_directory/"history");std::fprintf(stderr,"[boot][logging] current log busy; separate instance log=%s\\n",current.string().c_str());}\n#if defined(__ANDROID__) || defined(DKR_IOS)')
        elif name=='quick_join_transport.cpp':
            text=header('setup_policy.hpp')+text
            text=once(text,'if (!quick_join_admit(traffic, buffered, aggregate, bytes.size() + 1U))',
                'if (!(quick_join_admit(traffic, buffered, aggregate, bytes.size() + 1U) ||\n'
                '              dkr::online_performance::additional_capacity(traffic,dkr::online_performance::setup_bulk,buffered,aggregate,bytes.size()+1U)))')
        elif name=='direct_session.cpp':
            text=header('setup_policy.hpp')+text
            text=once(text,'            const DatagramSendStatus result = transport_->send_status(',
                '            dkr::online_performance::SetupBulk setup(packet.type==protocol::MessageType::ModData);\n'
                '            const DatagramSendStatus result = transport_->send_status(')
            text=once(text,'                    const auto sent=transport_->send_status(destination,encrypted,item.traffic,ignored);',
                '                    dkr::online_performance::SetupBulk setup(item.traffic==TransportTrafficClass::Checkpoint && !party->running && !party->stopping && state_==ConnectionState::Loading);\n'
                '                    const auto sent=transport_->send_status(destination,encrypted,item.traffic,ignored);')
        elif name=='online_mod_cache.cpp':
            text=header('setup_policy.hpp')+text
            text=once(text,'if(size_-flushed_>=64*1024)flush();','if(size_-flushed_>=dkr::online_performance::DurableBatch)flush();')
        elif name=='online_mod_sync.cpp':
            # Keep the bounded bursts, active-slot fairness and SDK backpressure.
            # Drain a full receive batch immediately instead of sleeping after it.
            text=once(text,'        pause(stop,std::chrono::milliseconds(2));\n    }\n    if(saw_offer)',
                '        if(drained>32)std::this_thread::yield();else pause(stop,std::chrono::milliseconds(2));\n    }\n    if(saw_offer)')
            text=once(text,'const auto download_began=Clock::now();','const auto download_began=Clock::now();Clock::time_point payload_began{};')
            text=once(text,'download.consent();progress(', 'download.consent();payload_began=Clock::now();progress(')
            text=once(text,'cached-bytes=%llu gap-retries=%llu refill-credits=%llu elapsed-ms=%lld',
                'cached-bytes=%llu gap-retries=%llu refill-credits=%llu setup-ms=%lld payload-phase-ms=%lld')
            text=once(text,'(Clock::now()-download_began).count()));',
                '(Clock::now()-download_began).count()),\n                static_cast<long long>(payload_began.time_since_epoch().count()?std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-payload_began).count():0));')
        elif name=='online_mod_runtime.cpp':
            text=header('setup_policy.hpp')+text
            text=text.replace('resource_cache_budget(','dkr::online_performance::resource_budget(')
            text=once(text,'std::weak_ptr<const RuntimeResources> live_resource_cache;',
                'std::weak_ptr<const RuntimeResources> live_resource_cache;\nstd::uint64_t retained_bytes=0;')
            text=once(text,'        if(auto reusable=live_resource_cache.lock();',
                '        if(resource_cache && retained_bytes>retention_budget())resource_cache.reset();\n        if(auto reusable=live_resource_cache.lock();')
            text=once(text,'if(cache_bytes<=budget)resource_cache=out;',
                'retained_bytes=cache_bytes;if(cache_bytes<=budget)resource_cache=out;')
            text=once(text,'    cancelled(stop);const auto sources=profile_sources(*profile);check_storage(rom,false);',
                '    auto phase_at=began;\n    const auto phase=[&](const char* name){const auto now=std::chrono::steady_clock::now();std::fprintf(stderr,"[online-mods][prepare-phase] phase=%s elapsed-ms=%lld\\n",name,static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(now-phase_at).count()));phase_at=now;};\n'
                '    cancelled(stop);const auto sources=profile_sources(*profile);check_storage(rom,false);phase("profile-verification");')
            text=once(text,'    out->stock_=AssetBank::stock(std::move(bytes));','    phase("rom-verification");out->stock_=AssetBank::stock(std::move(bytes));')
            text=once(text,'    std::vector<std::shared_ptr<const vehicles::Package>> cars;',
                '    phase("courses-characters-music");\n    std::vector<std::shared_ptr<const vehicles::Package>> cars;')
            text=once(text,'    const auto budget=retention_budget();','    phase("vehicles-and-world");\n    const auto budget=retention_budget();')
        elif name=='party_runtime.cpp':
            # Immutable input has its own reliable/unordered channel. Epoch,
            # lease, immutability and bounded repair history remain unchanged.
            count=len(re.findall(r'Msg::Input,body.b,Lane::Control',text))
            if count!=4:raise ValueError('Party input send surface differs')
            text=text.replace('Msg::Input,body.b,Lane::Control','Msg::Input,body.b,Lane::Authoritative')
            text=once(text,'            if(d.accepted&&chunks<4)if(const auto chunk=d.sender.next_chunk()) {',
                '            const unsigned total_budget=phase==Phase::Admission?16:4;\n'
                '            for(unsigned peer_chunks=0;d.accepted&&chunks<total_budget&&peer_chunks<(phase==Phase::Admission?4U:1U);++peer_chunks) {\n'
                '                const auto chunk=d.sender.next_chunk();if(!chunk)break;')
            text=once(text,'if(party_send(key,id,w.b,Lane::Checkpoint)){d.sender.sent();d.progress=now;++chunks;}',
                'if(party_send(key,id,w.b,Lane::Checkpoint)){d.sender.sent();d.progress=now;++chunks;}else break;')
            text=once(text,'    std::string text="Preparing party play",callback_error;',
                '    Clock::time_point performance_at{};std::uint64_t step_us=0,max_step_us=0;unsigned steps=0,stalls=0;\n'
                '    std::string text="Preparing party play",callback_error;')
            text=once(text,'            const auto step=driver->step(advance);',
                '            const auto step_began=Clock::now();const auto step=driver->step(advance);\n'
                '            const auto cost=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-step_began).count());step_us+=cost;max_step_us=std::max(max_step_us,cost);++steps;if(advance&&step!=ex::Step::Advanced)++stalls;')
            text=once(text,'            (void)stats;',
                '            if(now>=performance_at){performance_at=now+std::chrono::seconds(5);\n'
                '                std::fprintf(stderr,"[party][performance] epoch=%llu next=%u confirmed=%u delay=%u window=%u rollbacks=%u replayed=%u largest=%u predictions=%u step-avg-us=%llu step-max-us=%llu blocked-pulses=%u\\n",static_cast<unsigned long long>(cursor.epoch),after.frame,confirmed(),unsigned(launch.input_delay_frames),unsigned(launch.rollback_window),stats.rollbacks,stats.replayed_frames,stats.largest_rollback,stats.predicted_frames,static_cast<unsigned long long>(steps?step_us/steps:0),static_cast<unsigned long long>(max_step_us),stalls);\n'
                '                steps=stalls=0;step_us=max_step_us=0;}')
        def relative(m):
            path=Path(m[1]);path=path if path.is_absolute() else source.parent/path
            return '#include "'+path.resolve().as_posix()+'"' if path.is_file() else m[0]
        text=re.sub(r'^#include "([^"\n]+)"',relative,text,flags=re.M)
        filename=row['target']+'/'+source.name;files[filename]=text.encode()
        swaps.append({**row,'new':str(out/filename),'input_sha256':sha(raw),'output_sha256':sha(text.encode())})
    fixture=(ROOT/'patches/race-release-repair/tests/runtime_tests.cpp').read_text()
    fixture=once(fixture,'}\n}\nint main() {',(AUTH/'tests/party_checks.inl').read_text()+'\n}\n}\nint main() {')
    fixture=once(fixture,'        racer_transitions();','        input_lane_and_reordering();initial_checkpoint_burst();racer_transitions();')
    files['party_checks.cpp']=fixture.encode()
    return swaps,files
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--inputs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.is_relative_to(ROOT):raise ValueError('Performance staging must remain outside checkout')
    rows,files=compose(json.loads(a.inputs.read_text()),out)
    for name,data in files.items():write(out/name,data)
    write(out/'swaps.json',(json.dumps(rows,indent=2)+'\n').encode())
    print('Online performance layer composed; previous protected inputs untouched.')

#include "probe_driver.hpp"
#include "owned_game.hpp"
#include "rom_revision.hpp"
#include "dkr_save_codec.hpp"
#include "probe_component.h"
#include "probe_audio.h"
#include "probe_input.h"
#include "probe_pak.h"
#include "probe_magic.h"
#include "netplay/experimental_eeprom.hpp"
#include "netplay/experimental_pak.hpp"
#include "netplay/experimental_magic_codes.hpp"
#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/experimental_performance.hpp"
#include "netplay/experimental_effect_envelope.hpp"
#include "netplay/experimental_presentation.hpp"
#include "funcs.h"
#include "netplay/experimental_rollback.hpp"
#include "netplay/experimental_timeline.hpp"
#include "netplay/experimental_network.hpp"
#include "netplay/experimental_pump.hpp"
#include "netplay/experimental_pcm.hpp"
#include "netplay/replay_qualification.hpp"
#include "netplay/runtime_state.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <deque>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace {
using namespace dkr::runtime::netplay;
using namespace dkr::runtime::netplay::experimental;
struct ComponentAudio {
    std::uint32_t rate=0,remainder=0,next_frame=0,epoch_remainder=0;
    std::array<std::uint8_t,4096> dmem{};
    std::array<dkr_probe_audio_guard,8> guards{};
};
static_assert(sizeof(ComponentAudio)==16+4096+8*sizeof(dkr_probe_audio_guard));
void start_owned_video(std::uint8_t* ram,recomp_context*) {dkr_probe_video_start(ram);}
class Component final : public SceneSimulation {
public:
    explicit Component(std::span<const std::uint8_t> fixture,bool water_phases=false,bool mode_phases=false,bool audio_phases=false,bool input_phases=false,std::uint64_t save_epoch=42,bool authored_cpu=false,std::uint32_t magic_codes=0,std::span<const std::uint8_t> initial_save={},OwnedBootOptions boot={}) : ram_(kRollbackMemoryBytes), water_phases_(water_phases), mode_phases_(mode_phases), audio_phases_(audio_phases), input_phases_(input_phases), authored_cpu_(authored_cpu), current_epoch_(save_epoch) {
        assert(!authored_cpu_ || (input_phases_ && DKR_PROBE_HAS_AUTHORED_CPU));
        assert(state_.register_context(&context_));
        unsigned tag = 0;
        assert(state_.restore(ram_.data(), fixture, tag) && tag == DKR_PROBE_REVISION);
        if(authored_cpu_) {
            // Retail thread3_main starts sLogicUpdateRate at LOGIC_5FPS (12)
            // before its FIRST main_game_loop. A running-race fixture instead
            // has the VI-derived rate of 1..6. These are distinct admissions:
            // do not apply the running-race check to a pre-INTRO cold boot.
            // The normal-boot INTRO/empty-world/roster checks below still apply.
            // Ownership is now quiesced;
            // initialise ONLY this separate fixed-30-Hz world's cadence, before
            // any owned tick or network checkpoint. Never normalise a rewind or
            // silently accept an invalid incoming checkpoint here.
            const auto offset=DKR_PROBE_REVISION==77 ? 0xDD404:0xDD974;
            std::uint32_t rate=0;std::memcpy(&rate,ram_.data()+offset,4);
            const bool valid_rate=boot.normal_boot ? rate==12 : rate>=1 && rate<=6;
            if(!valid_rate)throw std::runtime_error(
                "Local bootstrap has an invalid retail logic rate ("+std::to_string(rate)+
                (boot.normal_boot ? "; pre-INTRO requires 12)." : "; running fixture requires 1..6)."));
            const std::uint32_t owned_rate=2;
            std::memcpy(ram_.data()+offset,&owned_rate,4);
            if(boot.normal_boot)std::fprintf(stderr,
                "[rollback][boot] retail cold logic rate=%u; owned cadence=%u\n",rate,owned_rate);
        }
        dkr_probe_offline_services(1);
        dkr_probe_canonical_presentation(mode_phases_);
        dkr_probe_scene_configure(mode_phases_ && DKR_PROBE_HAS_SCENE_CUTS);
        if(mode_phases_) {
            // Fixture was quiesced at a CPU boundary. Keep the initial music
            // animation counter coherent; each peer starts from identical RAM.
            const unsigned offset=DKR_PROBE_REVISION==77 ? 0xDC64C : 0xDCBBC;
            std::uint32_t seed=0; std::memcpy(&seed,ram_.data()+offset,4);
            dkr_probe_clock_start(seed);
        }
        if(authored_cpu_)assert(dkr_probe_run(start_owned_video,ram_.data(),ram_.size(),&context_,10000).completed);
        native_ = dkr_probe_native_capture();
        native_.restore_multiplayer_music=boot.normal_boot && boot.restore_multiplayer_music;
        if(DKR_PROBE_HAS_FULL_SCENES && input_phases_) {
            std::uint32_t a[7],count=0;dkr_probe_roster_addresses(a);
            std::memcpy(&count,ram_.data()+a[0]-0x80000000U,4);
            if(boot.normal_boot) {
                std::int32_t mode=0;std::uint32_t model=0,header=0;
                std::memcpy(&mode,ram_.data()+(DKR_PROBE_REVISION==77 ? 0x1234EC:0x123A6C),4);
                std::memcpy(&model,ram_.data()+(DKR_PROBE_REVISION==77 ? 0xDC918:0xDCE88),4);
                std::memcpy(&header,ram_.data()+(DKR_PROBE_REVISION==77 ? 0xDC91C:0xDCE8C),4);
                if(mode!=-1 || model || header || boot.players<2 || boot.players>4 ||
                   unsigned(boot.host_control)>unsigned(HostControlPolicy::EveryAssignedPort))
                    throw std::runtime_error("Owned normal boot requires the untouched retail INTRO and accepted lobby roster.");
                count=boot.players;native_.initial_world_vacant=1;
            }
            if(count<1 || count>4)throw std::runtime_error("Owned construction has an invalid controller roster.");
            owner_mask_=native_.owner_mask=(1U<<count)-1;
            host_control_=native_.host_control=unsigned(boot.host_control);
            native_.assigned_ports_released=boot.normal_boot ? 0U:1U;
        }
        if(audio_phases_) {
            assert(mode_phases_ && DKR_PROBE_HAS_AUDIO);
            // A second CPU context shares the SAME private guest RAM. Audio
            // synthesis runs synchronously after gameplay, never on a worker.
            assert(state_.register_context(&audio_context_)); repair_float_register_pointer(audio_context_);
            audio_context_.r29=std::int32_t(0x80FF0000U);
            const auto word=[&](unsigned offset) { std::uint32_t value; std::memcpy(&value,ram_.data()+offset,4); return value; };
            const auto globals=word(DKR_PROBE_REVISION==77 ? 0xE3780:0xE3D10);
            assert(globals>=0x80000000U && globals<0x80FFFFB4U);
            audio_rate_=audio_.rate=word(globals-0x80000000U+0x44); assert(audio_rate_>=8000 && audio_rate_<=48000);
            dkr_probe_audio_rsp_initialize(); dkr_probe_audio_dmem_capture(audio_.dmem.data());
            dkr_probe_audio_guards_initialize(); dkr_probe_audio_guards_capture(audio_.guards.data());
        }
        if(input_phases_) {
            assert(audio_phases_ && DKR_PROBE_HAS_INPUT);
            // The full-game owner starts with private fresh EEPROM. Retail
            // recognizes an UNSTARTED Adventure slot by its erased 40 bytes;
            // a checksummed zero-progress slot is an already-started game and
            // skips initials/the new-game cinematic. Keep config and records
            // checksum-valid, and let the real reader handle erased slots.
            // No single-player path/worker is used or overwritten.
            std::vector<std::uint8_t> blank(512,255);
            if(DKR_PROBE_HAS_FULL_SCENES) {
                blank=dkr::runtime::saves::codec::blank_bytes();
                std::fill_n(blank.begin(),3U*40U,255);
                assert(dkr::runtime::saves::codec::validate(blank));
            }
            if(!initial_save.empty()) {
                if(initial_save.size()!=Eeprom::kBytes || !dkr::runtime::saves::codec::validate(initial_save))
                    throw std::runtime_error("The owned online EEPROM is invalid.");
                blank.assign(initial_save.begin(),initial_save.end());
            }
            assert(save_.start(save_epoch,blank));
            assert(paks_.start(save_epoch,owner_mask_ ? owner_mask_:15,Paks::blank_images()));
            assert(magic_.start(save_epoch,magic_codes,true));
            dkr_probe_input_initialize(); input_=dkr_probe_input_capture();
        }
    }
    ~Component() { dkr_probe_scenery_observe(nullptr,nullptr);dkr_probe_input_configure(0); dkr_probe_bind_eeprom(nullptr);dkr_probe_bind_paks(nullptr);dkr_probe_bind_magic(nullptr); }
    std::span<const std::uint8_t> confirmed_save() const {return save_.confirmed_image();}
    std::span<const std::uint8_t> confirmed_paks() const {return paks_.confirmed_images();}
    bool install_initial_paks(std::uint64_t epoch,std::span<const std::uint8_t> images,std::string& error) {
        if(!input_phases_ || epoch!=current_epoch_ || confirmed_count_ || audio_.next_frame ||
           native_.video_frames || native_.pending_scene_site) {
            error="The experimental online MemPak seed must be installed at the initial zero-frame epoch.";return false;
        }
        // Construction already starts a blank private Pak owner. start() is
        // intentionally one-shot: do not start that owner again or weaken its
        // lifetime guard. Validate ALL seed images in a fresh owner, then swap
        // it into this unchanged member address before checkpoint admission.
        // A malformed seed leaves the original blank owner completely intact.
        Paks staged;
        if(!staged.start(epoch,owner_mask_?owner_mask_:15,images)) {
            error="The experimental online MemPak seed contains an invalid image.";return false;
        }
        paks_=std::move(staged);
        return true;
    }
    void set_confirmed_rumble_sink(ConfirmedRumbleSink sink) {rumble_sink_=std::move(sink);}
    SimulationContract contract() const override {
        // Complete only for THIS closed component: all reachable native imports
        // are audited private services or traps. No live participant exists.
        // Passing this contract must never advertise complete DKR replay.
        return {0x434F4D504F4E454EULL+unsigned(mode_phases_)*4+unsigned(water_phases_)+unsigned(DKR_PROBE_HAS_SCENE_CUTS)*8+unsigned(audio_phases_)*4096+unsigned(input_phases_)*128+unsigned(authored_cpu_)*768+unsigned(DKR_PROBE_HAS_FULL_SCENES)*8192+16384+32768+65536, state_.snapshot_size() + sizeof(native_) + (audio_phases_ ? sizeof(audio_):0) + (input_phases_ ? sizeof(input_)+Eeprom::kCheckpointBytes+Paks::kCheckpointBytes+MagicCodes::kCheckpointBytes:0),
                kRequiredStateDomains, true, true, true};
    }
    void attach_presentation_for_test(PresentationMailbox& box,bool independent_restore=false) {
        presentation_=&box;independent_restore_=independent_restore;
    }
    bool before_restore(std::uint64_t epoch,std::uint32_t first_frame,std::string& error) override {
        if(presentation_ && !presentation_->retire_for_restore(epoch,first_frame)) {
            error="Owned render generation refused retirement.";return false;
        }
        return true;
    }
    RestoreStep prepare_restore(std::uint64_t epoch,std::uint32_t first,std::string& error) override {
        if(!presentation_)return RestoreStep::Ready;
        const auto request=std::pair(epoch,first);
        if(!retiring_||*retiring_!=request) {
            if(!before_restore(epoch,first,error))return RestoreStep::Failed;
            retiring_=request;
        }
        // Ordinary gameplay rewinds only the owned CPU world. Decoder RAM and
        // GPU uploads are separate immutable/copied images; their admitted
        // generation may finish without blocking this rewind. Menu/scene
        // resource mutation keeps the conservative drain and quiescence gates.
        if(independent_restore_ && !requires_confirmed_tick() && !native_.pending_scene_site)
            return RestoreStep::Ready;
        return presentation_->submissions_drained()?RestoreStep::Ready:RestoreStep::Pending;
    }
    bool requires_confirmed_tick() const override {
        if(!DKR_PROBE_HAS_FULL_SCENES || !authored_cpu_)return false;
        std::uint32_t mode=0;
        std::memcpy(&mode,ram_.data()+(DKR_PROBE_REVISION==77 ? 0x1234EC:0x123A6C),4);
        return mode!=0; // Menu/intro, never ordinary racing or hub gameplay.
    }
    RestoreStep prepare_tick(std::uint64_t epoch,std::uint32_t frame,bool agreed,std::string& error) override {
        if(!requires_confirmed_tick())return RestoreStep::Ready;
        if(!agreed || epoch!=current_epoch_ || frame!=confirmed_count_ || native_.pending_scene_site) {
            error="Menu resource tick requires the confirmed input frontier.";return RestoreStep::Failed;
        }
        // Copied-image presentation owns ALL bytes read by F3DDKR. A menu
        // constructor changes only private CPU RAM; it cannot free a decoder's
        // image, upload vector or GPU resource. Do not retire interpolation and
        // serialize GPU completion on every ordinary menu animation tick.
        // Restore and agreed scene-epoch transitions retain their separate
        // retirement gates. Confirmed menu input admission remains mandatory.
        prepared_menu_tick_=frame;return RestoreStep::Ready;
    }
    bool capture(std::span<std::uint8_t> output, std::string&) override {
        if(input_phases_ && (!valid_input(input_) || output.size()!=contract().state_bytes ||
           !save_.capture(output.last(Eeprom::kCheckpointBytes+Paks::kCheckpointBytes).first(Eeprom::kCheckpointBytes)) ||
           !paks_.capture(output.last(Paks::kCheckpointBytes)))) return false;
        if (!valid_native(native_) || (audio_phases_ && !valid_audio(audio_)) || output.size() != contract().state_bytes || !state_.capture(ram_.data(),0,output.first(state_.snapshot_size()))) return false;
        std::memcpy(output.data()+state_.snapshot_size(), &native_,sizeof(native_));
        if(audio_phases_) std::memcpy(output.data()+state_.snapshot_size()+sizeof(native_),&audio_,sizeof(audio_));
        if(input_phases_) {
            std::memcpy(output.data()+output.size()-Eeprom::kCheckpointBytes-Paks::kCheckpointBytes-sizeof(input_),&input_,sizeof(input_));
            if(!magic_.capture(output.subspan(state_.snapshot_size()+sizeof(native_)+sizeof(audio_),MagicCodes::kCheckpointBytes)))return false;
        }
        return true;
    }
    bool restore(std::span<const std::uint8_t> input, std::string&) override {
        if (input.size() != contract().state_bytes) return false;
        dkr_probe_native_state value{};
        std::memcpy(&value,input.data()+state_.snapshot_size(),sizeof(value));
        // Validate ALL native participants before mutating guest RAM/registers.
        if (!valid_native(value)) return false;
        ComponentAudio audio{};
        if(audio_phases_) {
            std::memcpy(&audio,input.data()+state_.snapshot_size()+sizeof(native_),sizeof(audio));
            if(!valid_audio(audio)) return false;
        }
        dkr_probe_input_state pads{}; auto staged_save=save_;auto staged_paks=paks_;auto staged_magic=magic_;
        if(input_phases_) {
            std::memcpy(&pads,input.data()+input.size()-Eeprom::kCheckpointBytes-Paks::kCheckpointBytes-sizeof(pads),sizeof(pads));
            if(!valid_input(pads) || !staged_save.restore(input.last(Eeprom::kCheckpointBytes+Paks::kCheckpointBytes).first(Eeprom::kCheckpointBytes)) ||
               !staged_paks.restore(input.last(Paks::kCheckpointBytes)) ||
               !staged_magic.restore(input.subspan(state_.snapshot_size()+sizeof(native_)+sizeof(audio_),MagicCodes::kCheckpointBytes))) return false;
        }
        unsigned frame = 0;
        if (!state_.restore(ram_.data(),input.first(state_.snapshot_size()),frame)) return false;
        native_ = value; if(audio_phases_) audio_=audio;
        if(input_phases_) { input_=pads;save_=std::move(staged_save);paks_=std::move(staged_paks);magic_=std::move(staged_magic); }
        draw_events_.clear();retiring_.reset();preparing_tick_.reset();prepared_menu_tick_.reset();return true;
    }
    bool tick(std::uint32_t frame, const FrameInputs& inputs, TickOutput& output, std::string& error) override {
        dkr_probe_boss_diagnostic_tick(current_epoch_,frame);
        if(audio_phases_ && (frame!=audio_.next_frame || frame==UINT32_MAX)) return false;
        const bool menu_tick=requires_confirmed_tick();
        if(menu_tick && prepared_menu_tick_!=frame) {
            error="A resource-changing menu tick bypassed confirmed admission.";return false;
        }
        prepared_menu_tick_.reset();preparing_tick_.reset();
        std::array<dkr_probe_pad,4> pads;
        for (unsigned p=0;p<4;++p) {
            const bool host_only=menu_tick && p>0 &&
                host_control_!=unsigned(HostControlPolicy::EveryAssignedPort) &&
                (!native_.assigned_ports_released ||
                 (host_control_==unsigned(HostControlPolicy::HostSharedMenus) && menu_for_test()!=3));
            // Filter at the logical simulation frame, never during hardware
            // sampling. Replay uses the SAME captured menu/release state and
            // immutable owner-final inputs; no local clock/input reinterpretation.
            pads[p]=host_only || (owner_mask_ && !(owner_mask_&(1U<<p))) ? dkr_probe_pad{}:
                dkr_probe_pad{inputs[p].buttons,inputs[p].stick_x,inputs[p].stick_y};
        }
        dkr_probe_component_inputs(pads.data());
        dkr_probe_native_restore(native_);
        dkr_probe_canonical_presentation(mode_phases_);
        dkr_probe_scene_configure(mode_phases_ && DKR_PROBE_HAS_SCENE_CUTS);
        dkr_probe_menu_tick_configure(menu_tick);
        dkr_probe_menu_preview_configure(menu_tick && scene_sessions_ && independent_restore_);
        dkr_probe_effects_begin();
        dkr_probe_draw_begin();
        if(input_phases_) {
            if(!save_.begin_frame(frame) || !paks_.begin_frame(frame) || !magic_.begin_frame(frame)) return false;
            dkr_probe_input_restore(input_);dkr_probe_input_configure(1);dkr_probe_bind_eeprom(&save_);
            dkr_probe_bind_paks(&paks_);
            dkr_probe_bind_magic(&magic_);
            std::uint32_t flags=0;
            std::memcpy(&flags,ram_.data()+(DKR_PROBE_REVISION==77 ? 0xDD37C:0xDD8EC),4);
            dkr_probe_input_arguments(pads.data(),flags);
            dkr_probe_reset_poll_configure(1);
        }
        const auto previous_preview_loads=native_.preview_loads;
        const auto tick_started=std::chrono::steady_clock::now();
        dkr_probe_result result;
        {
            performance::Scope timing(performance::Stage::GuestTick);
            result = dkr_probe_run(authored_cpu_ ? dkr_probe_authored_component_tick : mode_phases_ ? dkr_probe_mode_component_tick : water_phases_ ? dkr_probe_water_component_tick : dkr_probe_component_tick,
                                  ram_.data(),ram_.size(),&context_,menu_tick ? 40000000:5000000);
        }
        dkr_probe_menu_tick_configure(0);
        native_ = dkr_probe_native_capture();
        if(result.completed && native_.preview_loads!=previous_preview_loads) {
            // The constructor reset its owned audio queue guards. Preserve
            // that reset before DSP restore, as the scene-epoch path does.
            if(audio_phases_)dkr_probe_audio_guards_capture(audio_.guards.data());
            std::fprintf(stderr,"[rollback][preview-load] epoch=%llu frame=%u serial=%u tick-us=%lld operations=%llu\n",
                static_cast<unsigned long long>(current_epoch_),frame,native_.preview_loads,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now()-tick_started).count()),
                static_cast<unsigned long long>(result.operations));
        }
        std::vector<std::uint8_t> save_journal,pak_journal,magic_journal;
        std::array<dkr_probe_motor_event,128> motor_journal{}; unsigned motor_count=0;
        if(input_phases_) {
            input_=dkr_probe_input_capture();motor_count=dkr_probe_motor_events(motor_journal.data());
            dkr_probe_input_configure(0);dkr_probe_bind_eeprom(nullptr);dkr_probe_reset_poll_configure(0);dkr_probe_bind_paks(nullptr);
            dkr_probe_bind_magic(nullptr);
            if(result.completed && (!valid_input(input_) || !save_.end_frame(save_journal) || !paks_.end_frame(pak_journal) || !magic_.end_frame(magic_journal))) return false;
        }
        if (!result.completed || native_.vehicle_audio_scope || native_.nature_audio_scope) {
            error = result.blocked ? result.blocked : "unbalanced audio scope";
            std::cerr << "component tick blocked: " << error << " address=0x" << std::hex << result.bad_address
                      << std::dec << " operations=" << result.operations << '\n';
            if (result.source_file) std::cerr << "  site=" << result.source_file << ':' << result.source_line << '\n';
            for (unsigned i=0;i<result.stack_depth;++i) std::cerr << "  " << result.stack[i] << '\n';
            std::cerr << std::hex << "  a0=" << context_.r4 << " a1=" << context_.r5 << " v0=" << context_.r2
                      << " s0=" << context_.r16 << " s1=" << context_.r17 << " s2=" << context_.r18
                      << " s3=" << context_.r19 << " sp=" << context_.r29 << std::dec << '\n';
            std::cerr << std::hex << "  a2=" << context_.r6 << " a3=" << context_.r7
                      << " t6=" << context_.r14 << " t8=" << context_.r24 << std::dec << '\n';
            return false;
        }
        // Query the complete bounded transaction first. Growing to all 8192
        // records every tick needlessly zeroed ~608 KiB even for small scenes.
        const auto observed_count=dkr_probe_draw_capture(nullptr,0);
        if(observed_count>DKR_OWNED_MAX_DRAW_EVENTS) {error="Owned draw metadata exceeded its bound.";return false;}
        draw_events_.resize(observed_count);
        const auto draw_count=dkr_probe_draw_capture(draw_events_.data(),unsigned(draw_events_.size()));
        if(draw_count!=observed_count) {error="Owned draw metadata changed during capture.";return false;}
        std::array<dkr_probe_effect,DKR_PROBE_MAX_OBJECT_EFFECTS> effects;
        const auto count = dkr_probe_effects_capture(effects.data(),effects.size());
        if (count > effects.size()) return false;
        // Canonical field encoding, not host struct padding.
        // The rollback ring owns this output. Keep its allocation when a
        // correction rebuilds the same entry; do not replace the vector.
        output.effects.clear();
        output.scene_boundary=native_.pending_scene_site!=0;
        output.effects.reserve(count*8);
        for (unsigned i=0;i<count;++i) for (auto value : {effects[i].kind,effects[i].object})
            for (unsigned b=0;b<4;++b) output.effects.push_back(std::uint8_t(value>>(8*b)));
        if(audio_phases_) {
            const auto object_bytes=output.effects.size(); unsigned samples=0;
            if(!output.scene_boundary) {
                const unsigned total=audio_.remainder+audio_.rate; samples=(total/480)*16;
                dkr_probe_audio_configure(1); dkr_probe_audio_dmem_restore(audio_.dmem.data());
                dkr_probe_audio_guards_restore(audio_.guards.data());
                dkr_probe_audio_arguments(samples,0); // Sounds originate in the real gameplay tick, not injection.
                dkr_probe_result dsp;
                {
                    performance::Scope timing(performance::Stage::AudioDsp);
                    dsp=dkr_probe_run(dkr_probe_audio_tick,ram_.data(),ram_.size(),&audio_context_,5000000);
                }
                dkr_probe_audio_configure(0); dkr_probe_audio_dmem_capture(audio_.dmem.data());
                dkr_probe_audio_guards_capture(audio_.guards.data()); native_=dkr_probe_native_capture();
                if(!dsp.completed || !valid_native(native_)) {
                    error=dsp.blocked ? dsp.blocked:"unbalanced combined audio state";
                    std::cerr<<"Combined gameplay/audio fenced: "<<error
                             <<" epoch="<<current_epoch_<<" frame="<<frame<<" samples="<<samples
                             <<" operations="<<dsp.operations<<'\n';
                    std::cerr<<std::hex<<"  bad_address=0x"<<dsp.bad_address
                             <<" sp=0x"<<audio_context_.r29<<" v0=0x"<<audio_context_.r2
                             <<" a0=0x"<<audio_context_.r4<<" a1=0x"<<audio_context_.r5
                             <<" a2=0x"<<audio_context_.r6<<" a3=0x"<<audio_context_.r7
                             <<" s0=0x"<<audio_context_.r16<<" s1=0x"<<audio_context_.r17
                             <<" s2=0x"<<audio_context_.r18<<" s7=0x"<<audio_context_.r23
                             <<std::dec<<'\n';
                    if(dsp.source_file)std::cerr<<"  source="<<dsp.source_file<<':'<<dsp.source_line<<'\n';
                    for(unsigned s=0;s<dsp.stack_depth;++s) std::cerr<<"  "<<dsp.stack[s]<<'\n';
                    return false;
                }
                audio_.remainder=total%480;
            }
            ++audio_.next_frame;
            // Composite frame journal: ordered object events plus canonical
            // PCM, with explicit lengths. No SDL/device output during replay.
            performance::Scope journal_timing(performance::Stage::EffectJournal);
            // Reserve the complete nested transaction once, then retain this
            // allocation in the rollback ring. No per-envelope heap buffers.
            const auto input_bytes=input_phases_ ? 16+save_journal.size()+motor_count*8+
                12+pak_journal.size()+8+magic_journal.size() : 0;
            const auto video_bytes=authored_cpu_&&!output.scene_boundary ? 56U : 0U;
            output.effects.reserve(20+object_bytes+samples*4+input_bytes+video_bytes);
            prepend_effect_envelope(output.effects,{'D','K','G','A'},
                std::array{frame,audio_.rate,samples,unsigned(object_bytes)});
            const auto pcm_start=output.effects.size();output.effects.resize(pcm_start+samples*4);
            if(!pcm_s16le_from_guest(ram_,DKR_PROBE_AUDIO_OUTPUT-0x80000000U,
                std::span(output.effects).subspan(pcm_start))) {error="Owned PCM source range invalid.";return false;}
        }
        if(input_phases_) {
            // A versioned envelope preserves the original gameplay/audio
            // journal. All lengths and motor records are validated BEFORE
            // confirmation changes any irreversible save/output participant.
            performance::Scope journal_timing(performance::Stage::EffectJournal);
            output.effects.reserve(16+output.effects.size()+save_journal.size()+motor_count*8+
                12+pak_journal.size()+8+magic_journal.size()+
                (authored_cpu_&&!output.scene_boundary ? 56U : 0U));
            prepend_effect_envelope(output.effects,{'D','K','G','I'},
                std::array{unsigned(output.effects.size()),unsigned(save_journal.size()),motor_count});
            output.effects.insert(output.effects.end(),save_journal.begin(),save_journal.end());
            for(unsigned m=0;m<motor_count;++m) for(auto value:{motor_journal[m].channel,motor_journal[m].enabled})
                for(unsigned b=0;b<4;++b) output.effects.push_back(std::uint8_t(value>>(b*8)));
            prepend_effect_envelope(output.effects,{'D','K','G','P'},
                std::array{unsigned(output.effects.size()),unsigned(pak_journal.size())});
            output.effects.insert(output.effects.end(),pak_journal.begin(),pak_journal.end());
            prepend_effect_envelope(output.effects,{'D','K','G','M'},
                std::array{unsigned(output.effects.size())});
            output.effects.insert(output.effects.end(),magic_journal.begin(),magic_journal.end());
        }
        if(authored_cpu_ && !output.scene_boundary) {
            const auto word=[&](unsigned offset){std::uint32_t v;std::memcpy(&v,ram_.data()+offset,4);return v;};
            const unsigned slot=1-word(DKR_PROBE_REVISION==77 ? 0x1234E8:0x123A68);
            const unsigned base=DKR_PROBE_REVISION==77 ? 0x1211F0:0x121770;
            const auto start=word(base+slot*4),end=word(base+8);
            if(slot>1 || start<0x80000000U || end<=start || end>0x81000000U) return false;
            performance::Scope journal_timing(performance::Stage::EffectJournal);
            // These two fields are PRIVATE qualification evidence, never
            // checkpoint/network validation. Production does not retain or
            // consume them: avoid rescanning all 16 MiB on every replay tick.
            // Private proof mode retains the original exact CPU/DL hashes.
            std::uint64_t ram_proof=0,display_proof=0;
            if(retain_proofs_) {
                performance::Scope proof_timing(performance::Stage::VideoProof);
                ram_proof=checkpoint_hash(ram_);
                display_proof=checkpoint_hash(std::span(ram_).subspan(start-0x80000000U,end-start));
            }
            prepend_effect_envelope(output.effects,{'D','K','G','V'},
                std::array{unsigned(output.effects.size()),frame,slot,start,end,
                    native_.video_framebuffer,native_.video_depthbuffer,
                    native_.video_black,native_.video_frames},
                std::array{ram_proof,display_proof});
        }
        return true;
    }
    bool commit(std::uint64_t epoch, std::uint32_t frame, std::span<const std::uint8_t> effects,
                bool boundary,std::string& error) override {
        if(input_phases_ && epoch!=current_epoch_)return false;
        if (frame != confirmed_count_ || frame==UINT32_MAX || (boundary && (!native_.pending_scene_site || confirmed_scene_))) return false;
        const auto original=effects;
        if(authored_cpu_ && !boundary) {
            if(effects.size()<56 || !std::equal(effects.begin(),effects.begin()+4,"DKGV"))return false;
            const auto word=[&](unsigned offset){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::uint32_t(effects[offset+b])<<(8*b);return v;};
            if(word(4)>65536 || effects.size()!=56+word(4) || word(8)!=frame || word(12)>1 ||
               word(16)<0x80000000U || word(20)<=word(16) || word(20)>0x80800000U ||
               (word(16)&7) || (word(20)&7) ||
               (word(20)-word(16))%8 || word(20)-word(16)<16 ||
               word(24)<0x80000000U || word(24)>0x807DA800U || (word(24)&7) ||
               word(28)<0x80000000U || word(28)>0x807DA800U || (word(28)&7) ||
               word(32)>1 || word(36)!=frame+1)return false;
            // Zero explicitly denotes omitted private evidence in production.
            // All descriptor/journal validation above and checkpoint hashes
            // in Driver remain mandatory; never accept hidden proof bytes.
            if(!retain_proofs_ && std::any_of(effects.begin()+40,effects.begin()+56,
                                            [](std::uint8_t value){return value!=0;}))return false;
            effects=effects.subspan(56);
        }
        auto staged_save=save_;auto staged_paks=paks_;auto staged_magic=magic_;
        std::array<std::int8_t,4> confirmed_motors{-1,-1,-1,-1};
        if(input_phases_) {
            if(effects.size()<8+MagicCodes::kJournalBytes || !std::equal(effects.begin(),effects.begin()+4,"DKGM"))return false;
            std::uint32_t inner_size=0;for(unsigned b=0;b<4;++b)inner_size|=std::uint32_t(effects[4+b])<<(8*b);
            if(inner_size>65536 || effects.size()!=8+inner_size+MagicCodes::kJournalBytes ||
               !staged_magic.commit(epoch,frame,effects.last(MagicCodes::kJournalBytes)))return false;
            effects=effects.subspan(8,inner_size);
            if(effects.size()<12 || !std::equal(effects.begin(),effects.begin()+4,"DKGP")) return false;
            const auto outer=[&](unsigned offset){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::uint32_t(effects[offset+b])<<(b*8);return v;};
            const auto inner=outer(4),pak_bytes=outer(8);
            if(inner>65536 || pak_bytes>49152 || effects.size()!=12+inner+pak_bytes ||
               !staged_paks.commit(epoch,frame,effects.subspan(12+inner,pak_bytes)))return false;
            effects=effects.subspan(12,inner);
            if(effects.size()<16 || !std::equal(effects.begin(),effects.begin()+4,"DKGI")) return false;
            const auto read=[&](unsigned offset){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::uint32_t(effects[offset+b])<<(b*8);return v;};
            const auto gameplay=read(4),save=read(8),motors=read(12);
            if(gameplay>20+DKR_PROBE_MAX_OBJECT_EFFECTS*8+2048*4 || save>65536 || motors>128 || effects.size()!=16+gameplay+save+motors*8) return false;
            for(unsigned m=0;m<motors;++m) {
                const auto port=read(16+gameplay+save+m*8),enabled=read(20+gameplay+save+m*8);
                if(port>3||enabled>1)return false;
                confirmed_motors[port]=std::int8_t(enabled);
            }
            if(!staged_save.commit(epoch,frame,effects.subspan(16+gameplay,save))) return false;
            effects=effects.subspan(16,gameplay);
        }
        if(audio_phases_) {
            if(effects.size()<20 || !std::equal(effects.begin(),effects.begin()+4,"DKGA") || frame>=audio_.next_frame) return false;
            const auto word=[&](unsigned offset) {
                std::uint32_t value=0; for(unsigned b=0;b<4;++b) value|=std::uint32_t(effects[offset+b])<<(b*8); return value;
            };
            const auto expected=((audio_.epoch_remainder+std::uint64_t(frame+1)*audio_rate_)/480-
                                 (audio_.epoch_remainder+std::uint64_t(frame)*audio_rate_)/480)*16;
            if(word(4)!=frame || word(8)!=audio_rate_ || word(12)!=(boundary ? 0:expected) ||
               word(16)>DKR_PROBE_MAX_OBJECT_EFFECTS*8 || word(16)%8 || effects.size()!=20+word(16)+word(12)*4) return false;
        }
        if(boundary) confirmed_scene_=native_.pending_scene_site;
        if(retain_proofs_)commits_.emplace_back(original.begin(),original.end());
        if(input_phases_) {save_=std::move(staged_save);paks_=std::move(staged_paks);magic_=std::move(staged_magic);}
        ++confirmed_count_;
        // All nested journals were validated before this irreversible sink.
        // No device access in tick()/replay; confirmation invokes exactly once.
        if(rumble_sink_)for(unsigned port=0;port<4;++port)if(confirmed_motors[port]>=0) {
            try {rumble_sink_(port,confirmed_motors[port]!=0);}
            catch(...) {error="Confirmed controller output threw; experimental world halted.";return false;}
        }
        if(audio_sink_ && audio_phases_ && !boundary) {
            const auto word=[&](unsigned offset){std::uint32_t v=0;for(unsigned b=0;b<4;++b)v|=std::uint32_t(effects[offset+b])<<(b*8);return v;};
            std::string sink_error;
            try {
                if(!audio_sink_({epoch,frame,word(8),effects.subspan(20+word(16))},sink_error)) {
                    error=sink_error.empty()?"Confirmed audio output refused the owned journal.":sink_error;return false;
                }
            } catch(...) {error="Confirmed audio output threw; experimental world halted.";return false;}
        }
        return true;
    }
    const auto& commits() const { return commits_; }
    std::uint32_t confirmed_count() const { return confirmed_count_; }
    void configure_owned_output(PresentationMailbox& box,ConfirmedAudioSink sink,bool independent_restore=false) {
        assert(authored_cpu_ && input_phases_);scene_sessions_=true;presentation_=&box;
        retain_proofs_=false;audio_sink_=std::move(sink);independent_restore_=independent_restore;
        scenery_.reset(native_.presentation.scene);
        dkr_probe_scenery_observe([](void* user,const std::uint8_t* ram,std::uint32_t object,std::uint32_t scene,std::uint32_t table) {
            auto& capture=static_cast<Component*>(user)->scenery_;
            if(!object)capture.reset(scene);
            else capture.capture({ram,kRollbackMemoryBytes},object,table);
        },this);
    }
    bool admit_initial(std::span<const std::uint8_t> bytes,std::string& error) {
        // Reject nonzero/scene-pending participants before restore can mutate
        // the local world. A hostile or malformed baseline must not turn an
        // unsuccessful admission into a partially transplanted simulation.
        if(!authored_cpu_ || !audio_phases_ || !input_phases_) {
            error="Initial admission requires the fully owned CPU/audio/input world.";return false;
        }
        dkr_probe_native_state incoming_native{};
        ComponentAudio incoming_audio{};
        if(bytes.size()==contract().state_bytes) {
            std::memcpy(&incoming_native,bytes.data()+state_.snapshot_size(),sizeof(incoming_native));
            std::memcpy(&incoming_audio,bytes.data()+state_.snapshot_size()+sizeof(incoming_native),sizeof(incoming_audio));
        }
        if(bytes.size()!=contract().state_bytes || confirmed_count_ || audio_.next_frame ||
           native_.video_frames || native_.pending_scene_site || incoming_audio.next_frame ||
           incoming_native.video_frames || incoming_native.pending_scene_site || !restore(bytes,error)) {
            error="Not a zero-frame owned launch baseline.";return false;
        }
        std::vector<std::uint8_t> canonical(contract().state_bytes);
        if(!capture(canonical,error) || !std::equal(bytes.begin(),bytes.end(),canonical.begin(),canonical.end())) {
            error="Owned launch baseline is not canonically restorable.";return false;
        }
        return true;
    }
    bool publish_cpu_frame_for_test(PresentationMailbox& box,std::uint64_t epoch,std::uint32_t frame) {
        if(!authored_cpu_ || native_.pending_scene_site)return false;
        const auto word=[&](unsigned offset){std::uint32_t v;std::memcpy(&v,ram_.data()+offset,4);return v;};
        const auto next_slot=word(DKR_PROBE_REVISION==77 ? 0x1234E8:0x123A68);
        if(next_slot>1)return false;
        const auto slot=1-next_slot;
        const auto base=DKR_PROBE_REVISION==77 ? 0x1211F0:0x121770;
        if(published_preview_loads_!=native_.preview_loads && !box.retire_for_restore(epoch,frame))return false;
        // Outside tick/commit/checkpoint: presentation ownership never rewinds
        // with guest RAM and never publishes a historical correction tick.
        const auto local_scene=scenery_.freeze(native_.presentation.scene);
        if(local_scene&&local_scene->scene!=reported_scenery_scene_) {
            reported_scenery_scene_=local_scene->scene;
            std::fprintf(stderr,"[rollback][local-scenery] scene=%u sprites=%zu bytes=%zu rejected=%u last=%s\n",
                local_scene->scene,local_scene->sprites.size(),local_scene->payload_bytes,
                scenery_.rejected(),scenery_.last_failure());
        }
        const bool published=box.publish(epoch,{frame,word(base+slot*4),word(base+8),
            native_.video_framebuffer,native_.video_depthbuffer,native_.video_black},ram_,draw_events_,local_scene);
        if(published)published_preview_loads_=native_.preview_loads;
        return published;
    }
    unsigned pending_scene() const { return native_.pending_scene_site; }
    unsigned owner_count_for_test() const { return std::popcount(owner_mask_); }
    OwnedSceneView scene_view() const {
        const auto word=[&](unsigned offset){std::uint32_t v;std::memcpy(&v,ram_.data()+offset,4);return v;};
        const bool rev80=DKR_PROBE_REVISION==80;
        const auto header=word(rev80 ? 0x1216E8:0x121168);
        const auto views=word(rev80 ? 0xDFA3C:0xDF4BC);
        OwnedSceneView view{requires_confirmed_tick(),menu_for_test(),std::uint8_t(owner_count_for_test())};
        view.level=std::int32_t(word(rev80 ? 0x1216E4:0x121164));
        if(header>=0x80000000U && header<=0x80FFFFB3U)
            view.race_type=std::int8_t(ram_[((header-0x80000000U)+0x4C)^3]);
        view.viewports=views<=4 ? std::uint8_t(views):0;
        view.two_player_adventure=word(rev80 ? 0xDFA40:0xDF4C0)!=0;
        const auto finished=word(rev80 ? 0x11B340:0x11ADC0);
        view.finished_racers=finished<=8 ? std::uint8_t(finished):0;
        const auto layout=word(rev80 ? 0x11D8FC:0x11D37C);
        const auto hud_players=word(rev80 ? 0x1272CC:0x126D0C);
        if(layout==2 && hud_players<4) {
            const auto hud_toggle=(rev80 ? 0xE2D24U:0xE27A4U)+hud_players;
            view.tt_camera=ram_[hud_toggle^3U]==0 && view.race_type!=0x40 &&
                view.race_type!=0x42 && view.race_type!=0x41;
        }
        return view;
    }
    unsigned menu_for_test() const {
        std::uint32_t id=0;
        std::memcpy(&id,ram_.data()+(DKR_PROBE_REVISION==77 ? 0xDF470:0xDF9F0),4);
        return id;
    }
    void request_scene_for_test(unsigned site) {
        // Explicit unit-test injection of a RETAIL request, not a fabricated
        // loader/scheduler result or a production gameplay input path.
        assert(mode_phases_ && DKR_PROBE_HAS_SCENE_CUTS && site>=1 && site<=2);
        const unsigned base=DKR_PROBE_REVISION==77 ? 0x1234F8:0x123A78;
        if(site==1) {
            // These isolated race fixtures were entered by the diagnostic
            // loader, not the retail lobby. Supply a REAL previous-level
            // descriptor for its return branch, rather than mistaking the
            // fixture's zeroed hub descriptor for a gameplay adapter failure.
            std::uint32_t map=0,entrance=0,cutscene=0;
            std::memcpy(&map,ram_.data()+base-4,4);
            std::memcpy(&entrance,ram_.data()+base+12,4);
            std::memcpy(&cutscene,ram_.data()+base+16,4);
            assert(map<DKR_PROBE_SCENE_LEVEL_COUNT && entrance<256 && cutscene<128);
            const unsigned settings=DKR_PROBE_REVISION==77 ? 0x121250:0x1217D0;
            ram_[(settings+0)^3]=std::uint8_t(map);
            ram_[(settings+1)^3]=0;
            ram_[(settings+8)^3]=std::uint8_t(cutscene);
            ram_[(settings+15)^3]=std::uint8_t(entrance);
        }
        const std::uint32_t request=site==1 ? 2:1;
        std::memcpy(ram_.data()+base+(site==1 ? 4:0),&request,4);
    }
    bool resume_fence_for_test(bool authorize) {
        dkr_probe_native_restore(native_); dkr_probe_canonical_presentation(1); dkr_probe_scene_configure(1);
        if(authorize && (!confirmed_scene_ || !dkr_probe_scene_authorize(confirmed_scene_))) return false;
        const auto old_sp=context_.r29;
        if(authored_cpu_) {
            dkr_probe_input_restore(input_);dkr_probe_input_configure(1);
            dkr_probe_bind_magic(&magic_);
        }
        const auto result=dkr_probe_run(authored_cpu_ ? dkr_probe_authored_component_tick:mode_game,ram_.data(),ram_.size(),&context_,5000000);
        if(authored_cpu_) {dkr_probe_input_configure(0);dkr_probe_bind_magic(nullptr);}
        native_=dkr_probe_native_capture();
        // The real loader's FIRST native ownership boundary must still fence.
        // Its prologue has not executed: resumption must not allocate mode's
        // 0x50-byte guest frame again or re-simulate/render the old scene.
        const char* expected=authorize ? "dkr_netplay_gameplay_level_end":"scene-continuation-not-confirmed";
        if(result.completed || !result.blocked || std::strcmp(result.blocked,expected)!=0 ||
           context_.r29!=old_sp || native_.pending_scene_site!=(authorize ? 0:confirmed_scene_)) {
            std::cerr<<"Unexpected scene-resume fence: "<<(result.blocked ? result.blocked:"none")<<'\n'; return false;
        }
        std::cout<<"  resume="<<(authorize ? "confirmed":"refused")<<" fence="<<expected<<" guest_stack_exact=1\n";
        return true;
    }
    PreparationStep unload_for_test(PresentationMailbox& box,std::vector<std::uint8_t>& proof,std::string& error) {
#if DKR_PROBE_HAS_AUTHORED_CPU
        if(!authored_cpu_ || !confirmed_scene_ || native_.pending_scene_site!=confirmed_scene_ || native_.scene_unload_phase) {
            error="No confirmed running world is available for teardown.";return PreparationStep::Failed;
        }
        if(!box.quiescent())return PreparationStep::Pending; // No mutation or blocking worker wait.
        dkr_probe_native_restore(native_);dkr_probe_canonical_presentation(1);dkr_probe_scene_configure(1);
        if(!dkr_probe_scene_unload_authorize(confirmed_scene_)) {
            error="Confirmed teardown ownership was not admitted.";return PreparationStep::Failed;
        }
        dkr_probe_effects_begin();const auto stack=context_.r29;
        const auto result=dkr_probe_run(dkr_probe_scene_unload_cpu,ram_.data(),ram_.size(),&context_,5000000);
        native_=dkr_probe_native_capture();
        if(!result.completed || native_.scene_unload_phase!=2 || context_.r29!=stack) {
            error=result.blocked ? result.blocked:"Incomplete private teardown";
            std::cerr<<"Actual teardown fenced: "<<error<<'\n';
            for(unsigned s=0;s<result.stack_depth;++s)std::cerr<<"  "<<result.stack[s]<<'\n';
            return PreparationStep::Failed;
        }
        std::array<dkr_probe_effect,DKR_PROBE_MAX_OBJECT_EFFECTS> effects;
        const auto count=dkr_probe_effects_capture(effects.data(),effects.size());
        if(!count || count>effects.size())return PreparationStep::Failed;
        proof.resize(state_.snapshot_size()+sizeof(native_));
        if(!state_.capture(ram_.data(),0,std::span(proof).first(state_.snapshot_size())))return PreparationStep::Failed;
        std::memcpy(proof.data()+state_.snapshot_size(),&native_,sizeof(native_));
        for(unsigned i=0;i<count;++i)for(auto v:{effects[i].kind,effects[i].object})
            for(unsigned b=0;b<4;++b)proof.push_back(std::uint8_t(v>>(b*8)));
        std::cout<<"  actual retail teardown freed-object-events="<<count<<" saved_stack_exact=1\n";
        return PreparationStep::Ready;
#else
        error="Private authored teardown was not generated.";return PreparationStep::Failed;
#endif
    }
    PreparationStep load_for_test(unsigned map,unsigned players_minus_one,std::vector<std::uint8_t>& proof,std::string& error) {
#if DKR_PROBE_HAS_AUTHORED_CPU
        if(!authored_cpu_ || !confirmed_scene_ || native_.scene_unload_phase!=2 || map>=DKR_PROBE_SCENE_LEVEL_COUNT || players_minus_one>3) {
            error="No retired confirmed scene is available for construction.";return PreparationStep::Failed;
        }
        dkr_probe_native_restore(native_);dkr_probe_canonical_presentation(1);dkr_probe_scene_configure(1);
        const auto frame=audio_.next_frame;
        if(!save_.begin_frame(frame) || !paks_.begin_frame(frame) || !magic_.begin_frame(frame))return PreparationStep::Failed;
        dkr_probe_input_restore(input_);dkr_probe_input_configure(1);dkr_probe_bind_eeprom(&save_);
        dkr_probe_bind_paks(&paks_);dkr_probe_bind_magic(&magic_);
        dkr_probe_audio_guards_restore(audio_.guards.data());dkr_probe_effects_begin();
        const auto stack=context_.r29;
        context_.r4=map;context_.r5=players_minus_one;context_.r6=0;context_.r7=0;
        const auto result=dkr_probe_run(dkr_probe_scene_load_cpu,ram_.data(),ram_.size(),&context_,20000000);
        native_=dkr_probe_native_capture();input_=dkr_probe_input_capture();
        dkr_probe_audio_guards_capture(audio_.guards.data());
        dkr_probe_input_configure(0);dkr_probe_bind_eeprom(nullptr);dkr_probe_bind_paks(nullptr);dkr_probe_bind_magic(nullptr);
        if(!result.completed || native_.scene_unload_phase!=4 || context_.r29!=stack || !valid_input(input_)) {
            error=result.blocked ? result.blocked:"Incomplete private constructor";
            std::cerr<<"Actual constructor fenced: "<<error<<" operations="<<result.operations<<'\n';
            for(unsigned s=0;s<result.stack_depth;++s)std::cerr<<"  "<<result.stack[s]<<'\n';
            return PreparationStep::Failed;
        }
        std::vector<std::uint8_t> saves,paks,magic;
        if(!save_.end_frame(saves) || !paks_.end_frame(paks) || !magic_.end_frame(magic))return PreparationStep::Failed;
        std::array<dkr_probe_effect,DKR_PROBE_MAX_OBJECT_EFFECTS> effects;
        const auto count=dkr_probe_effects_capture(effects.data(),effects.size());
        if(!count || count>effects.size())return PreparationStep::Failed;
        proof.resize(state_.snapshot_size());
        if(!state_.capture(ram_.data(),0,proof))return PreparationStep::Failed;
        const auto add=[&](const auto& value) {
            const auto bytes=std::as_bytes(std::span(&value,1));
            for(auto b:bytes)proof.push_back(std::to_integer<std::uint8_t>(b));
        };
        add(native_);add(audio_);add(input_);
        for(unsigned i=0;i<count;++i)for(auto v:{effects[i].kind,effects[i].object})
            for(unsigned b=0;b<4;++b)proof.push_back(std::uint8_t(v>>(b*8)));
        for(const auto* journal:{&saves,&paks,&magic}) {
            for(unsigned b=0;b<4;++b)proof.push_back(std::uint8_t(journal->size()>>(b*8)));
            proof.insert(proof.end(),journal->begin(),journal->end());
        }
        std::cout<<"  actual retail constructor map="<<map<<" players="<<players_minus_one+1
                 <<" object-events="<<count<<" operations="<<result.operations<<" saved_stack_exact=1\n";
        return PreparationStep::Ready;
#else
        error="Private constructor was not generated.";return PreparationStep::Failed;
#endif
    }
    PreparationStep resume_transaction_for_test(PresentationMailbox& box,std::uint64_t next_epoch,
                                               std::vector<std::uint8_t>& proof,std::string& error) {
#if DKR_PROBE_HAS_AUTHORED_CPU
        if(!authored_cpu_ || !confirmed_scene_ || native_.pending_scene_site!=confirmed_scene_ ||
           native_.scene_unload_phase || current_epoch_==UINT64_MAX || next_epoch!=current_epoch_+1 || audio_.next_frame!=confirmed_count_) {
            error="No confirmed suspended frame is available for private resumption.";return PreparationStep::Failed;
        }
        if(!box.quiescent())return PreparationStep::Pending;
        dkr_probe_native_restore(native_);dkr_probe_canonical_presentation(1);dkr_probe_scene_configure(1);
        if(!dkr_probe_scene_unload_authorize(confirmed_scene_))return PreparationStep::Failed;
        const auto frame=audio_.next_frame;
        if(!save_.begin_frame(frame) || !paks_.begin_frame(frame) || !magic_.begin_frame(frame))return PreparationStep::Failed;
        dkr_probe_input_restore(input_);dkr_probe_input_configure(1);dkr_probe_bind_eeprom(&save_);
        dkr_probe_bind_paks(&paks_);dkr_probe_bind_magic(&magic_);
        dkr_probe_audio_guards_restore(audio_.guards.data());dkr_probe_effects_begin();
        const auto stack=context_.r29;
        const bool background=DKR_PROBE_HAS_FULL_SCENES && confirmed_scene_==7;
        const unsigned stack_delta=background ? 0 : confirmed_scene_>=3 ? 0x48 : 0x68;
        // Retail thread30 has its own guest CPU stack. Do not replay main or
        // overwrite the suspended/main registers when servicing its request.
        recomp_context background_context=context_;
        repair_float_register_pointer(background_context);
        background_context.r29=std::int32_t(0x80FE0000U);
        const auto loaded_site=confirmed_scene_;
        const auto loader_started=std::chrono::steady_clock::now();
        const auto result=dkr_probe_run(background ? dkr_probe_menu_background_resume : dkr_probe_authored_scene_resume,
            ram_.data(),ram_.size(),background ? &background_context:&context_,40000000);
        const auto loader_us=std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-loader_started).count();
        native_=dkr_probe_native_capture();input_=dkr_probe_input_capture();
        dkr_probe_audio_guards_capture(audio_.guards.data());
        std::array<dkr_probe_motor_event,128> motors{};const auto motor_count=dkr_probe_motor_events(motors.data());
        dkr_probe_input_configure(0);dkr_probe_bind_eeprom(nullptr);dkr_probe_bind_paks(nullptr);dkr_probe_bind_magic(nullptr);
        if(!result.completed || native_.pending_scene_site || !valid_native(native_) ||
           context_.r29!=stack+stack_delta || !valid_input(input_) || native_.video_frames!=frame) {
            error=result.blocked ? result.blocked:"Incomplete confirmed suspended frame";
            std::cerr<<"Actual scene resumption fenced: "<<error<<" operations="<<result.operations<<'\n';
            std::cerr<<"  completed="<<result.completed<<" pending="<<native_.pending_scene_site
                     <<" native_valid="<<valid_native(native_)<<" input_valid="<<valid_input(input_)
                     <<" phase="<<native_.scene_unload_phase<<" resets="<<native_.scene_load_resets
                     <<" stack="<<context_.r29<<" expected="<<stack+stack_delta
                     <<" video_frames="<<native_.video_frames<<" expected="<<frame<<'\n';
            std::cerr<<"  clock_base="<<((std::uint64_t(native_.clock_base_hi)<<32)|native_.clock_base_lo)
                     <<" clock_count="<<((std::uint64_t(native_.clock_count_hi)<<32)|native_.clock_count_lo)
                     <<" owner_mask="<<native_.owner_mask<<" expected="<<owner_mask_<<'\n';
            std::cerr<<std::hex<<"  bad_address="<<result.bad_address<<" a0="<<context_.r4<<" a1="<<context_.r5
                     <<" s0="<<context_.r16<<" s1="<<context_.r17<<" s2="<<context_.r18<<" v0="<<context_.r2<<std::dec<<'\n';
            if(result.source_file)std::cerr<<"  source="<<result.source_file<<":"<<result.source_line<<'\n';
            for(unsigned s=0;s<result.stack_depth;++s)std::cerr<<"  "<<result.stack[s]<<'\n';
            return PreparationStep::Failed;
        }
        std::vector<std::uint8_t> saves,paks,magic;
        if(!save_.end_frame(saves) || !paks_.end_frame(paks) || !magic_.end_frame(magic))return PreparationStep::Failed;
        // Confirmed construction is a distinct journal in the retired epoch.
        // It cannot be replayed as another unconfirmed old-scene gameplay tick.
        auto staged_save=save_;auto staged_paks=paks_;auto staged_magic=magic_;
        if(!staged_save.commit(current_epoch_,frame,saves) || !staged_paks.commit(current_epoch_,frame,paks) ||
           !staged_magic.commit(current_epoch_,frame,magic) || !staged_save.begin_epoch(next_epoch) ||
           !staged_paks.begin_epoch(next_epoch) || !staged_magic.begin_epoch(next_epoch))return PreparationStep::Failed;
        std::array<dkr_probe_effect,DKR_PROBE_MAX_OBJECT_EFFECTS> effects{};const auto count=dkr_probe_effects_capture(effects.data(),effects.size());
        if(count>effects.size() || motor_count>motors.size())return PreparationStep::Failed;
        proof.clear();
        if(retain_proofs_) {
            const auto word=[&](std::uint32_t value){for(unsigned b=0;b<4;++b)proof.push_back(std::uint8_t(value>>(b*8)));};
            word(count);for(unsigned i=0;i<count;++i){word(effects[i].kind);word(effects[i].object);}
            word(motor_count);for(unsigned i=0;i<motor_count;++i){word(motors[i].channel);word(motors[i].enabled);}
            for(const auto* journal:{&saves,&paks,&magic}){word(journal->size());proof.insert(proof.end(),journal->begin(),journal->end());}
        }
        save_=std::move(staged_save);paks_=std::move(staged_paks);magic_=std::move(staged_magic);
        commits_.clear();confirmed_count_=0;confirmed_scene_=0;audio_.next_frame=0;audio_.epoch_remainder=audio_.remainder;
        current_epoch_=next_epoch;
        native_.video_frames=0;
        // The playable adapter discards private comparison proofs. Keep every
        // journal/capacity check above, but do not allocate/copy another entire
        // checkpoint only to throw it away. prepare_scene captures and hashes
        // the resulting baseline independently before any peer can release it.
        if(retain_proofs_) {
            const auto first=proof.size();proof.resize(first+contract().state_bytes);
            if(!capture(std::span(proof).subspan(first),error))return PreparationStep::Failed;
        }
        std::fprintf(stderr,"[rollback][scene-loader] site=%u next-epoch=%llu loader-us=%lld operations=%llu\n",
            loaded_site,static_cast<unsigned long long>(next_epoch),static_cast<long long>(loader_us),
            static_cast<unsigned long long>(result.operations));
        std::cout<<"  actual retail mode/main continuation operations="<<result.operations
                 <<" stack_unwound_exact=1 journals_confirmed=1 next_epoch="<<next_epoch<<'\n';
        return PreparationStep::Ready;
#else
        error="Private confirmed resumption was not generated.";return PreparationStep::Failed;
#endif
    }
    // Explicit private-only opt-in. The actual retail continuation is owned
    // only in the authored CPU target; no runtime/renderer admission follows.
    void enable_scene_sessions_for_test(PresentationMailbox& box) {
        assert(authored_cpu_ && input_phases_);
        scene_sessions_=true;presentation_=&box;
    }
    const auto& preparations_for_test() const { return preparations_; }
    bool confirmed_boundary(BoundaryIntent& intent, std::string& error) override {
        std::vector<std::uint8_t> state;
        return capture_boundary(intent,state,error);
    }
    PreparationStep prepare_scene(std::uint64_t scene_hash,std::uint64_t& baseline,std::string& error) override {
        if(!presentation_ || (retain_proofs_ && preparations_.size()>=2) || !has_confirmed_scene()) {
            error="Unagreed or excess private scene transaction.";return PreparationStep::Failed;
        }
        // Session is frozen at its confirmed boundary while preparation is
        // pending. Do not allocate/copy/hash the full state on each 1 ms poll
        // of GPU ownership. The exact intent is still revalidated below AFTER
        // the actual drain, and the resumer checks quiescence again before any
        // guest mutation. No gate or scene-identity comparison is bypassed.
        if(!presentation_->quiescent())return PreparationStep::Pending;
        const auto preparation_started=std::chrono::steady_clock::now();
        const auto loaded_site=confirmed_scene_;
        BoundaryIntent intent;
        std::vector<std::uint8_t> state;
        if(!capture_boundary(intent,state,error) || scene_hash!=intent.scene_hash) {
            error="Unagreed or excess private scene transaction.";return PreparationStep::Failed;
        }
        std::vector<std::uint8_t> proof;
        const auto result=resume_transaction_for_test(*presentation_,current_epoch_+1,proof,error);
        if(result!=PreparationStep::Ready)return result;
        if(!presentation_->begin_epoch(current_epoch_)) {
            error="Private presentation epoch could not advance after drain.";return PreparationStep::Failed;
        }
        // Reuse this transaction's validation buffer for the new baseline.
        // This is local scratch, never retained/cached across a tick or rewind.
        if(!capture(state,error))return PreparationStep::Failed;
        baseline=checkpoint_hash(state);
        if(retain_proofs_)preparations_.push_back(std::move(proof));
        std::fprintf(stderr,"[rollback][scene-preparation] site=%u epoch=%llu local-us=%lld state-bytes=%llu\n",
            loaded_site,static_cast<unsigned long long>(current_epoch_),
            static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now()-preparation_started).count()),
            static_cast<unsigned long long>(state.size()));
        return PreparationStep::Ready;
    }
private:
    bool has_confirmed_scene() const {
        return scene_sessions_ && confirmed_scene_ && native_.pending_scene_site==confirmed_scene_ &&
            !native_.scene_unload_phase && confirmed_count_ && audio_.next_frame==confirmed_count_;
    }
    bool capture_boundary(BoundaryIntent& intent,std::vector<std::uint8_t>& state,std::string& error) {
        if(!has_confirmed_scene()) {
            error="No confirmed private authored scene continuation.";return false;
        }
        state.resize(contract().state_bytes);
        if(!capture(state,error))return false;
        // Full owned state includes the exact saved retail registers/request.
        // The host cannot substitute a different loader target. This digest
        // agrees a continuation, not a fabricated public map-selection API.
        intent.boundary_hash=checkpoint_hash(state);
        intent.scene_hash=intent.boundary_hash^0x5343454E45494E54ULL^confirmed_scene_;
        return intent.scene_hash!=0;
    }
    bool valid_input(const dkr_probe_input_state& input) const {
        return input.requested==1 && !input.ready && !input.delivered && input.motors<=15;
    }
    bool valid_audio(const ComponentAudio& audio) const {
        if(audio.rate!=audio_rate_ || audio.remainder>=480 || audio.epoch_remainder>=480 ||
           audio.next_frame<confirmed_count_ || audio.next_frame==UINT32_MAX) return false;
        for(unsigned i=0;i<audio.guards.size();++i) {
            const auto& g=audio.guards[i];
            if(g.immediate_events>256 || g.logged_empty>1 || g.logged_corrupt>1 ||
               (g.queue && ((g.queue&3) || g.queue<0x80000000U || g.queue>0x80FFFFECU)) ||
               (!g.queue && (g.immediate_events || g.logged_empty || g.logged_corrupt))) return false;
            for(unsigned j=0;j<i;++j) if(g.queue && g.queue==audio.guards[j].queue) return false;
        }
        return true;
    }
    bool valid_native(const dkr_probe_native_state& value) const {
        if(value.owner_mask!=owner_mask_ || value.host_control!=host_control_ ||
           value.assigned_ports_released>1 || value.initial_world_vacant>1 ||
           value.postrace_contracted>1 || value.postrace_pending>1 ||
           value.restore_multiplayer_music>1 || (!DKR_PROBE_HAS_FULL_SCENES && value.restore_multiplayer_music) ||
           !dkr_probe_character_animation_valid(value.character_animation_active,value.character_animation_phase) ||
           (!DKR_PROBE_HAS_FULL_SCENES && (value.character_animation_active || value.preview_loads)) ||
           !dkr_probe_title_tail_valid(value.title_tail_phase,value.title_tail_units) ||
           (!DKR_PROBE_HAS_FULL_SCENES && (value.title_tail_phase || value.title_tail_units)))return false;
        if(value.scene_unload_phase)return false; // A retired world cannot tick or rewind as running.
        if(!value.presentation.scene || !value.presentation.next_generation || !value.presentation.next_token || value.presentation.next_token>65536)return false;
        for(const auto& owner:value.presentation.lifetimes) {
            if(owner.object && (owner.object<0x80000000U||owner.object>0x807FFF98U||(owner.object&3)||owner.token>65535||(!owner.generation&&owner.token)))return false;
            if(!owner.object&&(owner.generation||owner.token))return false;
            for(auto token:owner.attachments)if(token>65535||(!owner.generation&&token))return false;
        }
        for(const auto& camera:value.presentation.cameras)if(camera.valid>1||(camera.valid&&!camera.epoch))return false;
        if(value.video_enabled!=unsigned(authored_cpu_) || value.video_black>1)return false;
        if(!value.video_enabled && (value.video_black || value.video_frames ||
           value.video_framebuffer || value.video_depthbuffer))return false;
        if(value.video_enabled && value.video_frames &&
           (value.video_framebuffer<0x80000000U || value.video_framebuffer>0x807DA800U ||
            value.video_depthbuffer<0x80000000U || value.video_depthbuffer>0x807DA800U ||
            (value.video_framebuffer&7) || (value.video_depthbuffer&7)))return false;
        if(value.vehicle_audio_scope || value.nature_audio_scope || value.requested_table!=UINT32_MAX ||
           value.load_section!=UINT32_MAX || value.load_destination || value.load_offset || value.load_size ||
           value.clock_enabled!=unsigned(mode_phases_)) return false;
        const auto base=(std::uint64_t(value.clock_base_hi)<<32)|value.clock_base_lo;
        const auto count=(std::uint64_t(value.clock_count_hi)<<32)|value.clock_count_lo;
        // Ordinary captures follow a completed logical tick. Full-scene menu
        // background construction runs AFTER that tick has unwound, and can
        // make an actual Count read without simulating another gameplay tick.
        // Keep that deterministic subframe cursor in the checkpoint; do not
        // reset it or advance time by a fictitious 30 Hz frame. The next real
        // tick still ends at the same fixed deadline, with exhaustion fenced.
        if(value.pending_scene_site>DKR_PROBE_SCENE_SITE_MAX || (value.pending_scene_site && (!mode_phases_ || !DKR_PROBE_HAS_SCENE_CUTS))) return false;
        return !value.clock_enabled || (base<=UINT64_MAX-3125000 && count>=base &&
            ((value.pending_scene_site || DKR_PROBE_HAS_FULL_SCENES) ? count<=base+1562500 : count==base));
    }
    RuntimeState state_;
    recomp_context context_{};
    recomp_context audio_context_{};
    std::vector<std::uint8_t> ram_;
    dkr_probe_native_state native_{};
    std::uint32_t owner_mask_=0;
    std::uint32_t host_control_=0;
    std::vector<std::vector<std::uint8_t>> commits_;
    std::vector<dkr_owned_draw_event> draw_events_; // Derived anew by each completed tick, never a live registry.
    LocalSceneryCapture scenery_; // Immutable visuals; NEVER checkpoint/network state.
    std::uint32_t reported_scenery_scene_=UINT32_MAX;
    std::uint32_t confirmed_count_=0; // Non-rewinding, independent of test proof retention.
    std::uint32_t published_preview_loads_=0; // Presentation-only cursor; never captured or used for CPU admission.
    bool retain_proofs_=true;
    ConfirmedAudioSink audio_sink_;
    ConfirmedRumbleSink rumble_sink_;
    bool water_phases_ = false;
    bool mode_phases_ = false;
    bool audio_phases_ = false;
    bool input_phases_ = false;
    bool authored_cpu_ = false;
    bool independent_restore_ = false; // Only the copied-image renderer opts in.
    Eeprom save_;
    Paks paks_;
    MagicCodes magic_;
    dkr_probe_input_state input_{};
    ComponentAudio audio_{};
    unsigned audio_rate_=0;
    unsigned confirmed_scene_=0;
    std::uint64_t current_epoch_=0; // Irreversible owner cursor, not speculative RAM.
    bool scene_sessions_=false;
    std::vector<std::vector<std::uint8_t>> preparations_; // At most two PRIVATE proofs.
    PresentationMailbox* presentation_=nullptr; // Borrowed; tests retain it beyond this world.
    std::optional<std::pair<std::uint64_t,std::uint32_t>> retiring_; // Non-rewinding retirement transaction.
    std::optional<std::pair<std::uint64_t,std::uint32_t>> preparing_tick_;
    std::optional<std::uint32_t> prepared_menu_tick_;
};
// This façade is the only reusable entry into the checked CPU payload. Its
// effects and ownership policy are separate from the private proof runner and
// the stable native runtime. All guest service globals are rebound on this one
// owner; a renderer sees only PresentationMailbox immutable copies.
class OwnedAdapter final : public OwnedGame {
public:
    OwnedAdapter(std::span<const std::uint8_t> fixture,std::span<const std::uint8_t> rom,
                 std::uint64_t epoch,PresentationMailbox& box,ConfirmedAudioSink sink,bool start_at_title,std::uint32_t magic_codes,std::span<const std::uint8_t> initial_save,OwnedBootOptions boot)
        : rom_(rom.begin(),rom.end()),owner_(std::this_thread::get_id()),box_(box),
          world_(fixture,false,true,true,true,start_at_title ? epoch-1:epoch,true,magic_codes,initial_save,boot) {
        bind();world_.configure_owned_output(box,{});
        if(start_at_title) {
            // Local preparation before network admission: use the same real
            // retail pause/Quit/Yes path qualified by the menu-flow check.
            // Never transplant a Title context or skip a destructor. No
            // presentation/PCM escapes, no user's persistent save is touched.
            const auto players=std::uint8_t(world_.owner_count_for_test());
            Driver local(world_);std::string error;
            if(!local.start({epoch-1,players,6},error))throw std::runtime_error(error);
            bool ready=false;
            for(unsigned t=0;t<100 && !ready;++t) {
                FrameInputs inputs{};
                if(t==40)inputs[0].buttons=0x1000;
                if(t==50 || t==60)inputs[0].stick_y=80;
                if(t==55 || t==65)inputs[0].buttons=0x8000;
                for(unsigned p=0;p<players;++p)
                    if(local.receive(epoch-1,p,t,inputs[p])!=InputResult::Accepted)
                        throw std::runtime_error("Retail Title preparation input refused.");
                const auto step=local.step();
                if(step==Step::Advanced)continue;
                BoundaryIntent intent;std::uint64_t baseline=0;
                if(step!=Step::ConfirmedBoundary || world_.pending_scene()!=1 ||
                   !world_.confirmed_boundary(intent,error) ||
                   world_.prepare_scene(intent.scene_hash,baseline,error)!=PreparationStep::Ready ||
                   !world_.requires_confirmed_tick() || world_.menu_for_test()!=0)
                    throw std::runtime_error("Retail Title preparation stopped safely: "+error);
                ready=true;
            }
            if(!ready)throw std::runtime_error("Retail Title preparation did not reach its confirmed boundary.");
        }
        world_.configure_owned_output(box,std::move(sink),true);
    }
    ~OwnedAdapter() {dkr_probe_rom(nullptr,0);}
    OwnedSceneView scene_view() const override {
        // This view is copied by the simulation owner before passing UI status
        // through its existing mutex. The UI cannot read live guest RAM.
        if(std::this_thread::get_id()!=owner_)return {};
        return world_.scene_view();
    }
    SimulationContract contract() const override {return world_.contract();}
    bool requires_confirmed_tick() const override {return world_.requires_confirmed_tick();}
    RestoreStep prepare_tick(std::uint64_t epoch,std::uint32_t frame,bool agreed,std::string& error) override {
        return check(error)?world_.prepare_tick(epoch,frame,agreed,error):RestoreStep::Failed;
    }
    bool capture(std::span<std::uint8_t> bytes,std::string& error) override {return check(error) && world_.capture(bytes,error);}
    bool restore(std::span<const std::uint8_t> bytes,std::string& error) override {return check(error) && world_.restore(bytes,error);}
    RestoreStep prepare_restore(std::uint64_t epoch,std::uint32_t first,std::string& error) override {
        return check(error)?world_.prepare_restore(epoch,first,error):RestoreStep::Failed;
    }
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& output,std::string& error) override {
        return check(error) && world_.tick(frame,inputs,output,error);
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> effects,bool boundary,std::string& error) override {
        return check(error) && world_.commit(epoch,frame,effects,boundary,error);
    }
    bool confirmed_boundary(BoundaryIntent& intent,std::string& error) override {return check(error) && world_.confirmed_boundary(intent,error);}
    PreparationStep prepare_scene(std::uint64_t scene,std::uint64_t& baseline,std::string& error) override {
        return check(error)?world_.prepare_scene(scene,baseline,error):PreparationStep::Failed;
    }
    bool publish_current(std::uint64_t epoch,std::uint32_t frame,std::string& error) override {
        if(!check(error))return false;
        if(!world_.publish_cpu_frame_for_test(box_,epoch,frame)){error="Owned CPU frame could not publish an immutable render snapshot.";return false;}return true;
    }
    bool admit_initial(std::span<const std::uint8_t> state,std::string& error) override {return check(error) && world_.admit_initial(state,error);}
    std::uint32_t confirmed_count() const override {return world_.confirmed_count();}
    std::span<const std::uint8_t> confirmed_save() const override {return world_.confirmed_save();}
    std::span<const std::uint8_t> confirmed_paks() const override {return world_.confirmed_paks();}
    bool install_initial_paks(std::uint64_t epoch,std::span<const std::uint8_t> images,std::string& error) override {return check(error)&&world_.install_initial_paks(epoch,images,error);}
    void set_confirmed_rumble_sink(ConfirmedRumbleSink sink) override {if(std::this_thread::get_id()==owner_)world_.set_confirmed_rumble_sink(std::move(sink));}
private:
    void bind() {dkr_probe_rom(rom_.data(),rom_.size());}
    bool check(std::string& error) {
        if(std::this_thread::get_id()!=owner_){error="Owned DKR world was called from a foreign thread.";return false;}bind();return true;
    }
    const std::vector<std::uint8_t> rom_;
    const std::thread::id owner_;
    PresentationMailbox& box_;
    Component world_;
};
class SceneRequest final : public Simulation {
public:
    SceneRequest(std::span<const std::uint8_t> fixture,unsigned site,bool authored_cpu=false) : world(fixture,false,true,authored_cpu,authored_cpu,91,authored_cpu),site_(site) {}
    SimulationContract contract() const override { return world.contract(); }
    bool capture(std::span<std::uint8_t> out,std::string& error) override { return world.capture(out,error); }
    bool restore(std::span<const std::uint8_t> in,std::string& error) override { return world.restore(in,error); }
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        // The immutable test policy injects the request according to LOGICAL
        // frame/input, not physical call count, so replay can remove/move it.
        if(frame>=5 && (inputs[1].buttons&0x2000)) world.request_scene_for_test(site_);
        return world.tick(frame,inputs,out,error);
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> effects,bool boundary,std::string& error) override {
        return world.commit(epoch,frame,effects,boundary,error);
    }
    Component world;
private:
    unsigned site_;
};
FrameInputs scripted(unsigned frame,unsigned players) {
    FrameInputs inputs{};
    for (unsigned p=0;p<players;++p) inputs[p] = {
        std::uint16_t((frame % 4 != 0 ? 0x8000 : 0) | (frame == 6 ? 0x2000 : 0)),
        std::int8_t(((frame+p)%5)*25-50),std::int8_t((frame%3)*30-30)};
    return inputs;
}
// Private impairment wrapper around REAL loopback UDP, never a production
// transport policy. Delay/loss/reordering occur on sealed datagrams, rather
// than bypassing authentication or supplying inputs directly to the driver.
class ImpairedUdp final : public SessionTransport {
    struct Packet {
        Network::Clock::time_point due;
        PeerAddress destination;
        std::vector<std::uint8_t> bytes;
        TransportTrafficClass traffic;
    };
    std::unique_ptr<SessionTransport> udp_=make_udp_session_transport();
    std::deque<Packet> pending_;
    unsigned sends_=0;
public:
    bool open(std::uint16_t port,std::string& error) override { return udp_->open(port,error); }
    void close() override { udp_->close(); pending_.clear(); }
    bool is_open() const override { return udp_->is_open(); }
    std::uint16_t local_port() const override { return udp_->local_port(); }
    DatagramSendStatus send_status(const PeerAddress& destination,std::span<const std::uint8_t> bytes,
                                   TransportTrafficClass traffic,std::string&) override {
        if(pending_.size()+3>128) return DatagramSendStatus::WouldBlock;
        assert(bytes.size()<=118); ++sends_;
        if(sends_%5==0) return DatagramSendStatus::Sent; // 20% simulated network loss
        const auto now=Network::Clock::now();
        const auto due=now+std::chrono::milliseconds(100+(sends_%7)*8);
        pending_.push_back({due,destination,{bytes.begin(),bytes.end()},traffic});
        if(sends_%11==0) pending_.push_front({now+std::chrono::milliseconds(105),destination,{bytes.begin(),bytes.end()},traffic});
        if(sends_%13==0) {
            auto corrupt=std::vector<std::uint8_t>(bytes.begin(),bytes.end()); corrupt.back()^=1;
            pending_.push_front({now+std::chrono::milliseconds(103),destination,std::move(corrupt),traffic});
        }
        return DatagramSendStatus::Sent;
    }
    void service() override {
        udp_->service(); const auto now=Network::Clock::now(); unsigned sent=0;
        for(auto it=pending_.begin();it!=pending_.end() && sent<32;) {
            if(it->due>now) { ++it; continue; }
            std::string error;
            const auto result=udp_->send_status(it->destination,it->bytes,it->traffic,error); ++sent;
            if(result==DatagramSendStatus::WouldBlock) { it->due=now+std::chrono::milliseconds(5); ++it; }
            else if(result!=DatagramSendStatus::Sent) throw std::runtime_error("Private UDP impairment send failed: "+error);
            else it=pending_.erase(it);
        }
    }
    bool receive(PeerAddress& from,std::vector<std::uint8_t>& bytes,std::string& error) override { return udp_->receive(from,bytes,error); }
};
bool private_reference(std::span<const std::uint8_t> state,
                       const std::vector<std::vector<std::uint8_t>>& effects,unsigned players,
                       std::uint64_t schema,
                       std::string_view private_tag={}) {
    const char* prefix=std::getenv("DKR_PROBE_REFERENCE_OUTPUT");
    if(!prefix) return true;
    // Optional private byte-for-byte cross-OS evidence. These files contain
    // guest assets, NOT distributable builds. Never overwrite a previous run.
    const auto path=std::filesystem::u8path(std::string(prefix)+std::string(private_tag)+"-p"+std::to_string(players)+".dkr-component");
    std::FILE* file=nullptr;
#ifdef _WIN32
    if(_wfopen_s(&file,path.c_str(),L"wbx")!=0) return false;
#else
    file=std::fopen(path.c_str(),"wbx");
#endif
    if(!file) return false;
    bool valid=true;
    const auto write=[&](const void* bytes,std::size_t count) { if(std::fwrite(bytes,1,count,file)!=count) valid=false; };
    const auto integer=[&](std::uint64_t value) {
        std::array<std::uint8_t,8> bytes{};
        for(unsigned b=0;b<8;++b) bytes[b]=std::uint8_t(value>>(b*8));
        write(bytes.data(),bytes.size());
    };
    // Do not silently reinterpret a previous native-participant layout. These
    // private files are evidence, not a portable user save or network packet.
    write("DKRCOMP2",8); integer(DKR_PROBE_REVISION); integer(players);
    integer(state.size()); integer(effects.size()); integer(schema);
    integer(sizeof(dkr_probe_native_state)); integer(sizeof(SerializableContext));
    integer(RuntimeState{}.snapshot_size()); write(state.data(),state.size());
    for(const auto& effect:effects) { integer(effect.size()); write(effect.data(),effect.size()); }
    if(std::fclose(file)!=0) valid=false;
    return valid;
}
}
namespace dkr::runtime::netplay::experimental {
std::unique_ptr<OwnedGame> make_owned_game(std::span<const std::uint8_t> bootstrap,
    std::span<const std::uint8_t> rom,std::uint64_t epoch,PresentationMailbox& box,
    ConfirmedAudioSink sink,std::string& error,bool start_at_title,std::uint32_t magic_codes,std::span<const std::uint8_t> initial_save,OwnedBootOptions boot) {
#if DKR_PROBE_HAS_AUTHORED_CPU && DKR_PROBE_HAS_SYMBOL_ISOLATION && DKR_PROBE_HAS_AUDIO_ISOLATION
    if(!epoch || epoch==UINT64_MAX || (boot.normal_boot && (start_at_title || !DKR_PROBE_HAS_FULL_SCENES)) ||
       (start_at_title && (!DKR_PROBE_HAS_FULL_SCENES || epoch<2)) ||
       magic_codes!=dkr::runtime::magic_codes::normalise_magic_code_mask(magic_codes) ||
       (!DKR_PROBE_HAS_FULL_SCENES && magic_codes) || !sink || rom.size()!=dkr::runtime::rom::kRetailRomSize ||
       checkpoint_hash(rom)!=(DKR_PROBE_REVISION==77?dkr::runtime::rom::kUsV77Xxh3:dkr::runtime::rom::kUsV80Xxh3) ||
       bootstrap.size()!=RuntimeState{}.snapshot_size()) {
        error="Owned game requires a matching canonical user ROM, private local bootstrap and output owner.";return {};
    }
    // Check the local bootstrap's structural/revision tag before Component's
    // audited construction. Network checkpoints never enter this constructor.
    RuntimeState state;recomp_context cpu{};state.register_context(&cpu);
    std::vector<std::uint8_t> ram(kRollbackMemoryBytes);std::uint32_t revision=0;
    if(!state.restore(ram.data(),bootstrap,revision) || revision!=DKR_PROBE_REVISION) {
        error="Owned game bootstrap has an incompatible context/ROM revision.";return {};
    }
    dkr_probe_rom(rom.data(),rom.size());
    try {return std::make_unique<OwnedAdapter>(bootstrap,rom,epoch,box,std::move(sink),start_at_title,magic_codes,initial_save,boot);}
    catch(const std::exception& e) {error=std::string("Owned DKR construction failed: ")+e.what();}
    catch(...) {error="Owned DKR construction failed.";}
    dkr_probe_rom(nullptr,0);return {};
#else
    error="Owned DKR link isolation/authored CPU payload is not generated.";return {};
#endif
}
}
bool dkr_probe_owned_adapter_check(std::span<const std::uint8_t> fixture,std::span<const std::uint8_t> rom,unsigned frames) {
    using namespace dkr::runtime::netplay;
    using namespace dkr::runtime::netplay::experimental;
    std::array<std::vector<std::vector<std::uint8_t>>,2> pcm;
    std::array<PresentationMailbox,2> boxes;
    std::array<std::unique_ptr<OwnedGame>,2> worlds;
    std::string error;
    for(unsigned owner=0;owner<2;++owner) {
        if(!boxes[owner].begin_epoch(84))return false;
        worlds[owner]=make_owned_game(fixture,rom,84,boxes[owner],[&,owner](ConfirmedAudio audio,std::string&) {
            if(audio.epoch!=84 || audio.frame!=pcm[owner].size() || audio.rate!=22050 || audio.pcm.empty() || audio.pcm.size()%4)return false;
            pcm[owner].emplace_back(audio.pcm.begin(),audio.pcm.end());return true;
        },error);
        if(!worlds[owner]){std::cerr<<error<<'\n';return false;}
    }
    std::vector<std::uint8_t> baseline(worlds[0]->contract().state_bytes),initial(baseline.size());
    if(!worlds[0]->capture(baseline,error) || !worlds[1]->admit_initial(baseline,error) ||
       !worlds[1]->capture(initial,error) || initial!=baseline)return false;
    if(worlds[1]->admit_initial(std::span(baseline).first(baseline.size()-1),error) ||
       !worlds[1]->capture(initial,error) || initial!=baseline)return false;
    bool foreign_refused=false;
    std::thread foreign([&]{std::string message;foreign_refused=!worlds[0]->capture(initial,message) && !message.empty();});foreign.join();
    if(!foreign_refused)return false;
    Driver corrected(*worlds[1]);if(!corrected.start({84,2,20},error))return false;
    std::vector<FrameInputs> inputs;
    for(unsigned f=0;f<frames;++f)inputs.push_back(scripted(f,2));
    for(unsigned f=0;f<frames;++f) {
        TickOutput output;
        if(!worlds[0]->tick(f,inputs[f],output,error) || output.scene_boundary || pcm[0].size()!=f ||
           !worlds[0]->commit(84,f,output.effects,false,error) || pcm[0].size()!=f+1 ||
           worlds[0]->commit(84,f,output.effects,false,error))return false;
        if(corrected.receive(84,0,f,inputs[f][0])!=InputResult::Accepted)return false;
        if(f>=6 && corrected.receive(84,1,f-6,inputs[f-6][1])!=InputResult::Accepted)return false;
        for(unsigned n=0;n<24;++n) {
            const auto step=corrected.step();if(step==Step::Failed){std::cerr<<corrected.error()<<'\n';return false;}
            if(step!=Step::Replayed)break;
        }
        if(corrected.statistics().next_frame!=f+1)return false;
    }
    for(unsigned f=frames-6;f<frames;++f)if(corrected.receive(84,1,f,inputs[f][1])!=InputResult::Accepted)return false;
    for(unsigned n=0;n<60 && corrected.statistics().confirmed_frames<frames;++n)
        if(corrected.step(false)==Step::Failed)return false;
    if(!worlds[0]->capture(baseline,error) || !worlds[1]->capture(initial,error) || baseline!=initial ||
       pcm[0]!=pcm[1] || pcm[0].size()!=frames || worlds[0]->confirmed_count()!=frames ||
       worlds[1]->confirmed_count()!=frames || !corrected.statistics().rollbacks ||
       worlds[1]->admit_initial(baseline,error))return false;
    if(!worlds[1]->publish_current(84,frames-1,error))return false;
    const auto rendered=boxes[1].take();if(!rendered || rendered->descriptor().frame!=frames-1)return false;
    std::cout<<"Owned DKR adapter: canonical initial admission=1 foreign_thread_refused=1 frame_count="<<frames
             <<" actual_corrections="<<corrected.statistics().rollbacks<<" state_pcm_exact=1 confirmed_sink_once=1 immutable_publication=1\n";
    return true;
}
bool dkr_probe_owned_title_check(std::span<const std::uint8_t> fixture,std::span<const std::uint8_t> rom) {
    if(!DKR_PROBE_HAS_FULL_SCENES)return false;
    std::array<PresentationMailbox,2> boxes;
    std::array<std::unique_ptr<OwnedGame>,2> worlds;
    std::array<std::vector<std::vector<std::uint8_t>>,2> pcm;
    std::string error;
    for(unsigned p=0;p<2;++p) {
        if(!boxes[p].begin_epoch(90))return false;
        // Independent cold boot may have skipped a different number of VI
        // intervals. Rate 6 is legal retail state, not a malformed baseline.
        auto local_fixture=std::vector<std::uint8_t>(fixture.begin(),fixture.end());
        const std::uint32_t cold_rate=p ? 6:2;
        std::memcpy(local_fixture.data()+24+(DKR_PROBE_REVISION==77 ? 0xDD404:0xDD974),&cold_rate,4);
        worlds[p]=make_owned_game(local_fixture,rom,91,boxes[p],[&,p](ConfirmedAudio audio,std::string&) {
            if(audio.epoch!=91 || audio.frame!=pcm[p].size())return false;
            pcm[p].emplace_back(audio.pcm.begin(),audio.pcm.end());return true;
        },error,true);
        if(!worlds[p]){std::cerr<<error<<'\n';return false;}
        if(!worlds[p]->requires_confirmed_tick() || worlds[p]->confirmed_count() || !pcm[p].empty())return false;
    }
    std::vector<std::uint8_t> state(worlds[0]->contract().state_bytes),copy(state.size());
    if(!worlds[0]->capture(state,error) || !worlds[1]->admit_initial(state,error) ||
       !worlds[1]->capture(copy,error) || copy!=state) {std::cerr<<error<<'\n';return false;}
    auto bad=state;
    dkr_probe_native_state bad_native{};
    const auto native_offset=RuntimeState{}.snapshot_size();
    std::memcpy(&bad_native,bad.data()+native_offset,sizeof(bad_native));
    bad_native.video_frames=1;
    std::memcpy(bad.data()+native_offset,&bad_native,sizeof(bad_native));
    if(worlds[1]->admit_initial(bad,error) || !worlds[1]->capture(copy,error) || copy!=state)return false;
    for(unsigned p=0;p<2;++p) {
        Driver driver(*worlds[p]);
        if(!driver.start({91,2,6},error))return false;
        for(unsigned f=0;f<12;++f) {
            if(driver.receive(91,0,f,{})!=InputResult::Accepted ||
               driver.step()!=Step::WaitingForConfirmedInput ||
               driver.receive(91,1,f,{})!=InputResult::Accepted || driver.step()!=Step::Advanced) {
                std::cerr<<driver.error()<<'\n';return false;
            }
        }
        if(driver.statistics().confirmed_frames!=12 || driver.statistics().rollbacks || pcm[p].size()!=12)return false;
    }
    if(!worlds[0]->capture(state,error) || !worlds[1]->capture(copy,error) ||
       state!=copy || pcm[0]!=pcm[1])return false;
    std::cout<<"Owned retail Title launch: real local preparation=1 cold_retail_rates=2,6 canonical_admission=1 confirmed_menu_ticks=12 state_pcm_exact=1\n";
    return true;
}
namespace {
class SceneSessionWorld final : public SceneSimulation {
public:
    struct Effect {
        std::uint64_t epoch;std::uint32_t frame;bool boundary;
        std::vector<std::uint8_t> bytes;
        bool operator==(const Effect&) const=default;
    };
    PresentationMailbox mailbox;
    Component world;
    std::shared_ptr<const RenderSnapshot> delayed_consumer;
    std::vector<Effect> effects;
    std::array<std::array<bool,320>,3> sampled{};
    unsigned scene=0,pending_preparations=0;
    explicit SceneSessionWorld(std::span<const std::uint8_t> fixture,unsigned first_site)
        :world(fixture,false,true,true,true,91,true),first_site_(first_site) {
        assert(mailbox.begin_epoch(91));world.enable_scene_sessions_for_test(mailbox);
    }
    SimulationContract contract() const override {return world.contract();}
    bool capture(std::span<std::uint8_t> out,std::string& error) override {return world.capture(out,error);}
    bool before_restore(std::uint64_t epoch,std::uint32_t frame,std::string& error) override {
        return world.before_restore(epoch,frame,error);
    }
    RestoreStep prepare_restore(std::uint64_t epoch,std::uint32_t frame,std::string& error) override {
        return world.prepare_restore(epoch,frame,error);
    }
    bool restore(std::span<const std::uint8_t> in,std::string& error) override {return world.restore(in,error);}
    bool tick(std::uint32_t frame,const FrameInputs& inputs,TickOutput& out,std::string& error) override {
        // Test stimulus only. Alternates both ACTUAL retail branches across
        // consecutive scenes; no constructor result or barrier is fabricated.
        if(scene<2 && frame>=5 && (inputs[1].buttons&0x2000))
            world.request_scene_for_test(scene ? 3-first_site_:first_site_);
        return world.tick(frame,inputs,out,error);
    }
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes,
                bool boundary,std::string& error) override {
        if(!world.commit(epoch,frame,bytes,boundary,error))return false;
        effects.push_back({epoch,frame,boundary,{bytes.begin(),bytes.end()}});return true;
    }
    bool confirmed_boundary(BoundaryIntent& intent,std::string& error) override {
        return world.confirmed_boundary(intent,error);
    }
    PreparationStep prepare_scene(std::uint64_t hash,std::uint64_t& baseline,std::string& error) override {
        if(delayed_consumer) {
            std::vector<std::uint8_t> before(contract().state_bytes),after(before.size());
            if(!capture(before,error))return PreparationStep::Failed;
            const auto result=world.prepare_scene(hash,baseline,error);
            if(result!=PreparationStep::Pending || !capture(after,error) || before!=after ||
               checkpoint_hash(delayed_consumer->bytes())!=delayed_consumer->hash()) {
                error="Private scene mutated before its immutable consumer drained.";return PreparationStep::Failed;
            }
            if(++pending_preparations%8==0)delayed_consumer.reset();
            return result;
        }
        const auto result=world.prepare_scene(hash,baseline,error);
        if(result==PreparationStep::Ready)++scene;
        return result;
    }
    bool retain_picture(std::uint64_t epoch) {
        if(!world.publish_cpu_frame_for_test(mailbox,epoch,0))return false;
        delayed_consumer=mailbox.take();return bool(delayed_consumer);
    }
private:
    unsigned first_site_;
};
FrameInputs scene_inputs(unsigned scene,unsigned frame,unsigned players) {
    auto result=scripted(frame,players);
    if(scene<2) {
        result[1].buttons&=~0x2000;
        if(frame==5)result[1].buttons|=0x2000;
    }
    return result;
}
}
bool dkr_probe_scene_session_check(std::span<const std::uint8_t> fixture,unsigned players,unsigned final_frames) {
    if(!DKR_PROBE_HAS_AUTHORED_CPU || players<2 || players>4 || final_frames<12 || final_frames>300)return false;
    for(unsigned first_site:{1,2}) {
        std::array<std::unique_ptr<SceneSessionWorld>,4> worlds;
        std::array<std::unique_ptr<Session>,4> sessions;
        std::array<std::unique_ptr<SessionTransport>,4> transports;
        std::array<std::unique_ptr<Network>,4> networks;
        std::array<std::unique_ptr<Pump>,4> pumps;
        std::array<PeerAddress,4> addresses;
        std::array<secure::Key,4> keys{};
        std::array<std::uint64_t,4> previous_epoch{};
        std::array<bool,4> held_in_scene{};
        const auto incarnation=secure::generate_key();std::string error;
        for(unsigned p=0;p<players;++p) {
            if(p)keys[p]=secure::generate_key();
            worlds[p]=std::make_unique<SceneSessionWorld>(fixture,first_site);
            sessions[p]=std::make_unique<Session>(*worlds[p]);
            if(!sessions[p]->start({{91,std::uint8_t(players),6},std::uint8_t(p),0},error))return false;
            previous_epoch[p]=91;
            transports[p]=std::make_unique<ImpairedUdp>();
            if(!transports[p]->open(0,error) || !DatagramSocket::resolve("127.0.0.1",transports[p]->local_port(),addresses[p],error))return false;
        }
        for(unsigned p=0;p<players;++p) {
            NetworkConfiguration config{42,1000+p};config.incarnation=incarnation;
            for(unsigned q=0;q<players;++q)if(q!=p && (!p || !q))config.peers[q]={addresses[q],1000+q,keys[p ? p:q]};
            networks[p]=std::make_unique<Network>(*sessions[p],*transports[p]);
            if(!networks[p]->start(config,error))return false;
            pumps[p]=std::make_unique<Pump>(*sessions[p],*networks[p]);
        }
        const auto deadline=Network::Clock::now()+std::chrono::seconds(90+final_frames/5);
        unsigned waits=0,rollbacks=0;
        for(;;) {
            if(Network::Clock::now()>=deadline){std::cerr<<"Actual chained scene session deadline expired.\n";return false;}
            for(unsigned p=0;p<players;++p) {
                auto& session=*sessions[p];auto& world=*worlds[p];
                const bool final_limit=session.epoch()==93 && session.frontier()>=final_frames;
                if(final_limit) {
                    if(!networks[p]->service() || session.step(false)==SessionStep::Failed)return false;
                } else if(!pumps[p]->pulse(Network::Clock::now(),[&]{
                    const auto scene=unsigned(session.epoch()-91),frame=session.frontier();
                    if(scene>=world.sampled.size() || frame>=world.sampled[scene].size() || world.sampled[scene][frame])
                        throw std::runtime_error("Duplicate/out-of-range physical scene input assignment");
                    world.sampled[scene][frame]=true;
                    return scene_inputs(scene,frame,players)[p];
                })) {
                    std::cerr<<"Actual scene session peer="<<p<<" epoch="<<session.epoch()<<" "<<pumps[p]->error()<<'\n';return false;
                }
                if(session.epoch()!=previous_epoch[p]) {
                    if(session.epoch()!=previous_epoch[p]+1 || world.scene!=session.epoch()-91)return false;
                    previous_epoch[p]=session.epoch();held_in_scene[p]=false;
                }
                if(world.scene<2 && session.statistics().next_frame==1 && !held_in_scene[p]) {
                    if(!world.retain_picture(session.epoch()))return false;
                    held_in_scene[p]=true;
                }
                if(pumps[p]->view().waiting_for_clients())++waits;
                if(session.epoch()==93 && session.statistics().next_frame>final_frames)return false;
            }
            if(std::all_of(sessions.begin(),sessions.begin()+players,[&](const auto& s){
                return s->epoch()==93 && s->statistics().confirmed_frames==final_frames;
            }))break;
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
        SceneSessionWorld reference(fixture,first_site);
        for(unsigned scene=0;scene<3;++scene) {
            const unsigned count=scene<2 ? 6:final_frames;
            for(unsigned f=0;f<count;++f) {
                TickOutput output;
                if(!reference.tick(f,scene_inputs(scene,f,players),output,error) ||
                   output.scene_boundary!=(scene<2 && f==5) ||
                   !reference.commit(91+scene,f,output.effects,output.scene_boundary,error))return false;
            }
            if(scene<2) {
                BoundaryIntent intent;std::uint64_t baseline=0;
                if(!reference.confirmed_boundary(intent,error) || reference.prepare_scene(intent.scene_hash,baseline,error)!=PreparationStep::Ready)return false;
            }
        }
        std::vector<std::uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
        if(!reference.capture(expected,error))return false;
        for(unsigned p=0;p<players;++p) {
            if(!worlds[p]->capture(actual,error) || actual!=expected || worlds[p]->effects!=reference.effects ||
               worlds[p]->world.preparations_for_test()!=reference.world.preparations_for_test() ||
               worlds[p]->pending_preparations!=16 || !worlds[p]->mailbox.quiescent() ||
               worlds[p]->scene!=2 || pumps[p]->view().physical_samples<final_frames+12 ||
               pumps[p]->view().physical_samples>final_frames+24 || !networks[p]->statistics().rejected) {
                std::cerr<<"Actual chained session mismatch peer="<<p<<" preparations="<<worlds[p]->pending_preparations
                         <<" samples="<<pumps[p]->view().physical_samples<<'\n';return false;
            }
            rollbacks+=sessions[p]->statistics().rollbacks;
        }
        if(!rollbacks || !waits)return false;
        if(std::getenv("DKR_PROBE_REFERENCE_OUTPUT")) {
            auto proof=reference.world.preparations_for_test();
            for(const auto& effect:reference.effects) {
                std::vector<std::uint8_t> bytes;
                for(unsigned b=0;b<8;++b)bytes.push_back(std::uint8_t(effect.epoch>>(b*8)));
                for(unsigned b=0;b<4;++b)bytes.push_back(std::uint8_t(effect.frame>>(b*8)));
                bytes.push_back(effect.boundary);
                bytes.insert(bytes.end(),effect.bytes.begin(),effect.bytes.end());proof.push_back(std::move(bytes));
            }
            if(!private_reference(expected,proof,players,reference.contract().schema,"-scene"+std::to_string(first_site)))return false;
        }
        std::cout<<"ACTUAL SCENE SESSION: peers="<<players<<" first_site="<<first_site
                 <<" chained_epochs=91,92,93 final_ticks="<<final_frames<<" drained_consumer_waits=16 physical_assignment_unique=1"
                 <<" corrected_state_construction_effects_byte_exact=1 final_rollbacks="<<rollbacks
                 <<" sealed_loss=20% one_way_delay=100..148ms; GPU/VI/live lobby still unqualified\n";
    }
    return true;
}
bool dkr_probe_scene_cut_check(std::span<const std::uint8_t> fixture,bool authored_cpu) {
    if(!DKR_PROBE_HAS_SCENE_CUTS) return false;
    for(unsigned site:{1,2}) {
        SceneRequest qualification(fixture,site,authored_cpu);
        std::array<FrameInputs,6> inputs{}; inputs[5][1].buttons=0x2000;
        const auto result=qualify_replay(qualification,inputs);
        if(!result.passed() || result.replayed_ticks!=21) {
            std::cerr<<"Real scene-cut qualification failed: "<<result.detail<<'\n'; return false;
        }
        SceneRequest corrected(fixture,site,authored_cpu); Driver driver(corrected); std::string error;
        if(!driver.start({91,2,6},error)) return false;
        for(unsigned frame=0;frame<5;++frame) {
            if(driver.receive(91,0,frame,{})!=InputResult::Accepted ||
               driver.receive(91,1,frame,{std::uint16_t(frame==4 ? 0x2000:0),0,0})!=InputResult::Accepted ||
               driver.step()!=Step::Advanced) return false;
        }
        // A predicted held input requests unload. Neither player is allowed
        // to unload while the remote ACTUAL input remains unconfirmed.
        if(driver.receive(91,0,5,{})!=InputResult::Accepted || driver.step()!=Step::Advanced ||
           corrected.world.pending_scene()!=site || driver.step(false)!=Step::AwaitingBoundaryConfirmation) return false;
        std::vector<std::uint8_t> pending(corrected.contract().state_bytes),after(pending.size());
        if(!corrected.capture(pending,error)) return false;
        // Delayed actual releases the button: correction removes the intent.
        if(driver.receive(91,1,5,{})!=InputResult::Accepted || driver.step(false)!=Step::Replayed ||
           corrected.world.pending_scene() || driver.statistics().confirmed_frames!=6) return false;
        if(driver.receive(91,0,6,{})!=InputResult::Accepted ||
           driver.receive(91,1,6,{0x2000,0,0})!=InputResult::Accepted || driver.step()!=Step::ConfirmedBoundary ||
           corrected.world.pending_scene()!=site || driver.statistics().confirmed_frames!=7) return false;
        if(!corrected.capture(pending,error) || !corrected.world.resume_fence_for_test(false) ||
           !corrected.capture(after,error) || pending!=after || !corrected.world.resume_fence_for_test(true)) return false;
        std::cout<<"REAL CPU scene intent site="<<site<<" suffix_replays=21 predicted_unload_removed=1 confirmed_cursor=7; loader remains unowned\n";
    }
    return true;
}
bool dkr_probe_scene_unload_check(std::span<const std::uint8_t> fixture,bool construct) {
    if(!DKR_PROBE_HAS_AUTHORED_CPU)return false;
    for(unsigned site:{1,2}) {
        SceneRequest reference(fixture,site,true),delayed(fixture,site,true);
        // Confirmation is from the actual Driver, not a hand-edited owner flag.
        for(auto* world:{&reference,&delayed}) {
            Driver driver(*world);std::string error;
            if(!driver.start({91,2,6},error))return false;
            for(unsigned f=0;f<6;++f) {
                if(driver.receive(91,0,f,{})!=InputResult::Accepted ||
                   driver.receive(91,1,f,{std::uint16_t(f==5 ? 0x2000:0),0,0})!=InputResult::Accepted)return false;
                if(driver.step()!=(f==5 ? Step::ConfirmedBoundary:Step::Advanced))return false;
            }
        }
        PresentationMailbox empty,held;
        if(!empty.begin_epoch(91) || !held.begin_epoch(91))return false;
        // An actual authored CPU image is retained by a slow consumer. The
        // teardown owner must park BEFORE changing any allocator/guest state.
        Component picture(fixture,false,true,true,true,91,true);TickOutput out;std::string error;
        if(!picture.tick(0,{},out,error) || !picture.publish_cpu_frame_for_test(held,91,0))return false;
        auto lease=held.take();if(!lease)return false;
        std::vector<std::uint8_t> before(delayed.contract().state_bytes),after(before.size()),actual,expected;
        if(!delayed.capture(before,error))return false;
        for(unsigned attempt=0;attempt<8;++attempt)
            if(delayed.world.unload_for_test(held,actual,error)!=PreparationStep::Pending || !actual.empty())return false;
        if(!delayed.capture(after,error) || before!=after || checkpoint_hash(lease->bytes())!=lease->hash())return false;
        lease.reset();if(!held.quiescent())return false;
        if(reference.world.unload_for_test(empty,expected,error)!=PreparationStep::Ready ||
           delayed.world.unload_for_test(held,actual,error)!=PreparationStep::Ready || actual!=expected)return false;
        const auto retired=actual;
        if(delayed.world.unload_for_test(held,actual,error)!=PreparationStep::Failed || actual!=retired)return false;
        if(construct) {
            const unsigned map=DKR_PROBE_REVISION==77 ? 8:3;
            if(reference.world.load_for_test(map,3,expected,error)!=PreparationStep::Ready ||
               delayed.world.load_for_test(map,3,actual,error)!=PreparationStep::Ready || actual!=expected)return false;
            const auto prepared=actual;
            if(delayed.world.load_for_test(map,3,actual,error)!=PreparationStep::Failed || actual!=prepared)return false;
        }
        std::cout<<"REAL confirmed teardown site="<<site<<" delayed_consumer_checks=8 byte_exact=1 constructor="<<construct
                 <<"; epoch release, next tick and GPU integration remain unowned\n";
    }
    return true;
}
bool dkr_probe_scene_resume_check(std::span<const std::uint8_t> fixture) {
    if(!DKR_PROBE_HAS_AUTHORED_CPU)return false;
    for(unsigned site:{1,2}) {
        SceneRequest reference(fixture,site,true),delayed(fixture,site,true);
        for(auto* world:{&reference,&delayed}) {
            Driver driver(*world);std::string error;
            if(!driver.start({91,2,6},error))return false;
            for(unsigned f=0;f<6;++f) {
                if(driver.receive(91,0,f,{})!=InputResult::Accepted ||
                   driver.receive(91,1,f,{std::uint16_t(f==5 ? 0x2000:0),0,0})!=InputResult::Accepted ||
                   driver.step()!=(f==5 ? Step::ConfirmedBoundary:Step::Advanced))return false;
            }
        }
        PresentationMailbox empty,held;
        if(!empty.begin_epoch(91) || !held.begin_epoch(91))return false;
        Component picture(fixture,false,true,true,true,91,true);TickOutput out;std::string error;
        if(!picture.tick(0,{},out,error) || !picture.publish_cpu_frame_for_test(held,91,0))return false;
        auto lease=held.take();if(!lease)return false;
        std::vector<std::uint8_t> before(delayed.contract().state_bytes),after(before.size()),actual,expected;
        if(!delayed.capture(before,error))return false;
        for(unsigned n=0;n<8;++n)
            if(delayed.world.resume_transaction_for_test(held,92,actual,error)!=PreparationStep::Pending || !actual.empty())return false;
        if(!delayed.capture(after,error) || before!=after || checkpoint_hash(lease->bytes())!=lease->hash())return false;
        lease.reset();
        if(reference.world.resume_transaction_for_test(empty,92,expected,error)!=PreparationStep::Ready ||
           delayed.world.resume_transaction_for_test(held,92,actual,error)!=PreparationStep::Ready || actual!=expected)return false;
        const auto prepared=actual;
        if(delayed.world.resume_transaction_for_test(held,92,actual,error)!=PreparationStep::Failed || actual!=prepared)return false;
        if(!empty.begin_epoch(92) || !held.begin_epoch(92))return false;
        std::array<FrameInputs,12> inputs{};
        for(unsigned f=0;f<inputs.size();++f)inputs[f]=scripted(f,4);
        const auto qualification=qualify_replay(reference.world,inputs);
        if(!qualification.passed()) {
            std::cerr<<"Next real scene replay failed: frame="<<qualification.frame<<" reason="<<qualification.detail
                     <<" differing_byte="<<qualification.first_differing_byte<<'\n';return false;
        }
        for(unsigned f=0;f<inputs.size();++f) {
            TickOutput output;
            if(!reference.world.tick(f,inputs[f],output,error) || output.scene_boundary ||
               !reference.world.commit(92,f,output.effects,false,error))return false;
        }
        delayed.world.attach_presentation_for_test(held);
        Driver driver(delayed.world);if(!driver.start({92,4,6},error))return false;
        for(unsigned f=0;f<inputs.size()+3;++f) {
            if(f<inputs.size() && driver.receive(92,0,f,inputs[f][0])!=InputResult::Accepted)return false;
            // Three delayed remote ticks. Do not accidentally add a thirteenth
            // speculative tick when only correction/confirmation remains.
            if(f>=3)for(unsigned p=1;p<4;++p)
                if(driver.receive(92,p,f-3,inputs[f-3][p])!=InputResult::Accepted)return false;
            unsigned budget=30;
            do {
                if(!budget--)return false;
                const auto step=driver.step(driver.statistics().next_frame<(std::min)(f+1,unsigned(inputs.size())));
                if(step==Step::Failed || step==Step::ConfirmedBoundary){std::cerr<<driver.error()<<'\n';return false;}
                if(step==Step::PredictionLimit)break;
            }while(driver.correcting() || driver.statistics().next_frame<(std::min)(f+1,unsigned(inputs.size())));
        }
        expected.resize(reference.contract().state_bytes);actual.resize(expected.size());
        if(!reference.world.capture(expected,error) || !delayed.world.capture(actual,error) || expected!=actual ||
           reference.world.commits()!=delayed.world.commits() || !driver.statistics().rollbacks ||
           driver.statistics().next_frame!=inputs.size() || driver.statistics().confirmed_frames!=inputs.size())return false;
        std::cout<<"REAL scene resume site="<<site<<" delayed_consumer_checks=8 epoch=92 next_ticks=12 suffix_replays="
                 <<qualification.replayed_ticks<<" corrections="<<driver.statistics().rollbacks
                 <<" state_effects_byte_exact=1; real GPU/lobby integration remains unowned\n";
    }
    return true;
}
bool dkr_probe_menu_flow_check(std::span<const std::uint8_t> fixture,bool adventure) {
    std::cout.setf(std::ios::unitbuf);
    if(!DKR_PROBE_HAS_FULL_SCENES)return false;
    PresentationMailbox box;Component world(fixture,false,true,true,true,91,true,adventure ? (1U<<24):0);
    if(!box.begin_epoch(91))return false;
    world.configure_owned_output(box,{});
    Driver driver(world);std::string error;
    std::uint64_t epoch=91;
    const auto players=world.owner_count_for_test();
    if(players<2 || (adventure && players!=2) || !driver.start({epoch,std::uint8_t(players),6},error))return false;
    unsigned old_menu=UINT32_MAX,boundaries=0,confirmed_waits=0,menu_age=0;
    bool selected_character=false,selected_track=false,selected_save=false,cinematic=false;
    std::vector<int> cinematic_levels;
    for(unsigned time=0;time<(adventure ? 6000U:1200U);++time) {
        const auto frame=driver.statistics().next_frame;
        FrameInputs input{};
        // Actual controller path: pause, wrap up to Quit, select Yes and
        // return to Title. L/R debug shortcuts are ignored by the retail
        // race, so this deliberately exercises its real pause-menu actions.
        // Let the real title reveal finish; a button pressed during its
        // cinematic cannot stand in for the subsequent player-selection edge.
        if(time==40 || (old_menu==0 && time>=180 && time%30==0))input[0].buttons=0x1000;
        if(time==50 || time==60)input[0].stick_y=80;
        if(time==55 || time==65)input[0].buttons=0x8000;
        if(old_menu==3 && menu_age>=20 && menu_age%30==20) {
            for(unsigned p=0;p<players;++p)input[p].buttons=0x8000;
            selected_character=true;
        }
        // This fixture is in retail Tracks mode. Character Select goes
        // directly to MENU_TRACK_SELECT (15), not MENU_GAME_SELECT (19).
        if(old_menu==15 && menu_age>=50 && menu_age%30==20) {
            for(unsigned p=0;p<players;++p)input[p].buttons=0x8000;
            selected_track=true;
        }
        // JOINTVENTURE selects the actual retail Game Select / File Select
        // path. Repeated owner-0 A edges enter initials and confirm a fresh
        // private EEPROM, rather than manufacturing an Adventure RAM state.
        if(adventure && (old_menu==19 || old_menu==6) && menu_age>=30 && menu_age%30==20) {
            input[0].buttons=0x8000;
            if(old_menu==6)selected_save=true;
        }
        if(adventure && old_menu==23) {
            cinematic=true;
            const auto level=world.scene_view().level;
            if(cinematic_levels.empty() || cinematic_levels.back()!=level) {
                cinematic_levels.push_back(level);
                std::cout<<"Real consecutive new-game cinematic level="<<level<<" tick="<<time<<'\n';
            }
        }
        for(unsigned p=0;p<players-1;++p)
            if(driver.receive(epoch,p,frame,input[p])!=InputResult::Accepted)return false;
        if(world.requires_confirmed_tick()) {
            if(driver.step()!=Step::WaitingForConfirmedInput)return false;
            ++confirmed_waits;
        }
        if(driver.receive(epoch,players-1,frame,input[players-1])!=InputResult::Accepted)return false;
        const auto step=driver.step();
        if(step==Step::Failed) {std::cerr<<"Menu flow failed at tick="<<time<<": "<<driver.error()<<'\n';return false;}
        if(step==Step::ConfirmedBoundary) {
            BoundaryIntent intent;std::uint64_t baseline=0;
            std::cout<<"Real menu boundary site="<<world.pending_scene()<<" tick="<<time<<'\n';
            if(!world.confirmed_boundary(intent,error) ||
               world.prepare_scene(intent.scene_hash,baseline,error)!=PreparationStep::Ready) {
                std::cerr<<"Menu preparation failed: "<<error<<'\n';return false;
            }
            ++epoch;++boundaries;
            if(!driver.start({epoch,std::uint8_t(players),6},error))return false;
        } else if(step!=Step::Advanced)return false;
        if(world.requires_confirmed_tick() && old_menu!=world.menu_for_test()) {
            old_menu=world.menu_for_test();menu_age=0;std::cout<<"Real menu id="<<old_menu<<" tick="<<time<<'\n';
        }
        ++menu_age;
        if(world.owner_count_for_test()!=players)return false;
        if(!world.requires_confirmed_tick() &&
           (adventure ? selected_save && cinematic && boundaries>=2 && world.scene_view().race_type==5 : selected_track && boundaries>=2))break;
    }
    std::cout<<"Real controller menu flow boundaries="<<boundaries<<" confirmed_waits="<<confirmed_waits<<'\n';
    if(adventure) {
        const auto scene=world.scene_view();
        std::cout<<"Real Adventure final level="<<scene.level<<" type="<<scene.race_type
                 <<" viewports="<<unsigned(scene.viewports)<<" network_owners="<<unsigned(scene.owners)
                 <<" two_player_adventure="<<scene.two_player_adventure<<" new_game_cinematic_scenes="<<cinematic_levels.size()<<'\n';
        // Retail new-game introduction is one level (36) with multiple
        // animation segments, not two independently loaded cinematics.
        // Key/balloon award chains need their own distinct transition check.
        if(scene.race_type!=5 || scene.owners!=2 || scene.viewports!=1 || !scene.two_player_adventure || cinematic_levels.empty())return false;
    }
    return boundaries>=2 && confirmed_waits && selected_character &&
           (adventure ? selected_save && cinematic : selected_track) &&
           !world.requires_confirmed_tick();
}
bool dkr_probe_driver_check(std::span<const std::uint8_t> fixture,unsigned players,unsigned frames,bool water_phases,bool mode_phases,bool audio_phases,bool input_phases,bool authored_cpu) {
    if (players<2 || players>4 || frames<12 || frames>300 || (audio_phases && (!mode_phases || !DKR_PROBE_HAS_AUDIO))) return false;
    std::vector<FrameInputs> inputs(frames);
    for (unsigned f=0;f<frames;++f) inputs[f]=scripted(f,players);
    if(input_phases && !audio_phases) return false;
    Component qualification(fixture,water_phases,mode_phases,audio_phases,input_phases,42,authored_cpu);
    // Exhaustive suffix qualification remains at 12 frames: holding hundreds
    // of full-RAM images would exceed its explicit 256 MiB memory bound.
    // The longer run below exercises the bounded rolling store, not an
    // unbounded collection of all historical full checkpoints.
    const auto result=qualify_replay(qualification,std::span(inputs).first(12));
    if (!result.passed()) {
        std::cerr<<"Component qualification failed frame="<<result.frame<<" byte="<<result.first_differing_byte
                 <<" reason="<<result.detail<<'\n'; return false;
    }
    Component reference(fixture,water_phases,mode_phases,audio_phases,input_phases,42,authored_cpu);
    PresentationMailbox mailbox;
    std::shared_ptr<const RenderSnapshot> delayed_lease;
    std::unique_ptr<SubmissionPermit> started_submission;
    bool restore_drain_verified=false;
    std::uint64_t delayed_hash=0;
    if(authored_cpu && !mailbox.begin_epoch(42))return false;
    for (unsigned f=0;f<frames;++f) {
        TickOutput output; std::string error;
        if (!reference.tick(f,inputs[f],output,error) || !reference.commit(42,f,output.effects,output.scene_boundary,error)) return false;
        if(authored_cpu && f<12) {
            if(!reference.publish_cpu_frame_for_test(mailbox,42,f)) {
                std::cerr<<"Actual CPU display-list snapshot refused at frame="<<f<<'\n';return false;
            }
            if(!f) {
                delayed_lease=mailbox.take();if(!delayed_lease)return false;delayed_hash=delayed_lease->hash();
                started_submission=SubmissionPermit::acquire(delayed_lease);if(!started_submission)return false;
            }
            // The retail producer recycles both display-list heaps while the
            // delayed consumer retains frame zero. Its bytes must not change.
            if(checkpoint_hash(delayed_lease->bytes())!=delayed_hash || mailbox.take())return false;
        }
    }
    Component corrected(fixture,water_phases,mode_phases,audio_phases,input_phases,42,authored_cpu);
    if(authored_cpu)corrected.attach_presentation_for_test(mailbox);
    Driver driver(corrected);
    std::string error;
    if (!driver.start({42,std::uint8_t(players),6,128U*1024U*1024U},error)) return false;
    // Delay every remote owner by 3 ticks; local owner gets its own new input
    // immediately. A differing late actual must restore/replay real guest RAM.
    for (unsigned f=0;f<frames+3;++f) {
        if (f<frames && driver.receive(42,0,f,inputs[f][0]) != InputResult::Accepted) return false;
        if (f>=3) for (unsigned p=1;p<players;++p)
            if (driver.receive(42,p,f-3,inputs[f-3][p]) != InputResult::Accepted) return false;
        std::vector<std::uint8_t> parked_state,held_state;
        const auto parked_statistics=driver.statistics();
        const auto parked_commits=corrected.commits().size();
        if(started_submission) {
            parked_state.resize(corrected.contract().state_bytes);held_state.resize(parked_state.size());
            if(!corrected.capture(parked_state,error))return false;
        }
        unsigned budget=30;
        do {
            if (!budget--) return false;
            const auto step=driver.step(driver.statistics().next_frame < (std::min)(f+1,frames));
            if (step==Step::Failed) { std::cerr<<driver.error()<<'\n'; return false; }
            if(step==Step::WaitingForPresentation) {
                if(!started_submission||restore_drain_verified||mailbox.is_current(delayed_lease)||mailbox.submissions_drained())return false;
                for(unsigned pulse=0;pulse<8;++pulse) {
                    const auto& stats=driver.statistics();
                    if(!corrected.capture(held_state,error)||held_state!=parked_state||
                       stats.next_frame!=parked_statistics.next_frame||stats.confirmed_frames!=parked_statistics.confirmed_frames||
                       stats.rollbacks!=parked_statistics.rollbacks||corrected.commits().size()!=parked_commits)return false;
                    if(driver.step(false)!=Step::WaitingForPresentation)return false;
                }
                started_submission.reset();if(!mailbox.submissions_drained())return false;
                restore_drain_verified=true;
            }
            if (step==Step::PredictionLimit) break;
        } while (driver.correcting() || driver.statistics().next_frame < (std::min)(f+1,frames));
    }
    // Do not add a speculative thirteenth tick while draining the correction.
    if (driver.statistics().next_frame != frames || driver.statistics().confirmed_frames != frames) {
        std::cerr<<"unexpected component cursors next="<<driver.statistics().next_frame
                 <<" confirmed="<<driver.statistics().confirmed_frames<<'\n'; return false;
    }
    std::vector<std::uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if (!reference.capture(expected,error)||!corrected.capture(actual,error)||expected!=actual||reference.commits()!=corrected.commits()) return false;
    if(authored_cpu) {
        if(!restore_drain_verified||started_submission)return false;
        // The Driver itself retired the lease before its FIRST RAM restore.
        // Do not invalidate manually here: that would conceal a missing hook.
        if(mailbox.is_current(delayed_lease) ||
           checkpoint_hash(delayed_lease->bytes())!=delayed_hash ||
           !corrected.publish_cpu_frame_for_test(mailbox,42,frames-1) || mailbox.take())return false;
        delayed_lease.reset();auto corrected_lease=mailbox.take();
        if(!corrected_lease || !mailbox.is_current(corrected_lease) ||
           corrected_lease->descriptor().frame!=frames-1)return false;
        // DKGV's full-RAM evidence is emitted before publication, independently
        // of the mailbox. Match the corrected actual CPU output, not a model.
        const auto& journal=corrected.commits().back();std::uint64_t ram_hash=0;
        if(journal.size()<56)return false;
        for(unsigned b=0;b<8;++b)ram_hash|=std::uint64_t(journal[40+b])<<(8*b);
        if(corrected_lease->hash()!=ram_hash)return false;
        // Qualify the production copied-image policy separately from the
        // conservative parked-owner proof above. Retain both an admitted GPU
        // lease and its mutable decoder while real guest corrections replay.
        PresentationMailbox independent_box;
        if(!independent_box.begin_epoch(42) ||
           !independent_box.publish(42,corrected_lease->descriptor(),corrected_lease->bytes()))return false;
        auto held_image=independent_box.take();
        auto held_permit=SubmissionPermit::acquire(held_image);
        auto held_decode=DecodeWorkspace::create(held_image);
        if(!held_permit || !held_decode)return false;
        held_decode->bytes()[0]^=0x5a; // Decoder writes cannot alias guest/immutable RAM.
        const auto held_image_hash=held_image->hash(),held_decode_hash=checkpoint_hash(held_decode->bytes());
        Component independent(fixture,water_phases,mode_phases,audio_phases,input_phases,42,true);
        independent.attach_presentation_for_test(independent_box,true);
        Driver independent_driver(independent);
        if(!independent_driver.start({42,std::uint8_t(players),6,128U*1024U*1024U},error))return false;
        for(unsigned f=0;f<frames+3;++f) {
            if(f<frames && independent_driver.receive(42,0,f,inputs[f][0])!=InputResult::Accepted)return false;
            if(f>=3)for(unsigned p=1;p<players;++p)
                if(independent_driver.receive(42,p,f-3,inputs[f-3][p])!=InputResult::Accepted)return false;
            unsigned budget=30;
            do {
                if(!budget--)return false;
                const auto step=independent_driver.step(independent_driver.statistics().next_frame<(std::min)(f+1,frames));
                if(step==Step::Failed || step==Step::WaitingForPresentation)return false;
                if(step==Step::PredictionLimit)break;
            }while(independent_driver.correcting() || independent_driver.statistics().next_frame<(std::min)(f+1,frames));
        }
        const auto& independent_stats=independent_driver.statistics();
        if(!independent_stats.rollbacks || independent_stats.next_frame!=frames || independent_stats.confirmed_frames!=frames ||
           independent_box.is_current(held_image) || independent_box.submissions_drained() ||
           checkpoint_hash(held_image->bytes())!=held_image_hash || checkpoint_hash(held_decode->bytes())!=held_decode_hash ||
           !independent.capture(actual,error) || actual!=expected || independent.commits()!=reference.commits())return false;
        held_decode.reset();held_permit.reset();held_image.reset();
        if(!independent_box.quiescent())return false;
        std::cout<<"ACTUAL CPU independent restore: admitted image and decoder retained through all late-input corrections; complete state/effects exact, both render images unchanged, no presentation wait.\n";
        corrected_lease.reset();if(!mailbox.quiescent())return false;
        std::cout<<"ACTUAL CPU immutable display-list handover: both heaps recycled, started submission parks restore across 8 pulses without state/effect mutation, correction retires old lease, corrected RAM exact; GPU/VI not integrated.\n";
    }
    if(!private_reference(expected,reference.commits(),players,reference.contract().schema)) return false;
    std::cout<<"COMPONENT DRIVER ONLY: mode_phases="<<mode_phases<<" audio_phases="<<audio_phases<<" input_phases="<<input_phases<<" water_phases="<<water_phases<<" players="<<players<<" frames="<<frames<<" rollbacks="<<driver.statistics().rollbacks
             <<" replayed="<<driver.statistics().replayed_frames<<" checkpoint_bytes="<<driver.statistics().checkpoint_bytes
             <<" exact=1; full game adapter remains unqualified\n";
    return driver.statistics().rollbacks>0;
}

bool dkr_probe_timeline_check(std::span<const std::uint8_t> fixture,unsigned players,unsigned frames,bool water_phases) {
    if (players<2 || players>4 || frames<12 || frames>300) return false;
    std::array<std::unique_ptr<Component>,4> worlds;
    std::array<std::unique_ptr<Timeline>,4> timelines;
    std::string error;
    for(unsigned p=0;p<players;++p) {
        worlds[p]=std::make_unique<Component>(fixture,water_phases);
        timelines[p]=std::make_unique<Timeline>(*worlds[p]);
        if(!timelines[p]->start({{84,std::uint8_t(players),6},std::uint8_t(p),0},error)) return false;
    }
    struct Envelope { unsigned source,destination,at; std::vector<std::uint8_t> bytes; };
    std::deque<Envelope> network;
    unsigned sends=0,time=0,rollbacks=0,parked=0;
    for(;time<500;++time) {
        for(auto it=network.begin();it!=network.end();) {
            if(it->at>time) { ++it; continue; }
            const auto result=timelines[it->destination]->receive_authenticated(std::uint8_t(it->source),it->bytes);
            if(result!=OwnerInputResult::Accepted && result!=OwnerInputResult::Duplicate) return false;
            it=network.erase(it);
        }
        for(unsigned p=0;p<players;++p) {
            auto& timeline=*timelines[p];
            if(timeline.needs_local_input() && timeline.frontier()<frames &&
               timeline.sample_local(scripted(timeline.frontier(),players)[p])!=OwnerInputResult::Accepted) return false;
            for(unsigned budget=0;budget<12;++budget) {
                const auto step=timeline.step();
                if(step==TimelineStep::Failed) { std::cerr<<timeline.error()<<'\n'; return false; }
                if(timeline.statistics().next_frame>frames) return false;
                if(step==TimelineStep::PredictionLimit) ++parked;
                if(step!=TimelineStep::Replayed) break;
            }
            for(unsigned destination=0;destination<players;++destination) {
                if(p==destination || (p && destination)) continue;
                for(unsigned owner=0;owner<players;++owner) for(auto lane:{OwnerInputSend::Live,OwnerInputSend::Repair}) {
                    if(lane==OwnerInputSend::Repair && time%4) continue;
                    const auto packet=timeline.packet_for(std::uint8_t(owner),std::uint8_t(destination),lane);
                    if(!packet || ++sends%5==0) continue;
                    auto bytes=encode_owner_inputs(*packet);
                    network.push_back({p,destination,time+3+sends%3,bytes});
                    if(sends%7==0) network.push_front({p,destination,time+4+sends%3,bytes});
                }
            }
        }
        if(std::all_of(timelines.begin(),timelines.begin()+players,[frames](const auto& timeline){
            return timeline->statistics().confirmed_frames==frames && !timeline->correcting();
        })) break;
    }
    if(time==500) return false;
    Component reference(fixture,water_phases);
    for(unsigned f=0;f<frames;++f) {
        TickOutput out;
        if(!reference.tick(f,scripted(f,players),out,error) || !reference.commit(84,f,out.effects,false,error)) return false;
    }
    std::vector<std::uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if(!reference.capture(expected,error)) return false;
    for(unsigned p=0;p<players;++p) {
        if(!worlds[p]->capture(actual,error) || actual!=expected || worlds[p]->commits()!=reference.commits()) {
            std::cerr<<"Real component distributed mismatch peer="<<p<<'\n'; return false;
        }
        rollbacks+=timelines[p]->statistics().rollbacks;
    }
    if(!private_reference(expected,reference.commits(),players,reference.contract().schema)) return false;
    std::cout<<"COMPONENT DISTRIBUTED ONLY: water_phases="<<water_phases<<" peers="<<players<<" frames="<<frames<<" rollbacks="<<rollbacks
             <<" parked="<<parked<<" loss=20% exact=1; full game adapter remains unqualified\n";
    return rollbacks>0;
}

bool dkr_probe_network_check(std::span<const std::uint8_t> fixture,unsigned players,unsigned frames,bool water_phases,bool impaired_paced,bool mode_phases,bool audio_phases,bool input_phases,bool authored_cpu) {
    if(players<2 || players>4 || frames<12 || frames>300 || (audio_phases && (!mode_phases || !DKR_PROBE_HAS_AUDIO))) return false;
    if(input_phases && !audio_phases) return false;
    std::array<std::unique_ptr<Component>,4> worlds;
    std::array<std::unique_ptr<Session>,4> sessions;
    std::array<std::unique_ptr<SessionTransport>,4> transports;
    std::array<std::unique_ptr<Network>,4> networks;
    std::array<std::unique_ptr<Pump>,4> pumps;
    std::array<PeerAddress,4> addresses;
    std::array<secure::Key,4> keys{};
    const auto incarnation=secure::generate_key();
    std::string error;
    for(unsigned p=0;p<players;++p) {
        if(p) keys[p]=secure::generate_key();
        worlds[p]=std::make_unique<Component>(fixture,water_phases,mode_phases,audio_phases,input_phases,84,authored_cpu);
        sessions[p]=std::make_unique<Session>(*worlds[p]);
        if(!sessions[p]->start({{84,std::uint8_t(players),6},std::uint8_t(p),0},error)) return false;
        transports[p]=impaired_paced ? std::unique_ptr<SessionTransport>(std::make_unique<ImpairedUdp>()) : make_udp_session_transport();
        if(!transports[p]->open(0,error) || !DatagramSocket::resolve("127.0.0.1",transports[p]->local_port(),addresses[p],error)) return false;
    }
    for(unsigned p=0;p<players;++p) {
        NetworkConfiguration configuration{42,1000+p}; configuration.incarnation=incarnation;
        for(unsigned q=0;q<players;++q) if(q!=p && (!p || !q)) configuration.peers[q]={addresses[q],1000+q,keys[p ? p:q]};
        networks[p]=std::make_unique<Network>(*sessions[p],*transports[p]);
        if(!networks[p]->start(configuration,error)) return false;
        pumps[p]=std::make_unique<Pump>(*sessions[p],*networks[p]);
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(120);
    unsigned rollbacks=0,waiting_slices=0;
    for(;;) {
        if(std::chrono::steady_clock::now()>=deadline) { std::cerr<<"Real component encrypted UDP deadline expired.\n"; return false; }
        for(unsigned p=0;p<players;++p) {
            auto& session=*sessions[p];
            if(impaired_paced && session.frontier()<frames) {
                if(!pumps[p]->pulse(Network::Clock::now(),[&]{return scripted(session.frontier(),players)[p];})) {
                    std::cerr<<pumps[p]->error()<<'\n'; return false;
                }
                if(pumps[p]->view().ticks_this_pulse>4 || session.frontier()>frames) return false;
                if(pumps[p]->view().waiting_for_clients()) ++waiting_slices;
                continue;
            }
            if(!networks[p]->service()) { std::cerr<<networks[p]->error()<<'\n'; return false; }
            if(session.needs_local_input() && session.frontier()<frames &&
               session.sample_local(scripted(session.frontier(),players)[p])!=OwnerInputResult::Accepted) return false;
            // Bounded replay slice, with network returned to between slices.
            // The eventual live runtime also needs a measured wall-time limit.
            for(unsigned budget=0;budget<12;++budget) {
                const auto result=session.step(!impaired_paced);
                if(result==SessionStep::Failed) { std::cerr<<session.error()<<'\n'; return false; }
                if(session.statistics().next_frame>frames) return false;
                if(result!=SessionStep::Replayed || impaired_paced) break;
            }
        }
        if(std::all_of(sessions.begin(),sessions.begin()+players,[frames](const auto& session){
            return session->statistics().confirmed_frames==frames;
        })) break;
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    Component reference(fixture,water_phases,mode_phases,audio_phases,input_phases,84,authored_cpu);
    for(unsigned frame=0;frame<frames;++frame) {
        TickOutput output;
        if(!reference.tick(frame,scripted(frame,players),output,error) ||
           !reference.commit(84,frame,output.effects,false,error)) return false;
    }
    std::vector<std::uint8_t> expected(reference.contract().state_bytes),actual(expected.size());
    if(!reference.capture(expected,error)) return false;
    for(unsigned p=0;p<players;++p) {
        if(!worlds[p]->capture(actual,error) || actual!=expected || worlds[p]->commits()!=reference.commits()) {
            std::cerr<<"Real component encrypted UDP mismatch peer="<<p<<'\n'; return false;
        }
        rollbacks+=sessions[p]->statistics().rollbacks;
        if(!networks[p]->statistics().sent || !networks[p]->statistics().received ||
           (!impaired_paced && networks[p]->statistics().rejected) ||
           (impaired_paced && (!networks[p]->statistics().rejected || pumps[p]->view().physical_samples!=frames))) return false;
    }
    if(!private_reference(expected,reference.commits(),players,reference.contract().schema)) return false;
    std::cout<<"COMPONENT REAL ENCRYPTED LOOPBACK UDP ONLY: mode_phases="<<mode_phases<<" audio_phases="<<audio_phases<<" input_phases="<<input_phases<<" water_phases="<<water_phases<<" peers="<<players
             <<" frames="<<frames<<" rollbacks="<<rollbacks<<" exact=1; no full game/scene/presentation admission\n";
    if(impaired_paced) std::cout<<"  paced=30Hz, sealed UDP simulated one-way delay=100..148ms, loss=20%, duplication/reorder/tamper; wait_slices="<<waiting_slices<<'\n';
    return rollbacks>0;
}

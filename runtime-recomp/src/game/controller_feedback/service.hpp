#pragma once
#include "policy.hpp"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace dkr::feedback {
inline std::atomic<bool> overlay_requested{false};
using Clock=std::chrono::steady_clock;
inline double seconds(){return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();}
struct Device {
    int instance=-1;bool native=false,allowed=false,pedals=false;
    std::uint8_t pedal_mask=0;std::uint32_t capabilities=0;
    Kind kind=Kind::Generic;Transport transport=Transport::Unknown;
    int battery=-1;float gain=1;std::string name,key,route,detail;
    std::uint16_t vendor=0,product=0,firmware=0;
    std::uint64_t generation=0;
    std::uint64_t acknowledged=0,failures=0;int result=0;
    std::vector<std::pair<std::uint32_t,std::string>> audio;
    bool operator==(const Device&)const=default;
};
struct Status {Settings settings{};Device device{};Output requested{};bool testing=false;};
class Service {
    struct Port {
        Settings settings{};Device device{};Mixer mixer{};Output requested{},sent{};Sample last_sample{};
        double input_until=0,sample_until=0,classic_until=0,test_until=0,last_send=0;
        std::uint16_t test_l=0,test_r=0;bool classic=false,dirty=true;
        std::array<std::optional<Sample>,96> pending{};std::uint32_t epoch=0,scene=0,last_confirmed=0;
        bool confirmed_valid=false,muted=false;
    };
    std::mutex mutex_;std::condition_variable wake_;
    std::array<Port,4> ports_{};
    std::array<std::array<Settings,4>,4> settings_{};
    std::array<std::uint32_t,4> endpoints_{};
    std::uint32_t confirmed_epoch_=0,confirmed_frame_=0;
    double analogue_until_=0;
    std::function<bool(const Device&,Output)> output_;
    bool stopping_=false;std::thread worker_;
    void run() {
        std::unique_lock lock(mutex_);
        while(!stopping_) {
            wake_.wait_for(lock,std::chrono::milliseconds(20));
            const double now=seconds();
            for(unsigned i=0;i<4;++i) {
                auto& p=ports_[i];auto d=p.device;
                const bool live=d.allowed&&!p.muted&&now<p.input_until;
                auto settings=p.settings;settings.strength=int(settings.strength*(std::isfinite(d.gain)?std::clamp(d.gain,0.F,1.F):0.F));
                Output o=p.mixer.mix(settings,now,p.classic&&now<p.classic_until,live);
                if(now<p.test_until) {o.left=p.test_l;o.right=p.test_r;}
                if(live&&p.settings.mode==2) {
                    o.trigger_mask=now<p.sample_until&&p.settings.resistance&&d.pedals&&now<analogue_until_?d.pedal_mask:0;
                    o.resistance=o.trigger_mask?std::uint8_t(p.settings.resistance):0;
                    o.led_mode=std::uint8_t(p.settings.lights);
                    static constexpr std::array<std::array<unsigned,3>,4> colours{{{40,190,255},{255,100,40},{100,230,80},{210,80,255}}};
                    if(o.led_mode){o.brightness=std::uint8_t(p.settings.brightness);o.player=std::uint8_t(i);
                        const auto c=colours[i];o.red=std::uint8_t(c[0]);o.green=std::uint8_t(c[1]);o.blue=std::uint8_t(c[2]);}
                    if(p.settings.lights==2&&now<p.sample_until) {
                        if(p.last_sample.boosting){o.red=255;o.green=185;o.blue=20;}
                        else if(o.left>40000||o.right>40000){o.red=255;o.green=35;o.blue=25;}
                        else if(p.last_sample.finished){o.red=70;o.green=230;o.blue=110;}
                    }
                }
                if((o.left||o.right)&&p.settings.mode==2){o.pattern=std::uint8_t(p.settings.deck_pattern);o.waveform=std::uint8_t(p.settings.waveform);}
                o.audio_endpoint=o.waveform?endpoints_[i]:0;
                p.requested=o;
                const bool active=o!=Output{};
                if(d.instance<0||!output_||(!p.dirty&&o==p.sent&&(!active||now-p.last_send<.12)))continue;
                bool duplicate=false;
                for(unsigned j=0;j<i;++j)duplicate|=ports_[j].device.instance==d.instance&&ports_[j].device.native==d.native;
                if(duplicate)continue;
                p.sent=o;p.dirty=false;p.last_send=now;
                auto sink=output_;
                lock.unlock();bool accepted=false;try{accepted=sink(d,o);}catch(...){accepted=false;}lock.lock();
                if(ports_[i].device.instance==d.instance&&ports_[i].device.native==d.native&&!accepted){++ports_[i].device.failures;ports_[i].device.result=-1;}
                if(!d.native&&ports_[i].device.instance==d.instance) {
                    auto& result=ports_[i].device;++result.acknowledged;result.result=accepted?1:-1;
                    result.detail=accepted?"SDL2 rumble API accepted the command.":"SDL2 rumble rejected the command; check controller connection/backend.";
                }
            }
        }
        auto sink=output_;auto ports=ports_;lock.unlock();
        if(sink)for(const auto& p:ports)if(p.device.instance>=0)sink(p.device,{});
    }
public:
    explicit Service(bool threaded=true):worker_(threaded?std::thread([this]{run();}):std::thread{}){}
    ~Service(){shutdown();}
    void shutdown(){{std::lock_guard l(mutex_);stopping_=true;}wake_.notify_one();if(worker_.joinable())worker_.join();}
    void sink(std::function<bool(const Device&,Output)> callback){std::lock_guard l(mutex_);output_=std::move(callback);}
    void input(unsigned i,Device d) {
        if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];
        if(p.device.instance!=d.instance||p.device.native!=d.native||p.device.key!=d.key||p.device.generation!=d.generation){p.mixer.clear();p.pending={};p.classic=false;p.test_until=0;p.confirmed_valid=false;p.dirty=true;p.muted=false;endpoints_[i]=0;settings_[i][unsigned(d.kind)].waveform=0;}
        // The helper's result is authoritative; a successful IPC enqueue is not.
        if(!d.native&&p.device.instance==d.instance){d.acknowledged=p.device.acknowledged;d.failures=p.device.failures;d.result=p.device.result;d.detail=p.device.detail;}
        p.device=std::move(d);p.settings=settings_[i][unsigned(p.device.kind)];p.input_until=seconds()+.35;
        if(!p.device.allowed){p.mixer.clear();p.pending={};p.classic=false;p.sample_until=0;}
    }
    void sample(unsigned i,Sample s,std::uint32_t confirmed,bool online,double now=seconds()) {
        if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];
        if(p.epoch!=s.epoch||p.scene!=s.scene||(!s.driving)){p.pending={};p.mixer.clear();p.confirmed_valid=false;p.epoch=s.epoch;p.scene=s.scene;}
        if(!online){const bool advanced=p.mixer.observe(s,now);if(advanced&&s.driving&&p.classic)p.classic_until=now+.35;p.last_sample=s;p.sample_until=s.driving?now+.3:0;return;}
        if(p.confirmed_valid&&s.frame<=p.last_confirmed)return;
        p.pending[s.frame%p.pending.size()]=s;
        // Harvest in frame order. Rollback replaces a pending sample, never a
        // confirmed hardware event. Storage remains bounded to 96 tiny records.
        std::vector<Sample> ready;
        for(auto& pending:p.pending)if(pending&&pending->epoch==s.epoch&&pending->frame<=confirmed) {ready.push_back(*pending);pending.reset();}
        std::sort(ready.begin(),ready.end(),[](auto a,auto b){return a.frame<b.frame;});
        for(const auto& v:ready)if(!p.confirmed_valid||v.frame>p.last_confirmed){p.mixer.observe(v,now);if(v.driving&&p.classic)p.classic_until=now+.35;p.last_sample=v;p.last_confirmed=v.frame;p.confirmed_valid=true;p.sample_until=v.driving?now+.3:0;}
    }
    void classic(unsigned i,bool on){if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];if(on&&!p.classic&&p.settings.mode==2)p.mixer.pulse(Cue::Impact,seconds());p.classic=on;p.classic_until=seconds()+.35;}
    void stop(unsigned i){if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];p.mixer.clear();p.classic=false;p.test_until=p.sample_until=0;p.dirty=true;p.muted=true;}
    void test(unsigned i,unsigned side,unsigned level){if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];p.test_until=seconds()+.45;p.test_l=side==2?0:std::uint16_t(std::min(level,100U)*65535U/100);p.test_r=side==1?0:std::uint16_t(std::min(level,100U)*65535U/100);p.dirty=true;}
    void menu(unsigned i){if(i>=4)return;std::lock_guard l(mutex_);auto& p=ports_[i];if(p.settings.mode!=2||p.device.instance<0||p.settings.menus==0||!std::isfinite(p.device.gain))return;p.test_until=seconds()+.035;p.test_l=p.test_r=std::uint16_t(p.settings.menus*p.settings.strength*std::clamp(p.device.gain,0.F,1.F));p.dirty=true;}
    Status status(unsigned i){std::lock_guard l(mutex_);const auto& p=ports_[std::min(i,3U)];return {p.settings,p.device,p.requested,seconds()<p.test_until};}
    Output mixed(unsigned i,double now){std::lock_guard l(mutex_);const auto& p=ports_[std::min(i,3U)];return p.mixer.mix(p.settings,now,false,true);}
    Settings stored(unsigned i,unsigned kind){std::lock_guard l(mutex_);return settings_[std::min(i,3U)][std::min(kind,3U)];}
    void stored(unsigned i,unsigned kind,Settings s){if(i>=4||kind>=4)return;std::lock_guard l(mutex_);settings_[i][kind]=bounded(s);if(unsigned(ports_[i].device.kind)==kind){ports_[i].settings=bounded(s);ports_[i].dirty=true;ports_[i].muted=false;}}
    void settings(unsigned i,Settings s){if(i>=4)return;stored(i,unsigned(status(i).device.kind),s);}
    void endpoint(unsigned i,std::uint32_t id){if(i>=4)return;std::lock_guard l(mutex_);endpoints_[i]=id;ports_[i].dirty=true;}
    void cancel(){for(unsigned i=0;i<4;++i)stop(i);}
    void confirm(std::uint32_t epoch,std::uint32_t frame){std::lock_guard l(mutex_);confirmed_epoch_=epoch;confirmed_frame_=frame;}
    void analogue(bool active){std::lock_guard l(mutex_);analogue_until_=active?seconds()+1.:0;}
    std::uint32_t confirmed(std::uint32_t epoch){std::lock_guard l(mutex_);return epoch==confirmed_epoch_?confirmed_frame_:0;}
};
inline Service& service(){static Service s;return s;}
} // namespace dkr::feedback

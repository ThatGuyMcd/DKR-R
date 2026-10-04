#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

// C++17 seam for Patch Pipeline renderer copies. Opt-in, bounded telemetry;
// never logs per tick, allocates per sample, or changes simulation state.
namespace dkr::runtime::netplay::experimental::performance {
enum class Stage : unsigned { Capture, Hash, Store, Load, Restore, Tick,
    Publish, Decode, Process, Present, Drain, Metadata, UploadWait, Matching,
    RenderSetup, FrameInterval, GuestTick, AudioDsp, EffectJournal, VideoProof, Identity,
    SceneryCapture, SceneryUpload, SceneryDraw, Count };
struct Counter {
    std::atomic<std::uint64_t> count{0}, nanos{0}, maximum{0};
    std::array<std::atomic<std::uint64_t>, 32> histogram{};
};
inline std::array<Counter, unsigned(Stage::Count)> counters{};
inline constexpr std::array<std::uint64_t, 32> limits_us{
    50,100,250,500,1000,1500,2000,3000,4000,4500,5000,5500,6000,6500,
    7000,8000,9000,10000,11000,12000,14000,16000,18000,20000,24000,
    28000,33000,40000,50000,66000,100000,UINT64_MAX};
inline std::atomic<bool>& enabled_flag() {
    static std::atomic<bool> value{[] {const char* flag=std::getenv("DKR_ROLLBACK_PROFILE");
        return flag && flag[0]=='1' && !flag[1];}()};
    return value;
}
inline bool enabled() {return enabled_flag().load(std::memory_order_relaxed);}
inline void set_enabled(bool value) {enabled_flag().store(value,std::memory_order_relaxed);}
inline unsigned histogram_bucket(std::uint64_t ns) noexcept {
    // This is an upper-bound histogram: 50.001 us must not be reported in
    // the <=50 us bucket. Divide before rounding to avoid ns+999 overflow.
    const auto us=ns/1000+(ns%1000!=0);
    unsigned bucket=0;while(us>limits_us[bucket])++bucket;
    return bucket;
}
inline void record(Stage stage,std::uint64_t ns) {
    if(!enabled())return;
    auto& counter=counters[unsigned(stage)];
    counter.count.fetch_add(1,std::memory_order_relaxed);
    counter.nanos.fetch_add(ns,std::memory_order_relaxed);
    auto maximum=counter.maximum.load(std::memory_order_relaxed);
    while(maximum<ns && !counter.maximum.compare_exchange_weak(maximum,ns,std::memory_order_relaxed)){}
    counter.histogram[histogram_bucket(ns)].fetch_add(1,std::memory_order_relaxed);
}
class Scope final {
public:
    explicit Scope(Stage stage,bool admitted=true):stage_(stage),counter_(admitted && enabled()?&counters[unsigned(stage)]:nullptr) {
        if(counter_)start_=std::chrono::steady_clock::now();
    }
    ~Scope() {
        if(!counter_)return;
        const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start_).count());
        record(stage_,ns);
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    Stage stage_;
    Counter* counter_;
    std::chrono::steady_clock::time_point start_;
};
enum class Event : unsigned { HistoryInitial, HistoryEpoch, HistoryRestore,
    HistoryHidden, HistoryRepeat, RequestedFrames, RenderedFrames, SkippedWorkloads,
    LocalScenerySprites, LocalSceneryCandidates, LocalSceneryTextureBytes,
    DecodedWorkloads, DecodedVertices, DecodedTriangles,
    DecodedTransforms, DecodedProjections, DecodedDrawCalls, DecodedTextureLoads, Count };
inline std::array<std::atomic<std::uint64_t>,unsigned(Event::Count)> events{};
inline void event(Event kind,std::uint64_t count=1) {
    if(enabled())events[unsigned(kind)].fetch_add(count,std::memory_order_relaxed);
}
// Present worker-local state only; no mutex, guest pointer or per-frame log.
inline thread_local bool observed_present=false;
inline thread_local std::uint64_t observed_epoch=0;
inline thread_local std::chrono::steady_clock::time_point last_present{};
class PresentScope final {
public:
    PresentScope(bool owned,std::uint64_t epoch):previous_(observed_present) {
        observed_present=owned;
        if(observed_epoch!=epoch){observed_epoch=epoch;last_present={};}
    }
    ~PresentScope(){observed_present=previous_;}
private: bool previous_;
};
inline void presented() {
    if(!observed_present || !enabled()){last_present={};return;}
    const auto now=std::chrono::steady_clock::now();
    if(last_present.time_since_epoch().count())record(Stage::FrameInterval,
        std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(now-last_present).count()));
    last_present=now;
}
struct Sample {
    std::uint64_t count=0,nanos=0,maximum=0;
    std::array<std::uint64_t,limits_us.size()> histogram{};
};
inline Sample sample(const Counter& counter) noexcept {
    Sample result;
    result.count=counter.count.load(std::memory_order_relaxed);
    result.nanos=counter.nanos.load(std::memory_order_relaxed);
    result.maximum=counter.maximum.load(std::memory_order_relaxed);
    for(unsigned i=0;i<result.histogram.size();++i)
        result.histogram[i]=counter.histogram[i].load(std::memory_order_relaxed);
    return result;
}
inline Sample difference(const Sample& current,const Sample& previous) noexcept {
    Sample result;
    // Counters are never reset by reporting. Guard subtraction nonetheless:
    // wrap/replacement must not manufacture an enormous timing interval.
    result.count=current.count>=previous.count?current.count-previous.count:0;
    result.nanos=current.nanos>=previous.nanos?current.nanos-previous.nanos:0;
    for(unsigned i=0;i<result.histogram.size();++i)
        result.histogram[i]=current.histogram[i]>=previous.histogram[i]?
            current.histogram[i]-previous.histogram[i]:0;
    return result; // A lifetime maximum cannot be subtracted into an interval maximum.
}
inline std::uint64_t percentile_upper(const Sample& value,unsigned percent) noexcept {
    if(!percent||percent>100)return 0;
    std::uint64_t count=0;
    for(const auto bucket:value.histogram)count+=bucket;
    if(!count)return 0;
    // Nearest rank without count*percent overflow. Histogram and call count
    // are sampled independently: use the histogram's own total for its ranks.
    const auto rank=(count/100)*percent+((count%100)*percent+99)/100;
    std::uint64_t prefix=0;
    for(unsigned i=0;i<value.histogram.size();++i) {
        prefix+=value.histogram[i];
        if(prefix>=rank)return limits_us[i];
    }
    return 0;
}
struct ReportWindow {
    std::array<Sample,unsigned(Stage::Count)> stages{};
    std::array<std::uint64_t,unsigned(Event::Count)> events{};
    std::chrono::steady_clock::time_point started{};
};
inline thread_local ReportWindow report_window{};
inline void begin_report_window() noexcept {
    for(unsigned i=0;i<unsigned(Stage::Count);++i)report_window.stages[i]=sample(counters[i]);
    for(unsigned i=0;i<unsigned(Event::Count);++i)
        report_window.events[i]=events[i].load(std::memory_order_relaxed);
    report_window.started=std::chrono::steady_clock::now();
}
inline void report() {
    if(!enabled())return;
    constexpr const char* names[]{"capture","hash","store","load","restore","tick",
        "publish","decode","process","present","drain","metadata","upload-wait",
        "matching","render-setup","frame-interval","guest-tick","audio-dsp",
        "effect-journal","video-proof","identity","scenery-capture","scenery-upload","scenery-draw"};
    static_assert(sizeof(names)/sizeof(names[0])==unsigned(Stage::Count));
    const auto now=std::chrono::steady_clock::now();
    const double interval_ms=report_window.started.time_since_epoch().count()?
        std::chrono::duration<double,std::milli>(now-report_window.started).count():0.0;
    // Retain cumulative counters and subtract reader-owned snapshots. Never
    // reset counters while another thread records. These relaxed snapshots
    // are approximate CPU telemetry, not GPU/scanout timing or a safety gate.
    for(unsigned i=0;i<unsigned(Stage::Count);++i) {
        const auto current=sample(counters[i]);
        const auto interval=difference(current,report_window.stages[i]);
        report_window.stages[i]=current;
        if(!current.count)continue;
        std::fprintf(stderr,"[rollback][timing] stage=%s count=%llu mean-us=%.1f p95-upper-us=%llu p99-upper-us=%llu max-us=%.1f interval-ms=%.1f interval-count=%llu interval-mean-us=%.1f interval-p95-upper-us=%llu interval-p99-upper-us=%llu\n",
            names[i],static_cast<unsigned long long>(current.count),
            double(current.nanos)/double(current.count)/1000.0,
            static_cast<unsigned long long>(percentile_upper(current,95)),
            static_cast<unsigned long long>(percentile_upper(current,99)),double(current.maximum)/1000.0,
            interval_ms,static_cast<unsigned long long>(interval.count),
            interval.count?double(interval.nanos)/double(interval.count)/1000.0:0.0,
            static_cast<unsigned long long>(percentile_upper(interval,95)),
            static_cast<unsigned long long>(percentile_upper(interval,99)));
    }
    constexpr const char* event_names[]{"history-first","history-scene","history-correction",
        "history-hidden","history-repeat","requested-frames","rendered-frames","skipped-workloads",
        "local-scenery-sprites","local-scenery-candidates","local-scenery-texture-bytes",
        "decoded-workloads","decoded-vertices","decoded-triangles",
        "decoded-transforms","decoded-projections","decoded-draw-calls","decoded-texture-loads"};
    static_assert(sizeof(event_names)/sizeof(event_names[0])==unsigned(Event::Count));
    for(unsigned i=0;i<unsigned(Event::Count);++i) {
        const auto current=events[i].load(std::memory_order_relaxed);
        const auto previous=report_window.events[i];
        std::fprintf(stderr,"[rollback][counter] %s=%llu interval=%llu\n",event_names[i],
            static_cast<unsigned long long>(current),
            static_cast<unsigned long long>(current>=previous?current-previous:0));
        report_window.events[i]=current;
    }
    report_window.started=now;
}
}

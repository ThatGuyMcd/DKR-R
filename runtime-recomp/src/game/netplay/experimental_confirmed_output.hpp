#pragma once
#include "experimental_pak.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dkr::runtime::netplay::experimental {

// Output only: neither queue may retain borrowed guest memory or block on an
// OS device/file while holding its mutex. Gameplay input/checkpoints never use
// these queues. The producer is the one confirmation owner, not a replay tick.
class ConfirmedAudioMailbox final {
public:
    static constexpr std::size_t kCapacity=8, kMaximumBytes=8192;
    struct Block {
        std::uint32_t rate=0;
        std::size_t bytes=0;
        std::array<std::uint8_t,kMaximumBytes> pcm{};
    };
    bool push(std::uint32_t rate,std::span<const std::uint8_t> pcm,std::string& error) {
        if(rate<8000||rate>48000||pcm.size()%4||pcm.size()>kMaximumBytes) {
            error="Invalid confirmed owned audio block.";return false;
        }
        if(pcm.empty())return true;
        std::scoped_lock lock(mutex_);
        if(closed_)return true; // Retirement must not play a stale motor/audio edge.
        if(count_==kCapacity) {
            // A stalled output device must not stall input delivery or accumulate
            // seconds of old sound. Drop only the oldest ALREADY CONFIRMED PCM.
            head_=(head_+1)%kCapacity;--count_;++dropped_;
        }
        auto& block=blocks_[(head_+count_)%kCapacity];
        block.rate=rate;block.bytes=pcm.size();
        std::copy(pcm.begin(),pcm.end(),block.pcm.begin());++count_;return true;
    }
    bool take(Block& block) {
        std::scoped_lock lock(mutex_);
        if(!count_)return false;
        block=blocks_[head_];head_=(head_+1)%kCapacity;--count_;return true;
    }
    std::uint64_t dropped() const {std::scoped_lock lock(mutex_);return dropped_;}
    void close() {std::scoped_lock lock(mutex_);closed_=true;count_=0;}
private:
    mutable std::mutex mutex_;
    std::array<Block,kCapacity> blocks_{};
    std::size_t head_=0,count_=0;
    std::uint64_t dropped_=0;
    bool closed_=false;
};

struct ConfirmedSaveSnapshot {
    std::vector<std::uint8_t> eeprom,paks;
};

// One write in flight plus one latest pending snapshot. Intermediate confirmed
// snapshots may be superseded; speculative saves never enter this worker.
// The caller must finish/join BEFORE releasing the immutable match/save route.
class ConfirmedSaveWriter final {
public:
    using Sink=std::function<bool(const ConfirmedSaveSnapshot&,std::string&)>;
    explicit ConfirmedSaveWriter(Sink sink):sink_(std::move(sink)),worker_([this]{run();}) {}
    ConfirmedSaveWriter(const ConfirmedSaveWriter&)=delete;
    ConfirmedSaveWriter& operator=(const ConfirmedSaveWriter&)=delete;
    ~ConfirmedSaveWriter() {finish();join();}
    bool submit(std::span<const std::uint8_t> eeprom,std::span<const std::uint8_t> paks,std::string& error) {
        if(eeprom.size()!=512||paks.size()!=Paks::kImagesBytes) {
            error="Invalid confirmed online save snapshot size.";return false;
        }
        ConfirmedSaveSnapshot copy{{eeprom.begin(),eeprom.end()},{paks.begin(),paks.end()}};
        {
            std::scoped_lock lock(mutex_);
            if(!error_.empty()) {error=error_;return false;}
            if(finishing_) {error="The confirmed save writer is already retiring.";return false;}
            pending_=std::move(copy);
        }
        wake_.notify_one();return true;
    }
    void finish() {
        {std::scoped_lock lock(mutex_);finishing_=true;}
        wake_.notify_one();
    }
    bool done() const {return done_.load(std::memory_order_acquire);}
    std::string error() const {std::scoped_lock lock(mutex_);return error_;}
    void join() {if(worker_.joinable())worker_.join();}
private:
    void run() noexcept {
        try {
            for(;;) {
                ConfirmedSaveSnapshot next;
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait(lock,[this]{return finishing_||pending_.has_value();});
                    if(!pending_)break;
                    next=std::move(*pending_);pending_.reset();
                }
                // Never hold the mailbox lock over validation, durable writes,
                // backup rotation or a consumer callback.
                std::string error;
                if(!sink_(next,error)) {
                    if(error.empty())error="The confirmed online save writer failed.";
                    std::scoped_lock lock(mutex_);error_=std::move(error);pending_.reset();break;
                }
            }
        } catch(const std::exception& e) {
            std::scoped_lock lock(mutex_);error_=e.what();pending_.reset();
        } catch(...) {
            std::scoped_lock lock(mutex_);error_="The confirmed online save worker failed.";pending_.reset();
        }
        done_.store(true,std::memory_order_release);
    }
    Sink sink_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<ConfirmedSaveSnapshot> pending_;
    bool finishing_=false;
    std::string error_;
    std::atomic<bool> done_{false};
    // Last member: all state must exist before the worker can observe it.
    std::thread worker_;
};
}

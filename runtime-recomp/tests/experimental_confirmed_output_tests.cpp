#include "netplay/experimental_confirmed_output.hpp"
#include <cassert>
#include <chrono>
#include <future>
#include <stdexcept>

using namespace dkr::runtime::netplay::experimental;
using namespace std::chrono_literals;

int main() {
    std::string error;
    {
        ConfirmedAudioMailbox audio;ConfirmedAudioMailbox::Block block;
        std::array<std::uint8_t,4> source{1,2,3,4};
        assert(audio.push(22050,source,error));source.fill(9);
        assert(audio.take(block)&&block.rate==22050&&block.bytes==4);
        assert(block.pcm[0]==1&&block.pcm[3]==4); // no borrowed frame storage
        assert(!audio.take(block));
        assert(!audio.push(7999,source,error));
        assert(!audio.push(48001,source,error));
        assert(!audio.push(22050,std::span(source).first(3),error));
        std::vector<std::uint8_t> too_large(ConfirmedAudioMailbox::kMaximumBytes+4);
        assert(!audio.push(22050,too_large,error));
        assert(!audio.take(block));
        assert(audio.push(22050,{},error)&&!audio.take(block));
        for(unsigned i=0;i<12;++i) {source[0]=std::uint8_t(i);assert(audio.push(22050,source,error));}
        assert(audio.dropped()==4);
        for(unsigned i=4;i<12;++i) {assert(audio.take(block));assert(block.pcm[0]==i);}
        assert(!audio.take(block));
        assert(audio.push(48000,source,error));audio.close();
        assert(!audio.take(block));assert(audio.push(22050,source,error));assert(!audio.take(block));
    }
    {
        // The device owner and producer must not share a device/file lock.
        // Backpressure discards only old confirmed OUTPUT, never game inputs.
        ConfirmedAudioMailbox audio;
        std::atomic<bool> producer_done=false;
        std::thread producer([&] {
            std::array<std::uint8_t,4> source{};std::string message;
            for(unsigned i=1;i<=5000;++i) {
                source[0]=std::uint8_t(i);source[1]=std::uint8_t(i>>8);
                assert(audio.push(22050,source,message));
            }
            producer_done=true;
        });
        ConfirmedAudioMailbox::Block block;unsigned count=0,last=0;
        for(;;) {
            if(audio.take(block)) {
                const unsigned current=block.pcm[0]|unsigned(block.pcm[1])<<8;
                assert(current>last);last=current;++count;
            } else if(producer_done)break;
            else std::this_thread::yield();
        }
        producer.join();
        // Drain a producer completion that raced the preceding empty sample.
        while(audio.take(block)) {
            const unsigned current=block.pcm[0]|unsigned(block.pcm[1])<<8;
            assert(current>last);last=current;++count;
        }
        assert(last==5000&&count+audio.dropped()==5000);
    }
    {
        std::mutex mutex;std::condition_variable wake;
        bool writing=false,release=false;
        std::vector<unsigned> written;
        ConfirmedSaveWriter writer([&](const ConfirmedSaveSnapshot& snapshot,std::string&) {
            std::unique_lock lock(mutex);
            assert(snapshot.eeprom.size()==512&&snapshot.paks.size()==Paks::kImagesBytes);
            if(written.empty()) {
                writing=true;wake.notify_all();
                assert(wake.wait_for(lock,5s,[&]{return release;}));
            }
            assert(snapshot.paks[0]==snapshot.eeprom[0]);
            written.push_back(snapshot.eeprom[0]);return true;
        });
        std::vector<std::uint8_t> eeprom(512,1),paks(Paks::kImagesBytes,1);
        assert(writer.submit(eeprom,paks,error));
        {std::unique_lock lock(mutex);assert(wake.wait_for(lock,2s,[&]{return writing;}));}
        auto producer=std::async(std::launch::async,[&] {
            for(unsigned i=2;i<=4;++i) {
                eeprom.assign(512,std::uint8_t(i));paks.assign(Paks::kImagesBytes,std::uint8_t(i));
                assert(writer.submit(eeprom,paks,error));
            }
            std::fill(eeprom.begin(),eeprom.end(),255);std::fill(paks.begin(),paks.end(),255);
        });
        assert(producer.wait_for(2s)==std::future_status::ready);producer.get();
        writer.finish();assert(!writer.done()); // submission is not durability
        assert(!writer.submit(eeprom,paks,error));
        {std::scoped_lock lock(mutex);release=true;}wake.notify_all();
        writer.join();assert(writer.done()&&writer.error().empty());
        assert((written==std::vector<unsigned>{1,4})); // bounded latest snapshot
    }
    {
        ConfirmedSaveWriter writer([](const ConfirmedSaveSnapshot&,std::string& message) {
            message="isolated durable write failed";return false;
        });
        std::vector<std::uint8_t> eeprom(512),paks(Paks::kImagesBytes);
        assert(!writer.submit(std::span(eeprom).first(511),paks,error));
        assert(!writer.submit(eeprom,std::span(paks).first(Paks::kImagesBytes-1),error));
        assert(writer.submit(eeprom,paks,error));writer.finish();writer.join();
        assert(writer.done()&&writer.error()=="isolated durable write failed");
        assert(!writer.submit(eeprom,paks,error)&&error==writer.error());
    }
    {
        ConfirmedSaveWriter writer([](const ConfirmedSaveSnapshot&,std::string&)->bool {
            throw std::runtime_error("injected save exception");
        });
        std::vector<std::uint8_t> eeprom(512),paks(Paks::kImagesBytes);
        assert(writer.submit(eeprom,paks,error));writer.finish();writer.join();
        assert(writer.error()=="injected save exception");
    }
    {
        ConfirmedSaveWriter writer([](const ConfirmedSaveSnapshot&,std::string&)->bool {assert(false);return false;});
        writer.finish();writer.join();assert(writer.done()&&writer.error().empty());
    }
    return 0;
}

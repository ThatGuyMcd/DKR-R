#include "online_stability/qualification_delivery.hpp"
#include <cassert>
#include <iostream>

int main() {
    namespace q=dkr::online_stability::qualification_delivery;
    using namespace std::chrono_literals;
    q::Queue<int> queue;int source=0;std::vector<std::uint8_t> bytes;std::string error;bool available=true;
    auto fetch=[&](int& s,auto& b,auto& e){e.clear();if(!available)return false;available=false;s=17;b={1,2,3};return true;};
    const auto at=q::Clock::time_point{}+1s;
    assert(queue.receive(source,bytes,error,fetch,at)&&source==17&&bytes.size()==3);
    assert(!q::arm(2001,0,0,0)&&!q::enabled);
    assert(q::arm(375,25,0,97));available=true;
    assert(!queue.receive(source,bytes,error,fetch,at));
    assert(!queue.receive(source,bytes,error,fetch,at+349ms));
    assert(queue.receive(source,bytes,error,fetch,at+400ms)&&source==17&&bytes==std::vector<std::uint8_t>({1,2,3}));
    for(unsigned i=0;i<194;++i)q::drop(i%2?2:3);
    assert(q::dropped==2);
    for(unsigned traffic:{0,1,4})for(unsigned i=0;i<194;++i)assert(!q::drop(traffic));
    queue.clear();assert(q::arm(0,0,200,0));available=true;
    const auto spike=q::Clock::time_point{}+3100ms;
    assert(!queue.receive(source,bytes,error,fetch,spike));
    assert(!queue.receive(source,bytes,error,fetch,spike+199ms));
    assert(queue.receive(source,bytes,error,fetch,spike+200ms));
    available=true;
    auto oversized=[&](int& s,auto& b,auto& e){if(!available){e.clear();return false;}available=false;s=9;b.resize(8*1024*1024+1);return true;};
    assert(!queue.receive(source,bytes,error,oversized,at)&&!error.empty());
    q::enabled=false;queue.clear();available=true;
    assert(queue.receive(source,bytes,error,fetch,at)&&error.empty());
    std::cout<<"Qualification delivery bounds, delay, jitter, spikes, disposable loss and disabled path passed\n";
}

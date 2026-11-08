#include "policy.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace dkr::lobby_stability;
namespace {
unsigned checks=0;
void check(bool ok){++checks;if(!ok)throw std::runtime_error("Policy assertion "+std::to_string(checks));}
}
int main() {
    try {
        const auto t=Clock::time_point(std::chrono::seconds(100));
        const std::vector<Candidate> pending{{8,8},{4,4},{2,2},{1,1},{7,7},{3,3},{6,6},{5,5}};
        check(download_slots(pending)==std::vector<std::uint64_t>({1,2,3}));
        auto remaining=pending;
        for(unsigned round=0;round<3;++round) {
            const auto slots=download_slots(remaining);check(slots.size()<=MaxDownloads);
            for(auto id:slots)std::erase_if(remaining,[&](auto c){return c.id==id;});
        }
        check(remaining.empty());check(download_slots({}).empty());
        check(download_slots({{1,3},{2,1}})==std::vector<std::uint64_t>({2,1}));
        GapBudget gaps;gaps.progress(t);check(!gaps.due(t+std::chrono::seconds(2)));
        check(gaps.due(t+std::chrono::seconds(3)));
        gaps.queue(true,t+std::chrono::seconds(3));check(!gaps.due(t+std::chrono::seconds(4)));
        check(gaps.due(t+std::chrono::seconds(5)));
        gaps.queue(false,t+std::chrono::seconds(4));check(gaps.due(t+std::chrono::seconds(4)));
        auto now=t+std::chrono::seconds(10);
        for(unsigned i=0;i<MaxRepairs;++i) {
            check(!gaps.exhausted());gaps.repaired(now);check(!gaps.due(now+std::chrono::seconds(1)));now+=std::chrono::seconds(31);
        }
        check(gaps.exhausted());check(gaps.count()==5);gaps.queue(true,now);check(gaps.exhausted());
        gaps.progress(now);check(!gaps.exhausted());check(gaps.count()==0);
        PhaseBudget deadline;
        check(!deadline.expired(1,std::chrono::seconds(10),t));
        // Repeated heartbeats in the same phase cannot keep it alive forever.
        for(int i=1;i<10;++i)check(!deadline.expired(1,std::chrono::seconds(10),t+std::chrono::seconds(i)));
        check(deadline.expired(1,std::chrono::seconds(10),t+std::chrono::seconds(11)));
        check(!deadline.expired(2,std::chrono::seconds(20),t+std::chrono::seconds(11)));
        check(deadline.expired(2,std::chrono::seconds(20),t+std::chrono::seconds(32)));
        check(rejection(std::string(500,'x')).size()<=160);
        check(rejection("D:\\private\\file").find("private")==std::string::npos);
        check(rejection("/home/player/private").find("private")==std::string::npos);
        check(rejection("bad\npacket").find('\n')==std::string::npos);
        int first=0,second=0;
        const auto normal=decline_reason(&first,1);
        {RejectionScope outer(&first,1,"Wrong mod hash");
            check(decline_reason(&first,1).find("Wrong mod hash")!=std::string::npos);
            check(decline_reason(&first,2)==normal);check(decline_reason(&second,1)==normal);
            {RejectionScope inner(&second,2,"Invalid resource proof");check(decline_reason(&second,2).find("Invalid resource proof")!=std::string::npos);}
            check(decline_reason(&first,1).find("Wrong mod hash")!=std::string::npos);
            std::thread isolated([&]{check(decline_reason(&first,1)==normal);});isolated.join();
        }
        check(decline_reason(&first,1)==normal);
        std::cout<<"PASS "<<checks<<" host-only policy assertions; no game, guest code, mod or network execution.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

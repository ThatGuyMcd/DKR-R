#pragma once
#include <chrono>
#include <cstdint>

namespace dkr::online_stability {
// Presentation generation must still invalidate render caches. Diagnostic
// output has a separate wall-clock budget, so replay cannot reset its limit.
class DiagnosticBudget {
public:
    using Clock=std::chrono::steady_clock;
    bool allow(Clock::time_point now=Clock::now()){
        if(!begun_||now-start_>=std::chrono::seconds(1)){start_=now;count_=0;begun_=true;}
        if(count_>=8){++suppressed_;return false;}++count_;return true;
    }
    std::uint64_t take_suppressed(){const auto count=suppressed_;suppressed_=0;return count;}
private:
    Clock::time_point start_{};unsigned count_=0;std::uint64_t suppressed_=0;bool begun_=false;
};
}

#include "recomp.h"
#include "probe_bridge.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

// Standalone checked-memory seam. No game, renderer, networking or profile.
extern "C" void dkr_probe_boss_diagnostic_failure(const dkr_probe_result*,recomp_context*) {}
namespace {
unsigned width=1;
bool specialized=false;
bool foreign_ram=false;
std::uint64_t address=0;
constexpr const char* source="checked-memory-fixture";
void entry(std::uint8_t* ram,recomp_context*) {
    auto* input=foreign_ram?ram+4:ram;
    void* p=nullptr;
    if(!specialized)p=dkr_probe_memory_at(input,address,width,width==1?3:width==2?2:0,source,19);
    else if(width==1)p=dkr_probe_memory_1_at(input,address,source,19);
    else if(width==2)p=dkr_probe_memory_2_at(input,address,source,19);
    else p=dkr_probe_memory_4_at(input,address,source,19);
    std::memset(p,0x5A,width);
}
void compare(std::uint64_t candidate,unsigned bytes,bool wrong_ram=false) {
    width=bytes;address=candidate;foreign_ram=wrong_ram;
    std::array<std::uint8_t,32> generic{},fixed{};
    recomp_context a{},b{};
    specialized=false;const auto left=dkr_probe_run(entry,generic.data(),generic.size(),&a,8);
    specialized=true;const auto right=dkr_probe_run(entry,fixed.data(),fixed.size(),&b,8);
    assert(left.completed==right.completed);
    assert(left.operations==right.operations && left.memory_accesses==right.memory_accesses);
    assert(left.bad_address==right.bad_address && left.source_line==right.source_line);
    assert((left.blocked==nullptr)==(right.blocked==nullptr));
    if(left.blocked)assert(std::strcmp(left.blocked,right.blocked)==0);
    assert((left.source_file==nullptr)==(right.source_file==nullptr));
    if(left.source_file)assert(std::strcmp(left.source_file,right.source_file)==0);
    assert(generic==fixed); // Rejected accesses must not write anything.
}
}
int main() {
    constexpr std::uint64_t base=UINT64_C(0xffffffff80000000);
    for(const unsigned bytes:{1U,2U,4U}) {
        for(unsigned offset=0;offset<40;++offset)compare(base+offset,bytes);
        compare(base-1,bytes);
        compare(UINT64_C(0x80000000),bytes); // No accidental zero-extension admission.
        compare(UINT64_MAX,bytes);
        compare(base,bytes,true);
    }
}

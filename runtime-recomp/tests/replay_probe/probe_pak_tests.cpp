#include "probe_pak.h"
#include "recomp.h"
#include "netplay/experimental_pak.hpp"
#include "netplay/runtime_state.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
namespace {
const char* import_name=nullptr;int native_result=0;
void entry(uint8_t* ram,recomp_context* ctx) { native_result=dkr_probe_native(import_name,ram,ctx); }
}
bool dkr_probe_pak_import_check() {
    using dkr::runtime::netplay::experimental::Paks;
    std::vector<uint8_t> ram(dkr::runtime::netplay::kRollbackMemoryBytes,0xA5);
    Paks paks;assert(paks.start(107,15,Paks::blank_images()));
    recomp_context ctx{};dkr::runtime::netplay::repair_float_register_pointer(ctx);
    const auto word=[&](unsigned offset,uint32_t value){memcpy(ram.data()+offset,&value,4);};
    const auto read=[&](unsigned offset){uint32_t value;memcpy(&value,ram.data()+offset,4);return value;};
    const auto call=[&](const char* name){import_name=name;return dkr_probe_run(entry,ram.data(),ram.size(),&ctx,10000);};
    const auto snapshot=[&](){std::vector<uint8_t> bytes(Paks::kCheckpointBytes);assert(paks.capture(bytes));return bytes;};
    dkr_probe_offline_services(1);
    ctx.r4=int32_t(0x80004000U);ctx.r5=int32_t(0x80006000U);
    assert(!call("osPfsFreeBlocks_recomp").completed); // Unbound imports still fence.
    dkr_probe_bind_paks(&paks);
    const unsigned queue=DKR_PROBE_REVISION==77?0x801210E0:0x80121660;
    for(unsigned p=0;p<4;++p) {
        ctx.r4=int32_t(queue);ctx.r5=int32_t(0x80004000U+p*104);ctx.r6=p;
        assert(call("osPfsInitPak_recomp").completed && !ctx.r2);
        assert(read(0x4000+p*104)==1 && read(0x4004+p*104)==queue && read(0x4008+p*104)==p);
        assert(read(0x404C+p*104)==0x200 && read(0x4050+p*104)==16);
        assert(ram[(0x4066+p*104)^3]==0 && ram[(0x4067+p*104)^3]==1);
    }
    ctx.r4=3;ctx.r2=0xAABB;assert(call("dkr_virtual_pak_preferred_status").completed && native_result==0 && ctx.r2==0xAABB);
    ctx.r4=4;assert(call("dkr_virtual_pak_preferred_status").completed && native_result== -1);
    ctx.r5=int32_t(0x80006000U);assert(call("osPfsIsPlug_recomp").completed && ram[0x6003]==15);
    const auto initial=snapshot();assert(paks.begin_frame(0));
    const auto allocate_args=[&](){
        ctx.r29=int32_t(0x80003000U);ctx.r4=int32_t(0x80004000U);ctx.r5=0x3459;ctx.r6=0x4E445945;
        ctx.r7=int32_t(0x80005000U);word(0x3010,0x80005020);word(0x3014,1024);word(0x3018,0x80006000);
        for(unsigned b=0;b<16;++b)ram[(0x5000+b)^3]=uint8_t(b+1);
        for(unsigned b=0;b<4;++b)ram[(0x5020+b)^3]=uint8_t(b+21);
    };
    // Bad destination/alignment/metadata must refuse BEFORE creating a file.
    for(unsigned fault=0;fault<4;++fault) {
        allocate_args();
        if(fault==0)word(0x3018,0x80FFFFFF);
        if(fault==1)word(0x3018,0x80006001);
        if(fault==2)word(0x3010,0x80FFFFFE);
        if(fault==3)ctx.r7=int32_t(0x80FFFFF8U);
        const auto before=ram;
        assert(!call("osPfsAllocateFile_recomp").completed && ram==before);
        uint32_t count=99;assert(paks.num_files(0,count)==Paks::Ok && count==0);
    }
    allocate_args();assert(call("osPfsAllocateFile_recomp").completed && !ctx.r2 && read(0x6000)==0);
    ctx.r4=int32_t(0x80004000U);ctx.r5=0;ctx.r6=int32_t(0x80006000U);
    assert(call("osPfsFileState_recomp").completed && !ctx.r2 && read(0x6000)==1024 && read(0x6004)==0x4E445945);
    std::uint16_t company;memcpy(&company,ram.data()+(0x6008^2),2);assert(company==0x3459);
    for(unsigned b=0;b<4;++b)assert(ram[(0x600A+b)^3]==b+21);
    for(unsigned b=0;b<16;++b)assert(ram[(0x600EU+b)^3]==b+1);
    ctx.r4=int32_t(0x80004000U);ctx.r5=0;ctx.r6=1;ctx.r7=1016;
    word(0x3010,8);word(0x3014,0x80007000);
    for(unsigned b=0;b<8;++b)ram[(0x7000+b)^3]=uint8_t(50+b);
    assert(call("osPfsReadWriteFile_recomp").completed && !ctx.r2);
    for(unsigned b=0;b<8;++b)ram[(0x7000+b)^3]=0;ctx.r6=0;
    assert(call("osPfsReadWriteFile_recomp").completed && !ctx.r2);
    for(unsigned b=0;b<8;++b)assert(ram[(0x7000+b)^3]==50+b);
    std::vector<uint8_t> journal;assert(paks.end_frame(journal));const auto checkpoint=snapshot();
    for(unsigned fault=0;fault<6;++fault) {
        ctx.r4=int32_t(0x80004000U);ctx.r5=0;ctx.r6=1;ctx.r7=1016;
        word(0x3010,8);word(0x3014,0x80007000);
        if(fault==0)word(0x3014,0x80FFFFFF);
        if(fault==1)ctx.r4=int32_t(0x80FFFFF8U);
        if(fault==2)ctx.r6=2;
        if(fault==3)ctx.r7=uint32_t(-1);
        if(fault==4)word(0x3010,uint32_t(-1));
        // Valid write outside an open tick is an OWNER failure, never disk I/O.
        const auto before=ram;const auto result=call("osPfsReadWriteFile_recomp");
        assert((fault>=2 && fault<=4)?(result.completed && ctx.r2==5):!result.completed);
        assert(ram==before && snapshot()==checkpoint);
    }
    assert(paks.restore(initial));uint32_t count=99;assert(paks.num_files(0,count)==Paks::Ok && !count);
    assert(paks.begin_frame(0));allocate_args();assert(call("osPfsAllocateFile_recomp").completed && !ctx.r2);
    assert(paks.end_frame(journal) && paks.commit(107,0,journal) && !paks.restore(initial));
    dkr_probe_bind_paks(nullptr);assert(!call("osPfsAllocateFile_recomp").completed);
    std::cout<<"Exact owned DKR-R virtual Pak import ABI: four ports, memory priority, allocation/read/write/state, byte lanes, invalid buffer/alignment refusal and rewind/confirmation passed. No file or physical accessory access.\n";
    return true;
}

#include "netplay/experimental_checkpoint_hash.hpp"
#include "netplay/rollback_ring.hpp"
#define XXH_INLINE_ALL
#define XXH_VECTOR 0 // Independent scalar reference for the platform vector path.
#include "xxHash/xxhash.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

int main(int argc,char** argv) {
    using dkr::runtime::netplay::experimental::checkpoint_hash;
    assert(checkpoint_hash({})==UINT64_C(0x2D06800538D394C2));
    std::vector<std::uint8_t> storage(16U*1024U*1024U+8);
    for(std::size_t i=0;i<storage.size();++i) storage[i]=std::uint8_t((i*29)^(i>>12));
    for(std::size_t offset=0;offset<8;++offset) {
        for(std::size_t length: {0U,1U,3U,4U,8U,16U,17U,31U,32U,64U,65U,127U,128U,129U,
                                 240U,241U,255U,256U,257U,1023U,1024U,1025U,4095U,4096U,4097U,
                                 65536U,16U*1024U*1024U}) {
            auto bytes=std::span<const std::uint8_t>(storage).subspan(offset,length);
            const auto baseline=checkpoint_hash(bytes);
            assert(baseline==XXH3_64bits(bytes.data(),bytes.size()));
            if(length) {
                for(std::size_t bit: {std::size_t(0),length/2,length-1}) {
                    storage[offset+bit]^=0x80;
                    assert(checkpoint_hash(bytes)!=baseline);
                    storage[offset+bit]^=0x80;
                }
            }
        }
    }
    if(argc==2 && std::strcmp(argv[1],"--benchmark")==0) {
        auto bytes=std::span<const std::uint8_t>(storage).first(16U*1024U*1024U);
        std::uint64_t sink=0;
        const auto measure=[&](auto hash) {
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<30;++i) { storage[0]^=std::uint8_t(i+1); sink^=hash(bytes); }
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/30;
        };
        const auto serial=measure(dkr::runtime::netplay::state_checksum);
        const auto vector=measure(checkpoint_hash);
        std::cout<<"LOCAL 16 MiB checksum benchmark: original_ms="<<serial
                 <<" experimental_ms="<<vector<<" sink="<<sink<<'\n';
        // No timing assertion: machine load is not correctness evidence.
    } else if(argc!=1) return 2;
    std::cout<<"Experimental checkpoint hashes match scalar reference at all tested lengths/alignments.\n";
}

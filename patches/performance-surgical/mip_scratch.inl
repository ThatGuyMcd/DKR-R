// Included after GeneratedMips::Level/downsample; DKR's own extension only.
struct CpuMipScratch {
    std::vector<Level> levels;
    std::vector<std::uint64_t> offsets;
    bool busy=false;
};
struct CpuMipLease {
    CpuMipScratch& scratch;
    explicit CpuMipLease(CpuMipScratch& value):scratch(value){scratch.busy=true;}
    ~CpuMipLease() {
        std::uint64_t retained=0;
        for(const auto& level:scratch.levels)retained+=level.pixels.capacity();
        // Bound retained CPU payload per worker. Larger images still use the
        // unchanged upload path, but their temporary CPU storage is released.
        if(retained>8ULL*1024*1024) {std::vector<Level>().swap(scratch.levels);std::vector<std::uint64_t>().swap(scratch.offsets);}
        scratch.busy=false;
    }
};
inline CpuMipScratch& reusable_mip_scratch() {static thread_local CpuMipScratch value;return value;}

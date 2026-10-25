#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace dkr::runtime::render_provenance {
struct View {std::span<const std::uint8_t> submitted,decode;std::uint32_t root=0;};
inline View& current(){static thread_local View value;return value;}
class Scope {
    View previous_;
public:
    Scope(std::span<const std::uint8_t> submitted,std::span<const std::uint8_t> decode,std::uint32_t root)
        :previous_(current()){current()={submitted,decode,root};}
    ~Scope(){current()=previous_;}
    Scope(const Scope&)=delete;
};
enum class Result {Unavailable,SharedDecode,CommandChanged,PayloadChanged,Unchanged,PrivatePayload};
inline bool valid(std::span<const std::uint8_t> bytes,std::uint64_t start,std::uint64_t count) {
    return start<=bytes.size() && count<=bytes.size()-start;
}
inline Result inspect(View view,std::uint32_t command,std::uint32_t payload,std::uint32_t count) {
    if(!valid(view.submitted,command,8) || !valid(view.decode,command,8))return Result::Unavailable;
    if(view.submitted.data()==view.decode.data())return Result::SharedDecode;
    if(std::memcmp(view.submitted.data()+command,view.decode.data()+command,8))return Result::CommandChanged;
    if(count && !valid(view.submitted,payload,count))return Result::PrivatePayload;
    if(count && !valid(view.decode,payload,count))return Result::Unavailable;
    if(count && std::memcmp(view.submitted.data()+payload,view.decode.data()+payload,count))return Result::PayloadChanged;
    return Result::Unchanged;
}
inline const char* name(Result result) {
    switch(result) {
    case Result::SharedDecode:return "shared-decode-no-independent-baseline";
    case Result::CommandChanged:return "command-changed-after-submission";
    case Result::PayloadChanged:return "payload-changed-after-submission";
    case Result::Unchanged:return "checked-bytes-match-submission";
    case Result::PrivatePayload:return "private-or-outside-submission-payload";
    default:return "baseline-unavailable";
    }
}
inline void report(std::uint64_t task,std::uint32_t command,std::uint32_t payload,std::uint32_t count,
                   std::uint32_t scene,std::uint32_t map,std::uint32_t menu) {
    static std::atomic<unsigned> reports{0};if(reports.fetch_add(1,std::memory_order_relaxed)>=3)return;
    const auto view=current();const auto result=inspect(view,command,payload,count);
    std::fprintf(stderr,"[render][provenance] task=%llu root=%08X scene=%u map=%u menu=%u command=%08X payload=%08X bytes=%u origin=%s; guards retained; bounded report\n",
        static_cast<unsigned long long>(task),view.root,scene,map,menu,command,payload,count,name(result));
    if(valid(view.submitted,payload,std::min(count,32U))) {
        std::fprintf(stderr,"[render][submitted-payload]");
        for(unsigned i=0;i<std::min(count,32U)/4;++i) {
            std::uint32_t word=0;std::memcpy(&word,view.submitted.data()+payload+i*4,4);std::fprintf(stderr," %08X",word);
        }
        std::fputc('\n',stderr);
    }
}
}

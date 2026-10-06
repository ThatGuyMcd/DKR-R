#pragma once
#include "legacy_mod_format.hpp"
#include <limits>
#include <algorithm>

namespace dkr::mods {
// Canonical, pointer-free native sidecars. Never serialize C++ object layouts,
// pointers, mutexes or allocator addresses into a network/rollback checkpoint.
class CheckpointWriter {
public:
    static constexpr std::size_t Budget=256*1024;
    void u32(std::uint32_t value){room(4);for(unsigned i=0;i<4;++i)bytes_.push_back(static_cast<std::uint8_t>(value>>(24-i*8)));}
    void u64(std::uint64_t value){u32(static_cast<std::uint32_t>(value>>32));u32(static_cast<std::uint32_t>(value));}
    void flag(bool value){u32(value?1:0);}
    void text(std::string_view value,std::size_t limit=256){if(value.size()>limit)throw Error("Native mod checkpoint text exceeds its bound.");u32(static_cast<std::uint32_t>(value.size()));room(value.size());bytes_.insert(bytes_.end(),value.begin(),value.end());}
    void block(View value){if(value.size()>Budget-4)throw Error("Native mod checkpoint block exceeds its bound.");room(4+value.size());u32(static_cast<std::uint32_t>(value.size()));bytes_.insert(bytes_.end(),value.begin(),value.end());}
    Bytes finish()&&{return std::move(bytes_);}
private:
    void room(std::size_t count){if(count>Budget-bytes_.size())throw Error("Native mod checkpoint exceeds its fixed budget.");}
    Bytes bytes_;
};
class CheckpointReader {
public:
    explicit CheckpointReader(View bytes):bytes_(bytes){if(bytes.size()>CheckpointWriter::Budget)throw Error("Native mod checkpoint exceeds its fixed budget.");}
    std::uint32_t u32(){const auto value=be32(bytes_,at_);at_+=4;return value;}
    std::uint64_t u64(){const auto high=u32();return std::uint64_t(high)<<32|u32();}
    bool flag(){const auto value=u32();if(value>1)throw Error("Invalid native mod checkpoint flag.");return value!=0;}
    std::string text(std::size_t limit=256){const auto size=u32();if(size>limit)throw Error("Native mod checkpoint text exceeds its bound.");const auto value=take(size);return {value.begin(),value.end()};}
    View block(){const auto size=u32();return take(size);}
    std::uint32_t bounded(std::uint32_t maximum){const auto value=u32();if(value>maximum)throw Error("Native mod checkpoint value exceeds its bound.");return value;}
    void end()const{if(at_!=bytes_.size())throw Error("Native mod checkpoint has trailing fields.");}
private:
    View take(std::size_t size){const auto value=slice(bytes_,at_,size);at_+=size;return value;}
    View bytes_;std::size_t at_=0;
};
inline bool checkpoint_digest(std::string_view value){return value.size()==64 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});}
}

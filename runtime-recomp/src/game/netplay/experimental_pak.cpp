#include "experimental_pak.hpp"
#include <algorithm>
#include <limits>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr std::size_t kDataStart=1280,kCapacity=Paks::kImageBytes-kDataStart;
constexpr std::size_t kJournalHeader=20,kJournalLimit=48*1024;
constexpr std::array<std::uint8_t,8> kMagic{'D','K','R','M','P','K','1',0};
void put(std::span<std::uint8_t> bytes,std::size_t offset,std::uint64_t value,unsigned count=4) {
    for(unsigned b=0;b<count;++b) bytes[offset+b]=std::uint8_t(value>>(b*8));
}
std::uint64_t get(std::span<const std::uint8_t> bytes,std::size_t offset,unsigned count=4) {
    std::uint64_t value=0;for(unsigned b=0;b<count;++b)value|=std::uint64_t(bytes[offset+b])<<(b*8);return value;
}
std::uint32_t checksum(std::span<const std::uint8_t> bytes) {
    std::uint32_t hash=2166136261U;
    for(std::size_t i=0;i<bytes.size();++i) { hash^=(i>=16 && i<20)?0:bytes[i];hash*=16777619U; }return hash;
}
std::uint32_t aligned(std::uint32_t bytes) { return (bytes+255U)&~255U; }
}
std::uint32_t Paks::used(const Port& port) {
    std::uint32_t bytes=0;for(const auto& file:port.files)if(file.used)bytes+=aligned(std::uint32_t(file.data.size()));return bytes;
}
std::vector<std::uint8_t> Paks::encode(const Port& port) {
    std::vector<std::uint8_t> bytes(kImageBytes,0);std::copy(kMagic.begin(),kMagic.end(),bytes.begin());
    put(bytes,8,1);put(bytes,12,port.generation);unsigned count=0;std::size_t cursor=kDataStart;
    for(unsigned i=0;i<kFiles;++i) {
        const auto& file=port.files[i];if(!file.used)continue;
        const auto size=std::uint32_t(file.data.size());
        if(!size || size>kCapacity || aligned(size)>kImageBytes-cursor) return {};
        const unsigned entry=256+i*64;put(bytes,entry,1);put(bytes,entry+4,file.identity.company);
        put(bytes,entry+8,file.identity.game);put(bytes,entry+12,size);put(bytes,entry+16,cursor);
        std::copy(file.identity.extension.begin(),file.identity.extension.end(),bytes.begin()+entry+20);
        std::copy(file.identity.name.begin(),file.identity.name.end(),bytes.begin()+entry+24);
        std::copy(file.data.begin(),file.data.end(),bytes.begin()+cursor);cursor+=aligned(size);++count;
    }
    put(bytes,20,count);put(bytes,16,checksum(bytes));return bytes;
}
bool Paks::decode(std::span<const std::uint8_t> image,Port& port) {
    if(image.size()!=kImageBytes || !std::equal(kMagic.begin(),kMagic.end(),image.begin()) ||
       get(image,8)!=1 || get(image,16)!=checksum(image) || get(image,20)>kFiles) return false;
    Port staged;staged.generation=std::uint32_t(get(image,12));
    std::size_t cursor=kDataStart;unsigned count=0;
    for(unsigned i=0;i<kFiles;++i) {
        const unsigned entry=256+i*64;const auto flag=get(image,entry);
        if(!flag)continue;if(flag!=1 || get(image,entry+4)>UINT16_MAX) return false;
        const auto size=std::uint32_t(get(image,entry+12));const auto offset=get(image,entry+16);
        // Stable encoder writes packed page-aligned files. Reject overlapping,
        // aliased or noncanonical images before admitting a rollback baseline.
        if(!size || size>kCapacity || offset!=cursor || aligned(size)>kImageBytes-cursor) return false;
        auto& file=staged.files[i];file.used=true;file.identity.company=std::uint16_t(get(image,entry+4));
        file.identity.game=std::uint32_t(get(image,entry+8));
        std::copy_n(image.begin()+entry+20,4,file.identity.extension.begin());
        std::copy_n(image.begin()+entry+24,16,file.identity.name.begin());
        file.data.assign(image.begin()+cursor,image.begin()+cursor+size);cursor+=aligned(size);++count;
    }
    if(count!=get(image,20) || encode(staged)!=std::vector<std::uint8_t>(image.begin(),image.end())) return false;
    port=std::move(staged);return true;
}
std::vector<std::uint8_t> Paks::blank_images() {
    const auto blank=encode(Port{});std::vector<std::uint8_t> result;result.reserve(kImagesBytes);
    for(unsigned p=0;p<kPorts;++p)result.insert(result.end(),blank.begin(),blank.end());return result;
}
bool Paks::start(std::uint64_t epoch,std::uint8_t mask,std::span<const std::uint8_t> images) {
    if(epoch_ || !epoch || mask>15 || images.size()!=kImagesBytes) return false;
    std::array<Port,kPorts> staged;
    for(unsigned p=0;p<kPorts;++p)if(!decode(images.subspan(p*kImageBytes,kImageBytes),staged[p]))return false;
    std::vector<std::uint8_t> confirmed(images.begin(),images.end());
    working_=std::move(staged);confirmed_=std::move(confirmed);mask_=mask;epoch_=epoch;return true;
}
bool Paks::begin_epoch(std::uint64_t epoch) {
    if(!epoch_ || epoch<=epoch_ || open_ || next_!=confirmed_next_)return false;
    for(unsigned p=0;p<kPorts;++p) {
        const auto bytes=encode(working_[p]);if(!std::equal(bytes.begin(),bytes.end(),confirmed_.begin()+p*kImageBytes))return false;
    }
    epoch_=epoch;next_=confirmed_next_=0;return true;
}
bool Paks::begin_frame(std::uint32_t frame) {
    if(!epoch_ || open_ || frame!=next_ || frame==UINT32_MAX)return false;
    std::vector<std::uint8_t> journal(kJournalHeader,0);
    journal[0]='D';journal[1]='K';journal[2]='P';journal[3]='J';journal[4]=1;journal[5]=mask_;
    put(journal,8,epoch_,8);put(journal,16,frame);journal_=std::move(journal);operations_=0;open_=true;return true;
}
bool Paks::end_frame(std::vector<std::uint8_t>& journal) {
    if(!open_)return false;put(journal_,6,operations_,2);journal=std::move(journal_);++next_;open_=false;return true;
}
bool Paks::capture(std::span<std::uint8_t> bytes) const {
    if(!epoch_ || open_ || bytes.size()!=kCheckpointBytes)return false;
    std::fill_n(bytes.begin(),32,0);bytes[0]='D';bytes[1]='K';bytes[2]='P';bytes[3]='K';bytes[4]=1;bytes[5]=mask_;
    put(bytes,8,epoch_,8);put(bytes,16,next_);
    for(unsigned p=0;p<kPorts;++p) {
        const auto image=encode(working_[p]);if(image.size()!=kImageBytes)return false;
        std::copy(image.begin(),image.end(),bytes.begin()+32+p*kImageBytes);
    }return true;
}
bool Paks::restore(std::span<const std::uint8_t> bytes) {
    if(!epoch_ || bytes.size()!=kCheckpointBytes || bytes[0]!='D' || bytes[1]!='K' || bytes[2]!='P' ||
       bytes[3]!='K' || bytes[4]!=1 || bytes[5]!=mask_ || bytes[6] || bytes[7] || get(bytes,8,8)!=epoch_ ||
       std::any_of(bytes.begin()+20,bytes.begin()+32,[](auto v){return v!=0;}))return false;
    const auto frame=std::uint32_t(get(bytes,16));if(frame<confirmed_next_ || frame>next_)return false;
    std::array<Port,kPorts> staged;
    for(unsigned p=0;p<kPorts;++p)if(!decode(bytes.subspan(32+p*kImageBytes,kImageBytes),staged[p]))return false;
    working_=std::move(staged);next_=frame;open_=false;journal_.clear();operations_=0;return true;
}
bool Paks::commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal) {
    if(!epoch_ || epoch!=epoch_ || frame!=confirmed_next_ || frame>=next_ || journal.size()<kJournalHeader ||
       journal.size()>kJournalLimit || journal[0]!='D' || journal[1]!='K' || journal[2]!='P' || journal[3]!='J' ||
       journal[4]!=1 || journal[5]!=mask_ || get(journal,8,8)!=epoch || get(journal,16)!=frame || get(journal,6,2)>128)return false;
    auto images=confirmed_;std::size_t cursor=kJournalHeader;
    for(unsigned i=0;i<get(journal,6,2);++i) {
        if(journal.size()-cursor<8)return false;
        const unsigned p=journal[cursor];const auto offset=std::size_t(get(journal,cursor+2,2));
        const auto size=std::size_t(get(journal,cursor+4,2));
        if(p>=kPorts || !(mask_&(1U<<p)) || journal[cursor+1] || get(journal,cursor+6,2) ||
           !size || offset>=kImageBytes || size>kImageBytes-offset || size>journal.size()-cursor-8)return false;
        const auto generation=get(std::span(images).subspan(p*kImageBytes,kImageBytes),12);
        if(generation==UINT32_MAX)return false;
        cursor+=8;std::copy_n(journal.begin()+cursor,size,images.begin()+p*kImageBytes+offset);cursor+=size;
        Port validated;
        if(!decode(std::span(images).subspan(p*kImageBytes,kImageBytes),validated) || validated.generation!=generation+1)return false;
    }
    if(cursor!=journal.size())return false;confirmed_=std::move(images);++confirmed_next_;return true;
}
int Paks::status(unsigned p) const { return epoch_ && p<kPorts && (mask_&(1U<<p)) ? Ok:NoPak; }
int Paks::free_bytes(unsigned p,std::uint32_t& bytes) const {
    const int result=status(p);if(result!=Ok)return result;bytes=kCapacity-used(working_[p]);return Ok;
}
int Paks::num_files(unsigned p,std::uint32_t& count) const {
    const int result=status(p);if(result!=Ok)return result;
    count=unsigned(std::count_if(working_[p].files.begin(),working_[p].files.end(),[](const auto& f){return f.used;}));return Ok;
}
int Paks::find(unsigned p,const Identity& identity,unsigned& index) const {
    const int result=status(p);if(result!=Ok)return result;
    for(unsigned i=0;i<kFiles;++i)if(working_[p].files[i].used && working_[p].files[i].identity==identity){index=i;return Ok;}return Invalid;
}
int Paks::file_state(unsigned p,unsigned index,FileState& state) const {
    const int result=status(p);if(result!=Ok)return result;
    if(index>=kFiles || !working_[p].files[index].used)return Invalid;
    const auto& f=working_[p].files[index];state={f.identity,std::uint32_t(f.data.size())};return Ok;
}
int Paks::read(unsigned p,unsigned index,std::size_t offset,std::span<std::uint8_t> bytes) const {
    const int result=status(p);if(result!=Ok)return result;
    if(index>=kFiles || !working_[p].files[index].used)return Invalid;
    const auto& data=working_[p].files[index].data;if(offset>data.size() || bytes.size()>data.size()-offset)return Invalid;
    std::copy_n(data.begin()+offset,bytes.size(),bytes.begin());return Ok;
}
int Paks::mutate(unsigned p,Port replacement) {
    if(!open_ || operations_==128 || working_[p].generation==UINT32_MAX)return OwnerFailure;
    replacement.generation=working_[p].generation+1;
    const auto before=encode(working_[p]),after=encode(replacement);if(after.size()!=kImageBytes)return OwnerFailure;
    std::size_t first=0,last=kImageBytes;while(before[first]==after[first])++first;
    while(before[last-1]==after[last-1])--last;const auto size=last-first;
    if(size+8>kJournalLimit-journal_.size())return OwnerFailure;
    auto journal=journal_;const auto cursor=journal.size();journal.resize(cursor+8+size,0);
    journal[cursor]=std::uint8_t(p);put(journal,cursor+2,first,2);put(journal,cursor+4,size,2);
    std::copy(after.begin()+first,after.begin()+last,journal.begin()+cursor+8);
    journal_=std::move(journal);working_[p]=std::move(replacement);++operations_;return Ok;
}
int Paks::allocate(unsigned p,const Identity& identity,std::uint32_t bytes,unsigned& index) {
    const int result=status(p);if(result!=Ok)return result;if(!bytes)return Invalid;
    unsigned found=0;if(find(p,identity,found)==Ok)return Exists;
    const auto& port=working_[p];found=kFiles;
    for(unsigned i=0;i<kFiles;++i)if(!port.files[i].used){found=i;break;}if(found==kFiles)return DirFull;
    if(bytes>kCapacity || aligned(bytes)>kCapacity-used(port))return DataFull;
    auto staged=port;auto& file=staged.files[found];file.used=true;file.identity=identity;file.data.assign(bytes,0);
    const int changed=mutate(p,std::move(staged));if(changed==Ok)index=found;return changed;
}
int Paks::erase(unsigned p,const Identity& identity) {
    unsigned index=0;const int result=find(p,identity,index);if(result!=Ok)return result;
    auto staged=working_[p];staged.files[index]={};return mutate(p,std::move(staged));
}
int Paks::write(unsigned p,unsigned index,std::size_t offset,std::span<const std::uint8_t> bytes) {
    const int result=status(p);if(result!=Ok)return result;
    if(index>=kFiles || !working_[p].files[index].used)return Invalid;
    const auto& file=working_[p].files[index];if(offset>file.data.size() || bytes.size()>file.data.size()-offset)return Invalid;
    auto staged=working_[p];std::copy(bytes.begin(),bytes.end(),staged.files[index].data.begin()+offset);return mutate(p,std::move(staged));
}
int Paks::reformat(unsigned p) { const int result=status(p);return result!=Ok?result:mutate(p,Port{}); }
}

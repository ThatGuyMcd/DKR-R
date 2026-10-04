#include "experimental_eeprom.hpp"
#include <algorithm>
#include <limits>

namespace dkr::runtime::netplay::experimental {
namespace {
constexpr std::size_t kHeader=20,kJournalLimit=64*1024;
void integer(std::span<std::uint8_t> to,std::uint64_t value) {
    for(unsigned b=0;b<to.size();++b) to[b]=std::uint8_t(value>>(b*8));
}
std::uint64_t integer(std::span<const std::uint8_t> from) {
    std::uint64_t value=0; for(unsigned b=0;b<from.size();++b) value|=std::uint64_t(from[b])<<(b*8); return value;
}
bool valid_range(std::size_t offset,std::size_t size) {
    return size && !(offset%8) && !(size%8) && offset<=Eeprom::kBytes && size<=Eeprom::kBytes-offset;
}
}
bool Eeprom::start(std::uint64_t epoch,std::span<const std::uint8_t> image) {
    if(epoch_ || !epoch || image.size()!=kBytes) return false;
    std::copy(image.begin(),image.end(),working_.begin()); confirmed_=working_; epoch_=epoch; return true;
}
bool Eeprom::begin_epoch(std::uint64_t epoch) {
    // Do not discard an open frame, unconfirmed writes or a speculative tail
    // whose bytes happen to equal confirmation. The old scene must be drained.
    if(!epoch_ || epoch<=epoch_ || open_ || next_!=confirmed_next_ || working_!=confirmed_) return false;
    epoch_=epoch; next_=confirmed_next_=0; return true;
}
bool Eeprom::begin_frame(std::uint32_t frame) {
    if(!epoch_ || open_ || frame!=next_ || frame==UINT32_MAX) return false;
    std::vector<std::uint8_t> header(kHeader,0);
    header[0]='D';header[1]='K';header[2]='S';header[3]='E';header[4]=1;
    integer(std::span(header).subspan(8,8),epoch_); integer(std::span(header).subspan(16,4),frame);
    journal_=std::move(header); records_=0; open_=true; return true;
}
bool Eeprom::read(std::size_t offset,std::span<std::uint8_t> destination) const {
    if(!epoch_ || !valid_range(offset,destination.size())) return false;
    std::copy_n(working_.begin()+offset,destination.size(),destination.begin()); return true;
}
bool Eeprom::write(std::size_t offset,std::span<const std::uint8_t> source) {
    if(!open_ || !valid_range(offset,source.size()) || records_==128 ||
       source.size()+4>kJournalLimit-journal_.size()) return false;
    // Complete allocation and the intent before changing reversible bytes.
    const auto first=journal_.size(); journal_.resize(first+4+source.size());
    integer(std::span(journal_).subspan(first,2),offset); integer(std::span(journal_).subspan(first+2,2),source.size());
    std::copy(source.begin(),source.end(),journal_.begin()+first+4);
    std::copy(source.begin(),source.end(),working_.begin()+offset); ++records_; return true;
}
bool Eeprom::end_frame(std::vector<std::uint8_t>& journal) {
    if(!open_) return false;
    integer(std::span(journal_).subspan(6,2),records_);
    journal=std::move(journal_); ++next_; open_=false; return true;
}
bool Eeprom::capture(std::span<std::uint8_t> checkpoint) const {
    if(!epoch_ || open_ || checkpoint.size()!=kCheckpointBytes) return false;
    integer(checkpoint.first(8),epoch_); integer(checkpoint.subspan(8,4),next_);
    std::copy(working_.begin(),working_.end(),checkpoint.begin()+12); return true;
}
bool Eeprom::restore(std::span<const std::uint8_t> checkpoint) {
    if(!epoch_ || checkpoint.size()!=kCheckpointBytes || integer(checkpoint.first(8))!=epoch_) return false;
    const auto frame=std::uint32_t(integer(checkpoint.subspan(8,4)));
    if(frame<confirmed_next_ || frame>next_) return false;
    std::copy_n(checkpoint.begin()+12,kBytes,working_.begin()); next_=frame;
    // Also recover a failed tick which left its frame open. Confirmed bytes
    // and its irreversible cursor are never rolled back or copied from input.
    journal_.clear(); records_=0; open_=false; return true;
}
bool Eeprom::commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal) {
    if(!epoch_ || epoch!=epoch_ || frame!=confirmed_next_ || frame>=next_ ||
       journal.size()<kHeader || journal.size()>kJournalLimit || journal[0]!='D' || journal[1]!='K' ||
       journal[2]!='S' || journal[3]!='E' || journal[4]!=1 || journal[5] ||
       integer(journal.subspan(8,8))!=epoch || integer(journal.subspan(16,4))!=frame) return false;
    const auto count=integer(journal.subspan(6,2)); if(count>128) return false;
    auto image=confirmed_; std::size_t cursor=kHeader;
    for(unsigned record=0;record<count;++record) {
        if(journal.size()-cursor<4) return false;
        const auto offset=std::size_t(integer(journal.subspan(cursor,2)));
        const auto size=std::size_t(integer(journal.subspan(cursor+2,2))); cursor+=4;
        if(!valid_range(offset,size) || size>journal.size()-cursor) return false;
        std::copy_n(journal.begin()+cursor,size,image.begin()+offset); cursor+=size;
    }
    if(cursor!=journal.size()) return false;
    confirmed_=image; ++confirmed_next_; return true;
}
}

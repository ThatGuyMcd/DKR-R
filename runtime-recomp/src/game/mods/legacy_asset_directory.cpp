#include "legacy_asset_directory.hpp"
#include <algorithm>

namespace dkr::mods {
namespace {
void append_word(Bytes& bytes,std::uint32_t value) {
    bytes.push_back(value>>24);bytes.push_back(value>>16);bytes.push_back(value>>8);bytes.push_back(value);
}
int table_for(unsigned section) {
    switch(section) {
    case 2:case 4:case 12:return section+1;
    case 21:case 23:case 25:case 27:case 29:case 32:case 34:case 39:return section-1;
    default:return -1;
    }
}
}
std::shared_ptr<const AssetDirectory> AssetDirectory::build(std::shared_ptr<const AssetBank> bank) {
    if(!bank) throw Error("An immutable bank is required to build an asset directory.");
    auto directory=std::shared_ptr<AssetDirectory>(new AssetDirectory);
    directory->bank_=std::move(bank);
    for(unsigned section=0;section<50;++section) {
        const auto original=directory->bank_->stock_section(section);
        directory->pieces_[section].push_back({0,original});
        directory->sizes_[section]=original.size();
    }
    for(unsigned section=0;section<50;++section) if(directory->bank_->overrides_section(section)) {
        if(section==30 && directory->bank_->augmented()) {
            const auto bytes=directory->bank_->record(30,0);
            directory->pieces_[30]={{0,bytes}};directory->sizes_[30]=bytes.size();continue;
        }
        const auto table_id=table_for(section);
        if(table_id<0) throw Error("Overridden section has no coherent directory adapter.");
        auto& pieces=directory->pieces_[section];pieces.clear();
        auto& table=directory->tables_[table_id];
        std::size_t offset=0;
        if(section!=39) append_word(table,0); // Audio table omits its implicit zero.
        const auto count=directory->bank_->record_count(section);
        for(unsigned id=0;id<count;++id) {
            const auto bytes=directory->bank_->record(section,id);
            if(bytes.size()>MaxImage-offset) throw Error("Virtual asset section exceeds its address budget.");
            if(!bytes.empty()) pieces.push_back({offset,bytes});
            offset+=bytes.size();append_word(table,static_cast<std::uint32_t>(offset));
        }
        append_word(table,0xffffffff);
        directory->sizes_[section]=offset;
        directory->sizes_[table_id]=table.size();
        directory->pieces_[table_id]={{0,table}};
    }
    std::size_t total=0;
    for(const auto size : directory->sizes_) {
        if(size>MaxStaged-total) throw Error("Asset directory exceeds its total address budget.");
        total+=size;
    }
    return directory;
}
std::size_t AssetDirectory::section_size(unsigned section) const {
    if(section>=50) throw Error("Invalid asset directory section.");
    return sizes_[section];
}
Bytes AssetDirectory::read(unsigned section,std::size_t offset,std::size_t length) const {
    const auto size=section_size(section);
    if(offset>size || length>size-offset || length>MaxImage)
        throw Error("Asset DMA range exceeds its immutable section.");
    Bytes output(length);
    if(!length) return output;
    const auto& pieces=pieces_[section];
    auto piece=std::upper_bound(pieces.begin(),pieces.end(),offset,
        [](std::size_t address,const Piece& item){return address<item.offset;});
    if(piece==pieces.begin()) throw Error("Asset directory has no backing bytes for this address.");
    --piece;
    std::size_t written=0;
    while(written<length && piece!=pieces.end()) {
        if(offset<piece->offset || offset-piece->offset>piece->bytes.size())
            throw Error("Asset directory contains an unbacked range.");
        const auto within=offset-piece->offset;
        const auto count=std::min(length-written,piece->bytes.size()-within);
        std::copy_n(piece->bytes.data()+within,count,output.data()+written);
        written+=count;offset+=count;++piece;
    }
    if(written!=length) throw Error("Asset directory could not satisfy the complete DMA.");
    return output;
}
void AssetDirectory::copy_to_guest(unsigned section,std::size_t offset,
    std::span<std::uint8_t> rdram,std::uint32_t guest_address,std::size_t length) const {
    const auto size=section_size(section);
    if(offset>size || length>size-offset || length>MaxImage)
        throw Error("Asset DMA range exceeds its immutable section.");
    const auto segment=guest_address&0xe0000000U;
    if(segment!=0 && segment!=0x80000000U && segment!=0xa0000000U)
        throw Error("Asset DMA destination is not physical/KSEG RDRAM.");
    const auto physical=std::size_t(guest_address&0x1fffffffU);
    // The last logical byte maps within its complete word; a truncated final
    // host word must not allow XOR-3 to escape the supplied span.
    if(rdram.size()%4 || physical>rdram.size() || length>rdram.size()-physical)
        throw Error("Asset DMA destination exceeds RDRAM.");
    if(!length) return;
    const auto& pieces=pieces_[section];
    auto piece=std::upper_bound(pieces.begin(),pieces.end(),offset,
        [](std::size_t address,const Piece& item){return address<item.offset;});
    if(piece==pieces.begin()) throw Error("Asset directory has no backing bytes for this address.");
    --piece;
    std::size_t written=0;
    while(written<length) {
        // build() creates contiguous, immutable backing pieces. The whole
        // source/destination ranges above are checked before the first write.
        const auto within=offset-piece->offset;
        const auto count=std::min(length-written,piece->bytes.size()-within);
        for(std::size_t i=0;i<count;++i)
            rdram[(physical+written+i)^3U]=piece->bytes[within+i];
        written+=count;offset+=count;++piece;
    }
}
AssetDirectory::Address AssetDirectory::address(unsigned section,std::size_t offset) const {
    if(offset>section_size(section)) throw Error("Asset address is outside its section.");
    return {shared_from_this(),section,offset};
}
Bytes AssetDirectory::Address::read(std::size_t length) const {
    if(!owner) throw Error("Asset address has no live owner.");
    return owner->read(section,offset,length);
}
} // namespace dkr::mods

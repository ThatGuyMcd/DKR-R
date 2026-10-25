#include <utility>
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace dkr::runtime::netplay::experimental {
// Separate experimental owner. NEVER opens a file or calls the stable Pak
// singleton. Images use DKR-R's existing DKRMPK1 format, not raw N64 inode Paks.
class Paks final {
public:
    static constexpr std::size_t kImageBytes=32768,kPorts=4,kFiles=16;
    static constexpr std::size_t kImagesBytes=kImageBytes*kPorts;
    static constexpr std::size_t kCheckpointBytes=32+kImagesBytes;
    static constexpr int Ok=0,NoPak=1,Invalid=5,BadData=6,DataFull=7,DirFull=8,Exists=9;
    // A bounded-owner failure must halt speculation, not be returned to retail
    // as a transient accessory error. The C import bridge traps after return.
    static constexpr int OwnerFailure=-100;
    struct Identity {
        std::uint16_t company=0;
        std::uint32_t game=0;
        std::array<std::uint8_t,4> extension{};
        std::array<std::uint8_t,16> name{};
        bool operator==(const Identity&) const = default;
    };
    struct FileState { Identity identity{};std::uint32_t bytes=0; };
    static std::vector<std::uint8_t> blank_images();
    bool start(std::uint64_t epoch,std::uint8_t port_mask,std::span<const std::uint8_t> images);
    bool begin_epoch(std::uint64_t epoch);
    bool begin_frame(std::uint32_t frame);
    bool end_frame(std::vector<std::uint8_t>& journal);
    bool capture(std::span<std::uint8_t> checkpoint) const;
    bool restore(std::span<const std::uint8_t> checkpoint);
    bool commit(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> journal);
    std::span<const std::uint8_t> confirmed_images() const { return confirmed_; }
    std::uint8_t port_mask() const { return mask_; }
    int status(unsigned port) const;
    int free_bytes(unsigned port,std::uint32_t& bytes) const;
    int num_files(unsigned port,std::uint32_t& count) const;
    int find(unsigned port,const Identity& identity,unsigned& index) const;
    int file_state(unsigned port,unsigned index,FileState& state) const;
    int read(unsigned port,unsigned index,std::size_t offset,std::span<std::uint8_t> bytes) const;
    int allocate(unsigned port,const Identity& identity,std::uint32_t bytes,unsigned& index);
    int erase(unsigned port,const Identity& identity);
    int write(unsigned port,unsigned index,std::size_t offset,std::span<const std::uint8_t> bytes);
    int reformat(unsigned port);
    bool admit_confirmed(std::uint64_t epoch,std::uint32_t frame,std::span<const std::uint8_t> bytes) {
        if(!epoch||frame==UINT32_MAX||bytes.size()!=kCheckpointBytes||bytes[0]!='D'||bytes[1]!='K'||bytes[2]!='P'||bytes[3]!='K'||
           bytes[4]!=1||bytes[5]!=mask_||bytes[6]||bytes[7])return false;
        std::uint64_t incoming_epoch=0;std::uint32_t incoming_frame=0;
        for(unsigned i=0;i<8;++i)incoming_epoch|=std::uint64_t(bytes[8+i])<<(8*i);
        for(unsigned i=0;i<4;++i)incoming_frame|=std::uint32_t(bytes[16+i])<<(8*i);
        if(incoming_epoch!=epoch||incoming_frame!=frame)return false;
        for(unsigned i=20;i<32;++i)if(bytes[i])return false;
        Paks staged;if(!staged.start(epoch,mask_,bytes.subspan(32)))return false;
        staged.next_=staged.confirmed_next_=frame;*this=std::move(staged);return true;
    }
private:
    struct File { bool used=false;Identity identity{};std::vector<std::uint8_t> data; };
    struct Port { std::uint32_t generation=0;std::array<File,kFiles> files{}; };
    static std::vector<std::uint8_t> encode(const Port& port);
    static bool decode(std::span<const std::uint8_t> image,Port& port);
    static std::uint32_t used(const Port& port);
    int mutate(unsigned port,Port replacement);
    std::array<Port,kPorts> working_{};
    std::vector<std::uint8_t> confirmed_,journal_;
    std::uint64_t epoch_=0;
    std::uint32_t next_=0,confirmed_next_=0;
    std::uint16_t operations_=0;
    std::uint8_t mask_=0;
    bool open_=false;
};
}

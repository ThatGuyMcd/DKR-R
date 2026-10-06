#pragma once
#include "online_mod_manifest.hpp"
#include "online_mod_bundle.hpp"
#include <optional>

namespace dkr::mods::online {
inline constexpr std::size_t MaxChunkBytes=8192;
enum class Operation : std::uint8_t {
    Offer=1,ManifestRequest,ManifestChunk,Consent,PayloadRequest,PayloadChunk,
    Preparing,Verified,Cancel,ConsentAck,PreparingAck,Progress
};
enum class ProgressPhase : std::uint8_t {Review=1,Downloading,Preparing,Verified};
struct Message {
    Operation operation=Operation::Offer;
    std::string manifest,payload;
    std::uint32_t offset=0,total=0;
    Bytes bytes;
    bool operator==(const Message&)const=default;
};
Bytes encode_message(const Message& message);
Message decode_message(View bytes);
enum class TransferPhase {None,Manifest,Consent,Payloads,Preparing,Verified,Cancelled};
// One serial worker owns this state. Network/UI threads exchange bounded
// messages and immutable views; they never decode/import or write files.
class Download {
public:
    void offer(std::string digest,std::uint32_t manifest_bytes);
    bool manifest_chunk(const Message& message);
    const Manifest& manifest()const;
    const std::string& digest()const{return digest_;}
    TransferPhase phase()const{return phase_;}
    std::uint32_t offset()const{return offset_;}
    void consent();
    void cancel() noexcept;
    // The worker may skip a payload only after checking its full cached hash.
    void cached(std::string_view digest,std::uint64_t size);
    // Rejoin may reuse a durably written prefix only after renewed consent.
    // Its complete SHA is still mandatory; a prefix is never an asset proof.
    void resume_prefix(std::string_view digest,std::uint64_t bytes);
    Message request(std::size_t chunk_bytes)const;
    bool accept_payload_chunk(const Message& message)const;
    // Called ONLY after the staging writer has durably accepted these bytes.
    // Partial data never counts as a verified/prepared mod.
    void persisted(std::size_t bytes);
    // Called ONLY after the completed staging file's size and SHA-256 match.
    void payload_verified();
    void prepared(std::string_view locally_verified_manifest);
    std::uint64_t received_bytes()const{return received_;}
private:
    void next_payload();
    TransferPhase phase_=TransferPhase::None;
    std::string digest_;
    std::uint32_t manifest_size_=0,offset_=0;
    Bytes metadata_;
    std::optional<Manifest> manifest_;
    std::size_t payload_=0;
    std::uint64_t received_=0;
};
// Worker-owned per-recipient window. Each request opens at most 64 KiB, not
// an unbounded stream. A receiver retries its first missing offset; reordering
// and loss cannot silently advance it. The coordinator schedules peers fairly.
class Upload {
public:
    explicit Upload(std::shared_ptr<const Bundle> bundle);
    void request(const Message& message,std::size_t chunk_bytes);
    std::optional<Message> next()const;
    void sent();
    bool consented()const{return consented_;}
    bool preparing()const{return preparing_;}
    bool cancelled()const{return cancelled_;}
private:
    std::shared_ptr<const Bundle> bundle_;
    std::shared_ptr<const Bytes> metadata_,source_;
    std::string digest_,payload_;
    std::uint32_t offset_=0,end_=0;
    std::size_t chunk_=0;
    bool consented_=false,preparing_=false,cancelled_=false;
};
}

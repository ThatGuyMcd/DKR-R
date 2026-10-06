#include "online_mod_transfer.hpp"
#include <algorithm>

namespace dkr::mods::online {
namespace {
void put32(Bytes& out,std::uint32_t value) {for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(24-i*8)));}
void validate(const Message& value) {
    if(value.operation<Operation::Offer || value.operation>Operation::Progress || !valid_digest(value.manifest) ||
       (!value.payload.empty() && !valid_digest(value.payload)) || value.bytes.size()>MaxChunkBytes)
        throw Error("Invalid online mod transfer message.");
    const auto operation=value.operation;
    if(operation==Operation::Progress) {
        if(!value.payload.empty() || !value.total || value.total>MaxTransferBytes || value.offset>value.total ||
            value.bytes.empty() || value.bytes.size()>241 || value.bytes[0]<1 || value.bytes[0]>4 ||
            std::any_of(value.bytes.begin()+1,value.bytes.end(),[](auto c){return c<32 || c>126;}))
            throw Error("Invalid online mod progress report.");
        return;
    }
    const bool payload=operation==Operation::PayloadRequest || operation==Operation::PayloadChunk;
    const bool metadata=operation==Operation::ManifestRequest || operation==Operation::ManifestChunk;
    const bool chunk=operation==Operation::ManifestChunk || operation==Operation::PayloadChunk;
    if(payload!=!value.payload.empty() || (operation==Operation::Offer && (!value.total || value.total>MaxManifestBytes || value.offset || !value.bytes.empty())) ||
       ((payload||metadata) && (!value.total || value.total>(payload?MaxTransferBytes:MaxManifestBytes) || value.offset>=value.total)) ||
       (chunk && (value.bytes.empty() || value.bytes.size()>value.total-value.offset)) ||
       (!chunk && !value.bytes.empty()) ||
       (!payload && !metadata && operation!=Operation::Offer && (value.total || value.offset)))
        throw Error("Online mod transfer range or operation is invalid.");
}
}
Bytes encode_message(const Message& value) {
    validate(value);Bytes out{'D','K','M',1,static_cast<std::uint8_t>(value.operation)};
    out.insert(out.end(),value.manifest.begin(),value.manifest.end());out.push_back(value.payload.empty()?0:1);
    if(!value.payload.empty())out.insert(out.end(),value.payload.begin(),value.payload.end());
    put32(out,value.offset);put32(out,value.total);out.insert(out.end(),value.bytes.begin(),value.bytes.end());return out;
}
Message decode_message(View bytes) {
    if(bytes.size()<78 || bytes.size()>142+MaxChunkBytes || bytes[0]!='D' || bytes[1]!='K' || bytes[2]!='M' || bytes[3]!=1 || bytes[69]>1)
        throw Error("Invalid online mod transfer envelope.");
    Message out;out.operation=static_cast<Operation>(bytes[4]);out.manifest.assign(bytes.begin()+5,bytes.begin()+69);
    std::size_t at=70;
    if(bytes[69]) {const auto digest=slice(bytes,at,64);out.payload.assign(digest.begin(),digest.end());at+=64;}
    const auto range=slice(bytes,at,8);out.offset=be32(range,0);out.total=be32(range,4);at+=8;
    out.bytes.assign(bytes.begin()+at,bytes.end());validate(out);return out;
}
void Download::offer(std::string digest,std::uint32_t size) {
    if(!valid_digest(digest) || !size || size>MaxManifestBytes)throw Error("Invalid host mod manifest offer.");
    if(phase_!=TransferPhase::None && phase_!=TransferPhase::Cancelled) {
        if(digest_==digest && manifest_size_==size)return;
        throw Error("The host changed its mod manifest during admission. Rejoin the lobby to review the new selection.");
    }
    digest_=std::move(digest);manifest_size_=size;metadata_.clear();metadata_.reserve(size);manifest_.reset();
    offset_=0;payload_=0;received_=0;phase_=TransferPhase::Manifest;
}
bool Download::manifest_chunk(const Message& message) {
    validate(message);
    if(phase_!=TransferPhase::Manifest || message.operation!=Operation::ManifestChunk || message.manifest!=digest_ ||
       message.offset!=offset_ || message.total!=manifest_size_)return false;
    metadata_.insert(metadata_.end(),message.bytes.begin(),message.bytes.end());offset_+=static_cast<std::uint32_t>(message.bytes.size());
    if(offset_==manifest_size_) {
        if(sha256(metadata_)!=digest_)throw Error("Host mod manifest hash verification failed.");
        manifest_=decode(metadata_);metadata_.clear();phase_=TransferPhase::Consent;offset_=0;
    }
    return true;
}
const Manifest& Download::manifest()const {if(!manifest_)throw Error("Host mod manifest has not been verified.");return *manifest_;}
void Download::consent() {
    if(phase_!=TransferPhase::Consent)throw Error("Host mods cannot be downloaded before explicit consent.");
    phase_=TransferPhase::Payloads;next_payload();
}
void Download::cancel()noexcept {phase_=TransferPhase::Cancelled;metadata_.clear();manifest_.reset();offset_=0;}
void Download::next_payload() {if(payload_==manifest().payloads.size())phase_=TransferPhase::Preparing;offset_=0;}
void Download::cached(std::string_view digest,std::uint64_t size) {
    if(phase_!=TransferPhase::Payloads || offset_ || manifest().payloads.at(payload_).digest!=digest || manifest().payloads.at(payload_).size!=size)
        throw Error("Cached mod does not match the required host payload.");
    received_+=size;++payload_;next_payload();
}
void Download::resume_prefix(std::string_view digest,std::uint64_t size) {
    if(phase_!=TransferPhase::Payloads || offset_ || !size ||
       manifest().payloads.at(payload_).digest!=digest || size>=manifest().payloads.at(payload_).size)
        throw Error("Saved mod prefix does not match the authorized payload.");
    offset_=static_cast<std::uint32_t>(size);received_+=size;
}
Message Download::request(std::size_t chunk_bytes)const {
    if(!chunk_bytes || chunk_bytes>MaxChunkBytes)throw Error("Invalid mod transfer chunk budget.");
    // Request total is the exact complete size, not a requested allocation.
    if(phase_==TransferPhase::Manifest)return {Operation::ManifestRequest,digest_,{},offset_,manifest_size_,{}};
    if(phase_!=TransferPhase::Payloads)throw Error("No mod data request is authorized in the current admission phase.");
    const auto& payload=manifest().payloads.at(payload_);
    if(offset_>=payload.size)throw Error("The complete mod payload still requires hash verification.");
    return {Operation::PayloadRequest,digest_,payload.digest,offset_,static_cast<std::uint32_t>(payload.size),{}};
}
bool Download::accept_payload_chunk(const Message& message)const {
    validate(message);
    if(phase_!=TransferPhase::Payloads || message.operation!=Operation::PayloadChunk || message.manifest!=digest_ || message.offset!=offset_)return false;
    const auto& payload=manifest().payloads.at(payload_);return message.payload==payload.digest && message.total==payload.size;
}
void Download::persisted(std::size_t size) {
    if(phase_!=TransferPhase::Payloads || !size || size>MaxChunkBytes || size>manifest().payloads.at(payload_).size-offset_)
        throw Error("Invalid persisted online mod chunk acknowledgement.");
    offset_+=static_cast<std::uint32_t>(size);received_+=size;
}
void Download::payload_verified() {
    if(phase_!=TransferPhase::Payloads || offset_!=manifest().payloads.at(payload_).size)throw Error("An incomplete mod cannot be verified.");
    ++payload_;next_payload();
}
void Download::prepared(std::string_view digest) {
    if(phase_!=TransferPhase::Preparing || digest!=digest_)throw Error("Online mod preparation proof does not match this admission.");
    phase_=TransferPhase::Verified;
}
Upload::Upload(std::shared_ptr<const Bundle> bundle):bundle_(std::move(bundle)) {
    if(!bundle_)throw Error("Online mod upload has no immutable bundle.");
    metadata_=std::make_shared<const Bytes>(encode(bundle_->manifest));digest_=sha256(*metadata_);
    if(bundle_->payloads.size()!=bundle_->manifest.payloads.size())throw Error("Online mod bundle is incomplete.");
    for(const auto& payload:bundle_->manifest.payloads) {
        const auto found=bundle_->payloads.find(payload.digest);
        if(found==bundle_->payloads.end() || !found->second || found->second->size()!=payload.size)
            throw Error("Online mod bundle does not contain its declared payload.");
    }
}
void Upload::request(const Message& message,std::size_t chunk) {
    validate(message);
    if(message.manifest!=digest_ || !chunk || chunk>MaxChunkBytes || cancelled_)
        throw Error("Mod request does not belong to this frozen host selection.");
    switch(message.operation) {
    case Operation::Cancel:cancelled_=true;source_.reset();return;
    case Operation::Consent:consented_=true;return;
    case Operation::Preparing:
        if(!consented_)throw Error("Mods cannot be prepared before consent.");
        preparing_=true;source_.reset();return;
    case Operation::Verified:
        if(!preparing_)throw Error("Mod admission proof preceded local preparation.");return;
    case Operation::ManifestRequest:
        if(message.total!=metadata_->size())throw Error("Mod manifest request length is incorrect.");
        source_=metadata_;break;
    case Operation::PayloadRequest: {
        if(!consented_ || preparing_)throw Error("Mod payload download was not authorized.");
        const auto found=bundle_->payloads.find(message.payload);
        if(found==bundle_->payloads.end() || message.total!=found->second->size())
            throw Error("The requested payload is not in the host selection.");
        source_=found->second;break;
    }
    default:throw Error("Invalid online mod upload operation.");
    }
    payload_=message.payload;offset_=message.offset;chunk_=chunk;
    end_=static_cast<std::uint32_t>(offset_+(std::min)(source_->size()-offset_,std::size_t(64*1024)));
}
std::optional<Message> Upload::next()const {
    if(!source_ || offset_>=end_)return {};
    const auto size=(std::min)(chunk_,std::size_t(end_-offset_));
    return Message{payload_.empty()?Operation::ManifestChunk:Operation::PayloadChunk,digest_,payload_,offset_,
        static_cast<std::uint32_t>(source_->size()),Bytes(source_->begin()+offset_,source_->begin()+offset_+size)};
}
void Upload::sent() {
    if(!source_ || offset_>=end_)throw Error("No mod chunk was pending transmission.");
    offset_+=static_cast<std::uint32_t>((std::min)(chunk_,std::size_t(end_-offset_)));
}
}

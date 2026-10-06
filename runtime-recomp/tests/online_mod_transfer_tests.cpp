#include "online_mod_transfer.hpp"
#include "online_mod_cache.hpp"
#include "legacy_character_artifact.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

using namespace dkr::mods;
using namespace dkr::mods::online;
namespace {
unsigned checks=0;
void check(bool value){if(!value)throw Error("Mod transfer assertion "+std::to_string(checks));++checks;}
template<class F>void rejects(F function){bool rejected=false;try{function();}catch(const std::exception&){rejected=true;}check(rejected);}
Bytes bytes(std::string_view text){return {text.begin(),text.end()};}
Message response(const Message& request,const Bytes& source,unsigned chunk) {
    const auto size=std::min<std::size_t>(chunk,source.size()-request.offset);
    return {request.operation==Operation::ManifestRequest?Operation::ManifestChunk:Operation::PayloadChunk,
        request.manifest,request.payload,request.offset,request.total,Bytes(source.begin()+request.offset,source.begin()+request.offset+size)};
}
}
int main() {
    try {
        Manifest manifest;manifest.revision="us.v77";
        Bytes payload(25001);for(unsigned i=0;i<payload.size();++i)payload[i]=i%251;
        const auto hash=sha256(payload),id=sha256(bytes("character")),artifact=sha256(bytes("artifact"));
        manifest.payloads.push_back({PayloadKind::Xdelta,hash,"us.v77",payload.size(),"Example"});
        manifest.content.push_back({Kind::Character,id,"Example",hash,artifact,id,CharacterAdapterVersion});
        const auto metadata=encode(manifest);const auto digest=identity(manifest);
        Message status{Operation::Progress,digest,{},42,25001,Bytes{2,'D','o','w','n','l','o','a','d'}};
        check(decode_message(encode_message(status))==status);
        auto invalid_progress=status;invalid_progress.offset=25002;rejects([&]{encode_message(invalid_progress);});
        invalid_progress=status;invalid_progress.bytes[0]=5;rejects([&]{encode_message(invalid_progress);});
        invalid_progress=status;invalid_progress.bytes.push_back('\n');rejects([&]{encode_message(invalid_progress);});
        invalid_progress=status;invalid_progress.payload=hash;rejects([&]{encode_message(invalid_progress);});
        Download receiver;rejects([&]{receiver.consent();});rejects([&]{receiver.prepared(digest);});
        receiver.offer(digest,static_cast<std::uint32_t>(metadata.size()));
        receiver.offer(digest,static_cast<std::uint32_t>(metadata.size())); // idempotent retry
        rejects([&]{receiver.offer(hash,static_cast<std::uint32_t>(metadata.size()));});
        while(receiver.phase()==TransferPhase::Manifest) {
            auto request=receiver.request(768);check(decode_message(encode_message(request))==request);
            auto chunk=response(request,metadata,31);check(decode_message(encode_message(chunk))==chunk);
            const auto old=chunk;check(receiver.manifest_chunk(chunk));check(!receiver.manifest_chunk(old));
        }
        check(receiver.manifest()==canonical(manifest));check(receiver.phase()==TransferPhase::Consent);
        rejects([&]{receiver.request(768);}); // no bytes before user consent
        receiver.consent();rejects([&]{receiver.prepared(digest);});
        Bytes persisted;
        while(receiver.phase()==TransferPhase::Payloads) {
            auto request=receiver.request(768),chunk=response(request,payload,768);
            check(decode_message(encode_message(chunk))==chunk);check(receiver.accept_payload_chunk(chunk));
            auto wrong=chunk;wrong.manifest=hash;check(!receiver.accept_payload_chunk(wrong));
            wrong=chunk;wrong.payload=id;check(!receiver.accept_payload_chunk(wrong));
            persisted.insert(persisted.end(),chunk.bytes.begin(),chunk.bytes.end());receiver.persisted(chunk.bytes.size());
            check(!receiver.accept_payload_chunk(chunk));
            if(receiver.offset()==payload.size())receiver.payload_verified();
            else rejects([&]{receiver.payload_verified();});
        }
        check(persisted==payload);check(receiver.received_bytes()==payload.size());
        rejects([&]{receiver.prepared(hash);});receiver.prepared(digest);check(receiver.phase()==TransferPhase::Verified);
        rejects([&]{receiver.request(768);});receiver.cancel();check(receiver.phase()==TransferPhase::Cancelled);
        receiver.offer(digest,metadata.size());
        auto corrupt=response(receiver.request(768),metadata,MaxChunkBytes);corrupt.bytes[0]^=1;
        rejects([&]{receiver.manifest_chunk(corrupt);});check(receiver.phase()!=TransferPhase::Consent);
        for(std::size_t size=0;size<78;++size)rejects([&]{decode_message(Bytes(size,0));});
        auto packet=encode_message({Operation::Cancel,digest});packet.push_back(0);rejects([&]{decode_message(packet);});
        rejects([&]{encode_message({Operation::PayloadChunk,digest,hash,25000,25001,Bytes(2)});});
        rejects([&]{encode_message({Operation::PayloadChunk,digest,hash,0,25001,Bytes(MaxChunkBytes+1)});});
        rejects([&]{encode_message({Operation::Verified,digest,hash});});
        // Three recipients share immutable source bytes but have independent
        // consent/progress. Cancelling one cannot advance or cancel another.
        std::array<Download,3> peers;
        for(auto& peer:peers) {
            peer.offer(digest,metadata.size());check(peer.manifest_chunk(response(peer.request(768),metadata,MaxChunkBytes)));peer.consent();
        }
        peers[1].cancel();check(peers[0].phase()==TransferPhase::Payloads && peers[2].phase()==TransferPhase::Payloads);
        peers[0].cached(hash,payload.size());check(peers[0].phase()==TransferPhase::Preparing);check(peers[2].offset()==0);
        rejects([&]{peers[2].cached(id,payload.size());});
        auto bundle=std::make_shared<Bundle>();bundle->manifest=manifest;
        bundle->payloads.emplace(hash,std::make_shared<const Bytes>(payload));
        Upload upload(bundle);Download windowed;windowed.offer(digest,metadata.size());
        rejects([&]{upload.request({Operation::PayloadRequest,digest,hash,0,static_cast<std::uint32_t>(payload.size())},768);});
        while(windowed.phase()==TransferPhase::Manifest) {
            upload.request(windowed.request(768),768);
            while(auto chunk=upload.next()) {check(windowed.manifest_chunk(*chunk));upload.sent();}
        }
        windowed.consent();upload.request({Operation::Consent,digest},768);
        rejects([&]{upload.request({Operation::Verified,digest},768);});
        Bytes result;bool dropped=false;
        while(windowed.phase()==TransferPhase::Payloads) {
            upload.request(windowed.request(768),768);
            while(auto chunk=upload.next()) {
                upload.sent();
                if(!dropped && chunk->offset==768){dropped=true;continue;}
                if(!windowed.accept_payload_chunk(*chunk))continue;
                result.insert(result.end(),chunk->bytes.begin(),chunk->bytes.end());windowed.persisted(chunk->bytes.size());
                if(windowed.offset()==payload.size())windowed.payload_verified();
            }
        }
        check(result==payload && dropped);upload.request({Operation::Preparing,digest},768);
        upload.request({Operation::Verified,digest},768);check(upload.preparing()&&!upload.next());
        upload.request({Operation::Cancel,digest},768);check(upload.cancelled());
        rejects([&]{upload.request({Operation::ManifestRequest,digest,{},0,static_cast<std::uint32_t>(metadata.size())},768);});
        const auto cache=private_storage_path(std::filesystem::current_path()/("online-cache-check-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())));
        check(std::filesystem::create_directory(cache));std::filesystem::create_directory(cache/"partials");std::filesystem::create_directory(cache/"payloads");
        const auto descriptor=manifest.payloads.front();
        check(!cached_payload(cache/"payloads",descriptor));
        {
            PayloadFile partial(cache/"partials"/hash,descriptor);
            partial.append(View(payload).first(768));check(partial.size()==768);
            rejects([&]{partial.publish(cache/"payloads"/hash);});
        }
        {
            PayloadFile partial(cache/"partials"/hash,descriptor);check(partial.size()==768);
            Download resumed;resumed.offer(digest,metadata.size());check(resumed.manifest_chunk(response(resumed.request(768),metadata,MaxChunkBytes)));
            rejects([&]{resumed.resume_prefix(hash,768);});resumed.consent();resumed.resume_prefix(hash,partial.size());
            check(resumed.request(768).offset==768);
            while(partial.size()<payload.size()) {
                const auto chunk=response(resumed.request(768),payload,768);check(resumed.accept_payload_chunk(chunk));
                partial.append(chunk.bytes);resumed.persisted(chunk.bytes.size());
                if(partial.size()==payload.size()) {partial.publish(cache/"payloads"/hash);resumed.payload_verified();}
            }
            check(resumed.phase()==TransferPhase::Preparing && resumed.received_bytes()==payload.size());
        }
        check(cached_payload(cache/"payloads",descriptor));
        auto invalid=descriptor;invalid.size--;rejects([&]{cached_payload(cache/"payloads",invalid);});
        const auto corrupt_name=sha256(bytes("wrong expected bytes"));invalid=descriptor;invalid.digest=corrupt_name;
        {
            PayloadFile bad(cache/"partials"/corrupt_name,invalid);
            for(std::size_t at=0;at<payload.size();at+=768)bad.append(View(payload).subspan(at,std::min<std::size_t>(768,payload.size()-at)));
            rejects([&]{bad.publish(cache/"payloads"/corrupt_name);});
        }
        check(!std::filesystem::exists(cache/"payloads"/corrupt_name));
        std::filesystem::remove_all(cache); // exclusively created test fixture
        std::cout<<checks<<" bounded consent/range/retry/cache/resume/independent-recipient checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

#pragma once
#include "vehicle_protocol.hpp"
#include "vehicle_library.hpp"

namespace dkr::mods::online {
inline std::shared_ptr<const vehicles::Package> verified_car(const Manifest& m,const Content& c,View bytes) {
    const auto payload=std::find_if(m.payloads.begin(),m.payloads.end(),[&](const auto& p){return p.digest==c.payload;});
    if(c.kind!=CarContent||c.adapter!=CarAdapterVersion||payload==m.payloads.end()||payload->kind!=CarPayload||bytes.size()!=payload->size||sha256(bytes)!=c.payload)
        throw Error("A custom car does not match its exact host payload.");
    auto package=vehicles::inspect(bytes,payload->source_label,vehicles::Type::Car);
    if(package->digest!=c.payload||package->name!=c.name)throw Error("Custom car metadata differs from the host offer.");
    return package;
}
}

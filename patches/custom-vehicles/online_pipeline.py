"""Compose data-only custom-car transport over the accepted online system."""
from pipeline import once


def manifest_source(text):
    text = '#include "vehicle_protocol.hpp"\n' + text
    text = once(text, 'const char* kind(Kind value) {', 'const char* kind(Kind value) {\n    if(value==CarContent)return "car";')
    text = once(text, 'const char* payload_kind(PayloadKind value) {', 'const char* payload_kind(PayloadKind value) {\n    if(value==CarPayload)return "car";')
    text = once(text, 'p.kind==PayloadKind::Code?32*MiB:MaxStaged', '(p.kind==PayloadKind::Code||p.kind==CarPayload)?32*MiB:MaxStaged')
    text = once(text, 'std::size_t tracks=0,characters=0;', 'std::size_t tracks=0,characters=0,cars=0;')
    anchor = '        const auto expected=c.kind==Kind::Character?CharacterAdapterVersion:'
    text = once(text, anchor, '''        if(c.kind==CarContent) {
            const auto p=std::find_if(value.payloads.begin(),value.payloads.end(),[&](const auto& p){return p.digest==c.payload;});
            if(c.adapter!=CarAdapterVersion||c.artifact!=c.payload||c.bank!=c.payload||c.id!=c.payload||
               !c.behaviour.empty()||c.behaviour_abi||payloads.at(c.payload)!=CarPayload||p==value.payloads.end()||
               p->source_label.empty()||p->source_label.find_first_of("/\\\\:")!=std::string::npos||
               p->source_label.size()<9||p->source_label.substr(p->source_label.size()-8)!=".dkrkart")
                throw Error("This build cannot safely prepare the required custom car.");
            ++cars;used.insert(c.payload);continue;
        }
''' + anchor)
    text = once(text, 'if(tracks>MaxTracks || characters>MaxCharacters || used.size()!=payloads.size())', 'if(tracks>MaxTracks || characters>MaxCharacters || cars>16 || used.size()!=payloads.size())')
    text = once(text, 'tag!="xdelta" && tag!="tracklab"&&tag!="code"', 'tag!="xdelta" && tag!="tracklab"&&tag!="code"&&tag!="car"')
    text = once(text, 'tag=="xdelta"?PayloadKind::Xdelta:', 'tag=="car"?CarPayload:tag=="xdelta"?PayloadKind::Xdelta:')
    text = once(text, 'tag!="track" && tag!="character" && tag!="tracklab"', 'tag!="track" && tag!="character" && tag!="tracklab" && tag!="car"')
    text = once(text, 'tag=="track"?Kind::Track:', 'tag=="car"?CarContent:tag=="track"?Kind::Track:')
    return text


def profile_source(text):
    text = '#include "vehicle_online.hpp"\n' + text
    text = once(text, '    bundle.manifest=canonical(std::move(bundle.manifest));return bundle;', '''    auto cars=vehicles::Library(library.parent_path()/"vehicles").freeze(stop);
    for(const auto& car:cars.packages) {
        cancelled(stop);if(car->source.size()>MaxTransferBytes-total)throw Error("Custom cars exceed the online transfer budget.");
        auto label=car->source_name;label.replace(label.size()-8,8,".dkrkart");
        bundle.manifest.payloads.push_back({CarPayload,car->digest,{},car->source.size(),label});
        bundle.manifest.content.push_back({CarContent,car->digest,car->name,car->digest,car->digest,car->digest,CarAdapterVersion});
        if(!bundle.payloads.emplace(car->digest,std::make_shared<const Bytes>(car->source)).second)throw Error("A custom car payload is duplicated.");
        total+=car->source.size();
    }
    bundle.manifest=canonical(std::move(bundle.manifest));return bundle;''')
    text = once(text, 'refs["code"].size()>128', 'refs["code"].size()>144')
    text = once(text, '    if(found.size()!=expected.size())throw Error("Online profile is missing required host content.");', '''    // The existing bounded portable-blob map holds both executable code and
    // data-only cars. Only Manifest::code can ever grant code execution.
    for(const auto& c:manifest.content)if(c.kind==CarContent) {
        const auto at=sources.code.find(c.payload);if(at==sources.code.end())throw Error("A required custom car is missing.");
        check_storage(at->second,false);verified_car(manifest,c,read_file(at->second,vehicles::MaxPackageBytes));
        found.insert(std::make_pair(CarContent,c.id));
    }
    if(found.size()!=expected.size())throw Error("Online profile is missing required host content.");''')
    text = once(text, 'if(sources.code.size()!=manifest.code.size())', 'if(sources.code.size()!=manifest.code.size()+car_count(manifest))')
    text = once(text, '    if(profile.manifest.content.empty())return;', '''    vehicles::Library cars(library.parent_path()/"vehicles");
    for(const auto& c:profile.manifest.content)if(c.kind==CarContent) {
        cancelled(stop);const auto bytes=read_file(sources.code.at(c.payload),vehicles::MaxPackageBytes);
        const auto package=verified_car(profile.manifest,c,bytes);
        cars.import({package->source_name,bytes,vehicles::Type::Car},false);
    }
    if(profile.manifest.content.empty())return;''')
    text = once(text, '    // Authored Track Lab packages also have a canonical wire representation.', '''    if(!input_library.empty())for(const auto& p:manifest.payloads)if(p.kind==CarPayload&&!cached_payload(payloads,p)) {
        cancelled(stop);
        try {
            const auto file=input_library.parent_path()/"vehicles"/"packages"/(p.digest+".dkrkart");
            check_storage(file,false);const auto bytes=read_file(file,vehicles::MaxPackageBytes);
            if(bytes.size()!=p.size||sha256(bytes)!=p.digest)continue;
            try{write_new_file(payloads/p.digest,bytes);}catch(...){if(!cached_payload(payloads,p))throw;}
        }catch(const std::exception&){cancelled(stop);}
    }
    // Authored Track Lab packages also have a canonical wire representation.''')
    text = once(text, '        if(payload.kind==PayloadKind::Code) {', '''        if(payload.kind==CarPayload) {
            if(!cached_payload(payloads,payload))throw Error("A custom car failed its SHA/size check.");
            const auto wanted=std::find_if(out->manifest.content.begin(),out->manifest.content.end(),[&](const auto& c){return c.kind==CarContent&&c.payload==payload.digest;});
            if(wanted==out->manifest.content.end())throw Error("A custom car has no selected host identity.");
            verified_car(out->manifest,*wanted,read_file(payloads/payload.digest,vehicles::MaxPackageBytes));
            sources.code.emplace(payload.digest,payloads/payload.digest);++index;
            if(progress)progress("Verifying custom cars");continue;
        }
        if(payload.kind==PayloadKind::Code) {''')
    return text


def online_runtime_source(text):
    from music_pipeline import runtime_resources
    text = runtime_resources(text)
    text = '#include "vehicle_online.hpp"\n#include "vehicle_binding.hpp"\n' + text
    text = once(text, '    auto prepared=out->new_session();', '''    std::vector<std::shared_ptr<const vehicles::Package>> cars;
    for(const auto& c:out->profile_->manifest.content)if(c.kind==CarContent) {
        cancelled(stop);cars.push_back(verified_car(out->profile_->manifest,c,read_file(sources.code.at(c.payload),vehicles::MaxPackageBytes)));
    }
    if(!cars.empty()) {
        auto resources=vehicles::prepare_resources(out->stock_,AssetBank::append_courses(boot,out->additions_),out->characters_,vehicles::freeze_packages(std::move(cars)));
        vehicles::bind_resources(out,resources);
    }
    auto prepared=out->new_session();''')
    text = once(text, '    if(out->code_)identity+="\\ncode:"+out->code_->identity;', '    if(out->code_)identity+="\\ncode:"+out->code_->identity;\n    if(const auto cars=vehicles::resources_for(out.get()))identity+="\\ncars:"+cars->identity;')
    text = once(text, '    if(out->code_)for(const auto& package:out->code_->ordered) {', '''    if(const auto cars=vehicles::resources_for(out.get())) {
        for(const auto& car:cars->library.packages) {
            cache_bytes+=car->source.size();
            for(const auto& lod:car->lods)for(const auto& mesh:lod.meshes)
                cache_bytes+=mesh.vertices.size()*sizeof(vehicles::Vec3)+mesh.uv.size()*sizeof(vehicles::Vec2)+mesh.faces.size()*sizeof(std::array<unsigned,3>);
            for(const auto& t:car->textures)cache_bytes+=t.image.rgba.size();
        }
    }
    if(out->code_)for(const auto& package:out->code_->ordered) {''')
    text = once(text, '    if(prepared_world_)return prepared_world_->instantiate();', '''    if(prepared_world_) {
        auto copy=prepared_world_->instantiate();vehicles::inherit_resources(prepared_world_.get(),copy);return copy;
    }
    const auto cars=vehicles::resources_for(this);''')
    text = once(text, '        [additions,counts](std::shared_ptr<const AssetBank> bank) {', '        [additions,counts,cars](std::shared_ptr<const AssetBank> bank) {')
    text = once(text, '            return AssetBank::append_courses(std::move(bank),*additions);', '            auto next=AssetBank::append_courses(std::move(bank),*additions);return cars?cars->apply(std::move(next)):next;')
    text = once(text, '    for(const auto& track:tracks_)result->admit(track);', '    vehicles::bind_resources(result,cars);\n    for(const auto& track:tracks_)result->admit(track);')
    return text

#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace dkr::mods {
// Check BEFORE any retail dereference. Do not mask a bad pointer into RDRAM
// or "repair" an instance whose allocation may already belong to somebody else.
struct ModelReferenceCheck {
    const char* failure=nullptr;
    std::uint32_t object=0,header=0,slots=0,count=0,index=0,instance=0,model=0,normals=0;
    bool no_shading=false;
};
class ModelReferenceMemory {
    std::span<const std::uint8_t> memory_;
public:
    explicit ModelReferenceMemory(std::span<const std::uint8_t> memory):memory_(memory){}
    bool range(std::uint32_t address,std::size_t bytes,unsigned alignment=4)const {
        const auto offset=std::size_t(address&0x1fffffffU);
        return (address&0xe0000000U)==0x80000000U && !(address&(alignment-1)) &&
            memory_.size()%4==0 && offset<=memory_.size() && bytes<=memory_.size()-offset;
    }
    std::uint32_t read(std::uint32_t address,unsigned bytes=4)const {
        std::uint32_t result=0;const auto offset=address&0x1fffffffU;
        for(unsigned i=0;i<bytes;++i)result=(result<<8)|memory_[(offset+i)^3];
        return result;
    }
};
inline void check_model_reference(const ModelReferenceMemory& g,ModelReferenceCheck& out) {
    if(!g.range(out.model,0x58)){out.failure="model pointer";return;}
    out.normals=g.read(out.model+0x40);
    const auto vertices=g.read(out.model+0x24,2);
    if(vertices>=32768){out.failure="model vertex count";return;}
    if(vertices && !g.range(g.read(out.model+4),std::size_t(vertices)*10,2)){
        out.failure="model vertex array";return;
    }
    // Normals cover only dynamically lit batches, not every model vertex.
    // Check the pointer, not a guessed full-vertex allocation length.
    if(out.normals && !g.range(out.normals,6,2))
        out.failure="model normals array";
}
inline ModelReferenceCheck check_model_instance(std::span<const std::uint8_t> memory,std::uint32_t instance) {
    ModelReferenceMemory g(memory);ModelReferenceCheck out;out.instance=instance;
    if(!g.range(instance,0x24)){out.failure="model instance pointer";return out;}
    out.model=g.read(instance);check_model_reference(g,out);return out;
}
inline ModelReferenceCheck check_object_models(std::span<const std::uint8_t> memory,
    std::uint32_t object,bool shading_only) {
    ModelReferenceMemory g(memory);ModelReferenceCheck out;out.object=object;
    if(!g.range(object,0x80)){out.failure="object pointer";return out;}
    out.header=g.read(object+0x40);
    if(!g.range(out.header,0x78)){out.failure="object header pointer";return out;}
    if(g.read(out.header+0x53,1)!=0)return out; // Sprite/misc paths stay retail.
    out.count=g.read(out.header+0x55,1);out.slots=g.read(object+0x68);
    if(out.count>=128 || !g.range(out.slots,4*out.count)){
        out.failure="model slot array";return out;
    }
    for(out.index=0;out.index<out.count;++out.index){
        out.instance=g.read(out.slots+4*out.index);
        if(!out.instance)continue; // Sparse character-selected arrays are normal.
        auto check=check_model_instance(memory,out.instance);
        out.model=check.model;out.normals=check.normals;out.failure=check.failure;
        if(out.failure || shading_only)return out;
    }
    // Retail scans unboundedly when every entry is NULL. Its normal unshaded
    // return is four bytes, not zero, and must clear the shading pointer.
    out.no_shading=shading_only;return out;
}
inline ModelReferenceCheck check_model_operation(std::span<const std::uint8_t> memory,
    unsigned operation,std::uint32_t address) {
    if(operation<2)return check_object_models(memory,address,operation==0);
    if(operation==3)return check_model_instance(memory,address);
    ModelReferenceCheck out;
    if(operation==2){out.model=address;check_model_reference(ModelReferenceMemory(memory),out);}
    else out.failure="model safety operation";
    return out;
}
}

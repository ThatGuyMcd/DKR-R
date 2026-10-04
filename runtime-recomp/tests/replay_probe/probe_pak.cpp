#include "probe_pak.h"
#include "netplay/experimental_pak.hpp"
#include <algorithm>
namespace { dkr::runtime::netplay::experimental::Paks* owned=nullptr; }
extern "C" void dkr_probe_bind_paks(void* paks) { owned=static_cast<dkr::runtime::netplay::experimental::Paks*>(paks); }
extern "C" int dkr_probe_paks_enabled(void) { return owned!=nullptr; }
extern "C" unsigned dkr_probe_pak_mask(void) { return owned?owned->port_mask():0; }
extern "C" int dkr_probe_pak_request(unsigned op,unsigned port,uint32_t a[4],uint8_t* bytes,unsigned count) {
    using Paks=dkr::runtime::netplay::experimental::Paks;
    try {
        if(!owned)return Paks::OwnerFailure;
        Paks::Identity identity;
        if(op==DKR_PAK_FIND || op==DKR_PAK_ALLOCATE || op==DKR_PAK_ERASE) {
            if(count!=20)return Paks::OwnerFailure;
            identity.company=std::uint16_t(a[0]);identity.game=a[1];
            std::copy_n(bytes,16,identity.name.begin());std::copy_n(bytes+16,4,identity.extension.begin());
        }
        unsigned index=0;int status;
        switch(op) {
        case DKR_PAK_STATUS:return owned->status(port);
        case DKR_PAK_FREE:return owned->free_bytes(port,a[0]);
        case DKR_PAK_COUNT:return owned->num_files(port,a[0]);
        case DKR_PAK_FIND:status=owned->find(port,identity,index);if(status==Paks::Ok)a[0]=index;return status;
        case DKR_PAK_STATE: {
            if(count!=20)return Paks::OwnerFailure;
            Paks::FileState state;status=owned->file_state(port,a[0],state);
            if(status==Paks::Ok) {
                a[0]=state.bytes;a[1]=state.identity.game;a[2]=state.identity.company;
                std::copy(state.identity.extension.begin(),state.identity.extension.end(),bytes);
                std::copy(state.identity.name.begin(),state.identity.name.end(),bytes+4);
            }return status;
        }
        case DKR_PAK_READ:return owned->read(port,a[0],a[1],{bytes,count});
        case DKR_PAK_ALLOCATE:status=owned->allocate(port,identity,a[2],index);if(status==Paks::Ok)a[0]=index;return status;
        case DKR_PAK_ERASE:return owned->erase(port,identity);
        case DKR_PAK_WRITE:return owned->write(port,a[0],a[1],{bytes,count});
        case DKR_PAK_FORMAT:return owned->reformat(port);
        default:return Paks::OwnerFailure;
        }
    } catch(...) {return Paks::OwnerFailure;}
}

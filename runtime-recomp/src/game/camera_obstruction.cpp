#include "recomp.h"
#include "camera_obstruction_guest.h"
#include "revision_addresses.hpp"

extern "C" void dkr_resolve_follow_camera(std::uint8_t* rdram,
    recomp_context*,std::uint32_t object) {
    using namespace dkr::runtime;
    const bool rev_a=revision_addresses::selected_revision()==rom::Revision::UsV80;
    if(!rev_a&&revision_addresses::selected_revision()!=rom::Revision::UsV77)return;
    const std::uint32_t fields[8]={revision_addresses::GameMode,
        revision_addresses::CurrentLevelHeader,revision_addresses::ViewportLayout,
        revision_addresses::CutsceneCameraActive,rev_a?0x800DCE88U:0x800DC918U,
        rev_a?0x8011DA88U:0x8011D508U,rev_a?0x8011DB06U:0x8011D586U,
        rev_a?0x8011DADCU:0x8011D55CU};
    dkr_cam_resolve(rdram,object,fields);
}

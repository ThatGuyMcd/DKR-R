#include "probe_bridge.h"
#include "netplay/experimental_eeprom.hpp"
namespace { dkr::runtime::netplay::experimental::Eeprom* owned=nullptr; }
extern "C" void dkr_probe_bind_eeprom(void* save) { owned=static_cast<dkr::runtime::netplay::experimental::Eeprom*>(save); }
extern "C" int dkr_probe_eeprom_enabled(void) {
    std::array<std::uint8_t,8> bytes{}; return owned && owned->read(0,bytes);
}
extern "C" int dkr_probe_eeprom_transfer(int write,unsigned offset,uint8_t* bytes,unsigned count) {
    // Return to the C trampoline BEFORE it raises a probe trap: no C++ stack
    // objects/destructors can be crossed by the C guest's longjmp boundary.
    try {
        if(!owned) return 0;
        return write ? owned->write(offset,{bytes,count}) : owned->read(offset,{bytes,count});
    } catch(...) { return 0; }
}

#pragma once
#include <cstdint>
#include <span>
#include <string>
namespace RT64 { struct Application; }
namespace dkr::store::overlay {
struct View { void* id=nullptr; float aspect=1.8F; };
void begin(RT64::Application&);
void end();
View image(const std::string&,unsigned width,unsigned height,std::span<const unsigned char> pixels);
void completed(); // Called after the existing present-worker fence; never waits.
void release();   // Same Inspector/device teardown boundary as CRT resources.
}

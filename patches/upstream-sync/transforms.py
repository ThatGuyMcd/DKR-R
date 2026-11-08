"""Copy only reviewed upstream regions into the accepted staged sources."""
def once(text,before,after):
 if text.count(before)!=1:raise ValueError('Upstream sync anchor changed: '+before[:120])
 return text.replace(before,after,1)

def section(text,start,end):
 if text.count(start)!=1:raise ValueError('Upstream section is ambiguous: '+start)
 a=text.index(start);return text[a:text.index(end,a)]

def tracks(text,upstream):
 start='// A zip without a *.dkrmap folder installs under its manifest id'
 helper=section(upstream,start,'bool install(')
 text=once(text,'bool install(const std::filesystem::path& source, std::string& error,',
           helper+'bool install(const std::filesystem::path& source, std::string& error,')
 anchor="    // A zip's inner folder can be named anything; fall back to the manifest id.\n"
 guard=section(upstream,anchor,'    const std::string folder_name =')
 text=once(text,anchor,guard)
 start='    const std::filesystem::path destination = destination_root / folder_name;'
 finish='    if (outcome != nullptr) {'
 return once(text,section(text,start,finish),section(upstream,start,finish))

def graphics(text,upstream):
 start='    SettingsCard(width, [&](float inner) {\n        SettingsCardHead("Performance presets",'
 finish='    PaddockGap(18.0F);\n    SettingsCard(width, [&](float inner) {\n        SettingsCardHead("Game display",'
 return once(text,section(text,start,finish),section(upstream,start,finish))

def preset_header(text,upstream):
 text=once(text,'#include "ultramodern/config.hpp"\n','#include "ultramodern/config.hpp"\n#include <optional>\n')
 finish='// Explicit opt-in only.'
 helpers=section(upstream,'// Applying a preset also sets texture filtering.',finish)
 return once(text,finish,helpers+finish)

def dispatch_header(text,upstream):
 start='#ifdef __cplusplus\nextern "C" {\n';end='#elif defined(_MSC_VER)'
 return once(text,section(text,start,end),section(upstream,start,end))

def code_mods(text,upstream,header):
 # Include the matching declaration before any frozen transitive header can
 # claim the same include guard with Darwin's old C++ TLS declaration.
 text='#include "'+header.as_posix()+'"\n'+text
 old='extern "C" {thread_local int dkr_code_callbacks_enabled=0;thread_local const std::uint8_t* dkr_code_hook_mask=nullptr;}\n'
 start='#if defined(__APPLE__)\n// Must match magic_code_dispatch.h:'
 new=section(upstream,start,'namespace dkr::runtime::code_mods {')
 return once(text,old,new)

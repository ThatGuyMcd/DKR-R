#pragma once
#include <nfd.h>
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
namespace dkr::file_picker {
nfdresult_t init();
void quit();
const char* error();
void release(nfdu8char_t*);
nfdresult_t open(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*);
nfdresult_t save(nfdu8char_t**, const nfdu8filteritem_t*, nfdfiltersize_t, const nfdu8char_t*, const nfdu8char_t*);
nfdresult_t folder(nfdu8char_t**, const nfdu8char_t*);
} // namespace dkr::file_picker
// Only this first-party UI translation unit uses the client boundary. The
// helper links the unmodified native NFD implementation with its own lifetime.
#define NFD_Init dkr::file_picker::init
#define NFD_Quit dkr::file_picker::quit
#define NFD_GetError dkr::file_picker::error
#define NFD_FreePathU8 dkr::file_picker::release
#define NFD_OpenDialogU8 dkr::file_picker::open
#define NFD_SaveDialogU8 dkr::file_picker::save
#define NFD_PickFolderU8 dkr::file_picker::folder
#endif

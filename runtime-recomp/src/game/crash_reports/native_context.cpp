#include "native_context.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <link.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace dkr::crash_reports {
#ifdef _WIN32
void print_native_frame(std::uintptr_t pc, unsigned frame) {
    const auto process = GetCurrentProcess();
    const auto base = SymGetModuleBase64(process, pc);
    IMAGEHLP_MODULE64 info{};
    info.SizeOfStruct = sizeof(info);
    const bool known = base && SymGetModuleInfo64(process, pc, &info);
    // ModuleName is a basename owned by DbgHelp, not LoadedImageName/PDB paths.
    std::fprintf(stderr,
                 "[boot][crash] frame=%u pc=0x%llX module=%s base=0x%llX rva=0x%llX timestamp=%08lX image-size=%08lX\n",
                 frame, static_cast<unsigned long long>(pc), known ? info.ModuleName : "unknown",
                 static_cast<unsigned long long>(base), static_cast<unsigned long long>(base ? pc - base : 0),
                 known ? info.TimeDateStamp : 0UL, known ? info.ImageSize : 0UL);
}
void refresh_native_context(bool log_binary) {
    if (!log_binary)
        return;
    const auto module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 pe{};
    SIZE_T read = 0;
    const auto process = GetCurrentProcess();
    if (ReadProcessMemory(process, reinterpret_cast<void*>(module), &dos, sizeof(dos), &read) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 && dos.e_lfanew < 1024 * 1024 &&
        ReadProcessMemory(process, reinterpret_cast<void*>(module + dos.e_lfanew), &pe, sizeof(pe), &read) &&
        pe.Signature == IMAGE_NT_SIGNATURE) {
        char id[65]{};
        std::snprintf(id, sizeof(id), "%08lX%08lX", pe.FileHeader.TimeDateStamp, pe.OptionalHeader.SizeOfImage);
        const auto directory = pe.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
        if (directory.VirtualAddress && directory.Size <= 4096)
            for (unsigned at = 0; at + sizeof(IMAGE_DEBUG_DIRECTORY) <= directory.Size;
                 at += sizeof(IMAGE_DEBUG_DIRECTORY)) {
                IMAGE_DEBUG_DIRECTORY debug{};
                struct CodeView {
                    DWORD signature;
                    GUID guid;
                    DWORD age;
                } code{};
                if (!ReadProcessMemory(process, reinterpret_cast<void*>(module + directory.VirtualAddress + at), &debug,
                                       sizeof(debug), &read) ||
                    debug.Type != IMAGE_DEBUG_TYPE_CODEVIEW || debug.SizeOfData < sizeof(code))
                    continue;
                if (ReadProcessMemory(process, reinterpret_cast<void*>(module + debug.AddressOfRawData), &code,
                                      sizeof(code), &read) &&
                    code.signature == 0x53445352) {
                    const auto* guid = reinterpret_cast<const unsigned char*>(&code.guid);
                    for (unsigned i = 0; i < 16; ++i)
                        std::snprintf(id + 16 + i * 2, 3, "%02X", guid[i]);
                    std::snprintf(id + 48, sizeof(id) - 48, "%08lX", code.age);
                    break;
                }
            }
        std::fprintf(stderr, "[boot][binary] id=%s format=PE\n", id);
    }
}
void fatal_signal(int, void*, void*) {}
#elif defined(__linux__) && !defined(__ANDROID__)
namespace {
struct Module {
    std::uintptr_t low = 0, high = 0, base = 0;
    char id[65]{};
};
struct Modules {
    std::array<Module, 256> values{};
    unsigned count = 0;
};
std::atomic<const Modules*> published{nullptr};
static_assert(decltype(published)::is_always_lock_free);
// Never recycle a published snapshot: a fault on another thread can read one
// without a lock, even while normal startup publishes a newer module set.
std::mutex owners_mutex;
std::vector<std::unique_ptr<Modules>> owners;
int collect(dl_phdr_info* info, size_t, void* opaque) {
    auto& list = *static_cast<Modules*>(opaque);
    if (list.count == list.values.size())
        return 1;
    Module m;
    m.base = info->dlpi_addr;
    m.low = ~std::uintptr_t{};
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        if (ph.p_type == PT_LOAD) {
            m.low = std::min(m.low, m.base + ph.p_vaddr);
            m.high = std::max(m.high, m.base + ph.p_vaddr + ph.p_memsz);
        }
        if (ph.p_type != PT_NOTE || ph.p_memsz > 1024 * 1024)
            continue;
        auto* bytes = reinterpret_cast<const unsigned char*>(m.base + ph.p_vaddr);
        size_t at = 0;
        while (at + sizeof(ElfW(Nhdr)) <= ph.p_memsz) {
            ElfW(Nhdr) note;
            std::memcpy(&note, bytes + at, sizeof(note));
            at += sizeof(note);
            const auto name = (size_t(note.n_namesz) + 3) & ~size_t(3), desc = (size_t(note.n_descsz) + 3) & ~size_t(3);
            if (name > ph.p_memsz - at || desc > ph.p_memsz - at - name)
                break;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 && !std::memcmp(bytes + at, "GNU", 4)) {
                const auto n = std::min<size_t>(note.n_descsz, 32);
                for (size_t b = 0; b < n; ++b) {
                    const auto v = bytes[at + name + b];
                    m.id[b * 2] = digits[v >> 4];
                    m.id[b * 2 + 1] = digits[v & 15];
                }
            }
            at += name + desc;
        }
    }
    if (m.low < m.high)
        list.values[list.count++] = m;
    return 0;
}
struct Line {
    char data[512]{};
    size_t size = 0;
    void text(const char* s) {
        while (*s && size < sizeof(data) - 1)
            data[size++] = *s++;
    }
    void number(std::uintptr_t v, unsigned radix = 16) {
        char tmp[32];
        unsigned n = 0;
        do {
            tmp[n++] = "0123456789abcdef"[v % radix];
            v /= radix;
        } while (v && n < sizeof(tmp));
        while (n && size < sizeof(data) - 1)
            data[size++] = tmp[--n];
    }
    void emit() {
        text("\n");
        const char* p = data;
        auto left = size;
        while (left) {
            const auto n = write(STDERR_FILENO, p, left);
            if (n <= 0)
                break;
            p += n;
            left -= n;
        }
    }
};
void frame_line(std::uintptr_t pc, unsigned frame) {
    Line line;
    line.text("[boot][crash] frame=");
    line.number(frame, 10);
    line.text(" pc=0x");
    line.number(pc);
    const auto* list = published.load(std::memory_order_acquire);
    const Module* module = nullptr;
    if (list)
        for (unsigned i = 0; i < list->count; ++i)
            if (pc >= list->values[i].low && pc < list->values[i].high) {
                module = &list->values[i];
                break;
            }
    if (module) {
        line.text(" module-id=");
        line.text(module->id[0] ? module->id : "unavailable");
        line.text(" base=0x");
        line.number(module->base);
        line.text(" rva=0x");
        line.number(pc - module->base);
    } else
        line.text(" module-id=unknown");
    line.emit();
}
} // namespace
void refresh_native_context(bool log_binary) {
    std::lock_guard lock(owners_mutex);
    // Startup and renderer transitions are bounded; avoid growing on reloads.
    if (owners.size() >= 16)
        return;
    auto next = std::make_unique<Modules>();
    dl_iterate_phdr(collect, next.get());
    if (log_binary && next->count && next->values[0].id[0])
        std::fprintf(stderr, "[boot][binary] id=%s format=ELF\n", next->values[0].id);
    const auto* value = next.get();
    owners.push_back(std::move(next));
    published.store(value, std::memory_order_release);
}
void print_native_frame(std::uintptr_t pc, unsigned frame) {
    frame_line(pc, frame);
}
void fatal_signal(int signal, void* information, void* context) {
    Line line;
    line.text("[boot][crash] signal=");
    line.number(signal, 10);
    if (information) {
        line.text(" memory-address=0x");
        line.number(reinterpret_cast<std::uintptr_t>(static_cast<siginfo_t*>(information)->si_addr));
    }
    line.text(" thread=");
    line.number(syscall(SYS_gettid), 10);
    line.emit();
    std::uintptr_t pc = 0, sp = 0;
    if (context) {
        const auto& c = *static_cast<ucontext_t*>(context);
#if defined(__x86_64__)
        pc = c.uc_mcontext.gregs[REG_RIP];
        sp = c.uc_mcontext.gregs[REG_RSP];
#elif defined(__aarch64__)
        pc = c.uc_mcontext.pc;
        sp = c.uc_mcontext.sp;
#endif
    }
    frame_line(pc, 0);
    Line registers;
    registers.text("[boot][crash] sp=0x");
    registers.number(sp);
    registers.emit();
    _exit(128 + signal);
}
#else
void refresh_native_context(bool) {}
void print_native_frame(std::uintptr_t, unsigned) {}
void fatal_signal(int, void*, void*) {}
#endif
} // namespace dkr::crash_reports

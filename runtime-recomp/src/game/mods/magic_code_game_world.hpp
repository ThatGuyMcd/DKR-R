#pragma once
#include "magic_code_library.hpp"
#include "magic_code_world.hpp"
#include "magic_code_dispatch.h"
#include "magic_code_trophy_catalog.hpp"
#include "recomp.h"
#include <mutex>
#include "sdk2_bridge.hpp"

namespace dkr::mods::code {
using GuestFunction = void (*)(std::uint8_t*, recomp_context*);
using DispatchEntry = dkr_code_dispatch_entry;
// Addresses and pointers are generated from the ACTUAL compiled namespace.
// Never resolve a private world's call through librecomp's global overlay map.
struct DispatchTable {
    std::span<const DispatchEntry> entries;
};
class GameWorld {
  public:
    using Invoke = std::function<void(GuestFunction, std::uint8_t*, recomp_context*)>;
    GameWorld(const FrozenSet&, std::string_view revision, DispatchTable, std::uint64_t seed, bool online,
              Invoke invoke = {});
    static LinkPolicy policy(DispatchTable = {}, bool online = false);
    void course_selector(std::function<void(const std::string&, unsigned)> selector) {
        course_selector_ = std::move(selector);
    }
    void trophy_selector(std::function<std::string()> selector) {
        trophy_selector_ = std::move(selector);
    }
    void attach(std::span<std::uint8_t> ram, bool install);
    void initialize();
    void event(unsigned, const Registers& arguments = {});
    int dispatch_event(unsigned, std::uint8_t*, recomp_context*) noexcept;
    int indirect(std::uint32_t, std::uint8_t*, recomp_context*, std::uint64_t& result) noexcept;
    int lap_address(std::uint32_t, std::uint64_t& result) noexcept;
    // Called by the C-only private dispatcher, or the ordinary guest wrapper.
    // Errors return AFTER all C++ locks/leases have unwound.
    int dispatch(bool leaving, std::uint32_t address, std::uint8_t*, recomp_context*, std::uint64_t& result) noexcept;
    unsigned sdk2_pulse();
    void sdk2_reload();
    int sdk2_dispatch_pulse() noexcept;
    bool sdk2_frame_starts_at(std::uint32_t) const;
    void sdk2_command(const dkr::sdk2::Command&);
    void sdk2_development(bool value) {
        sdk2_development_ = value;
    }
    void sdk2_frame_agreed(bool value) {
        sdk2_frame_agreed_ = value;
    }
    void sdk2_current_course(std::function<std::string()> query) {
        sdk2_current_course_ = std::move(query);
    }
    void sdk2_assets(const std::string& value) {
        if (!value.empty())
            sdk2_assets_ =
                parse_document(View(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()), 2 * MiB);
    }
    bool sdk2_wants_assets() const {
        for (const auto& o : linked().owners)
            if (o.package->api == 2 && o.package->modules.contains("catalogues2"))
                return true;
        return false;
    }
    const std::string& error() const {
        return error_;
    }
    Bytes checkpoint() const;
    // Quiesced native-to-owned handoff only, while the supplied RAM is alive.
    // Retires the old binding even if checkpoint validation fails.
    Bytes retire_checkpoint(std::span<std::uint8_t> live_ram);
    std::shared_ptr<World::Restore> stage(View) const;
    void commit(std::shared_ptr<World::Restore>);
    std::vector<Effect> effects();
    bool frame_ends_at(std::uint32_t) const;
    const LinkedSet& linked() const {
        return world_.linked();
    }
    const std::uint8_t* hook_mask() const {
        return hook_mask_.data();
    }
    std::size_t required_ram() const {
        return 16 * MiB + linked().resident_bytes;
    }
    std::size_t checkpoint_limit() const;

  private:
    bool sdk2_enabled() const;
    void sdk2_boundary(unsigned);
    void sdk2_inspector();
    void sdk2_lifetime(std::string_view, bool, const Registers&);
    void sdk2_emit(Memory&, std::uint32_t, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
    bool sdk2_service(std::string_view, std::size_t, Memory&, Registers&, Budget&);
    bool sdk2_operation_available(unsigned) const;
    int sdk2_operation(unsigned, std::size_t, Memory&, const std::array<std::uint32_t, 16>&,
                       std::array<std::uint32_t, 16>&, Budget&);
    int sdk2_cast(Memory&, std::uint32_t, std::uint32_t, Budget&);
    int sdk2_track_catalogue(Memory&, std::string_view, unsigned, Json&, Budget&);
    int sdk2_control(Memory&, std::size_t, unsigned, unsigned);
    int sdk2_tune(Memory&, std::size_t, unsigned);
    int sdk2_setting_apply(Memory&, std::size_t, unsigned, unsigned, std::uint32_t, Budget&);
    void sdk2_vehicle(std::string_view, bool, const Registers&);
    void sdk2_apply_overrides(std::size_t, unsigned, bool);
    void sdk2_shading(std::string_view, bool, const Registers&);
    bool sdk2_freeze(std::string_view) const;
    void sdk2_native_return(std::string_view, Registers&);
    int sdk2_document(std::size_t, Memory&, std::string_view, Registers&, Budget&);
    bool sdk2_document_command(const dkr::sdk2::Command&, std::size_t, Memory&);
    std::string sdk2_course_identity() const;
    unsigned sdk2_phase_ = 1;
    bool sdk2_command_ = false;
    bool sdk2_development_ = false;
    bool sdk2_active_ = false;
    bool sdk2_pulsing_ = false;
    bool sdk2_frame_agreed_ = false;
    unsigned sdk2_inspector_serial_ = 0;
    Budget sdk2_budget_{};
    std::map<std::string, unsigned> sdk2_modules_;
    std::vector<trophy::Course> sdk2_courses_;
    Json sdk2_inventory_ = Json::object(), sdk2_cups_ = Json::array();
    Json sdk2_assets_ = Json::object();
    std::function<std::string()> sdk2_current_course_;
    static Registers read(const recomp_context&);
    static void write(recomp_context&, const Registers&);
    DispatchTable table_;
    Invoke invoke_;
    bool online_ = false;
    std::span<std::uint8_t> ram_;
    std::unique_ptr<Memory> native_bus_;
    mutable std::recursive_mutex mutex_;
    Budget budget_;
    unsigned nested_ = 0;
    unsigned native_depth_ = 0;
    struct CallbackContext {
        Registers* registers;
        bool entry, skip = false;
        std::uint32_t address;
        bool repeat = false;
    };
    CallbackContext* callback_ = nullptr;
    std::map<std::uint32_t, std::pair<std::string, std::string>> named_events_;
    std::map<std::size_t, std::vector<trophy::Frozen>> cups_;
    std::vector<std::uint8_t> hook_mask_;
    std::function<void(const std::string&, unsigned)> course_selector_;
    std::function<std::string()> trophy_selector_;
    World world_;
    std::string error_;
    void native(std::uint32_t, Registers&, Budget&, bool curated = false);
    bool service(std::string_view, std::size_t, Memory&, Registers&, Budget&);
    std::uint32_t symbol(std::string_view) const;
};
} // namespace dkr::mods::code

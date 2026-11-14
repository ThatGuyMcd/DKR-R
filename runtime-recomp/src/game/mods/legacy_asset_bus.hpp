#pragma once
#include "legacy_asset_directory.hpp"
#include <map>
#include <mutex>
#include <optional>

namespace dkr::mods {
// Guest ROM-offset namespace, intercepted BEFORE librecomp's physical-address
// mask. It is not an expanded or mutable cartridge. No production hook is
// installed by this class; every DMA call site must route through the adapter.
class AssetBus {
  public:
    static constexpr std::uint32_t Begin = 0x40000000U, End = 0x70000000U;
    static constexpr unsigned MaxBanks = 128;
    class Mount {
      public:
        std::uint32_t address(unsigned section, std::size_t offset = 0) const;
        const std::shared_ptr<const AssetDirectory>& directory() const {
            return directory_;
        }

      private:
        friend class AssetBus;
        std::shared_ptr<const AssetDirectory> directory_;
        std::array<std::uint32_t, 50> bases_{};
    };
    struct Read {
        std::shared_ptr<const Mount> mount;
        unsigned section = 0;
        std::size_t offset = 0, length = 0;
        Bytes bytes() const;
        void copy_to_guest(std::span<std::uint8_t> rdram, std::uint32_t address) const;
    };
    // The scene, resident caches and audio must hold Mounts for their entire
    // lifetime. A resolved Read pins its owner even after a scene retires.
    std::shared_ptr<const Mount> mount(std::shared_ptr<const AssetDirectory> directory);
    // Only nonvirtual addresses return nullopt. Stale, unknown, overflowing or
    // cross-section virtual reads throw: NEVER fall through to cartridge DMA.
    std::optional<Read> resolve(std::uint32_t address, std::size_t length) const;
    // Reserve the entire tagged region, including the unallocated upper
    // guard. A bad virtual pointer must never become a stock cartridge read.
    static bool is_virtual(std::uint32_t address) {
        return (address & 0xc0000000U) == Begin;
    }

  private:
    struct Bank {
        std::array<std::uint32_t, 50> bases{};
        std::array<std::size_t, 50> sizes{};
        std::weak_ptr<const Mount> owner;
    };
    struct Range {
        std::string fingerprint;
        unsigned section;
    };
    mutable std::mutex mutex_;
    std::map<std::string, Bank> banks_;
    std::map<std::uint32_t, Range> ranges_;
    std::uint32_t next_ = Begin;
    // Addresses are never reused for different fingerprints. Keep the tiny
    // tombstones until game shutdown; don't reset this bus between scenes.
};
} // namespace dkr::mods

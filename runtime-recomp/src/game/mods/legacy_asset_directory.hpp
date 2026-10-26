#pragma once
#include "legacy_asset_bank.hpp"

namespace dkr::mods {
// A coherent, immutable view of the asset APIs, without a mutable ROM swap.
// It does not install itself in the runtime or retire resident game caches.
// Addresses retain this directory, so a late read cannot silently resolve
// through whichever mod the user happened to select more recently.
class AssetDirectory : public std::enable_shared_from_this<AssetDirectory> {
  public:
    struct Address {
        std::shared_ptr<const AssetDirectory> owner;
        unsigned section = 0;
        std::size_t offset = 0;
        Bytes read(std::size_t length) const;
    };
    static std::shared_ptr<const AssetDirectory> build(std::shared_ptr<const AssetBank> bank);
    std::size_t section_size(unsigned section) const;
    Bytes read(unsigned section, std::size_t offset, std::size_t length) const;
    // No temporary allocation on the DMA path. Guest memory is byte-swapped
    // within each 32-bit word, like N64Recomp MEM_B; validate before writing.
    void copy_to_guest(unsigned section, std::size_t offset, std::span<std::uint8_t> rdram, std::uint32_t guest_address,
                       std::size_t length) const;
    Address address(unsigned section, std::size_t offset) const;
    const std::string& fingerprint() const {
        return bank_->fingerprint();
    }

  private:
    struct Piece {
        std::size_t offset;
        View bytes;
    };
    std::shared_ptr<const AssetBank> bank_;
    std::array<std::vector<Piece>, 50> pieces_;
    std::array<Bytes, 50> tables_;
    std::array<std::size_t, 50> sizes_{};
    AssetDirectory() = default;
};
} // namespace dkr::mods

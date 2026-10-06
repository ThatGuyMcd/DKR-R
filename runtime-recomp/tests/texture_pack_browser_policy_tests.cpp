#include "texture_pack_browser_policy.hpp"

#include <cassert>
#include <set>

namespace {

dkr::runtime::texture_packs::PackInfo pack(
    const char* id, const char* name,
    dkr::runtime::texture_packs::Format format, bool enabled,
    bool compatible, bool hidden, std::uintmax_t size,
    std::int64_t imported,
    dkr::runtime::texture_packs::Origin origin =
        dkr::runtime::texture_packs::Origin::User) {
    dkr::runtime::texture_packs::PackInfo result{};
    result.id = id;
    result.name = name;
    result.format = format;
    result.enabled = enabled;
    result.compatible = compatible;
    result.hidden = hidden;
    result.managed_size_bytes = size;
    result.imported_at_unix_seconds = imported;
    result.origin = origin;
    return result;
}

} // namespace

int main() {
    using namespace dkr::runtime;
    using namespace texture_browser;
    const std::vector<texture_packs::PackInfo> packs{
        pack("charlie", "Charlie HD", texture_packs::Format::RiceRt64,
             true, true, false, 300U, 30),
        pack("alpha", "alpha native", texture_packs::Format::NativeRt64,
             false, true, false, 100U, 10),
        pack("bravo", "Bravo Jabo", texture_packs::Format::LegacyJabo,
             false, false, true, 200U, 20),
    };

    Filters filters{};
    assert(select(packs, filters, SortMode::NameAscending).size() == 2U);
    filters.visibility = VisibilityFilter::All;
    auto selected = select(packs, filters, SortMode::NameAscending);
    assert(selected[0].id == "alpha");
    assert(selected[1].id == "bravo");
    assert(selected[2].id == "charlie");

    selected = select(packs, filters, SortMode::SizeLargest);
    assert(selected[0].id == "charlie");
    selected = select(packs, filters, SortMode::ImportOldest);
    assert(selected[0].id == "alpha");

    filters.query = "RT64 BRIDGE";
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "charlie");
    filters.query.clear();
    filters.state = StateFilter::Active;
    assert(select(packs, filters, SortMode::NameAscending).size() == 1U);
    filters.state = StateFilter::All;
    filters.compatibility = CompatibilityFilter::Incompatible;
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "bravo");
    filters.compatibility = CompatibilityFilter::All;
    filters.format = texture_packs::Format::NativeRt64;
    selected = select(packs, filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "alpha");

    // Migration keeps the old last-alphabetical winner; inactive and hidden
    // packs still own a stable position without entering the renderer list.
    auto layers = packs;
    texture_priority::Preferences priorities;
    assert(texture_priority::normalize(layers, priorities));
    assert(layers[0].priority == 1U); // Charlie
    assert(layers[1].priority == 3U); // Alpha
    assert(layers[2].priority == 2U); // Bravo, incompatible/hidden
    assert(!texture_priority::normalize(layers, priorities));
    auto loading = texture_priority::load_order(layers);
    assert(loading.size() == 1U && loading.back()->id == "charlie");
    assert(texture_priority::move(layers, priorities, "alpha", 1U));
    assert(layers[1].priority == 1U && layers[0].priority == 2U && layers[2].priority == 3U);
    assert(!texture_priority::move(layers, priorities, "alpha", 0U));
    assert(!texture_priority::move(layers, priorities, "alpha", 4U));
    assert(!texture_priority::move(layers, priorities, "missing", 1U));
    layers[1].enabled = true;
    loading = texture_priority::load_order(layers);
    assert(loading.size() == 2U && loading.front()->id == "charlie" && loading.back()->id == "alpha");
    selected = select(layers, Filters{}, SortMode::Priority);
    assert(selected.size() == 2U && selected.front().id == "alpha");
    // Adding a new layer never unexpectedly replaces an existing winner.
    layers.push_back(pack("zulu", "Zulu", texture_packs::Format::NativeRt64,
                          true, true, false, 100U, 40));
    assert(texture_priority::normalize(layers, priorities));
    assert(layers.back().priority == 4U && layers[1].priority == 1U);
    assert(texture_priority::move(layers, priorities, "alpha", 4U));
    loading = texture_priority::load_order(layers);
    assert(loading.front()->id == "alpha" && loading.back()->id == "charlie");
    // Restart/rescan restores saved ranks rather than name or input order.
    std::reverse(layers.begin(), layers.end());
    for (auto& layer : layers) layer.priority = 0U;
    assert(!texture_priority::normalize(layers, priorities));
    loading = texture_priority::load_order(layers);
    assert(loading.front()->id == "alpha" && loading.back()->id == "charlie");
    // Corrupt duplicate ranks are repaired deterministically to unique slots.
    priorities["alpha"] = 1U;
    priorities["charlie"] = 1U;
    texture_priority::normalize(layers, priorities);
    std::set<std::size_t> unique_priorities;
    for (const auto& layer : layers) unique_priorities.insert(layer.priority);
    assert(unique_priorities.size() == layers.size() && *unique_priorities.begin() == 1U);

    assert(responsive_column_count(259.0F, 8.0F) == 1);
    assert(responsive_column_count(528.0F, 8.0F) == 2);
    assert(responsive_column_count(1064.0F, 8.0F) == 4);
    assert(responsive_column_count(-1.0F, 8.0F) == 1);

    // A pack that came with a custom track is kept out of every ordinary view
    // and appears only under the "Track packs" filter.
    const std::vector<texture_packs::PackInfo> with_track_pack{
        pack("alpha", "alpha native", texture_packs::Format::NativeRt64,
             true, true, false, 100U, 10),
        pack("remix-hd", "Ancient Lake Remix HD",
             texture_packs::Format::RiceRt64, true, true, false, 500U, 40,
             texture_packs::Origin::TrackPack),
    };
    Filters track_filters{};
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).size() == 1U);
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending)[0].id == "alpha");
    track_filters.visibility = VisibilityFilter::All;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).size() == 1U);
    track_filters.visibility = VisibilityFilter::Hidden;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).empty());
    track_filters.visibility = VisibilityFilter::TrackPacks;
    selected = select(with_track_pack, track_filters, SortMode::NameAscending);
    assert(selected.size() == 1U && selected[0].id == "remix-hd");
    // The other filters still narrow within the track-pack view.
    track_filters.state = StateFilter::Inactive;
    assert(select(with_track_pack, track_filters,
                  SortMode::NameAscending).empty());
    return 0;
}

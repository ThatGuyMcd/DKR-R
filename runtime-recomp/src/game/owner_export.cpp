#include "owner_export.hpp"

namespace dkr::runtime::spectators {
OwnerExport::OwnerExport(std::uint64_t match,bool host)
    :stream_(host?acquire_export(match):nullptr),binding_(stream_.get()){}
}

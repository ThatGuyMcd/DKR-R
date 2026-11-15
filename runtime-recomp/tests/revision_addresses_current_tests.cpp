// ROM-free check of select(), current() and selected_revision().
#include "revision_addresses.hpp"

#include <cstdio>
#include <cstdlib>

namespace addresses = dkr::runtime::revision_addresses;
namespace rom = dkr::runtime::rom;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "%s\n", what);
        ++failures;
    }
}

void check_revision(const rom::Revision revision, const addresses::AddressTable& expected, const char* name) {
    check(addresses::select(revision), name);
    check(&addresses::current() == &expected, "current() is not the selected table");
    check(addresses::selected_revision() == revision, "selected_revision() is wrong");
    check(addresses::gSelectedRevision.load() == revision, "gSelectedRevision is wrong");
    // The two tables differ in every field the game's v80 build moved; GameMode is one.
    check(addresses::current().GameMode == expected.GameMode, "GameMode differs from the table");
    std::printf("%s: current() is the selected table\n", name);
}

} // namespace

int main() {
    // Both orders, so a select() that forgets to publish cannot hide behind the default.
    check_revision(rom::Revision::UsV80, addresses::kUsV80, "v80");
    check_revision(rom::Revision::UsV77, addresses::kUsV77, "v77");
    check_revision(rom::Revision::UsV80, addresses::kUsV80, "v80");
    check(addresses::kUsV77.GameMode != addresses::kUsV80.GameMode, "the revisions share GameMode");

    // A refused revision must leave everything as it was.
    check(!addresses::select(static_cast<rom::Revision>(255)), "an unknown revision was accepted");
    check(!addresses::select(rom::Revision::Unsupported), "Unsupported was accepted");
    check(&addresses::current() == &addresses::kUsV80 && addresses::selected_revision() == rom::Revision::UsV80,
          "a refused select() changed the selection");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

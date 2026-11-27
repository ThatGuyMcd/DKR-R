#include "library_ui/rom_browser.hpp"
#include "temporary_directory.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    namespace fs = std::filesystem;
    using dkr::library_ui::RomBrowser;

    const TemporaryDirectory scratch("dkrr-rom-browser");
    const auto& root = scratch.path();
    const auto downloads = root / "Downloads";
    const auto folder = downloads / "Kart ROMs";
    const auto documents = root / "Documents";
    const auto extra_folder = downloads / "A folder";
    fs::create_directories(folder);
    fs::create_directories(extra_folder);
    fs::create_directories(documents);
    fs::create_directories(downloads / "Z folder.z64");

    const auto invalid_contents = downloads / "Broken.Z64";
    const auto rom = downloads / "Race##two.v64";
    const auto nested_rom = folder / "course.n64";
    const auto unicode_rom = folder / fs::path(u8"Course café.z64");
    std::ofstream(invalid_contents) << "eligible filename, invalid ROM bytes";
    std::ofstream(rom) << "fixture";
    std::ofstream(nested_rom) << "fixture";
    std::ofstream(unicode_rom) << "fixture";
    std::ofstream(downloads / "readme.txt") << "fixture";

    const std::vector<RomBrowser::Entry> locations{{downloads, true}, {documents, true}};
    RomBrowser browser;
    browser.open(downloads, locations);
    assert(browser.directory() == downloads);
    assert(browser.take_focus_row() == 0);
    assert(!browser.take_focus_row());
    assert(!browser.back());

    // Directories come first, even when they happen to have a ROM extension.
    const auto& rows = browser.entries();
    assert(rows.size() == 5);
    assert(rows[0].path == extra_folder && rows[0].directory);
    assert(rows[1].path == folder && rows[1].directory);
    assert(rows[2].path == downloads / "Z folder.z64" && rows[2].directory);
    assert(rows[3].path == invalid_contents && !rows[3].directory);
    assert(rows[4].path == rom && !rows[4].directory);

    browser.visit(folder);
    assert(browser.directory() == folder);
    assert(browser.take_focus_row() == 0);
    assert(browser.entries().size() == 2);
    assert(browser.entries()[0].path == unicode_rom);
    assert(browser.entries()[1].path == nested_rom);
    assert(browser.back());
    assert(browser.directory() == downloads);
    assert(browser.take_focus_row() == 1);
    assert(!browser.back());

    browser.visit(folder);
    browser.up();
    assert(browser.directory() == downloads);
    assert(browser.take_focus_row() == 1);
    assert(browser.back());
    assert(browser.directory() == folder);

    browser.visit({});
    assert(browser.directory().empty() && browser.entries().size() == 2);
    assert(browser.entries()[0].path == downloads && browser.entries()[1].path == documents);
    browser.visit(documents);
    assert(browser.entries().empty() && !browser.error());
    assert(browser.back() && browser.directory().empty());
    assert(browser.take_focus_row() == 1);
    assert(browser.back() && browser.directory() == folder);

    // A stale location keeps a way back and does not overwrite prior entries.
    browser.visit(folder / "unavailable");
    assert(browser.entries().empty() && browser.error());
    assert(browser.back() && browser.directory() == folder);
    assert(!browser.error() && browser.entries().size() == 2);

    browser.open(downloads, locations);
    assert(!browser.back());
}

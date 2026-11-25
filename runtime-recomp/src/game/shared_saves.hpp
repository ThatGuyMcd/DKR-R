#pragma once
#include "save_manager.hpp"
#include <optional>

namespace dkr::runtime::saves::shared {
// Offline Adventure editing uses the selected Vanilla or Modded save.
bool prepare(const std::filesystem::path& root, std::string& error);
bool load_draft(codec::SaveImage& image, std::string& revision, std::string& error, bool modded);
bool apply(const codec::SaveImage& image, std::string_view expected, std::string& error, bool modded = false);
void complete(codec::SaveImage& image, int slot);
void unlock(codec::SaveImage& image);
bool adopt(const std::filesystem::path& source, bool repair, std::string& error, bool modded = false);
bool export_raw(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& error);
bool import_host(const std::filesystem::path& source, std::string& error);
bool apply_bundle(const std::filesystem::path& source, std::string& error, bool modded = false);
bool reset(std::string& error, bool modded = false);
bool recover_transaction(std::string& error);
// Development keeps its own write owner, but a new sandbox inherits personal
// progress. Refreshing an existing sandbox is a separately confirmed action.
bool prepare_development(const std::filesystem::path& root, const std::filesystem::path& folder, std::string& error);
bool queue_development_copy(std::string& error, bool modded = false);
std::string describe(const std::filesystem::path& path);
} // namespace dkr::runtime::saves::shared

#pragma once

#include <filesystem>

namespace dkr::runtime::ios {

std::filesystem::path documents_directory();
void prepare_storage();
void import_inbox();
// Also copies a readable dropped file that landed outside Documents.
void import_external_file(const char* utf8_path);
void configure_session();
const char* build_identity();

} // namespace dkr::runtime::ios

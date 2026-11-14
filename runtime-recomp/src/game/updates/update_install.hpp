#pragma once
#include "update_service.hpp"
namespace dkr::updates {
std::string installation_unavailable(const Configuration&);
void prepare_install(const Configuration&, const Manifest&, const std::filesystem::path& package, std::stop_token);
void acknowledge_updated_launcher(const Configuration&);
// The helper and Windows bootstrap do not link SDL/RT64 or the HTTP transport.
int updater_main(int argc, char** argv);
} // namespace dkr::updates

#pragma once
#include "mod_store_install.hpp"
#include <string_view>

namespace dkr::store {
bool vehicle_store_category(std::string_view);
bool vehicle_store_extension(std::string_view);
StorePrepared prepare_store_vehicle(const std::filesystem::path&, const std::filesystem::path&, std::stop_token);
Json publish_store_vehicles(const std::filesystem::path& config, const StorePrepared&,
                           const Json& previous, const Json& other_owners, std::stop_token);
void remove_store_vehicle(const std::filesystem::path& config, const Json& member, std::stop_token);
std::filesystem::path prepare_vehicle_store_upload(const std::filesystem::path&,
                                                  const std::filesystem::path& cache, std::stop_token);
}

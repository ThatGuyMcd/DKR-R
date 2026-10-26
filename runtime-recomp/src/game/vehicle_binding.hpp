#pragma once
#include "vehicle_resources.hpp"

namespace dkr::mods::vehicles {
// Immutable preparation attachments, not mutable gameplay or global choices.
// This avoids changing any accepted RuntimeSession/RuntimeResources layout.
void bind_resources(std::shared_ptr<const void> owner, std::shared_ptr<const Resources>);
std::shared_ptr<const Resources> resources_for(const void* owner);
void inherit_resources(const void* source, std::shared_ptr<const void> destination);
void publish_render_resources(std::shared_ptr<const Resources>);
std::shared_ptr<const Resources> render_resources();
} // namespace dkr::mods::vehicles

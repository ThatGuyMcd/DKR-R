// Implemented after vehicle_ui.inl. Store UI calls the declarations above its
// include, so the established vehicle library stays in its original position.
bool StoreVehiclesBusy() {
    const auto view = g_vehicle_library.view(); return view->busy || bool(view->pending);
}
std::shared_ptr<const void> StoreVehiclesSnapshot() { return g_vehicle_library.view(); }
void StoreRefreshVehicles() { g_vehicle_library.job(); }
dkr::store::Json StoreVehicleInventory() {
    dkr::store::Json result = dkr::store::Json::array();
    const auto view = g_vehicle_library.view();
    if (!view->error.empty() || view->busy) return result;
    for (const auto& item : view->items) result.push_back({{"kind", "vehicle"}, {"key", "vehicle:" + item.digest},
        {"id", item.id}, {"name", item.name}, {"digest", item.digest}, {"type", unsigned(item.type)},
        {"path", "mods/vehicles/packages/" + item.digest + vehicle_mods::extension(item.type)}, {"enabled", item.enabled}});
    return result;
}

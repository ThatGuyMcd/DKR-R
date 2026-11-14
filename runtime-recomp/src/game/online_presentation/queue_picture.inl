// Included inside the selected production present_gate translation unit and
// qualified against that same library. This classifies immutable metadata only;
// it never grants admission or retains a queue lease after returning.
bool requires_gpu_picture(const void* slot, std::uint64_t present, std::uint64_t workload) {
    std::shared_ptr<const RenderSnapshot> snapshot;
    {
        auto& all = registry();
        std::scoped_lock registry_lock(all.mutex);
        for (const auto& owner : all.owners)
            if (owner) {
                std::scoped_lock owner_lock(owner->mutex);
                for (const auto& record : owner->records)
                    if (record.slot == slot && record.present == present && record.workload == workload &&
                        record.experimental) {
                        snapshot = record.snapshot.lock();
                        break;
                    }
                if (snapshot)
                    break;
            }
    }
    if (!snapshot || snapshot->descriptor().black)
        return false;
    unsigned players = 0;
    for (const auto& event : snapshot->draw_events())
        if (event.kind == DKR_OWNED_ONLINE_VIEW) {
            players = event.parameters[4];
            break;
        }
    return dkr::online_stability::view_eligibility(0, players, snapshot->draw_events()).eligible;
}

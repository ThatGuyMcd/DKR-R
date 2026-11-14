#include "legacy_mod_library.hpp"
#include <map>

namespace dkr::mods {
void ModLibrary::configure(const std::filesystem::path& root, const std::filesystem::path& worker) {
    imports_.configure(root, worker);
    tracks_.configure(root, worker);
    characters_.configure(root, worker);
    phase_ = Phase::Scan;
    modal_ = false;
    success_ = false;
    cancelled_ = false;
    result_.clear();
    batch_.clear();
    batch_at_ = batch_ok_ = 0;
    batch_result_.clear();
}
ModLibraryView ModLibrary::snapshot() const {
    ModLibraryView out;
    out.imports = imports_.snapshot();
    out.tracks = tracks_.snapshot();
    out.characters = characters_.snapshot();
    out.busy = phase_ != Phase::Idle;
    out.modal = modal_;
    out.succeeded = success_;
    out.result = result_;
    out.imports_completed = batch_at_;
    out.imports_total = static_cast<unsigned>(batch_.size());
    if (phase_ == Phase::Import) {
        out.stage = out.imports->stage;
        out.completed = out.imports->patch;
        out.total = out.imports->count;
    } else if (phase_ == Phase::Tracks || phase_ == Phase::ToggleTracks) {
        out.stage = out.tracks->stage;
        out.completed = out.tracks->completed;
        out.total = out.tracks->total;
    } else if (phase_ == Phase::Characters || phase_ == Phase::ToggleCharacters) {
        out.stage = out.characters->stage;
        out.completed = out.characters->completed;
        out.total = out.characters->total;
    } else
        out.stage = out.busy ? "Reading mod libraries" : "Mod library ready";
    if (!batch_.empty() && out.busy)
        out.stage =
            "Source import " + std::to_string(batch_at_ + 1) + "/" + std::to_string(batch_.size()) + ": " + out.stage;
    return out;
}
void ModLibrary::finish(bool success, std::string message) {
    if (!success && (phase_ == Phase::ToggleTracks || phase_ == Phase::ToggleCharacters))
        modal_ = true;
    phase_ = Phase::Idle;
    success_ = success;
    result_ = std::move(message);
    roms_.clear();
    batch_.clear();
    batch_at_ = batch_ok_ = 0;
    batch_result_.clear();
    imports_.dismiss();
    tracks_.dismiss();
    characters_.dismiss();
}
void ModLibrary::advance() {
    if (cancelled_) {
        finish(false, result_ + "\nCancelled. Completed imports remain installed but were not enabled.");
        return;
    }
    if (need_tracks_) {
        need_tracks_ = false;
        phase_ = Phase::Tracks;
        if (!tracks_.prepare_review(review_, roms_)) {
            success_ = false;
            result_ += "\nCould not start track preparation.";
            advance();
        }
    } else if (need_characters_) {
        need_characters_ = false;
        phase_ = Phase::Characters;
        if (!characters_.prepare_review(review_, roms_)) {
            success_ = false;
            result_ += "\nCould not start character preparation.";
            advance();
        }
    } else if (!batch_.empty())
        finish_source();
    else
        finish(success_, result_.empty() ? "No supported tracks or characters were identified."
                                         : result_ + "\nChoose which prepared mods to enable in the libraries below.");
}
void ModLibrary::next_source() {
    const auto& source = batch_.at(batch_at_);
    review_ = source.review;
    need_tracks_ = source.tracks;
    need_characters_ = source.characters;
    success_ = true;
    result_.clear();
    advance();
}
void ModLibrary::finish_source() {
    if (success_)
        ++batch_ok_;
    batch_result_ += "\nImport " + std::to_string(batch_at_ + 1) + " [" + review_.substr(0, 8) +
                     "]: " + (success_ ? "prepared. " : "needs attention. ") + result_.substr(0, 512);
    ++batch_at_;
    if (batch_at_ < batch_.size()) {
        next_source();
        return;
    }
    finish(
        batch_ok_ == batch_.size(),
        "Prepared " + std::to_string(batch_ok_) + " of " + std::to_string(batch_.size()) +
            " source imports. Existing editions and selections were preserved; new editions are inactive. Review individual compatibility notes before enabling." +
            batch_result_);
}
void ModLibrary::tick() {
    if (phase_ == Phase::Idle)
        return;
    const auto state = snapshot();
    if (phase_ == Phase::Scan) {
        if (state.imports->busy || state.tracks->busy || state.characters->busy)
            return;
        std::string errors;
        if (!state.imports->succeeded)
            errors += state.imports->result + "\n";
        if (!state.tracks->succeeded)
            errors += state.tracks->result + "\n";
        if (!state.characters->succeeded)
            errors += state.characters->result + "\n";
        finish(errors.empty(), errors.empty() ? "Mod libraries refreshed." : errors);
    } else if (phase_ == Phase::Import) {
        if (state.imports->busy)
            return;
        if (!state.imports->succeeded) {
            finish(false, state.imports->result);
            return;
        }
        review_ = state.imports->imported_review;
        need_tracks_ = state.imports->imported_tracks;
        need_characters_ = state.imports->imported_characters;
        success_ = need_tracks_ || need_characters_;
        advance();
    } else {
        const auto catalog =
            (phase_ == Phase::Tracks || phase_ == Phase::ToggleTracks) ? state.tracks : state.characters;
        if (catalog->busy)
            return;
        if (phase_ == Phase::ToggleTracks || phase_ == Phase::ToggleCharacters) {
            finish(catalog->succeeded, catalog->result);
            return;
        }
        if (!result_.empty())
            result_ += '\n';
        result_ += (phase_ == Phase::Tracks ? "Tracks: " : "Characters: ") + catalog->result;
        success_ &= catalog->succeeded;
        // A rejected track does not discard a separately valid character (or
        // vice versa). Neither phase activates anything automatically.
        advance();
    }
}
bool ModLibrary::import_file(std::filesystem::path source, std::vector<std::filesystem::path> roms) {
    if (phase_ != Phase::Idle)
        return false;
    if (!imports_.import_file(std::move(source), roms))
        return false;
    batch_.clear();
    batch_at_ = batch_ok_ = 0;
    batch_result_.clear();
    roms_ = std::move(roms);
    result_.clear();
    modal_ = true;
    cancelled_ = false;
    success_ = false;
    phase_ = Phase::Import;
    return true;
}
bool ModLibrary::prepare_review(std::string review, std::vector<std::filesystem::path> roms) {
    if (phase_ != Phase::Idle)
        return false;
    need_tracks_ = need_characters_ = false;
    for (const auto& item : imports_.snapshot()->items)
        if (item.review == review) {
            need_tracks_ |= item.kind == "Track";
            need_characters_ |= item.kind == "Character";
        }
    if (!need_tracks_ && !need_characters_)
        return false;
    batch_.clear();
    batch_at_ = batch_ok_ = 0;
    batch_result_.clear();
    review_ = std::move(review);
    roms_ = std::move(roms);
    result_.clear();
    modal_ = true;
    cancelled_ = false;
    success_ = true;
    advance();
    return true;
}
bool ModLibrary::prepare_all_reviews(std::vector<std::filesystem::path> roms) {
    if (phase_ != Phase::Idle || roms.empty() || roms.size() > 8)
        return false;
    std::map<std::string, Source> sources;
    const auto add = [&](const std::string& review, bool character) {
        if (review.empty())
            return;
        auto& source = sources[review];
        source.review = review;
        if (character)
            source.characters = true;
        else
            source.tracks = true;
    };
    for (const auto& item : imports_.snapshot()->items)
        add(item.review, item.kind == "Character");
    // Include installed hidden/duplicate editions whose import cards may have
    // been deduplicated. A missing/corrupt source is reported by its worker.
    for (const auto& item : tracks_.snapshot()->tracks)
        add(item.review, false);
    for (const auto& item : characters_.snapshot()->tracks)
        add(item.review, true);
    // Installed online-retained sources can exceed the import-history limit.
    // The three validated snapshot catalogues already bound this collection;
    // do not reject an otherwise valid installed library at 128 sources.
    if (sources.empty())
        return false;
    batch_.clear();
    for (auto& [id, source] : sources)
        batch_.push_back(std::move(source));
    batch_at_ = batch_ok_ = 0;
    batch_result_.clear();
    roms_ = std::move(roms);
    modal_ = true;
    cancelled_ = false;
    next_source();
    return true;
}
bool ModLibrary::set_enabled(TrackCatalog::Kind kind, std::string id, bool enabled) {
    if (phase_ != Phase::Idle)
        return false;
    auto& catalog = kind == TrackCatalog::Kind::Track ? tracks_ : characters_;
    if (!catalog.set_enabled(std::move(id), enabled))
        return false;
    phase_ = kind == TrackCatalog::Kind::Track ? Phase::ToggleTracks : Phase::ToggleCharacters;
    modal_ = false;
    cancelled_ = false;
    result_.clear();
    return true;
}
bool ModLibrary::refresh() {
    if (phase_ != Phase::Idle)
        return false;
    imports_.refresh();
    tracks_.refresh();
    characters_.refresh();
    phase_ = Phase::Scan;
    result_.clear();
    return true;
}
bool ModLibrary::set_hidden(TrackCatalog::Kind kind, std::string id, bool hidden) {
    if (phase_ != Phase::Idle)
        return false;
    auto& catalog = kind == TrackCatalog::Kind::Track ? tracks_ : characters_;
    if (!catalog.set_hidden(std::move(id), hidden))
        return false;
    phase_ = kind == TrackCatalog::Kind::Track ? Phase::ToggleTracks : Phase::ToggleCharacters;
    modal_ = false;
    cancelled_ = false;
    result_.clear();
    return true;
}
bool ModLibrary::remove(TrackCatalog::Kind kind, std::string id) {
    if (phase_ != Phase::Idle)
        return false;
    auto& catalog = kind == TrackCatalog::Kind::Track ? tracks_ : characters_;
    if (!catalog.remove(std::move(id)))
        return false;
    phase_ = kind == TrackCatalog::Kind::Track ? Phase::ToggleTracks : Phase::ToggleCharacters;
    modal_ = true;
    cancelled_ = false;
    result_.clear();
    return true;
}
bool ModLibrary::disable_all(TrackCatalog::Kind kind) {
    if (phase_ != Phase::Idle)
        return false;
    auto& catalog = kind == TrackCatalog::Kind::Track ? tracks_ : characters_;
    if (!catalog.disable_all())
        return false;
    phase_ = kind == TrackCatalog::Kind::Track ? Phase::ToggleTracks : Phase::ToggleCharacters;
    modal_ = false;
    result_.clear();
    return true;
}
void ModLibrary::cancel() {
    cancelled_ = true;
    imports_.cancel();
    tracks_.cancel();
    characters_.cancel();
    if (!batch_.empty())
        result_ = "Cancelled after " + std::to_string(batch_at_) + " of " + std::to_string(batch_.size()) +
                  " source imports." + batch_result_;
}
void ModLibrary::dismiss() {
    if (phase_ == Phase::Idle)
        modal_ = false;
}
} // namespace dkr::mods

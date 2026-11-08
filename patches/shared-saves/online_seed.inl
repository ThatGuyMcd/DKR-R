    if (mode == OnlineSaveSeedMode::Fresh) {
        bytes = codec::blank_bytes();
    } else {
        const auto source=mode==OnlineSaveSeedMode::CopySinglePlayer?AdventurePath():destination;
        std::vector<std::uint8_t> stored;
        if(ReadAdventure(source,stored))bytes=std::move(stored);
        else {
            std::error_code ec;const bool exists=std::filesystem::exists(source,ec);
            if(mode!=OnlineSaveSeedMode::ContinuePreviousSession||exists||ec) {
                error="That Adventure is missing or needs recovery. Choose a working save in Save Manager, copy your personal save, or start fresh.";
                return false;
            }
            // First lobby only: seed from personal progress when available.
            // Once a host Adventure exists, continuing never re-copies it.
            if(ReadAdventure(AdventurePath(),stored))bytes=std::move(stored);
            else {
                std::error_code personal_error;
                if(std::filesystem::exists(AdventurePath(),personal_error)||personal_error) {
                    error="Your personal Adventure needs recovery. Choose Start Fresh explicitly or repair it in Save Manager.";
                    return false;
                }
                bytes=codec::blank_bytes();
            }
        }
    }

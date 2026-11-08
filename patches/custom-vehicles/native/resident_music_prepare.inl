    auto music=std::make_shared<MusicLibrary>();std::size_t music_encoded_bytes=0;
    std::map<std::string,resident_music::Prepared> decoded;
    std::string music_identity;
    for(const auto& course:out->authored_->courses) {
        const auto track=std::ranges::find_if(out->authored_->tracks,[&](const auto& t){return t.id==course.id;});
        if(track==out->authored_->tracks.end())throw Error("Frozen course has no authored resource root.");
        if(!track->music)continue;
        MusicSong song;song.info=*track->music;
        if(song.info.kind==dkr::runtime::custom_tracks::MusicKind::Recording) {
            cancelled(stop);auto input=read_file(song.info.file,dkr::runtime::custom_tracks::kMaxMusicBytes);
            if(input.size()!=song.info.bytes || sha256(input)!=song.info.sha256)
                throw Error("Music for \""+track->name+"\" changed after package verification. Re-import that track.");
            auto found=decoded.find(song.info.sha256);
            if(found==decoded.end()) {
                if(input.size()>128*MiB-music_encoded_bytes)
                    throw Error("Encoded custom music exceeds the 128 MiB session limit.");
                const auto encoded_size=input.size();resident_music::Prepared prepared;
                try{prepared=resident_music::prepare(std::move(input),song.info,stop);}
                catch(const std::exception& e){throw Error("Music for \""+track->name+"\" cannot be prepared: "+e.what());}
                music_encoded_bytes+=encoded_size;
                music_identity+="\n"+song.info.sha256+":"+prepared.pcm_sha256;
                found=decoded.emplace(song.info.sha256,std::move(prepared)).first;
            }
            song.pcm=found->second.metadata;const auto& shape=*song.pcm;
            if(!shape.frames || shape.frames!=song.info.frames || shape.sample_rate!=song.info.sample_rate ||
               song.info.loop_start>=shape.frames ||
               (song.info.loop_end && (song.info.loop_end>shape.frames || song.info.loop_end<=song.info.loop_start)))
                throw Error("Music for \""+track->name+"\" has invalid frame, sample-rate or loop settings. Re-export that track.");
            std::fprintf(stderr,"[online-mods][music] course=%s frames=%llu rate=%u unique-recordings=%zu encoded-bytes=%zu idle-decoded-bytes=0\n",
                track->id.c_str(),static_cast<unsigned long long>(shape.frames),shape.sample_rate,decoded.size(),music_encoded_bytes);
        }
        music->emplace(course.level_id,std::move(song));
    }
    out->music_=std::move(music);

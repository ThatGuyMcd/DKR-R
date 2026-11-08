const SaveManagerViewCache& CachedSaveManagerView() {
    static std::future<std::pair<std::uint64_t,SaveManagerViewCache>> pending;
    const auto now=std::chrono::steady_clock::now();
    if(pending.valid()&&pending.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
        try {auto result=pending.get();if(result.first==g_save_cache_revision){g_save_manager_view_cache=std::move(result.second);}}
        catch(const std::exception& e){g_save_manager_status=e.what();}
    }
    if(!pending.valid()&&(!g_save_manager_view_cache.valid||now>=g_save_manager_view_cache.next_refresh)) {
        pending=std::async(std::launch::async,[generation=g_save_cache_revision]{
            SaveManagerViewCache view;view.adventure=dkr::runtime::saves::adventure_info();
            dkr::runtime::saves::codec::SaveImage image;std::string error;
            if(view.adventure.valid&&dkr::runtime::saves::load_adventure(image,error)) {
                view.image=image;
                for(unsigned i=0;i<3;++i){if(i)view.preview+=" | ";const auto& s=image.slots[i];view.preview+="Slot "+std::to_string(i+1)+": "+s.name+" ("+std::to_string(s.balloons[0])+" balloons)";}
            }
            for(int i=0;i<4;++i)view.controller_paks[i]=dkr::runtime::saves::controller_pak_info(i);
            view.adventure_backups=dkr::runtime::saves::adventure_backups();
            view.valid=true;view.next_refresh=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            return std::pair{generation,std::move(view)};
        });
    }
    return g_save_manager_view_cache;
}

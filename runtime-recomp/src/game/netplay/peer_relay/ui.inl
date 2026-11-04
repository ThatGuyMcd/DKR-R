void DrawOlProxyHost(float inner) {
    bool enabled=dkr::relay::host_enabled.load();
    if(OlSwitchRow("##proxy-host","Let Clients host by Proxy",
        "Allows a better route through another player when available. May improve stability over long distances.",
        &enabled,inner,dkr::runtime::netplay::session().active())){dkr::relay::host_enabled=enabled;SaveSettings();}
}
void DrawOlProxyConnection(float width) {
    bool volunteer=dkr::relay::volunteer_enabled.load();
    DrawOlFeature(width,"Connection routes","",[&](float inner) {
        if(OlSwitchRow("##proxy-volunteer","Allow this device to relay connections",
            "Uses extra upload bandwidth to help players in your lobby. The host must also enable proxy connections.",
            &volunteer,inner,false)){dkr::relay::volunteer_enabled=volunteer;SaveSettings();}
        const auto status=dkr::relay::view(&dkr::runtime::netplay::session());
        PaddockGap(12.F);
        OlParagraph(status.enabled?"Measuring direct and one-hop routes. The original host stays in control.":"Direct connections. Proxy routing is off for this lobby.",14.F,kOlSoft,inner);
        std::set<dkr::relay::Id> shown;
        for(const auto& row:status.rows)if(shown.insert(row.endpoint).second) {
            const auto active=std::find_if(status.rows.begin(),status.rows.end(),[&](const auto& r){return r.endpoint==row.endpoint&&r.selected;});
            const auto direct=std::find_if(status.rows.begin(),status.rows.end(),[&](const auto& r){return r.endpoint==row.endpoint&&r.via=="Direct";});
            const auto& selected=active!=status.rows.end()?*active:(direct!=status.rows.end()?*direct:row);
            std::string line=row.name+": "+(active!=status.rows.end()?"Via "+selected.via:"Direct");
            if(selected.local.samples)line+=" - "+std::to_string(selected.local.p95)+" ms p95 ("+std::to_string(selected.local.samples)+" samples)";
            else line+=" - measuring";
            OlParagraph(line,14.F,kOlCream,inner);
            for(const auto& candidate:status.rows)if(candidate.endpoint==row.endpoint&&candidate.via!="Direct")
                OlParagraph("Via "+candidate.via+": "+candidate.reason,13.F,kOlSoft,inner);
        }
        PaddockGap(12.F);
        if(OlButton("Copy proxy connection diagnostics",OlTone::Plain,inner)) {
            const auto text=dkr::relay::diagnostics(status);SDL_SetClipboardText(text.c_str());OlNotify("Proxy connection diagnostics copied.");
        }
    });PaddockGap(16.F);
}

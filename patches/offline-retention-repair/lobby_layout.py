"""Reposition existing launcher controls without changing their actions."""
from pathlib import Path


def once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Lobby layout anchor changed: ' + before[:120])
    return text.replace(before, after, 1)


def compose(text, stage):
    text = once(text, 'void DrawOlGuideCard(float width, const OnlineFrame& frame) {',
        'extern float g_ol_page_width;\nvoid DrawOlLobbyFilters(float);\nvoid DrawOlSidebarReadiness(float, const OnlineFrame&);\n'
        'void DrawOlGuideCard(float width, const OnlineFrame& frame) {')
    text = once(text, '    DrawOlFeature(width, "Online guide", "", [&](float inner) {',
        '''    if (!frame.active && g_online_page.section == OlSection::Lobbies) {
        DrawOlSidebarReadiness(width, frame);
        PaddockGap(20.F);
    }
    DrawOlFeature(width, "Online guide", "", [&](float inner) {''')
    text = once(text, '        PaddockGap(13.0F);\n        if (profile_section) {',
        '''        PaddockGap(13.0F);
        if (!profile_section && g_online_page.section == OlSection::Lobbies && g_ol_page_width > 1000.F) {
            DrawOlLobbyFilters(inner);
        }
        if (profile_section) {''')
    text = once(text, '''    static const std::vector<std::string> sources{"Public","Friends"};
    int source=g_directory_friends_view?1:0;
    if(OlSelect("directory-source","Browse lobbies",&source,sources,width))g_directory_friends_view=source!=0;
    PaddockGap(12.F);
''', '')
    text = once(text, '''        static const std::vector<std::string> join_roles{"Racer","Watch Race"};
        int join_role=g_online_join_watching?1:0;
        if(OlSelect("browse-join-role","Join as",&join_role,join_roles,width))g_online_join_watching=join_role!=0;
        PaddockGap(12.F);
''', '''        // On narrow windows, keep the filters before the list. The wide
        // layout places the same controls beneath the account details.
        if (g_ol_page_width <= 1000.F) {
            DrawOlCard(width, OlPanelLook{}, [&](float inner) { DrawOlLobbyFilters(inner); });
            PaddockGap(20.F);
        }
''')
    text = once(text,
        '(g_online_page.section == OlSection::Play || g_online_page.section == OlSection::Lobbies)) {',
        'g_online_page.section == OlSection::Play) {')
    text = once(text, '// ------------------------------------------------------------ host settings',
        '#include "' + (stage / 'lobby_sidebar.inl').as_posix() + '"\n\n'
        '// ------------------------------------------------------------ host settings')
    return text

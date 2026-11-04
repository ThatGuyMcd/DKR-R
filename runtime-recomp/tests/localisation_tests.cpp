#include "localisation/service.hpp"
#include "localisation/save_preview.hpp"
#include "localisation/choice.hpp"
#include "launcher_copy_policy.hpp"
#include <array>
#include <cassert>
#include <stdexcept>
#include <string>

int main() {
    namespace ui = dkr::localisation;
    assert(ui::languages().size() == 11);
    assert(ui::normalise("pt_BR.UTF-8") == "pt-BR");
    assert(ui::normalise("de_DE@euro") == "de");
    assert(ui::normalise("../../fr").empty());
    assert(ui::normalise("C.UTF-8") == "en");
    assert(ui::resolve(std::array<std::string, 2>{"ja_JP", "uk_UA"}) == "uk");
    assert(ui::resolve_posix("C.UTF-8", "de_DE", "fr_FR", "pl:uk") == "en");
    assert(ui::resolve_posix("POSIX", "de_DE", "fr_FR", "pl:uk") == "en");
    assert(ui::resolve_posix("", "de_DE", "fr_FR", "ja:pl_PL:uk") == "pl");
    assert(ui::resolve_posix("", "", "pt_BR.UTF-8", "") == "pt-BR");
    assert(ui::resolve_posix("", "uk_UA", "en_GB", "") == "uk");
    ui::system_preferences(std::array<std::string, 1>{"de_DE.UTF-8"});
    assert(ui::active() == "de");
    const auto first = ui::generation();
    assert(ui::select("de-DE"));
    assert(ui::generation() == first);
    const auto held = ui::text("nav.play");
    assert(held == "Spielen");
    assert(ui::select("fr"));
    assert(ui::active() == "fr" && ui::preference() == "fr");
    assert(held == "Spielen"); // immutable pointers survive live switching
    assert(ui::literal("Play") == "Jouer");
    const std::string mod_name = "Play";
    assert(ui::literal(mod_name) == "Play");
    const char* chat_message = "Play";
    assert(ui::literal(chat_message) == "Play");
    char mutable_name[] = "Play";
    assert(ui::literal(mutable_name) == "Play");
    const ui::Choice application_choice("Play");
    const ui::Choice user_choice(std::string("Play"));
    assert(application_choice.display_label() == "Jouer");
    assert(user_choice.display_label() == "Play");
    const std::string diagnostic="Invalid save checksum at C:/User/Play.sav";
    const auto summary=dkr::runtime::copy::error_summary(diagnostic,dkr::runtime::copy::Area::Saves);
    assert(dkr::runtime::copy::looks_like_error(diagnostic));
    assert(ui::source(summary)!=summary);
    assert(diagnostic=="Invalid save checksum at C:/User/Play.sav");
    assert(ui::source("unregistered copy") == "unregistered copy");
    assert(ui::relay_caption("Via Play {name} %s",true)=="Via Play {name} %s");
    assert(ui::directional_caption("Left stick ↑")=="Stick gauche ↑");
    assert(ui::format("library.by", {{"author", "Play {name} %s"}}) == "par Play {name} %s");
    bool missing_argument = false;
    try { (void)ui::format("library.by", {}); }
    catch (const std::invalid_argument&) { missing_argument = true; }
    assert(missing_argument);
    assert(ui::label("Play##action") == "Jouer###Play##action");
    assert(ui::label("Downloads (3)##jobs") == "Téléchargements (3)###Downloads (3)##jobs");
    assert(ui::label("##input") == "##input");
    assert(ui::select("auto"));
    assert(ui::active() == "de");
    assert(application_choice.display_label() == "Spielen");
    assert(user_choice.display_label() == "Play");
    ui::system_preferences(std::array<std::string, 1>{"es-MX"});
    assert(ui::active() == "es");
    assert(!ui::select("../../fr"));
    assert(ui::preference() == "auto" && ui::active() == "es");
    ui::system_preferences(std::array<std::string, 1>{"ja-JP"});
    assert(ui::active() == "en");
    assert(ui::source("PLAY") == "PLAY");
    assert(ui::plural_category("ru", 1) == "one");
    assert(ui::plural_category("ru", 11) == "many");
    assert(ui::plural_category("ru", 22) == "few");
    assert(ui::plural_category("pl", 21) == "many");
    assert(ui::plural_category("uk", 101) == "one");
    assert(ui::plural_category("fr", 0) == "one");
    assert(ui::plural_category("pt-PT", 0) == "other");
    assert(ui::plural_category("es", 1000000) == "many");
    assert(ui::select("ru"));
    assert(ui::plural("chat.unread", 22) == "22 непрочитанных сообщения");
    dkr::runtime::saves::codec::SaveImage image;
    image.slots[0].name = "Empty {name} %s";
    image.slots[0].balloons[0] = 22;
    assert(ui::save_preview(image).find("Empty {name} %s (22 шарика)") != std::string::npos);
    assert(ui::printf_source("Play") == "Play");
    assert(ui::plural("library.rating",22,{{"average","4.5"}}).find("22 отзыва")!=std::string::npos);
    assert(ui::select("en"));
    ui::debug_pseudo(true);
    const auto pseudo_copy = ui::source("Play");
    assert(pseudo_copy != "Play" && pseudo_copy.size() > 4);
    assert(ui::source(pseudo_copy)==pseudo_copy);
    assert(ui::format("library.by", {{"author", "{name} %s"}}).find("{name} %s") != std::string::npos);
    assert(ui::preference() == "en");
    ui::debug_pseudo(false);
    assert(ui::source("Play") == "Play" && pseudo_copy.front() == '[');
    const auto bytes = ui::fallback_font();
    assert(bytes.size() == 393200 && bytes[0] == 0 && bytes[1] == 1);
    assert(ui::fallback_ranges().back() == 0);
    for (const auto& language : ui::languages()) {
        assert(ui::select(language.code));
        assert(!ui::text("common.language").empty());
    }
}

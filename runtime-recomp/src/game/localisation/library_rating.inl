// Community content stays in its author's language. Only the application's
// category labels and numeric rating summary are translated.
std::string StoreRating(const StoreJson& item) {
    if(item.contains("rating")&&item["rating"].is_object()) {
        const auto& rating=item["rating"];
        const int count=StoreInt(rating,"count");
        if(count>0&&rating.contains("average")&&rating["average"].is_number()) {
            char average[32];
            std::snprintf(average,sizeof(average),"%.1f",std::clamp(rating["average"].get<double>(),0.,5.));
            return dkr::localisation::plural("library.rating",std::uint64_t(count),{{"average",average}});
        }
    }
    return std::string(dkr::localisation::text("library.no_reviews"));
}

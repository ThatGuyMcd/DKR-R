#include "magic_code_trophy_definition.hpp"
#include <algorithm>
#include <map>
#include <numeric>

namespace dkr::mods::code::trophy {
namespace {
[[noreturn]] void bad(const std::string& s) {
    throw Error("Custom Trophy Races: " + s);
}
unsigned number(const Json& value, unsigned maximum) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > maximum)
        bad("number is outside its supported range");
    return value.get<unsigned>();
}
std::string text(const Json& value, std::size_t maximum) {
    if (!value.is_string())
        bad("expected text");
    auto s = value.get<std::string>();
    if (s.empty() || s.size() > maximum ||
        std::any_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        bad("invalid name or course identity");
    return s;
}
Json document(const Definition& d) {
    if (!valid_id(d.id) || d.name.empty() || d.name.size() > 160 || d.rounds.empty() || d.rounds.size() > MaxRounds)
        bad("invalid cup definition");
    text(d.name, 160);
    Json out = {{"schema", DefinitionSchema},
                {"id", d.id},
                {"name", d.name},
                {"shuffle", d.shuffle},
                {"rounds", Json::array()}};
    for (const auto& r : d.rounds) {
        text(r.course, 160);
        if (!r.laps || r.laps > MaxLaps || (r.vehicle > 2 && r.vehicle != AutomaticVehicle))
            bad("invalid lap count or vehicle");
        out["rounds"].push_back({{"course", r.course}, {"laps", r.laps}, {"vehicle", r.vehicle}});
    }
    return out;
}
Definition definition(const Json& j) {
    if (!j.is_object() || j.size() != 5 || j.value("schema", 0) != DefinitionSchema || !j.contains("id") ||
        !j.contains("name") || !j.contains("shuffle") || !j["shuffle"].is_boolean() || !j.contains("rounds") ||
        !j["rounds"].is_array() || j["rounds"].empty() || j["rounds"].size() > MaxRounds)
        bad("unsupported cup definition");
    Definition d;
    d.id = text(j["id"], 96);
    d.name = text(j["name"], 160);
    d.shuffle = j["shuffle"].get<bool>();
    for (const auto& r : j["rounds"]) {
        if (!r.is_object() || r.size() != 3 || !r.contains("course") || !r.contains("laps") || !r.contains("vehicle"))
            bad("invalid cup round");
        d.rounds.push_back(
            {text(r["course"], 160), number(r["laps"], MaxLaps), number(r["vehicle"], AutomaticVehicle)});
    }
    document(d);
    return d;
}
Json document(const Frozen& f) {
    Json out = {{"schema", DefinitionSchema},
                {"definition", document(f.definition)},
                {"seed", f.seed},
                {"order", Json::array()}};
    for (const auto& r : f.rounds)
        out["order"].push_back({{"course", r.course.id},
                                {"content", r.course.content},
                                {"carrier", r.course.carrier},
                                {"laps", r.laps},
                                {"vehicle", r.vehicle},
                                {"source", r.source}});
    return out;
}
Bytes bytes(const Json& j) {
    const auto s = j.dump();
    return {s.begin(), s.end()};
}
class Random {
  public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        auto v = state_ += 0x9e3779b97f4a7c15ULL;
        v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
        v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
        return v ^ (v >> 31);
    }
    unsigned below(unsigned count) {
        const auto bound = std::uint64_t(count), threshold = (std::uint64_t(0) - bound) % bound;
        for (unsigned tries = 0; tries < 128; ++tries) {
            const auto v = next();
            if (v >= threshold)
                return unsigned(v % bound);
        }
        bad("shuffle work budget exceeded");
    }

  private:
    std::uint64_t state_;
};
} // namespace
Definition decode(View data) {
    return definition(parse_document(data, 256 * 1024));
}
Bytes encode(const Definition& d) {
    return bytes(document(d));
}
Frozen freeze(const Definition& d, const std::vector<Course>& catalog, std::uint64_t seed) {
    document(d);
    if (catalog.size() > 1024)
        bad("course catalogue exceeds its supported limit");
    std::map<std::string, const Course*> courses;
    for (const auto& c : catalog) {
        text(c.id, 160);
        text(c.name, 160);
        if (c.content.size() != 64 ||
            !std::all_of(c.content.begin(), c.content.end(),
                         [](char v) { return (v >= '0' && v <= '9') || (v >= 'a' && v <= 'f'); }) ||
            c.carrier > 255 || !c.vehicles || (c.vehicles & ~7U) || c.default_vehicle > 2 ||
            !(c.vehicles & (1U << c.default_vehicle)) || !courses.emplace(c.id, &c).second)
            bad("invalid or ambiguous admitted course");
    }
    std::vector<unsigned> order(d.rounds.size());
    std::iota(order.begin(), order.end(), 0U);
    Random random(seed);
    if (d.shuffle)
        for (unsigned i = unsigned(order.size()); i > 1; --i)
            std::swap(order[i - 1], order[random.below(i)]);
    Frozen out;
    out.definition = d;
    out.seed = seed;
    for (unsigned source : order) {
        const auto& r = d.rounds[source];
        const auto found = courses.find(r.course);
        if (found == courses.end() || !found->second->race)
            bad("course is missing or is not a race track: " + r.course);
        const auto& c = *found->second;
        const auto vehicle = r.vehicle == AutomaticVehicle ? c.default_vehicle : r.vehicle;
        if (!(c.vehicles & (1U << vehicle)))
            bad("the selected vehicle is not supported by " + c.name);
        out.rounds.push_back({c, r.laps, vehicle, source});
    }
    out.identity = sha256(bytes(document(out)));
    return out;
}
Bytes encode(const Frozen& f) {
    // Re-derive rather than trusting caller-mutated round/order fields.
    std::vector<Course> catalog;
    std::set<std::string> seen;
    for (const auto& r : f.rounds)
        if (seen.insert(r.course.id).second)
            catalog.push_back(r.course);
    const auto expected = freeze(f.definition, catalog, f.seed);
    const auto encoded = bytes(document(f));
    if (f.identity != expected.identity || encoded != bytes(document(expected)))
        bad("frozen cup no longer matches its admitted definition");
    return encoded;
}
Frozen verify(View data, const std::vector<Course>& catalog) {
    const auto j = parse_document(data, 256 * 1024);
    if (j.size() != 4 || j.value("schema", 0) != DefinitionSchema || !j.contains("definition") || !j.contains("seed") ||
        !j["seed"].is_number_unsigned() || !j.contains("order") || !j["order"].is_array() ||
        j["order"].size() > MaxRounds)
        bad("invalid frozen cup");
    const auto out = freeze(definition(j["definition"]), catalog, j["seed"].get<std::uint64_t>());
    if (document(out) != j)
        bad("host cup order or course content differs from the admitted catalogue");
    return out;
}
} // namespace dkr::mods::code::trophy

#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Address level rank lookup, based on Nominatim's settings/address-levels.json.
// https://github.com/osm-search/Nominatim/blob/master/settings/address-levels.json
//
// Ranks are mapped to semantic levels (country/state/county/city/suburb/postcode)
// in the get_semantic() helper. Tag values for boundary=administrative use the form
// "administrative<N>" where N is the admin_level.

namespace address_levels {

// Each rule lists (tag_key, tag_value) -> (polygon_rank, node_rank).
// A rank of -1 means the tag doesn't produce an address entry in that form.
struct Rule {
    const char* key;
    const char* value;
    int8_t polygon_rank;
    int8_t node_rank;
};

// Default rules (used when no country override matches).
static const Rule kDefaultRules[] = {
    // boundary=administrative
    {"boundary", "administrative2",  4,  4},
    {"boundary", "administrative3",  6,  6},
    {"boundary", "administrative4",  8,  8},
    {"boundary", "administrative5", 10, 10},
    {"boundary", "administrative6", 12, 12},
    {"boundary", "administrative7", 14, 14},
    {"boundary", "administrative8", 16, 16},
    {"boundary", "administrative9", 18, 18},
    {"boundary", "administrative10", 20, 20},
    {"boundary", "administrative11", 22, 22},
    {"boundary", "administrative12", 24, 24},
    // place=* (polygon rank, node rank)
    {"place", "country",       4,  4},
    {"place", "state",         8,  8},
    {"place", "province",      8,  8},
    {"place", "county",       12, 12},
    {"place", "city",         16, 16},
    {"place", "town",         18, 16},
    {"place", "village",      19, 16},
    {"place", "borough",      18, 18},
    {"place", "suburb",       19, 20},
    {"place", "hamlet",       20, 20},
    {"place", "neighbourhood", 24, 24},
    {"place", "quarter",      20, 22},
    {"place", "locality",     25, 25},
};

// Country-specific overrides (sparse - only deltas from defaults).
struct CountryOverride {
    const char* country_code; // lowercase ISO 3166-1 alpha-2
    const Rule* rules;
    size_t rule_count;
};

static const Rule kAuRules[]  = {{"boundary", "administrative6", 12, 0}};
static const Rule kCaRules[]  = {{"place", "county", 12, 0}};
static const Rule kCzRules[]  = {
    {"boundary", "administrative5",  12, 12},
    {"boundary", "administrative6",  13, 0},
    {"boundary", "administrative7",  14, 0},
    {"boundary", "administrative8",  14, 14},
    {"boundary", "administrative9",  15, 15},
    {"boundary", "administrative10", 16, 16},
};
static const Rule kDeRules[]  = {
    {"place", "region",  10, 0},
    {"place", "county",  12, 0},
    {"boundary", "administrative5", 10, 0},
};
static const Rule kBeRules[]  = {
    {"boundary", "administrative3",   5,  0},
    {"boundary", "administrative4",   6,  6},
    {"boundary", "administrative5",   7,  0},
    {"boundary", "administrative6",   8,  8},
    {"boundary", "administrative7",  12, 12},
    {"boundary", "administrative8",  14, 14},
    {"boundary", "administrative9",  16, 16},
    {"boundary", "administrative10", 18, 18},
};
static const Rule kBrRules[]  = {
    {"boundary", "administrative5", 10, 0},
    {"boundary", "administrative6", 12, 0},
    {"boundary", "administrative7", 14, 0},
};
static const Rule kNordicRules[] = {
    {"boundary", "administrative3",  8,  8},
    {"boundary", "administrative4", 12, 12},
};
static const Rule kIdRules[]  = {
    {"place", "municipality", 18, 18},
    {"boundary", "administrative5",  12, 12},
    {"boundary", "administrative6",  14, 14},
    {"boundary", "administrative7",  16, 16},
    {"boundary", "administrative8",  20, 20},
    {"boundary", "administrative9",  22, 22},
    {"boundary", "administrative10", 24, 24},
};
static const Rule kRuRules[]  = {
    {"place", "municipality", 18, 18},
    {"boundary", "administrative5", 10, 0},
    {"boundary", "administrative7", 13, 0},
    {"boundary", "administrative8", 14, 14},
};
static const Rule kNlRules[]  = {
    {"boundary", "administrative7",  13, 0},
    {"boundary", "administrative8",  14, 14},
    {"boundary", "administrative9",  15, 0},
    {"boundary", "administrative10", 16, 16},
};
static const Rule kEsRules[]  = {
    {"place", "province",     10, 10},
    {"place", "civil_parish", 18, 18},
    {"boundary", "administrative5",  10, 0},
    {"boundary", "administrative6",  10, 10},
    {"boundary", "administrative7",  12, 12},
    {"boundary", "administrative10", 22, 22},
};
static const Rule kSaRules[]  = {
    {"place", "province",     12, 12},
    {"place", "municipality", 18, 18},
};
static const Rule kJpRules[]  = {
    {"boundary", "administrative7",  16, 16},
    {"boundary", "administrative8",  18, 18},
    {"boundary", "administrative9",  20, 20},
    {"boundary", "administrative10", 22, 22},
    {"boundary", "administrative11", 24, 24},
};

static const CountryOverride kCountryOverrides[] = {
    {"au", kAuRules,    sizeof(kAuRules)/sizeof(Rule)},
    {"ca", kCaRules,    sizeof(kCaRules)/sizeof(Rule)},
    {"cz", kCzRules,    sizeof(kCzRules)/sizeof(Rule)},
    {"de", kDeRules,    sizeof(kDeRules)/sizeof(Rule)},
    {"be", kBeRules,    sizeof(kBeRules)/sizeof(Rule)},
    {"br", kBrRules,    sizeof(kBrRules)/sizeof(Rule)},
    {"se", kNordicRules, sizeof(kNordicRules)/sizeof(Rule)},
    {"no", kNordicRules, sizeof(kNordicRules)/sizeof(Rule)},
    {"id", kIdRules,    sizeof(kIdRules)/sizeof(Rule)},
    {"ru", kRuRules,    sizeof(kRuRules)/sizeof(Rule)},
    {"nl", kNlRules,    sizeof(kNlRules)/sizeof(Rule)},
    {"es", kEsRules,    sizeof(kEsRules)/sizeof(Rule)},
    {"sa", kSaRules,    sizeof(kSaRules)/sizeof(Rule)},
    {"jp", kJpRules,    sizeof(kJpRules)/sizeof(Rule)},
};

// Semantic level enum stored in the index.
enum class Semantic : uint8_t {
    None     = 0,
    Country  = 1,
    State    = 2,
    County   = 3,
    City     = 4,
    Suburb   = 5,
    Postcode = 6,
};

// Look up rank for (country, tag_key, tag_value). is_polygon picks polygon vs node rank.
// Returns -1 if no rule matches (caller should skip this entry).
inline int8_t lookup_rank(const char* country_code, const char* key,
                          const char* value, bool is_polygon) {
    auto find_in = [&](const Rule* rules, size_t n) -> int8_t {
        for (size_t i = 0; i < n; i++) {
            if (std::strcmp(rules[i].key, key) == 0 &&
                std::strcmp(rules[i].value, value) == 0) {
                int8_t r = is_polygon ? rules[i].polygon_rank : rules[i].node_rank;
                return r;
            }
        }
        return -1;
    };

    if (country_code && country_code[0]) {
        char lower[3] = {static_cast<char>(std::tolower(country_code[0])),
                         static_cast<char>(std::tolower(country_code[1])), 0};
        for (const auto& ov : kCountryOverrides) {
            if (std::strcmp(ov.country_code, lower) == 0) {
                int8_t r = find_in(ov.rules, ov.rule_count);
                if (r >= 0) return r;
                break;
            }
        }
    }
    return find_in(kDefaultRules, sizeof(kDefaultRules)/sizeof(Rule));
}

// Map a Nominatim rank to our semantic level. Returns Semantic::None for
// ranks outside our address chain (highway, building, etc.).
inline Semantic rank_to_semantic(int8_t rank) {
    if (rank >= 4  && rank <= 7)  return Semantic::Country;
    if (rank >= 8  && rank <= 11) return Semantic::State;
    if (rank >= 12 && rank <= 15) return Semantic::County;
    if (rank >= 16 && rank <= 17) return Semantic::City;
    if (rank >= 18 && rank <= 21) return Semantic::Suburb;
    return Semantic::None;
}

} // namespace address_levels

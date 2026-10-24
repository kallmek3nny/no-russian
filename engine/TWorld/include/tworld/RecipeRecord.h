#pragma once
// Legend Engine T - infusion recipe records (Sections 8, 14, 21, 25): decodes data/recipes/*.json and
// mods/<id>/recipes.json into RecipeDef as strictly as the other records. A recipe spends primal aspects
// ("cost":{"motion":4}) to make one item definition; "unlock" names the Thaumonomicon entry gating it.
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcore/Json.h"
#include "tcore/JsonParser.h"
#include "tworld/Research.h"

namespace lt::world {

// Mirrors game::InfusionRecipe field for field, so the game layer converts without loss.
struct RecipeDef {
    std::string id;              // stable key, e.g. infusion.aether_core
    std::string output;          // item definition produced
    AspectPool cost{};           // aspects consumed by every attempt
    std::uint32_t flux = 10;     // flux released into the workshop per attempt
    std::uint32_t risk = 10;     // base failure chance, percent
    std::string requiredUnlock;  // Thaumonomicon unlock gating the recipe (empty = none)
};

inline constexpr std::string_view kRecipeRecordKeys[] = {"id", "output", "cost", "flux", "risk", "unlock"};
inline constexpr std::uint32_t kMaxRecipeAspectCost = 10000;
inline constexpr std::uint32_t kMaxRecipeFlux = 1000;

namespace recipe_detail {

inline bool fail(std::string& error, std::string msg) { error = std::move(msg); return false; }

// Ids end up in Thaumonomicon entries ("<id>.<outcome>"), which must stay whitespace-free.
inline bool readKey(const core::Json& j, std::string_view key, std::string& out, std::string& error) {
    const core::Json& v = j[key];
    bool good = v.isString() && !v.str().empty();
    for (char c : v.str()) good = good && c != ' ' && c != '\t' && c != '\n' && c != '\r';
    if (!good) return fail(error, "'" + std::string(key) + "' must be a non-empty string without whitespace");
    out = v.str();
    return true;
}

inline bool readUint(const core::Json& j, std::string_view key, std::uint32_t hi, std::uint32_t& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isInteger() || v.asInt() < 0 || v.asInt() > static_cast<std::int64_t>(hi))
        return fail(error, "'" + std::string(key) + "' must be an integer from 0 to " + std::to_string(hi));
    out = static_cast<std::uint32_t>(v.asInt());
    return true;
}

// "cost" names primal aspects only, each once; a recipe that costs nothing would be a free item farm.
inline bool readCost(const core::Json& j, AspectPool& out, std::string& error) {
    const core::Json& v = j["cost"];
    if (!v.isObject()) return fail(error, "'cost' must be an object of primal aspect amounts");
    AspectPool pool{};
    std::uint32_t total = 0;
    const std::vector<std::string>& keys = v.keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        std::size_t a = kPrimalAspectCount;
        for (std::size_t n = 0; n < kPrimalAspectCount; ++n)
            if (keys[i] == toString(static_cast<Aspect>(n))) a = n;
        if (a == kPrimalAspectCount) return fail(error, "'cost' names unknown aspect '" + keys[i] + "'");
        for (std::size_t k = 0; k < i; ++k)
            if (keys[k] == keys[i]) return fail(error, "'cost' repeats aspect '" + keys[i] + "'");
        if (!readUint(v, keys[i], kMaxRecipeAspectCost, pool[a], error)) return fail(error, "'cost': " + error);
        total += pool[a];
    }
    if (total == 0) return fail(error, "'cost' must spend at least one aspect");
    out = pool;
    return true;
}

}  // namespace recipe_detail

// Decodes one recipe record. "id", "output" and "cost" are required; "flux" and "risk" default to the
// InfusionRecipe defaults and "unlock" to none. Unknown and repeated keys are rejected.
inline bool decodeRecipe(const core::Json& j, RecipeDef& out, std::string& error) {
    namespace rd = recipe_detail;
    if (!j.isObject()) return rd::fail(error, "record must be an object, not " + std::string(j.typeName()));
    const std::vector<std::string>& keys = j.keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        bool known = false;
        for (std::string_view k : kRecipeRecordKeys) known = known || keys[i] == k;
        if (!known) return rd::fail(error, "unknown key '" + keys[i] + "'");
        for (std::size_t k = 0; k < i; ++k)
            if (keys[k] == keys[i]) return rd::fail(error, "duplicate key '" + keys[i] + "'");
    }
    RecipeDef d;
    if (!rd::readKey(j, "id", d.id, error) || !rd::readKey(j, "output", d.output, error) || !rd::readCost(j, d.cost, error))
        return false;
    if (j.contains("flux") && !rd::readUint(j, "flux", kMaxRecipeFlux, d.flux, error)) return false;
    if (j.contains("risk") && !rd::readUint(j, "risk", 100, d.risk, error)) return false;
    if (j.contains("unlock") && !rd::readKey(j, "unlock", d.requiredUnlock, error)) return false;
    out = std::move(d);
    return true;
}

// Result of decoding one recipe document; same shape as BiomeLoadResult and BossLoadResult.
struct RecipeLoadResult {
    std::vector<RecipeDef> recipes;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes a recipe document: a top-level array of recipe records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `recipes` is empty, so a broken file never half-loads.
inline RecipeLoadResult decodeRecipeRecords(const core::Json& doc, std::string_view source = "recipes") {
    RecipeLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of recipe records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        RecipeDef d;
        std::string error;
        bool good = decodeRecipe(rec, d, error);
        for (std::size_t k = 0; good && k < r.recipes.size(); ++k) {
            if (r.recipes[k].id == d.id) {
                error = "duplicate id '" + d.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.recipes.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.recipes.push_back(std::move(d));
    }
    return r;
}

// Parses and decodes recipe JSON text; parse errors read "source:line:col: message".
inline RecipeLoadResult parseRecipeRecords(std::string_view text, std::string_view source = "recipes") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        RecipeLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeRecipeRecords(p.value, source);
}

// Loads data/recipes/*.json or mods/<id>/recipes.json; the path is the error source.
inline RecipeLoadResult loadRecipeFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        RecipeLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeRecipeRecords(p.value, source);
}

}  // namespace lt::world

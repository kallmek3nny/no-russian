#pragma once
// Legend Engine T - biome records (Sections 4.2, 14, 21, 25): decodes data/biomes/*.json and
// mods/<id>/biomes.json into BiomeDef (tworld/Biome.h) as strictly as the item, enemy, event and boss
// records. Blocks are named ("snow", "packed_ice") so data never embeds raw block ids, and noise feature
// sizes are written in blocks (scale = 1 / feature_size) so a record can reproduce frostCrown() bit for bit.
// Errors read "source: record N (id): message", or "source:line:col: message" for parse errors.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tcore/Json.h"
#include "tcore/JsonParser.h"
#include "tworld/Biome.h"

namespace lt::world {

struct BiomeBlockName {
    std::string_view name;
    BlockId id;
};

// Stable data names for every generator block; numeric ids stay engine-side.
inline constexpr BiomeBlockName kBiomeBlockNames[] = {
    {"bedrock", blocks::kBedrock},                {"stone", blocks::kStone},
    {"frozen_dirt", blocks::kFrozenDirt},         {"snow", blocks::kSnow},
    {"ice", blocks::kIce},                        {"packed_ice", blocks::kPackedIce},
    {"frost_iron_ore", blocks::kFrostIronOre},    {"aurora_crystal", blocks::kAuroraCrystal},
    {"taumium_ore", blocks::kTaumiumOre},         {"frost_pine_log", blocks::kFrostPineLog},
    {"frost_pine_needles", blocks::kFrostPineNeedles}, {"ice_spire", blocks::kIceSpire},
};

// Block id for a data name; kAir when unknown, since air is never a valid generator block.
inline BlockId biomeBlockByName(std::string_view name) noexcept {
    for (const BiomeBlockName& b : kBiomeBlockNames)
        if (b.name == name) return b.id;
    return kAir;
}

// Data name for a block id; empty when the id has none.
inline std::string_view biomeBlockName(BlockId id) noexcept {
    for (const BiomeBlockName& b : kBiomeBlockNames)
        if (b.id == id) return b.name;
    return {};
}

namespace biome_detail {

inline bool fail(std::string& error, std::string msg) {
    error = std::move(msg);
    return false;
}

inline std::string quoteKey(std::string_view key) { return "'" + std::string(key) + "'"; }

// Rejects unknown and repeated keys so a typo never silently falls back to a default.
template <std::size_t N>
inline bool keysKnown(const core::Json& j, const std::string_view (&names)[N], std::string& error) {
    const std::vector<std::string>& keys = j.keys();
    for (std::size_t i = 0; i < keys.size(); ++i) {
        bool known = false;
        for (std::string_view w : names) known = known || keys[i] == w;
        if (!known) return fail(error, "unknown key " + quoteKey(keys[i]));
        for (std::size_t k = 0; k < i; ++k)
            if (keys[k] == keys[i]) return fail(error, "duplicate key " + quoteKey(keys[i]));
    }
    return true;
}

inline bool readName(const core::Json& j, std::string_view key, std::string& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isString() || v.str().empty()) return fail(error, quoteKey(key) + " must be a non-empty string");
    out = v.str();
    return true;
}

inline bool readInt(const core::Json& j, std::string_view key, int lo, int hi, int& out, std::string& error) {
    const core::Json& v = j[key];
    if (!v.isInteger() || v.asInt() < lo || v.asInt() > hi)
        return fail(error, quoteKey(key) + " must be an integer from " + std::to_string(lo) + " to " +
                               std::to_string(hi));
    out = static_cast<int>(v.asInt());
    return true;
}

inline bool readBlock(const core::Json& j, std::string_view key, BlockId& out, std::string& error) {
    const core::Json& v = j[key];
    const BlockId id = v.isString() ? biomeBlockByName(v.str()) : kAir;
    if (id == kAir)
        return fail(error, quoteKey(key) + " must name a known block" +
                               (v.isString() ? ", not " + quoteKey(v.str()) : std::string()));
    out = id;
    return true;
}

// Strictly positive finite number, such as a noise feature size in blocks.
inline bool readPositive(const core::Json& j, std::string_view key, double& out, std::string& error) {
    const core::Json& v = j[key];
    const double d = v.asNumber(0.0);
    if (!v.isNumber() || !std::isfinite(d) || d <= 0.0) return fail(error, quoteKey(key) + " must be a positive number");
    out = d;
    return true;
}

// Highest data Y for strata, caves and ore veins, so a record never writes outside a desktop chunk.
inline int maxDataY() noexcept { return static_cast<int>(kChunkHeightDesktop) - 1; }

// Cave threshold against value noise in [0, 1): above 0 (0 would hollow every column), at most 1 (no caves).
inline bool readThreshold(const core::Json& j, std::string_view key, double& out, std::string& error) {
    const core::Json& v = j[key];
    const double d = v.asNumber(-1.0);
    if (!v.isNumber() || !std::isfinite(d) || d <= 0.0 || d > 1.0)
        return fail(error, quoteKey(key) + " must be a number greater than 0 and at most 1");
    out = d;
    return true;
}

inline bool needObject(const core::Json& j, std::string& error) {
    if (!j.isObject()) return fail(error, "must be an object, not " + std::string(j.typeName()));
    return true;
}

inline constexpr std::string_view kPaletteKeys[] = {"top", "filler", "base", "lake", "cave_lining", "filler_depth"};
inline constexpr std::string_view kTerrainKeys[] = {"base_height", "amplitude",      "valley_depth",
                                                    "feature_size", "sea_level",     "cave_feature_size",
                                                    "cave_threshold", "cave_min_y",  "cave_max_y"};
inline constexpr std::string_view kOreKeys[] = {"block", "min_y", "max_y", "veins_per_chunk", "vein_size"};
inline constexpr std::string_view kVegetationKeys[] = {"id", "trunk", "crown", "per_chunk", "min_height", "max_height"};

// Every palette key is required: a missing block would silently fall back to a Frost Crown default.
inline bool decodePalette(const core::Json& j, SurfacePalette& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kPaletteKeys, error)) return false;
    SurfacePalette p;
    if (!readBlock(j, "top", p.top, error) || !readBlock(j, "filler", p.filler, error) ||
        !readBlock(j, "base", p.base, error) || !readBlock(j, "lake", p.lake, error) ||
        !readBlock(j, "cave_lining", p.caveLining, error) || !readInt(j, "filler_depth", 0, 64, p.fillerDepth, error))
        return false;
    out = p;
    return true;
}

// Feature sizes are in blocks; scale = 1 / feature_size reproduces the 1.0 / 96.0 style constants exactly.
inline bool decodeTerrain(const core::Json& j, TerrainParams& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kTerrainKeys, error)) return false;
    TerrainParams t;
    double feature = 0.0, caveFeature = 0.0;
    const int top = maxDataY();
    if (!readInt(j, "base_height", 1, top, t.baseHeight, error) || !readInt(j, "amplitude", 0, top, t.amplitude, error) ||
        !readInt(j, "valley_depth", 0, top, t.valleyDepth, error) || !readPositive(j, "feature_size", feature, error) ||
        !readInt(j, "sea_level", 0, top, t.seaLevel, error) ||
        !readPositive(j, "cave_feature_size", caveFeature, error) ||
        !readThreshold(j, "cave_threshold", t.caveThreshold, error) ||
        !readInt(j, "cave_min_y", 1, top, t.caveMinY, error) || !readInt(j, "cave_max_y", 1, top, t.caveMaxY, error))
        return false;
    if (t.caveMaxY < t.caveMinY) return fail(error, "'cave_max_y' must not be below 'cave_min_y'");
    t.scale = 1.0 / feature;
    t.caveScale = 1.0 / caveFeature;
    out = t;
    return true;
}

// One ore vein rule; Y starts at 1 so bedrock (y = 0) is never replaced.
inline bool decodeOre(const core::Json& j, OreEntry& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kOreKeys, error)) return false;
    OreEntry o;
    int veins = 0;
    const int top = maxDataY();
    if (!readBlock(j, "block", o.block, error) || !readInt(j, "min_y", 1, top, o.minY, error) ||
        !readInt(j, "max_y", 1, top, o.maxY, error) || !readInt(j, "veins_per_chunk", 0, 1024, veins, error) ||
        !readInt(j, "vein_size", 1, 4096, o.veinSize, error))
        return false;
    if (o.maxY < o.minY) return fail(error, "'max_y' must not be below 'min_y'");
    o.veinsPerChunk = static_cast<std::uint32_t>(veins);
    out = o;
    return true;
}

// One vegetation grammar rule; trunk and crown may be the same block (ice spires have no crown).
inline bool decodeVegetation(const core::Json& j, VegetationRule& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kVegetationKeys, error)) return false;
    VegetationRule v;
    int perChunk = 0;
    if (!readName(j, "id", v.id, error) || !readBlock(j, "trunk", v.trunk, error) ||
        !readBlock(j, "crown", v.crown, error) || !readInt(j, "per_chunk", 0, 256, perChunk, error) ||
        !readInt(j, "min_height", 1, 64, v.minHeight, error) || !readInt(j, "max_height", 1, 64, v.maxHeight, error))
        return false;
    if (v.maxHeight < v.minHeight) return fail(error, "'max_height' must not be below 'min_height'");
    v.perChunk = static_cast<std::uint32_t>(perChunk);
    out = std::move(v);
    return true;
}

inline constexpr std::string_view kBiomeRecordKeys[] = {
    "id", "name", "palette", "terrain", "ores", "vegetation", "weather", "ambient_audio",
    "ruin_kit", "enemy_families", "events", "boss_modifiers"};
inline constexpr std::string_view kWeightedKeys[] = {"id", "weight"};
inline constexpr std::string_view kBossModifierKeys[] = {"id", "value"};

// One weighted table entry; a zero weight could never be picked, so weights start at 1.
inline bool decodeWeighted(const core::Json& j, WeightedId& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kWeightedKeys, error)) return false;
    WeightedId w;
    int weight = 0;
    if (!readName(j, "id", w.id, error) || !readInt(j, "weight", 1, 1000000, weight, error)) return false;
    w.weight = static_cast<std::uint32_t>(weight);
    out = std::move(w);
    return true;
}

// A boss modifier scales a boss stat, so it is a positive factor small enough to stay finite as a float.
inline bool decodeBossModifier(const core::Json& j, BossModifier& out, std::string& error) {
    if (!needObject(j, error) || !keysKnown(j, kBossModifierKeys, error)) return false;
    BossModifier m;
    double value = 0.0;
    if (!readName(j, "id", m.id, error) || !readPositive(j, "value", value, error)) return false;
    if (value > 1000.0) return fail(error, "'value' must be at most 1000");
    m.value = static_cast<float>(value);
    out = std::move(m);
    return true;
}

// Optional array key: absent decodes as empty; entries are numbered from 1 in errors ("'ores' entry 2: ...").
template <typename T, typename Decode>
inline bool decodeList(const core::Json& j, std::string_view key, std::vector<T>& out, Decode decode,
                       std::string& error) {
    out.clear();
    const core::Json* v = j.find(key);
    if (v == nullptr) return true;
    if (!v->isArray()) return fail(error, quoteKey(key) + " must be an array, not " + std::string(v->typeName()));
    for (std::size_t i = 0; i < v->size(); ++i) {
        T item;
        if (!decode((*v)[i], item, error))
            return fail(error, quoteKey(key) + " entry " + std::to_string(i + 1) + ": " + error);
        out.push_back(std::move(item));
    }
    return true;
}

// Repeated ids in one table would shadow each other (bossModifier() returns the first match).
template <typename T>
inline bool idsUnique(const std::vector<T>& items, std::string_view key, std::string& error) {
    for (std::size_t i = 0; i < items.size(); ++i)
        for (std::size_t k = 0; k < i; ++k)
            if (items[k].id == items[i].id)
                return fail(error, quoteKey(key) + " repeats id " + quoteKey(items[i].id));
    return true;
}

}  // namespace biome_detail

// Decodes one biome record. "id", "name", "palette" and "terrain" are required; the ore, vegetation and
// weighted tables are optional and default to empty, since an empty table simply picks nothing.
inline bool decodeBiome(const core::Json& j, BiomeDef& out, std::string& error) {
    namespace bd = biome_detail;
    if (!j.isObject()) return bd::fail(error, "record must be an object, not " + std::string(j.typeName()));
    if (!bd::keysKnown(j, bd::kBiomeRecordKeys, error)) return false;
    BiomeDef b;
    if (!bd::readName(j, "id", b.id, error) || !bd::readName(j, "name", b.name, error)) return false;
    if (!bd::decodePalette(j["palette"], b.palette, error)) return bd::fail(error, "'palette': " + error);
    if (!bd::decodeTerrain(j["terrain"], b.terrain, error)) return bd::fail(error, "'terrain': " + error);
    if (!bd::decodeList(j, "ores", b.ores, bd::decodeOre, error) ||
        !bd::decodeList(j, "vegetation", b.vegetation, bd::decodeVegetation, error) ||
        !bd::idsUnique(b.vegetation, "vegetation", error) ||
        !bd::decodeList(j, "weather", b.weather, bd::decodeWeighted, error) ||
        !bd::idsUnique(b.weather, "weather", error) ||
        !bd::decodeList(j, "ambient_audio", b.ambientAudio, bd::decodeWeighted, error) ||
        !bd::idsUnique(b.ambientAudio, "ambient_audio", error) ||
        !bd::decodeList(j, "ruin_kit", b.ruinKit, bd::decodeWeighted, error) ||
        !bd::idsUnique(b.ruinKit, "ruin_kit", error) ||
        !bd::decodeList(j, "enemy_families", b.enemyFamilies, bd::decodeWeighted, error) ||
        !bd::idsUnique(b.enemyFamilies, "enemy_families", error) ||
        !bd::decodeList(j, "events", b.events, bd::decodeWeighted, error) ||
        !bd::idsUnique(b.events, "events", error) ||
        !bd::decodeList(j, "boss_modifiers", b.bossModifiers, bd::decodeBossModifier, error) ||
        !bd::idsUnique(b.bossModifiers, "boss_modifiers", error))
        return false;
    out = std::move(b);
    return true;
}

// Result of decoding one biome document; same shape as BossLoadResult.
struct BiomeLoadResult {
    std::vector<BiomeDef> biomes;
    std::string error;  // empty on success: "source: record N (id): message" or "source:line:col: message"

    bool ok() const noexcept { return error.empty(); }
    explicit operator bool() const noexcept { return ok(); }
};

// Decodes a biome document: a top-level array of biome records, numbered from 1 in file order.
// Stops at the first bad record so the error names it; duplicate ids in one document are rejected.
// On failure `biomes` is empty, so a broken file never half-loads.
inline BiomeLoadResult decodeBiomeRecords(const core::Json& doc, std::string_view source = "biomes") {
    BiomeLoadResult r;
    if (!doc.isArray()) {
        r.error = std::string(source) + ": top level must be an array of biome records, not " + doc.typeName();
        return r;
    }
    for (std::size_t i = 0; i < doc.size(); ++i) {
        const core::Json& rec = doc[i];
        std::string label = std::string(source) + ": record " + std::to_string(i + 1);
        if (rec.isObject() && rec["id"].isString() && !rec["id"].str().empty()) label += " (" + rec["id"].str() + ")";
        BiomeDef b;
        std::string error;
        bool good = decodeBiome(rec, b, error);
        for (std::size_t k = 0; good && k < r.biomes.size(); ++k) {
            if (r.biomes[k].id == b.id) {
                error = "duplicate id '" + b.id + "' (first defined in record " + std::to_string(k + 1) + ")";
                good = false;
            }
        }
        if (!good) {
            r.biomes.clear();
            r.error = label + ": " + error;
            return r;
        }
        r.biomes.push_back(std::move(b));
    }
    return r;
}

// Parses and decodes biome JSON text; parse errors read "source:line:col: message".
inline BiomeLoadResult parseBiomeRecords(std::string_view text, std::string_view source = "biomes") {
    const core::JsonParseResult p = core::parseJson(text);
    if (!p.ok()) {
        BiomeLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeBiomeRecords(p.value, source);
}

// Loads data/biomes/*.json or mods/<id>/biomes.json; the path is the error source.
inline BiomeLoadResult loadBiomeFile(const std::filesystem::path& path) {
    const std::string source = path.generic_string();
    const core::JsonParseResult p = core::loadJsonFile(path);
    if (!p.ok()) {
        BiomeLoadResult r;
        r.error = p.describe(source);
        return r;
    }
    return decodeBiomeRecords(p.value, source);
}

}  // namespace lt::world

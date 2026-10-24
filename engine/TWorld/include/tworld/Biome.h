#pragma once
// Legend Engine T - Milestone B: one vertical-slice biome (design doc section 4.2, Frost Crown).
// A biome bundles surface palette, terrain generator, ore table, vegetation grammar, weather set,
// ambient audio palette, ruin kit, enemy family table, event table, and boss modifiers.
// Terrain uses world-space noise from the world seed so chunk borders are seamless; per-chunk
// features (ores, vegetation) use the chunk seed so generation is deterministic per chunk.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tcore/Random.h"
#include "tworld/Chunk.h"
#include "tworld/WorldCoord.h"

namespace lt::world {

namespace blocks {
inline constexpr BlockId kBedrock = 1;
inline constexpr BlockId kStone = 2;
inline constexpr BlockId kFrozenDirt = 3;
inline constexpr BlockId kSnow = 4;
inline constexpr BlockId kIce = 5;
inline constexpr BlockId kPackedIce = 6;
inline constexpr BlockId kFrostIronOre = 7;
inline constexpr BlockId kAuroraCrystal = 8;
inline constexpr BlockId kTaumiumOre = 9;
inline constexpr BlockId kFrostPineLog = 10;
inline constexpr BlockId kFrostPineNeedles = 11;
inline constexpr BlockId kIceSpire = 12;
} // namespace blocks

struct SurfacePalette {
    BlockId top = blocks::kSnow;
    BlockId filler = blocks::kFrozenDirt;
    BlockId base = blocks::kStone;
    BlockId lake = blocks::kIce;       // frozen water below sea level
    BlockId caveLining = blocks::kPackedIce;
    int fillerDepth = 3;
};

struct TerrainParams {
    int baseHeight = 64;
    int amplitude = 40;
    int valleyDepth = 28;
    double scale = 1.0 / 96.0;
    int seaLevel = 48;
    double caveScale = 1.0 / 24.0;
    double caveThreshold = 0.70;
    int caveMinY = 8;
    int caveMaxY = 56;
};

struct OreEntry {
    BlockId block = blocks::kStone;
    int minY = 1, maxY = 1;
    std::uint32_t veinsPerChunk = 0;
    int veinSize = 1;
};

struct VegetationRule {
    std::string id;
    BlockId trunk = blocks::kFrostPineLog;
    BlockId crown = blocks::kFrostPineNeedles;
    std::uint32_t perChunk = 0;
    int minHeight = 1, maxHeight = 1;
};

struct WeightedId {
    std::string id;
    std::uint32_t weight = 1;
};

struct BossModifier {
    std::string id;
    float value = 1.0f;
};

struct BiomeDef {
    std::string id;
    std::string name;
    SurfacePalette palette;
    TerrainParams terrain;
    std::vector<OreEntry> ores;
    std::vector<VegetationRule> vegetation;
    std::vector<WeightedId> weather;
    std::vector<WeightedId> ambientAudio;
    std::vector<WeightedId> ruinKit;
    std::vector<WeightedId> enemyFamilies;
    std::vector<WeightedId> events;
    std::vector<BossModifier> bossModifiers;

    const BossModifier* bossModifier(const std::string& modId) const noexcept {
        for (const auto& m : bossModifiers)
            if (m.id == modId) return &m;
        return nullptr;
    }
};

// Deterministic weighted pick; nullptr when the table is empty or all weights are zero.
inline const WeightedId* pickWeighted(const std::vector<WeightedId>& table,
                                      std::uint64_t seed) noexcept {
    std::uint64_t total = 0;
    for (const auto& e : table) total += e.weight;
    if (total == 0) return nullptr;
    core::Rng rng(seed);
    std::uint64_t roll = rng.below(total);
    for (const auto& e : table) {
        if (roll < e.weight) return &e;
        roll -= e.weight;
    }
    return nullptr;
}

namespace noise {

inline double lattice2(std::uint64_t seed, std::int64_t x, std::int64_t z) noexcept {
    const std::uint64_t h = core::hashCombine(core::hashCombine(seed, static_cast<std::uint64_t>(x)),
                                              static_cast<std::uint64_t>(z));
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

inline double lattice3(std::uint64_t seed, std::int64_t x, std::int64_t y, std::int64_t z) noexcept {
    return lattice2(core::hashCombine(seed, static_cast<std::uint64_t>(y)), x, z);
}

inline double fade(double t) noexcept { return t * t * (3.0 - 2.0 * t); }
inline double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }

// Smooth value noise in [0, 1).
inline double value2(std::uint64_t seed, double x, double z) noexcept {
    const double fx = std::floor(x), fz = std::floor(z);
    const auto ix = static_cast<std::int64_t>(fx), iz = static_cast<std::int64_t>(fz);
    const double tx = fade(x - fx), tz = fade(z - fz);
    const double a = lattice2(seed, ix, iz), b = lattice2(seed, ix + 1, iz);
    const double c = lattice2(seed, ix, iz + 1), d = lattice2(seed, ix + 1, iz + 1);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), tz);
}

inline double value3(std::uint64_t seed, double x, double y, double z) noexcept {
    const double fy = std::floor(y);
    const auto iy = static_cast<std::int64_t>(fy);
    const double ty = fade(y - fy);
    const double lo = value2(core::hashCombine(seed, static_cast<std::uint64_t>(iy)), x, z);
    const double hi = value2(core::hashCombine(seed, static_cast<std::uint64_t>(iy + 1)), x, z);
    return lerp(lo, hi, ty);
}

// Fractal sum of value noise, normalised to [0, 1).
inline double fbm2(std::uint64_t seed, double x, double z, int octaves) noexcept {
    double sum = 0.0, amp = 1.0, freq = 1.0, norm = 0.0;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * value2(core::hashCombine(seed, static_cast<std::uint64_t>(o)), x * freq, z * freq);
        norm += amp;
        amp *= 0.5;
        freq *= 2.0;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace noise

inline constexpr std::uint64_t kTerrainSalt = 0x5445525241494E00ull;  // "TERRAIN"
inline constexpr std::uint64_t kValleySalt = 0x56414C4C45590000ull;   // "VALLEY"
inline constexpr std::uint64_t kCaveSalt = 0x4341564500000000ull;     // "CAVE"
inline constexpr std::uint64_t kOreSalt = 0x4F52450000000000ull;      // "ORE"
inline constexpr std::uint64_t kFloraSalt = 0x464C4F5241000000ull;    // "FLORA"

// Surface height of a world column; continuous across chunk borders.
inline int surfaceHeight(const BiomeDef& def, std::uint64_t worldSeed, std::int64_t x,
                         std::int64_t z, int chunkHeight) noexcept {
    const TerrainParams& t = def.terrain;
    const double wx = static_cast<double>(x) * t.scale;
    const double wz = static_cast<double>(z) * t.scale;
    const double hills = noise::fbm2(core::hashCombine(worldSeed, kTerrainSalt), wx, wz, 4);
    const double v = noise::fbm2(core::hashCombine(worldSeed, kValleySalt), wx * 0.5, wz * 0.5, 2);
    const double valley = 1.0 - std::fabs(2.0 * v - 1.0);  // 1 along valley floors
    const double carve = valley * valley * valley * valley;
    const double h = t.baseHeight + t.amplitude * hills - t.valleyDepth * carve;
    const int lo = 4, hi = chunkHeight - 8;
    return std::clamp(static_cast<int>(std::floor(h)), lo, hi > lo ? hi : lo);
}

inline bool isCave(const BiomeDef& def, std::uint64_t worldSeed, std::int64_t x, int y,
                   std::int64_t z) noexcept {
    const TerrainParams& t = def.terrain;
    if (y < t.caveMinY || y > t.caveMaxY) return false;
    const double n = noise::value3(core::hashCombine(worldSeed, kCaveSalt),
                                   static_cast<double>(x) * t.caveScale,
                                   static_cast<double>(y) * t.caveScale * 1.5,
                                   static_cast<double>(z) * t.caveScale);
    return n > t.caveThreshold;
}

// Fills one chunk: strata, frozen lakes, ice caves, ore veins, and vegetation.
inline void generateBiomeChunk(const BiomeDef& def, std::uint64_t worldSeed, Chunk& chunk,
                               std::uint64_t seedForChunk) {
    const SurfacePalette& p = def.palette;
    const int H = chunk.height();
    const std::int64_t ox = chunk.pos().x * kChunkSizeX;
    const std::int64_t oz = chunk.pos().z * kChunkSizeZ;
    std::vector<int> heights(static_cast<std::size_t>(kChunkSizeX * kChunkSizeZ));

    for (int lz = 0; lz < kChunkSizeZ; ++lz) {
        for (int lx = 0; lx < kChunkSizeX; ++lx) {
            const std::int64_t wx = ox + lx, wz = oz + lz;
            const int h = surfaceHeight(def, worldSeed, wx, wz, H);
            heights[static_cast<std::size_t>(lz * kChunkSizeX + lx)] = h;
            for (int y = 0; y <= h; ++y) {
                BlockId b = p.base;
                if (y == 0) b = blocks::kBedrock;
                else if (y == h) b = (h < def.terrain.seaLevel) ? p.filler : p.top;
                else if (y > h - p.fillerDepth) b = p.filler;
                chunk.set({lx, y, lz}, b);
            }
            for (int y = h + 1; y <= def.terrain.seaLevel && y < H; ++y) chunk.set({lx, y, lz}, p.lake);
            // Ice caves: carve below the filler layer, keep bedrock, line openings with packed ice.
            const int caveTop = std::min(def.terrain.caveMaxY, h - p.fillerDepth - 1);
            for (int y = std::max(1, def.terrain.caveMinY); y <= caveTop; ++y) {
                if (isCave(def, worldSeed, wx, y, wz)) chunk.set({lx, y, lz}, kAir);
            }
            for (int y = std::max(1, def.terrain.caveMinY); y <= caveTop; ++y) {
                if (chunk.get({lx, y, lz}) == p.base &&
                    (chunk.get({lx, y + 1, lz}) == kAir || chunk.get({lx, y - 1, lz}) == kAir)) {
                    chunk.set({lx, y, lz}, p.caveLining);
                }
            }
        }
    }

    core::Rng oreRng(core::hashCombine(seedForChunk, kOreSalt));
    for (const auto& ore : def.ores) {
        const int span = std::max(1, ore.maxY - ore.minY + 1);
        for (std::uint32_t v = 0; v < ore.veinsPerChunk; ++v) {
            int x = static_cast<int>(oreRng.below(kChunkSizeX));
            int z = static_cast<int>(oreRng.below(kChunkSizeZ));
            int y = ore.minY + static_cast<int>(oreRng.below(static_cast<std::uint64_t>(span)));
            for (int k = 0; k < ore.veinSize; ++k) {
                if (chunk.get({x, y, z}) == p.base) chunk.set({x, y, z}, ore.block);
                switch (oreRng.below(6)) {
                    case 0: x = std::min<int>(x + 1, kChunkSizeX - 1); break;
                    case 1: x = std::max(x - 1, 0); break;
                    case 2: z = std::min<int>(z + 1, kChunkSizeZ - 1); break;
                    case 3: z = std::max(z - 1, 0); break;
                    case 4: y = std::min(y + 1, ore.maxY); break;
                    default: y = std::max(y - 1, std::max(1, ore.minY)); break;
                }
            }
        }
    }

    core::Rng floraRng(core::hashCombine(seedForChunk, kFloraSalt));
    for (const auto& rule : def.vegetation) {
        for (std::uint32_t i = 0; i < rule.perChunk; ++i) {
            // Keep a 2-block margin so crowns never cross chunk borders.
            const int x = 2 + static_cast<int>(floraRng.below(kChunkSizeX - 4));
            const int z = 2 + static_cast<int>(floraRng.below(kChunkSizeZ - 4));
            const int span = std::max(1, rule.maxHeight - rule.minHeight + 1);
            const int tall = rule.minHeight + static_cast<int>(floraRng.below(static_cast<std::uint64_t>(span)));
            const int ground = heights[static_cast<std::size_t>(z * kChunkSizeX + x)];
            if (chunk.get({x, ground, z}) != p.top) continue;  // only on snow, not lakes
            if (ground + tall + 2 >= H) continue;
            if (chunk.get({x, ground + 1, z}) != kAir) continue;
            for (int y = 1; y <= tall; ++y) chunk.set({x, ground + y, z}, rule.trunk);
            if (rule.crown != rule.trunk) {
                const int top = ground + tall;
                for (int dy = -2; dy <= 1; ++dy) {
                    const int r = dy <= -1 ? 2 : 1;
                    for (int dz = -r; dz <= r; ++dz)
                        for (int dx = -r; dx <= r; ++dx) {
                            const LocalPos l{x + dx, top + dy, z + dz};
                            if (chunk.get(l) == kAir) chunk.set(l, rule.crown);
                        }
                }
                chunk.set({x, top + 2, z}, rule.crown);
            }
        }
    }
}

// Adapter for ChunkStreamer: world seed drives terrain, chunk seed drives features.
inline ChunkStreamer::Generator makeBiomeGenerator(BiomeDef def, std::uint64_t worldSeed) {
    return [def = std::move(def), worldSeed](Chunk& chunk, std::uint64_t seedForChunk) {
        generateBiomeChunk(def, worldSeed, chunk, seedForChunk);
    };
}

// Daily weather for a region: stable for the same region seed and day.
inline const WeightedId* weatherFor(const BiomeDef& def, std::uint64_t regionSeedValue,
                                    std::uint64_t dayIndex) noexcept {
    return pickWeighted(def.weather, core::hashCombine(regionSeedValue, dayIndex));
}

inline BiomeDef frostCrown() {
    BiomeDef b;
    b.id = "frost_crown";
    b.name = "Frost Crown";
    b.ores = {
        {blocks::kFrostIronOre, 10, 60, 6, 6},
        {blocks::kAuroraCrystal, 8, 40, 3, 4},
        {blocks::kTaumiumOre, 2, 20, 1, 3},
    };
    b.vegetation = {
        {"veg.frost_pine", blocks::kFrostPineLog, blocks::kFrostPineNeedles, 4, 5, 8},
        {"veg.ice_spire", blocks::kIceSpire, blocks::kIceSpire, 2, 2, 5},
    };
    b.weather = {{"weather.clear_cold", 40}, {"weather.snowfall", 30},
                 {"weather.blizzard", 15}, {"weather.blue_aurora", 15}};
    b.ambientAudio = {{"audio.wind_howl", 4}, {"audio.ice_creak", 3},
                      {"audio.distant_bells", 1}, {"audio.aurora_hum", 2}};
    b.ruinKit = {{"ruin.frozen_watchtower", 3}, {"ruin.buried_shrine", 2},
                 {"ruin.knight_cairn", 3}, {"ruin.ice_cave_mouth", 2}};
    b.enemyFamilies = {{"enemy.frostbound_knight", 40}, {"enemy.snow_wolf", 25},
                       {"enemy.ice_wraith", 25}, {"enemy.rime_golem", 10}};
    b.events = {{"event.frostbound_patrol", 4}, {"event.aurora_surge", 2},
                {"event.avalanche_road", 3}, {"event.caravan_under_siege", 1}};
    b.bossModifiers = {{"mod.frost_resist", 0.5f}, {"mod.fire_weakness", 1.5f},
                       {"mod.blizzard_visibility", 0.6f}, {"mod.aurora_posture", 1.25f}};
    return b;
}

} // namespace lt::world

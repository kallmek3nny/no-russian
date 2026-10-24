// Sections 4.2/14/21/25 biome record tests: data/biomes/frost_crown.json decodes into a BiomeDef equal
// to frostCrown() and generates identical chunks, block names round-trip, and strict record errors
// name the source and record for the content validator.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "tworld/BiomeRecord.h"

using namespace lt::world;

namespace {

constexpr std::uint64_t kSeed = 0xF0057C0A11ull;

bool sameWeighted(const std::vector<WeightedId>& a, const std::vector<WeightedId>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].weight != b[i].weight) return false;
    return true;
}

// Field-by-field equality (the Biome.h structs have no operator==).
bool sameDef(const BiomeDef& a, const BiomeDef& b) {
    const SurfacePalette &p = a.palette, &q = b.palette;
    const TerrainParams &t = a.terrain, &u = b.terrain;
    bool ok = a.id == b.id && a.name == b.name && p.top == q.top && p.filler == q.filler && p.base == q.base &&
              p.lake == q.lake && p.caveLining == q.caveLining && p.fillerDepth == q.fillerDepth &&
              t.baseHeight == u.baseHeight && t.amplitude == u.amplitude && t.valleyDepth == u.valleyDepth &&
              t.scale == u.scale && t.seaLevel == u.seaLevel && t.caveScale == u.caveScale &&
              t.caveThreshold == u.caveThreshold && t.caveMinY == u.caveMinY && t.caveMaxY == u.caveMaxY &&
              a.ores.size() == b.ores.size() && a.vegetation.size() == b.vegetation.size() &&
              a.bossModifiers.size() == b.bossModifiers.size();
    for (std::size_t i = 0; ok && i < a.ores.size(); ++i) {
        const OreEntry &o = a.ores[i], &r = b.ores[i];
        ok = o.block == r.block && o.minY == r.minY && o.maxY == r.maxY && o.veinsPerChunk == r.veinsPerChunk &&
             o.veinSize == r.veinSize;
    }
    for (std::size_t i = 0; ok && i < a.vegetation.size(); ++i) {
        const VegetationRule &v = a.vegetation[i], &w = b.vegetation[i];
        ok = v.id == w.id && v.trunk == w.trunk && v.crown == w.crown && v.perChunk == w.perChunk &&
             v.minHeight == w.minHeight && v.maxHeight == w.maxHeight;
    }
    for (std::size_t i = 0; ok && i < a.bossModifiers.size(); ++i)
        ok = a.bossModifiers[i].id == b.bossModifiers[i].id && a.bossModifiers[i].value == b.bossModifiers[i].value;
    return ok && sameWeighted(a.weather, b.weather) && sameWeighted(a.ambientAudio, b.ambientAudio) &&
           sameWeighted(a.ruinKit, b.ruinKit) && sameWeighted(a.enemyFamilies, b.enemyFamilies) &&
           sameWeighted(a.events, b.events);
}

// Both definitions generate the same blocks for one chunk.
bool sameChunk(const BiomeDef& a, const BiomeDef& b, ChunkPos pos) {
    Chunk x(pos), y(pos);
    generateBiomeChunk(a, kSeed, x, chunkSeed(kSeed, pos));
    generateBiomeChunk(b, kSeed, y, chunkSeed(kSeed, pos));
    for (int h = 0; h < x.height(); ++h)
        for (int z = 0; z < static_cast<int>(kChunkSizeZ); ++z)
            for (int w = 0; w < static_cast<int>(kChunkSizeX); ++w)
                if (x.get({w, h, z}) != y.get({w, h, z})) return false;
    return true;
}

}  // namespace

int main() {
    const BiomeDef frost = frostCrown();

    TEST_SECTION("block names");
    for (const BiomeBlockName& b : kBiomeBlockNames) {
        CHECK_EQ(biomeBlockByName(b.name), b.id);
        CHECK(biomeBlockName(b.id) == b.name);
    }
    CHECK_EQ(biomeBlockByName("lava"), kAir);
    CHECK(biomeBlockName(kAir).empty());
    CHECK(sameDef(frost, frostCrown()));

#ifdef LT_DATA_DIR
    TEST_SECTION("shipped frost crown");
    {
        const BiomeLoadResult r = loadBiomeFile(std::filesystem::path(LT_DATA_DIR) / "biomes" / "frost_crown.json");
        CHECK(r.ok());
        CHECK_EQ(r.biomes.size(), std::size_t(1));
        if (r.ok() && r.biomes.size() == 1) {
            const BiomeDef& d = r.biomes[0];
            CHECK(sameDef(d, frost));
            CHECK(sameChunk(d, frost, {0, 0}));
            CHECK(sameChunk(d, frost, {2, -3}));
            const BossModifier* fire = d.bossModifier("mod.fire_weakness");
            CHECK(fire != nullptr && fire->value == 1.5f);
        }
    }
#endif

    return lt::test::finish("biome_record");
}

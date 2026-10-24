// Milestone B biome tests: Frost Crown definition, deterministic generation, seamless terrain
// across chunk borders, strata/ore/vegetation placement, weighted tables, and streamer wiring.
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>

#include "TestHarness.h"
#include "tworld/Biome.h"

using namespace lt::world;

namespace {

constexpr std::uint64_t kSeed = 0xF0057C0A11ull;

Chunk generate(const BiomeDef& def, ChunkPos pos) {
    Chunk c(pos);
    generateBiomeChunk(def, kSeed, c, chunkSeed(kSeed, pos));
    return c;
}

int countBlock(const Chunk& c, BlockId id) {
    int n = 0;
    for (int y = 0; y < c.height(); ++y)
        for (int z = 0; z < static_cast<int>(kChunkSizeZ); ++z)
            for (int x = 0; x < static_cast<int>(kChunkSizeX); ++x)
                if (c.get({x, y, z}) == id) ++n;
    return n;
}

} // namespace

int main() {
    const BiomeDef def = frostCrown();

    TEST_SECTION("definition");
    {
        CHECK(def.id == "frost_crown");
        CHECK(def.name == "Frost Crown");
        CHECK_EQ(def.ores.size(), std::size_t(3));
        CHECK_EQ(def.vegetation.size(), std::size_t(2));
        CHECK(!def.weather.empty());
        CHECK(!def.ambientAudio.empty());
        CHECK(!def.ruinKit.empty());
        CHECK(!def.enemyFamilies.empty());
        CHECK(!def.events.empty());
        const BossModifier* fire = def.bossModifier("mod.fire_weakness");
        CHECK(fire != nullptr);
        if (fire) CHECK(fire->value > 1.0f);
        CHECK(def.bossModifier("mod.missing") == nullptr);
    }

    TEST_SECTION("weighted tables");
    {
        CHECK(pickWeighted({}, 1) == nullptr);
        const std::vector<WeightedId> zero{{"a", 0}, {"b", 0}};
        CHECK(pickWeighted(zero, 1) == nullptr);
        const std::vector<WeightedId> only{{"a", 0}, {"b", 5}};
        for (std::uint64_t s = 0; s < 32; ++s) {
            const WeightedId* w = pickWeighted(only, s);
            CHECK(w != nullptr && w->id == "b");
        }
        std::set<std::string> seen;
        for (std::uint64_t s = 0; s < 256; ++s) {
            const WeightedId* w = pickWeighted(def.enemyFamilies, s);
            CHECK(w != nullptr);
            if (w) seen.insert(w->id);
            CHECK(pickWeighted(def.enemyFamilies, s) == w);
        }
        CHECK(seen.size() > 1);
    }

    TEST_SECTION("weather");
    {
        const std::uint64_t rs = regionSeed(kSeed, {0, 0});
        const WeightedId* a = weatherFor(def, rs, 3);
        CHECK(a != nullptr);
        CHECK(weatherFor(def, rs, 3) == a);
        std::set<std::string> days;
        for (std::uint64_t d = 0; d < 64; ++d) {
            const WeightedId* w = weatherFor(def, rs, d);
            if (w) days.insert(w->id);
        }
        CHECK(days.size() > 1);
    }

    TEST_SECTION("determinism");
    {
        const Chunk a = generate(def, {2, -3});
        const Chunk b = generate(def, {2, -3});
        const Chunk c = generate(def, {3, -3});
        CHECK(a.contentHash() == b.contentHash());
        CHECK(a.contentHash() != c.contentHash());
    }

    TEST_SECTION("strata");
    {
        const Chunk c = generate(def, {0, 0});
        bool bedrockOk = true, surfaceOk = true, skyOk = true;
        for (int z = 0; z < static_cast<int>(kChunkSizeZ); ++z) {
            if (c.get({0, 0, z}) != blocks::kBedrock) bedrockOk = false;
            const int h = surfaceHeight(def, kSeed, 0, z, c.height());
            const BlockId top = c.get({0, h, z});
            if (top != def.palette.top && top != def.palette.filler) surfaceOk = false;
            const int clear = std::max(h, def.terrain.seaLevel) + 1;
            if (c.get({0, clear, z}) != kAir) skyOk = false;
        }
        CHECK(bedrockOk);
        CHECK(surfaceOk);
        CHECK(skyOk);
    }

    TEST_SECTION("seamless borders");
    {
        int worst = 0;
        for (std::int64_t z = -64; z < 64; ++z) {
            for (std::int64_t edge : {std::int64_t(0), kChunkSizeX, -kChunkSizeX}) {
                const int l = surfaceHeight(def, kSeed, edge - 1, z, kChunkHeightDesktop);
                const int r = surfaceHeight(def, kSeed, edge, z, kChunkHeightDesktop);
                worst = std::max(worst, std::abs(l - r));
            }
        }
        CHECK(worst <= 4);
        const Chunk left = generate(def, {0, 0});
        const Chunk right = generate(def, {1, 0});
        const int hl = surfaceHeight(def, kSeed, kChunkSizeX - 1, 5, left.height());
        const int hr = surfaceHeight(def, kSeed, kChunkSizeX, 5, right.height());
        CHECK(left.get({static_cast<int>(kChunkSizeX) - 1, hl, 5}) != kAir);
        CHECK(right.get({0, hr, 5}) != kAir);
    }

    TEST_SECTION("ores and vegetation");
    {
        ChunkStreamer streamer(kSeed, 1, makeBiomeGenerator(def, kSeed));
        const auto r = streamer.update({0, 0});
        CHECK_EQ(r.loaded, 9);
        int iron = 0, logs = 0, needles = 0;
        for (std::int64_t dz = -1; dz <= 1; ++dz)
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                const Chunk* c = streamer.find({dx, dz});
                CHECK(c != nullptr);
                if (!c) continue;
                CHECK(!c->dirty());
                iron += countBlock(*c, blocks::kFrostIronOre);
                logs += countBlock(*c, blocks::kFrostPineLog);
                needles += countBlock(*c, blocks::kFrostPineNeedles);
            }
        CHECK(iron > 0);
        CHECK(logs > 0);
        CHECK(needles > 0);
        const Chunk* centre = streamer.find({0, 0});
        if (centre) CHECK(centre->contentHash() == generate(def, {0, 0}).contentHash());
    }

    return lt::test::finish("biome");
}

// Milestone A foundation tests: deterministic seed + world coordinates.
#include "TestHarness.h"
#include "tworld/WorldCoord.h"

using namespace lt;

int main() {
    // Deterministic replay: same seed, same stream.
    core::Rng a(42), b(42), c(43);
    bool same = true, differs = false;
    for (int i = 0; i < 1000; ++i) {
        const auto va = a.next();
        same = same && (va == b.next());
        differs = differs || (va != c.next());
    }
    CHECK(same);
    CHECK(differs);

    core::Rng r(7);
    bool inRange = true;
    for (int i = 0; i < 10000; ++i) inRange = inRange && (r.below(6) < 6);
    CHECK(inRange);
    const double u = r.unit();
    CHECK(u >= 0.0 && u < 1.0);

    // Chunk / region math across negative coordinates.
    CHECK(world::floorDiv(-1, 32) == -1);
    CHECK(world::floorDiv(31, 32) == 0);
    CHECK(world::floorMod(-1, 32) == 31);
    CHECK((world::chunkOf({-1, 0, -33}) == world::ChunkPos{-1, -2}));
    CHECK((world::localInChunk({-1, 64, -33}) == world::LocalPos{31, 64, 31}));
    CHECK((world::regionOf({-1, 16}) == world::RegionPos{-1, 1}));

    // 35,000 km world fits comfortably in 64-bit block coordinates.
    const world::BlockPos farEdge{35'000'000, 0, -35'000'000};
    CHECK((world::chunkOf(farEdge) == world::ChunkPos{1'093'750, -1'093'750}));

    // Seeds are stable and distinct per chunk/region.
    constexpr std::uint64_t seed = 0xC0FFEEull;
    CHECK(world::chunkSeed(seed, {3, 4}) == world::chunkSeed(seed, {3, 4}));
    CHECK(world::chunkSeed(seed, {3, 4}) != world::chunkSeed(seed, {4, 3}));
    CHECK(world::regionSeed(seed, {0, 0}) != world::chunkSeed(seed, {0, 0}));

    // Floating origin keeps world position invariant across rebases.
    world::FloatingOrigin fo;
    world::Vec3 player{100.25, 10.0, -50.5};
    CHECK(!fo.maybeRebase(player));
    player = {600.75, 10.0, -700.25};
    const world::BlockPos before = fo.toWorldBlock(player);
    CHECK(fo.maybeRebase(player));
    CHECK((fo.toWorldBlock(player) == before));
    CHECK(player.x >= 0.0 && player.x < 1.0);
    CHECK(fo.rebaseCount() == 1);

    return lt::test::finish("foundation");
}

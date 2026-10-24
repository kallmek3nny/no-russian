// Milestone A save-journal tests: World gate (unload/reload preserves edits)
// and Save gate (interrupted write recovers from backup).
#include <filesystem>
#include <fstream>

#include "TestHarness.h"
#include "tworld/EditJournal.h"

namespace {
constexpr lt::world::BlockId kStone = 1;
} // namespace

using namespace lt;
using namespace lt::world;

int main() {
    // CRC-32 known answer.
    const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(core::crc32(digits, 9) == 0xCBF43926u);

    // Record, compact, round-trip, reject corruption.
    EditJournal j;
    j.record({1, 20, 1}, 5);
    j.record({1, 20, 1}, 6);
    j.record({-40, 3, 70}, 9);
    CHECK(j.compact() == 1);
    CHECK(j.size() == 2);
    CHECK(j.edits()[0].id == 6);
    const auto rt = EditJournal::deserialize(j.serialize());
    CHECK(rt.has_value());
    CHECK(rt && rt->edits() == j.edits() && rt->nextSeq() == j.nextSeq());
    auto bad = j.serialize();
    bad[10] ^= 0xFF;
    CHECK(!EditJournal::deserialize(bad).has_value());

    // World gate: streamed chunks keep edits across unload/reload.
    EditJournal live;
    ChunkStreamer streamer(0xC0FFEEull, 1, [](Chunk& c, std::uint64_t) {
        for (int y = 0; y < 10; ++y)
            for (int z = 0; z < kChunkSizeZ; ++z)
                for (int x = 0; x < kChunkSizeX; ++x) c.set({x, y, z}, kStone);
    });
    streamer.setOnLoad([&live](Chunk& c) { live.applyTo(c); });
    CHECK(streamer.update({0, 0}).loaded == 9);
    const BlockPos dug{5, 9, 5};
    const BlockPos placed{-3, 30, 12};
    live.record(dug, kAir);
    live.record(placed, 42);
    Chunk* dugChunk = streamer.find(chunkOf(dug));
    Chunk* placedChunk = streamer.find(chunkOf(placed));
    CHECK(dugChunk && placedChunk);
    if (dugChunk) dugChunk->set(localInChunk(dug), kAir);
    if (placedChunk) placedChunk->set(localInChunk(placed), 42);
    const auto hashBefore = placedChunk ? placedChunk->contentHash() : 0;
    CHECK(streamer.update({100, 100}).unloaded == 9);
    CHECK(streamer.find(chunkOf(dug)) == nullptr);
    streamer.update({0, 0});
    dugChunk = streamer.find(chunkOf(dug));
    placedChunk = streamer.find(chunkOf(placed));
    CHECK(dugChunk && dugChunk->get(localInChunk(dug)) == kAir);
    CHECK(dugChunk && dugChunk->get(localInChunk({5, 8, 5})) == kStone);
    CHECK(placedChunk && placedChunk->get(localInChunk(placed)) == 42);
    CHECK(placedChunk && placedChunk->contentHash() == hashBefore);

    // Save gate: atomic writes, rolling backups, recovery from a broken save.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "thaumtmourge_journal_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "journal_r0_0.tmj";
    EditJournal first;
    first.record({1, 1, 1}, 3);
    CHECK(first.saveAtomic(file));
    CHECK(live.saveAtomic(file));
    CHECK(fs::exists(EditJournal::withSuffix(file, ".bak1")));
    CHECK(!fs::exists(EditJournal::withSuffix(file, ".tmp")));
    int src = -1;
    auto loaded = EditJournal::loadWithRecovery(file, &src);
    CHECK(loaded && src == 0 && loaded->edits() == live.edits());
    {
        // Simulate an interrupted write by truncating the main save.
        std::ofstream f(file, std::ios::binary | std::ios::trunc);
        f << "TMJ";
    }
    loaded = EditJournal::loadWithRecovery(file, &src);
    CHECK(loaded && src == 1 && loaded->edits() == first.edits());
    CHECK(live.saveAtomic(file));
    CHECK(fs::exists(EditJournal::withSuffix(file, ".bak2")));
    fs::remove_all(dir);

    return lt::test::finish("journal");
}

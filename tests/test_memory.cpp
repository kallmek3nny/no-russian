// TCore memory tests (Sections 15, 19, 23): linear-arena alignment, exhaustion without throwing,
// rewindable markers, nested scratch scopes, peak tracking and value-initialised placement.
#include <cstddef>
#include <cstdint>
#include <utility>

#include "TestHarness.h"
#include "tcore/Memory.h"

using namespace lt::core;

namespace {
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
struct alignas(64) CacheLine {
    std::uint8_t bytes[64];
};
}  // namespace

int main() {
    TEST_SECTION("helpers");
    CHECK(isPowerOfTwo(1));
    CHECK(isPowerOfTwo(64));
    CHECK(!isPowerOfTwo(0));
    CHECK(!isPowerOfTwo(48));
    CHECK_EQ(alignUp(0, 16), 0u);
    CHECK_EQ(alignUp(1, 16), 16u);
    CHECK_EQ(alignUp(16, 16), 16u);
    CHECK_EQ(alignUp(17, 8), 24u);

    TEST_SECTION("allocation and alignment");
    LinearArena arena(1024);
    CHECK_EQ(arena.capacity(), 1024u);
    CHECK_EQ(arena.used(), 0u);
    CHECK_EQ(arena.remaining(), 1024u);
    void* a = arena.allocate(3, 1);
    CHECK(a != nullptr);
    CHECK_EQ(arena.used(), 3u);
    void* b = arena.allocate(8, 8);
    CHECK(b != nullptr);
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(b) % 8, 0u);
    CHECK(static_cast<std::byte*>(b) >= static_cast<std::byte*>(a) + 3);
    Vec3* v = arena.create<Vec3>(Vec3{1.0f, 2.0f, 3.0f});
    CHECK(v != nullptr && v->x == 1.0f && v->y == 2.0f && v->z == 3.0f);
    CacheLine* line = arena.create<CacheLine>();
    CHECK(line != nullptr);
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(line) % 64, 0u);
    CHECK(line != nullptr && line->bytes[0] == 0 && line->bytes[63] == 0);
    int* ints = arena.createArray<int>(16);
    CHECK(ints != nullptr);
    bool zeroed = ints != nullptr;
    for (int i = 0; zeroed && i < 16; ++i) zeroed = ints[i] == 0;
    CHECK(zeroed);
    CHECK(arena.peak() == arena.used());

    TEST_SECTION("invalid alignment");
    const std::uint64_t failuresBefore = arena.failures();
    const std::size_t usedBefore = arena.used();
    CHECK(arena.allocate(4, 3) == nullptr);
    CHECK_EQ(arena.failures(), failuresBefore + 1);
    CHECK_EQ(arena.used(), usedBefore);

    TEST_SECTION("markers and rewind");
    const LinearArena::Marker m = arena.mark();
    CHECK(arena.allocate(100) != nullptr);
    CHECK(arena.used() > usedBefore);
    arena.rewind(m);
    CHECK_EQ(arena.used(), usedBefore);
    arena.rewind(arena.used() + 50);  // markers past the current top are ignored
    CHECK_EQ(arena.used(), usedBefore);

    TEST_SECTION("nested scratch scopes");
    {
        ArenaScope frame(arena);
        CHECK(arena.createArray<float>(32) != nullptr);
        const std::size_t afterOuter = arena.used();
        {
            ArenaScope inner(arena);
            CHECK(arena.allocate(64) != nullptr);
            CHECK(arena.used() > afterOuter);
        }
        CHECK_EQ(arena.used(), afterOuter);
    }
    CHECK_EQ(arena.used(), usedBefore);

    TEST_SECTION("exhaustion never throws");
    LinearArena small(64);
    CHECK(small.allocate(48, 1) != nullptr);
    CHECK(small.allocate(32, 1) == nullptr);
    CHECK_EQ(small.failures(), 1u);
    CHECK_EQ(small.used(), 48u);
    CHECK(small.allocate(16, 1) != nullptr);
    CHECK_EQ(small.remaining(), 0u);
    CHECK(small.createArray<std::uint64_t>(1000) == nullptr);
    CHECK(small.createArray<std::uint64_t>(static_cast<std::size_t>(-1) / 2) == nullptr);
    CHECK_EQ(small.failures(), 3u);

    TEST_SECTION("reset keeps peak");
    CHECK_EQ(small.peak(), 64u);
    small.reset();
    CHECK_EQ(small.used(), 0u);
    CHECK_EQ(small.remaining(), 64u);
    CHECK(small.allocate(8, 1) != nullptr);
    CHECK_EQ(small.peak(), 64u);

    TEST_SECTION("empty and moved arenas");
    LinearArena empty(0);
    CHECK(empty.allocate(1) == nullptr);
    CHECK_EQ(empty.failures(), 1u);
    CHECK_EQ(empty.remaining(), 0u);
    LinearArena moved(std::move(small));
    CHECK_EQ(moved.capacity(), 64u);
    CHECK_EQ(moved.used(), 8u);
    CHECK(moved.allocate(56, 1) != nullptr);
    CHECK_EQ(moved.remaining(), 0u);

    return lt::test::finish("memory");
}

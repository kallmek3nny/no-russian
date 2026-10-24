// Milestone B dungeon tests: Rimeheart Hollow definition, deterministic assembly,
// layout validation across many seeds, and save/reload round-trip.
#include <cstdint>
#include <string>

#include "TestHarness.h"
#include "tworld/Dungeon.h"

using namespace lt::world;

int main() {
    const DungeonDef def = rimeheartHollow();
    const BlockPos anchor{128, 40, -64};

    TEST_SECTION("definition");
    {
        CHECK(def.id == "dungeon.rimeheart_hollow");
        CHECK(def.biome == "frost_crown");
        CHECK(def.bossId == "boss.frost_warden");
        CHECK(def.minSpine <= def.maxSpine);
        int entrances = 0, arenas = 0;
        for (const auto& p : def.prefabs) {
            if (p.role == RoomRole::Entrance) ++entrances;
            if (p.role == RoomRole::BossArena) ++arenas;
        }
        CHECK(entrances > 0);
        CHECK(arenas > 0);
    }

    TEST_SECTION("determinism");
    {
        const DungeonLayout a = generateDungeon(def, anchor, 42);
        const DungeonLayout b = generateDungeon(def, anchor, 42);
        CHECK(a.serialize() == b.serialize());
        bool differs = false;
        for (std::uint64_t s = 43; s < 75 && !differs; ++s)
            if (generateDungeon(def, anchor, s).serialize() != a.serialize()) differs = true;
        CHECK(differs);
    }

    TEST_SECTION("validation across seeds");
    {
        int bad = 0;
        bool sawSide = false;
        for (std::uint64_t s = 0; s < 500; ++s) {
            const DungeonLayout l = generateDungeon(def, anchor, s);
            std::string why;
            if (!validateLayout(l, &why)) { ++bad; continue; }
            int spine = 0;
            for (const auto& r : l.rooms) {
                if (r.onSpine) ++spine;
                else sawSide = true;
            }
            // entrance + spine rooms + arena
            if (spine < def.minSpine + 2 || spine > def.maxSpine + 2) ++bad;
            const PlacedRoom* boss = l.boss();
            if (!boss || boss->index != static_cast<int>(l.rooms.size()) - 1 && boss->onSpine == false) ++bad;
            if (l.rooms.front().origin != anchor) ++bad;
        }
        CHECK_EQ(bad, 0);
        CHECK(sawSide);
    }

    TEST_SECTION("validator rejects broken layouts");
    {
        DungeonLayout empty;
        CHECK(!validateLayout(empty));
        DungeonLayout l = generateDungeon(def, anchor, 7);
        CHECK(validateLayout(l));
        DungeonLayout overlap = l;
        if (overlap.rooms.size() > 1) overlap.rooms[1].origin = overlap.rooms[0].origin;
        CHECK(!validateLayout(overlap));
        DungeonLayout noArena = l;
        for (auto& r : noArena.rooms)
            if (r.role == RoomRole::BossArena) r.role = RoomRole::Corridor;
        CHECK(!validateLayout(noArena));
        DungeonDef broken = def;
        broken.prefabs.clear();
        CHECK(generateDungeon(broken, anchor, 1).rooms.empty());
    }

    TEST_SECTION("save and reload");
    {
        const DungeonLayout l = generateDungeon(def, anchor, 0xBEEFull);
        DungeonLayout back;
        CHECK(DungeonLayout::deserialize(l.serialize(), back));
        CHECK(back.serialize() == l.serialize());
        CHECK(validateLayout(back));
        CHECK(!DungeonLayout::deserialize("garbage", back));
        CHECK(!DungeonLayout::deserialize("dungeon x 1 1\n0 room 99 0 0 0 1 1 1 -1 1\n", back));
    }

    return lt::test::finish("dungeon");
}

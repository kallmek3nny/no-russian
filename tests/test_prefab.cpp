// Sections 4.4/15/25 TTools: schematic validator, nav baker and prefab converter.
#include <cstddef>
#include <string>
#include <vector>

#include "TestHarness.h"
#include "ttools/PrefabConverter.h"

using namespace lt::tools;
using lt::world::kAir;
using lt::world::RoomRole;

namespace {

constexpr BlockId kStone = 1;
constexpr BlockId kCrystal = 2; // solid, emissive Taumium
constexpr BlockId kGrass = 3;   // non-solid decoration

BlockPalette testPalette() {
    BlockPalette p;
    p.blocks = {{kStone, true, false}, {kCrystal, true, true}, {kGrass, false, false}};
    return p;
}

void put(Schematic& s, int x, int y, int z, BlockId id) { s.cells[s.index(Cell{x, y, z})] = id; }

// Open room: solid floor layer, one centred probe, doors on the -X and +X walls.
Schematic makeRoom(const char* id, RoomRole role, int sx, int sy, int sz, BlockId floor = kStone) {
    Schematic s;
    s.id = id;
    s.role = role;
    s.sizeX = sx;
    s.sizeY = sy;
    s.sizeZ = sz;
    s.cells.assign(static_cast<std::size_t>(sx * sy * sz), kAir);
    for (int z = 0; z < sz; ++z)
        for (int x = 0; x < sx; ++x) put(s, x, 0, z, floor);
    s.probes = {Cell{sx / 2, sy / 2, sz / 2}};
    s.doors = {Cell{0, 1, sz / 2}, Cell{sx - 1, 1, sz / 2}};
    return s;
}

int issuesOf(const std::vector<ImportIssue>& issues, IssueKind k) {
    int n = 0;
    for (const auto& i : issues)
        if (i.kind == k) ++n;
    return n;
}

} // namespace

int main() {
    const BlockPalette pal = testPalette();

    TEST_SECTION("valid room converts to a native prefab");
    {
        Schematic s = makeRoom("room.test.hall", RoomRole::Corridor, 8, 4, 8);
        put(s, 0, 1, 4, kGrass); // non-solid decoration inside a doorway is allowed
        const ImportResult r = convertSchematic(s, pal, desktopBudget());
        CHECK(r.ok);
        CHECK(r.issues.empty());
        CHECK_EQ(r.prefab.id, std::string("room.test.hall"));
        CHECK(r.prefab.role == RoomRole::Corridor);
        CHECK_EQ(r.prefab.sizeX, 8);
        CHECK_EQ(r.prefab.sizeY, 4);
        CHECK_EQ(r.prefab.sizeZ, 8);
        CHECK_EQ(r.prefab.weight, 1);
        CHECK_EQ(r.nav.regionCount, 1);
        CHECK_EQ(r.nav.walkableCount(), std::size_t{64});
        CHECK_EQ(r.nav.regionAt(Cell{3, 1, 3}), 0);
        CHECK_EQ(r.nav.regionAt(Cell{3, 2, 3}), -1); // no floor beneath
        CHECK_EQ(r.nav.regionAt(Cell{-1, 1, 0}), -1);
        CHECK(convertSchematic(s, pal, androidBudget()).ok);
    }

    TEST_SECTION("nav bake: one-cell steps join, tall pillar tops are islands");
    {
        Schematic s = makeRoom("room.test.pillars", RoomRole::Combat, 10, 6, 10);
        put(s, 2, 1, 2, kStone); // step block
        put(s, 5, 1, 5, kStone); // two-high pillar
        put(s, 5, 2, 5, kStone);
        const NavGrid a = bakeNav(s, pal);
        const NavGrid b = bakeNav(s, pal);
        CHECK(a.region == b.region);
        CHECK_EQ(a.regionCount, 2);
        CHECK_EQ(a.walkableCount(), std::size_t{100});
        CHECK_EQ(a.regionAt(Cell{2, 1, 2}), -1);
        CHECK_EQ(a.regionAt(Cell{2, 2, 2}), a.regionAt(Cell{0, 1, 0}));
        CHECK(a.regionAt(Cell{5, 3, 5}) >= 0);
        CHECK(a.regionAt(Cell{5, 3, 5}) != a.regionAt(Cell{0, 1, 0}));
        CHECK(convertSchematic(s, pal, desktopBudget()).ok);
    }

    TEST_SECTION("malformed volumes are rejected before baking");
    {
        Schematic s = makeRoom("room.test.short", RoomRole::Corridor, 4, 4, 4);
        s.cells.pop_back();
        const ImportResult r = convertSchematic(s, pal, desktopBudget());
        CHECK(!r.ok);
        CHECK_EQ(r.issues.size(), std::size_t{1});
        CHECK(!r.issues.empty() && r.issues.front().kind == IssueKind::BadDimensions);
        CHECK_EQ(r.nav.regionCount, 0);
        CHECK_EQ(issuesOf(validateSchematic(Schematic{}, pal, desktopBudget()), IssueKind::BadDimensions), 1);
    }

    TEST_SECTION("floating collision islands are rejected, anchored overhangs pass");
    {
        Schematic s = makeRoom("room.test.float", RoomRole::Combat, 8, 4, 8);
        put(s, 4, 3, 4, kStone); // two-cell island hanging in the air
        put(s, 5, 3, 4, kCrystal);
        const std::vector<ImportIssue> a = validateCollisionAndNav(s, pal, bakeNav(s, pal));
        const std::vector<ImportIssue> b = validateCollisionAndNav(s, pal, bakeNav(s, pal));
        CHECK_EQ(issuesOf(a, IssueKind::ImpossibleCollision), 1);
        CHECK_EQ(issuesOf(a, IssueKind::NavHole), 0);
        CHECK_EQ(a.size(), b.size());
        const ImportResult r = convertSchematic(s, pal, desktopBudget());
        CHECK(!r.ok);
        CHECK(issuesOf(r.issues, IssueKind::ImpossibleCollision) >= 1);

        Schematic o = makeRoom("room.test.overhang", RoomRole::Combat, 8, 4, 8);
        put(o, 6, 1, 6, kStone); // column rising from the floor...
        put(o, 6, 2, 6, kStone);
        put(o, 6, 3, 6, kStone);
        put(o, 5, 3, 6, kStone); // ...carrying a face-connected ledge
        CHECK(validateCollisionAndNav(o, pal, bakeNav(o, pal)).empty());
        CHECK(convertSchematic(o, pal, desktopBudget()).ok);
    }

    TEST_SECTION("door checks catch nav holes and blocked exits");
    {
        auto doorIssues = [&pal](const Schematic& room, IssueKind k) {
            return issuesOf(validateCollisionAndNav(room, pal, bakeNav(room, pal)), k);
        };
        const Schematic base = makeRoom("room.test.doors", RoomRole::Corridor, 8, 4, 8);
        CHECK(validateCollisionAndNav(base, pal, bakeNav(base, pal)).empty());

        Schematic none = base;
        none.doors.clear();
        CHECK_EQ(doorIssues(none, IssueKind::NavHole), 1);

        Schematic outside = base;
        outside.doors.push_back(Cell{-1, 1, 4});
        CHECK_EQ(doorIssues(outside, IssueKind::NavHole), 1);

        Schematic interior = base;
        interior.doors.push_back(Cell{3, 1, 3});
        CHECK_EQ(doorIssues(interior, IssueKind::NavHole), 1);
        CHECK_EQ(doorIssues(interior, IssueKind::ImpossibleCollision), 0);

        Schematic blocked = base;
        put(blocked, 0, 1, 4, kStone); // anchored on the floor but fills the doorway
        CHECK_EQ(doorIssues(blocked, IssueKind::ImpossibleCollision), 1);
        CHECK_EQ(doorIssues(blocked, IssueKind::NavHole), 0);

        Schematic pit = base;
        put(pit, 0, 0, 4, kAir); // doorway has no floor to stand on
        CHECK_EQ(doorIssues(pit, IssueKind::NavHole), 1);
        CHECK_EQ(doorIssues(pit, IssueKind::ImpossibleCollision), 0);

        Schematic split = base;
        for (int z = 0; z < 8; ++z)
            for (int y = 1; y < 4; ++y) put(split, 4, y, z, kStone); // full-height dividing wall
        CHECK_EQ(doorIssues(split, IssueKind::NavHole), 1);
        CHECK_EQ(doorIssues(split, IssueKind::ImpossibleCollision), 0);
        CHECK(!convertSchematic(split, pal, desktopBudget()).ok);
    }

    return lt::test::finish("prefab");
}

#pragma once
// Legend Engine T - Milestone B: one vertical-slice dungeon (design doc sections 4.4, 10).
// "Rimeheart Hollow": a Frost Crown ice cave built from authored room prefabs, assembled
// deterministically from a seed into a linear spine with optional side rooms, ending in
// the Frost Warden arena. Layout is pure data so it can be validated and saved/reloaded.
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "tcore/Random.h"
#include "tworld/WorldCoord.h"

namespace lt::world {

enum class RoomRole : std::uint8_t { Entrance = 0, Corridor, Puzzle, Combat, Treasure, Shrine, BossArena };

inline const char* roomRoleName(RoomRole r) noexcept {
    switch (r) {
        case RoomRole::Entrance: return "entrance";
        case RoomRole::Corridor: return "corridor";
        case RoomRole::Puzzle: return "puzzle";
        case RoomRole::Combat: return "combat";
        case RoomRole::Treasure: return "treasure";
        case RoomRole::Shrine: return "shrine";
        case RoomRole::BossArena: return "boss_arena";
    }
    return "unknown";
}

// Engine-native authored room prefab (section 4.4). Size is in 1 m cells.
struct RoomPrefab {
    std::string id;
    RoomRole role = RoomRole::Corridor;
    int sizeX = 16, sizeY = 8, sizeZ = 16;
    int weight = 1;
};

struct DungeonDef {
    std::string id;
    std::string family;   // e.g. "ice_cave"
    std::string biome;    // must match a biome id
    std::string bossId;   // boss placed in the arena
    int minSpine = 4;     // rooms between entrance and arena (inclusive range)
    int maxSpine = 6;
    int sideRoomChancePct = 35;
    std::vector<RoomPrefab> prefabs;
};

struct PlacedRoom {
    int index = 0;
    std::string prefabId;
    RoomRole role = RoomRole::Corridor;
    BlockPos origin;      // min corner in world cells
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    int parent = -1;      // room this one connects back to (-1 for entrance)
    bool onSpine = true;
};

struct DungeonLayout {
    std::string dungeonId;
    std::uint64_t seed = 0;
    std::vector<PlacedRoom> rooms;

    const PlacedRoom* boss() const noexcept {
        for (const auto& r : rooms)
            if (r.role == RoomRole::BossArena) return &r;
        return nullptr;
    }

    std::string serialize() const {
        std::ostringstream os;
        os << "dungeon " << dungeonId << ' ' << seed << ' ' << rooms.size() << '\n';
        for (const auto& r : rooms)
            os << r.index << ' ' << r.prefabId << ' ' << static_cast<int>(r.role) << ' ' << r.origin.x << ' '
               << r.origin.y << ' ' << r.origin.z << ' ' << r.sizeX << ' ' << r.sizeY << ' ' << r.sizeZ << ' '
               << r.parent << ' ' << (r.onSpine ? 1 : 0) << '\n';
        return os.str();
    }

    static bool deserialize(const std::string& text, DungeonLayout& out) {
        std::istringstream is(text);
        std::string tag;
        std::size_t count = 0;
        DungeonLayout l;
        if (!(is >> tag >> l.dungeonId >> l.seed >> count) || tag != "dungeon") return false;
        for (std::size_t i = 0; i < count; ++i) {
            PlacedRoom r;
            int role = 0, spine = 0;
            if (!(is >> r.index >> r.prefabId >> role >> r.origin.x >> r.origin.y >> r.origin.z >> r.sizeX >>
                  r.sizeY >> r.sizeZ >> r.parent >> spine))
                return false;
            if (role < 0 || role > static_cast<int>(RoomRole::BossArena)) return false;
            r.role = static_cast<RoomRole>(role);
            r.onSpine = spine != 0;
            l.rooms.push_back(r);
        }
        out = l;
        return true;
    }
};

inline bool roomsOverlap(const PlacedRoom& a, const PlacedRoom& b) noexcept {
    return a.origin.x < b.origin.x + b.sizeX && b.origin.x < a.origin.x + a.sizeX &&
           a.origin.y < b.origin.y + b.sizeY && b.origin.y < a.origin.y + a.sizeY &&
           a.origin.z < b.origin.z + b.sizeZ && b.origin.z < a.origin.z + a.sizeZ;
}

// Picks a weighted prefab of the given role; returns nullptr if none exists.
inline const RoomPrefab* pickPrefab(const DungeonDef& def, RoomRole role, core::Rng& rng) {
    std::uint64_t total = 0;
    for (const auto& p : def.prefabs)
        if (p.role == role && p.weight > 0) total += static_cast<std::uint64_t>(p.weight);
    if (total == 0) return nullptr;
    std::uint64_t roll = rng.below(total);
    for (const auto& p : def.prefabs) {
        if (p.role != role || p.weight <= 0) continue;
        if (roll < static_cast<std::uint64_t>(p.weight)) return &p;
        roll -= static_cast<std::uint64_t>(p.weight);
    }
    return nullptr;
}

inline constexpr std::int64_t kRoomGap = 4; // corridor cells between connected rooms

// Deterministic assembly: same def + anchor + seed => identical layout on every platform.
// The spine runs along +X; side rooms branch along +/-Z. Rooms never overlap.
inline DungeonLayout generateDungeon(const DungeonDef& def, BlockPos anchor, std::uint64_t seed) {
    DungeonLayout layout;
    layout.dungeonId = def.id;
    layout.seed = seed;
    core::Rng rng(core::hashCombine(seed, 0x44554E47454F4E00ull));

    const RoomRole spineRoles[] = {RoomRole::Corridor, RoomRole::Combat, RoomRole::Puzzle, RoomRole::Combat};
    const RoomRole sideRoles[] = {RoomRole::Treasure, RoomRole::Shrine};

    auto place = [&](const RoomPrefab& p, BlockPos origin, int parent, bool spine) {
        PlacedRoom r;
        r.index = static_cast<int>(layout.rooms.size());
        r.prefabId = p.id;
        r.role = p.role;
        r.origin = origin;
        r.sizeX = p.sizeX;
        r.sizeY = p.sizeY;
        r.sizeZ = p.sizeZ;
        r.parent = parent;
        r.onSpine = spine;
        layout.rooms.push_back(r);
        return r.index;
    };

    const RoomPrefab* entrance = pickPrefab(def, RoomRole::Entrance, rng);
    const RoomPrefab* arena = pickPrefab(def, RoomRole::BossArena, rng);
    if (!entrance || !arena || def.maxSpine < def.minSpine || def.minSpine < 0) return layout;

    int prev = place(*entrance, anchor, -1, true);
    std::int64_t cursorX = anchor.x + entrance->sizeX + kRoomGap;

    const int span = def.maxSpine - def.minSpine + 1;
    const int spineCount = def.minSpine + static_cast<int>(rng.below(static_cast<std::uint64_t>(span)));
    for (int i = 0; i < spineCount; ++i) {
        const RoomRole role = spineRoles[rng.below(4)];
        const RoomPrefab* p = pickPrefab(def, role, rng);
        if (!p) p = pickPrefab(def, RoomRole::Corridor, rng);
        if (!p) continue;
        const int spineIndex = place(*p, BlockPos{cursorX, anchor.y, anchor.z}, prev, true);
        cursorX += p->sizeX + kRoomGap;

        if (static_cast<int>(rng.below(100)) < def.sideRoomChancePct) {
            const RoomPrefab* s = pickPrefab(def, sideRoles[rng.below(2)], rng);
            if (s) {
                const PlacedRoom& host = layout.rooms[static_cast<std::size_t>(spineIndex)];
                const bool north = rng.below(2) == 0;
                const std::int64_t z = north ? host.origin.z + host.sizeZ + kRoomGap
                                             : host.origin.z - kRoomGap - s->sizeZ;
                PlacedRoom probe;
                probe.origin = BlockPos{host.origin.x, anchor.y, z};
                probe.sizeX = s->sizeX;
                probe.sizeY = s->sizeY;
                probe.sizeZ = s->sizeZ;
                bool clear = true;
                for (const auto& r : layout.rooms)
                    if (roomsOverlap(r, probe)) { clear = false; break; }
                if (clear) place(*s, probe.origin, spineIndex, false);
            }
        }
        prev = spineIndex;
    }
    place(*arena, BlockPos{cursorX, anchor.y, anchor.z}, prev, true);
    return layout;
}

// Validation gate: one entrance first, exactly one arena, valid parents, no overlaps,
// and every room reachable from the entrance through parent links.
inline bool validateLayout(const DungeonLayout& l, std::string* why = nullptr) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (l.rooms.empty()) return fail("empty");
    if (l.rooms.front().role != RoomRole::Entrance || l.rooms.front().parent != -1) return fail("entrance");
    int arenas = 0;
    for (std::size_t i = 0; i < l.rooms.size(); ++i) {
        const auto& r = l.rooms[i];
        if (r.index != static_cast<int>(i)) return fail("index");
        if (r.role == RoomRole::BossArena) ++arenas;
        if (i > 0 && (r.parent < 0 || r.parent >= static_cast<int>(i))) return fail("parent");
        if (r.sizeX <= 0 || r.sizeY <= 0 || r.sizeZ <= 0) return fail("size");
        for (std::size_t j = 0; j < i; ++j)
            if (roomsOverlap(r, l.rooms[j])) return fail("overlap");
    }
    if (arenas != 1) return fail("arena");
    return true;
}

// The vertical-slice dungeon: Rimeheart Hollow in the Frost Crown, home of the Frost Warden.
inline DungeonDef rimeheartHollow() {
    DungeonDef d;
    d.id = "dungeon.rimeheart_hollow";
    d.family = "ice_cave";
    d.biome = "frost_crown";
    d.bossId = "boss.frost_warden";
    d.minSpine = 4;
    d.maxSpine = 6;
    d.sideRoomChancePct = 40;
    d.prefabs = {
        {"room.rime.gate", RoomRole::Entrance, 16, 10, 16, 1},
        {"room.rime.icicle_hall", RoomRole::Corridor, 20, 8, 10, 3},
        {"room.rime.frozen_stair", RoomRole::Corridor, 12, 12, 10, 1},
        {"room.rime.knight_barracks", RoomRole::Combat, 24, 10, 20, 2},
        {"room.rime.wolf_den", RoomRole::Combat, 18, 8, 18, 1},
        {"room.rime.aurora_mirrors", RoomRole::Puzzle, 20, 12, 20, 1},
        {"room.rime.cache", RoomRole::Treasure, 10, 6, 10, 1},
        {"room.rime.cold_shrine", RoomRole::Shrine, 12, 10, 12, 1},
        {"room.rime.warden_throne", RoomRole::BossArena, 40, 24, 40, 1},
    };
    return d;
}

} // namespace lt::world

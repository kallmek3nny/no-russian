#pragma once
// Legend Engine T - TTools prefab converter / schematic validator (Sections 4.4, 15, 25).
// Schematic-like imports become engine-native room prefabs only through this converter,
// which validates block IDs, collision, nav data, lighting probes and platform budgets.
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "tworld/Chunk.h"
#include "tworld/Dungeon.h"

namespace lt::tools {

using world::BlockId;

// Engine block registry entry that schematic cells are checked against.
struct BlockInfo {
    BlockId id = 0;
    bool solid = true;     // participates in collision and nav floors
    bool emissive = false; // counts against the emissive budget
};

struct BlockPalette {
    std::vector<BlockInfo> blocks;
    const BlockInfo* find(BlockId id) const noexcept {
        for (const auto& b : blocks)
            if (b.id == id) return &b;
        return nullptr;
    }
    bool solid(BlockId id) const noexcept {
        const BlockInfo* b = find(id);
        return b && b->solid;
    }
};

struct Cell { int x = 0, y = 0, z = 0; };

// Raw import: dense block volume stored x-fastest, then z, then y (same order as Chunk).
struct Schematic {
    std::string id;
    world::RoomRole role = world::RoomRole::Corridor;
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    int weight = 1;
    std::vector<BlockId> cells;
    std::vector<Cell> probes; // baked lighting probe anchors
    std::vector<Cell> doors;  // connection cells: must be walkable and mutually reachable

    bool inBounds(Cell c) const noexcept {
        return c.x >= 0 && c.x < sizeX && c.y >= 0 && c.y < sizeY && c.z >= 0 && c.z < sizeZ;
    }
    std::size_t index(Cell c) const noexcept {
        return (static_cast<std::size_t>(c.y) * static_cast<std::size_t>(sizeZ) +
                static_cast<std::size_t>(c.z)) * static_cast<std::size_t>(sizeX) +
               static_cast<std::size_t>(c.x);
    }
    BlockId at(Cell c) const noexcept {
        return inBounds(c) && index(c) < cells.size() ? cells[index(c)] : world::kAir;
    }
};

// Per-platform import budgets (Sections 4.1, 15, 23). Android uses the 96-cell mobile chunk height.
struct PlatformBudget {
    const char* name = "desktop";
    int maxHeight = world::kChunkHeightDesktop;
    int maxFootprint = 64; // X and Z extent in cells
    int maxProbes = 8;
    int maxEmissive = 512;
    std::size_t maxSolidCells = 65536;
};

inline PlatformBudget desktopBudget() { return {}; }
inline PlatformBudget androidBudget() { return {"android_c31", 96, 64, 4, 128, 32768}; }

enum class IssueKind : std::uint8_t {
    BadDimensions = 0, UnknownBlock, ImpossibleCollision, NavHole, MissingProbe, BadProbe, OverBudget
};

struct ImportIssue {
    IssueKind kind = IssueKind::BadDimensions;
    Cell cell;
    std::string detail;
};

// Static checks: dimensions, unknown block IDs, probe placement and platform budgets.
inline std::vector<ImportIssue> validateSchematic(const Schematic& s, const BlockPalette& palette,
                                                  const PlatformBudget& budget) {
    std::vector<ImportIssue> out;
    auto add = [&](IssueKind k, Cell c, std::string d) { out.push_back({k, c, std::move(d)}); };
    const std::string tier = budget.name;
    if (s.sizeX <= 0 || s.sizeY <= 0 || s.sizeZ <= 0 ||
        s.cells.size() != static_cast<std::size_t>(s.sizeX) * static_cast<std::size_t>(s.sizeY) *
                              static_cast<std::size_t>(s.sizeZ)) {
        add(IssueKind::BadDimensions, {}, "cell count does not match dimensions");
        return out;
    }
    if (s.sizeY > budget.maxHeight) add(IssueKind::OverBudget, {}, "height exceeds " + tier + " chunk height");
    if (s.sizeX > budget.maxFootprint || s.sizeZ > budget.maxFootprint)
        add(IssueKind::OverBudget, {}, "footprint exceeds " + tier + " budget");
    std::size_t solid = 0;
    int emissive = 0;
    for (int y = 0; y < s.sizeY; ++y)
        for (int z = 0; z < s.sizeZ; ++z)
            for (int x = 0; x < s.sizeX; ++x) {
                const Cell c{x, y, z};
                const BlockId id = s.at(c);
                if (id == world::kAir) continue;
                const BlockInfo* b = palette.find(id);
                if (!b) {
                    add(IssueKind::UnknownBlock, c, "unknown block id " + std::to_string(id));
                    continue;
                }
                if (b->solid) ++solid;
                if (b->emissive) ++emissive;
            }
    if (solid > budget.maxSolidCells) add(IssueKind::OverBudget, {}, "solid cells exceed " + tier + " budget");
    if (emissive > budget.maxEmissive) add(IssueKind::OverBudget, {}, "emissive cells exceed " + tier + " budget");
    if (s.probes.empty()) add(IssueKind::MissingProbe, {}, "no lighting probe anchors");
    if (static_cast<int>(s.probes.size()) > budget.maxProbes)
        add(IssueKind::OverBudget, {}, "probe count exceeds " + tier + " budget");
    for (const Cell& p : s.probes) {
        if (!s.inBounds(p)) add(IssueKind::BadProbe, p, "probe outside volume");
        else if (palette.solid(s.at(p))) add(IssueKind::BadProbe, p, "probe inside solid block");
    }
    return out;
}

inline const char* issueKindName(IssueKind k) noexcept {
    switch (k) {
        case IssueKind::BadDimensions: return "bad_dimensions";
        case IssueKind::UnknownBlock: return "unknown_block";
        case IssueKind::ImpossibleCollision: return "impossible_collision";
        case IssueKind::NavHole: return "nav_hole";
        case IssueKind::MissingProbe: return "missing_probe";
        case IssueKind::BadProbe: return "bad_probe";
        case IssueKind::OverBudget: return "over_budget";
    }
    return "unknown";
}

// Nav baker (Section 15 TTools): a cell is walkable when it is open, has a solid floor below
// and open headroom above (or the open top of the volume). Walkable cells are flood-filled
// into connected regions; a character may step one cell up or down between neighbours.
inline bool walkable(const Schematic& s, const BlockPalette& palette, Cell c) noexcept {
    if (!s.inBounds(c) || c.y == 0 || palette.solid(s.at(c))) return false;
    if (!palette.solid(s.at(Cell{c.x, c.y - 1, c.z}))) return false;
    const Cell head{c.x, c.y + 1, c.z};
    return !s.inBounds(head) || !palette.solid(s.at(head));
}

struct NavGrid {
    int sizeX = 0, sizeY = 0, sizeZ = 0;
    int regionCount = 0;
    std::vector<int> region; // -1 = not walkable, otherwise connected region id

    int regionAt(Cell c) const noexcept {
        if (c.x < 0 || c.x >= sizeX || c.y < 0 || c.y >= sizeY || c.z < 0 || c.z >= sizeZ) return -1;
        const std::size_t i = (static_cast<std::size_t>(c.y) * static_cast<std::size_t>(sizeZ) +
                               static_cast<std::size_t>(c.z)) * static_cast<std::size_t>(sizeX) +
                              static_cast<std::size_t>(c.x);
        return i < region.size() ? region[i] : -1;
    }
    std::size_t walkableCount() const noexcept {
        std::size_t n = 0;
        for (int r : region)
            if (r >= 0) ++n;
        return n;
    }
};

inline NavGrid bakeNav(const Schematic& s, const BlockPalette& palette) {
    NavGrid g;
    if (s.sizeX <= 0 || s.sizeY <= 0 || s.sizeZ <= 0) return g;
    g.sizeX = s.sizeX;
    g.sizeY = s.sizeY;
    g.sizeZ = s.sizeZ;
    g.region.assign(static_cast<std::size_t>(s.sizeX) * static_cast<std::size_t>(s.sizeY) *
                        static_cast<std::size_t>(s.sizeZ),
                    -1);
    constexpr int dx[4] = {1, -1, 0, 0};
    constexpr int dz[4] = {0, 0, 1, -1};
    std::vector<Cell> stack;
    for (int y = 0; y < s.sizeY; ++y)
        for (int z = 0; z < s.sizeZ; ++z)
            for (int x = 0; x < s.sizeX; ++x) {
                const Cell start{x, y, z};
                if (g.region[s.index(start)] != -1 || !walkable(s, palette, start)) continue;
                const int id = g.regionCount++;
                g.region[s.index(start)] = id;
                stack.push_back(start);
                while (!stack.empty()) {
                    const Cell c = stack.back();
                    stack.pop_back();
                    for (int d = 0; d < 4; ++d)
                        for (int dy = -1; dy <= 1; ++dy) {
                            const Cell n{c.x + dx[d], c.y + dy, c.z + dz[d]};
                            if (!walkable(s, palette, n) || g.region[s.index(n)] != -1) continue;
                            g.region[s.index(n)] = id;
                            stack.push_back(n);
                        }
                }
            }
    return g;
}

// Collision and nav checks (Section 25 validator): every solid cell must be anchored to the
// floor layer through face-connected solids (no floating collision); doors must sit on the
// room boundary, fit a character (open cell plus headroom), stand on walkable nav, and all
// share one connected nav region so no room exit becomes a nav hole.
inline std::vector<ImportIssue> validateCollisionAndNav(const Schematic& s, const BlockPalette& palette,
                                                        const NavGrid& nav) {
    std::vector<ImportIssue> out;
    auto add = [&](IssueKind k, Cell c, std::string d) { out.push_back({k, c, std::move(d)}); };
    if (s.sizeX <= 0 || s.sizeY <= 0 || s.sizeZ <= 0 ||
        s.cells.size() != static_cast<std::size_t>(s.sizeX) * static_cast<std::size_t>(s.sizeY) *
                              static_cast<std::size_t>(s.sizeZ))
        return out;
    std::vector<std::uint8_t> mark(s.cells.size(), 0); // 0 = unvisited, 1 = anchored, 2 = floating
    std::vector<Cell> stack;
    auto flood = [&](Cell start, std::uint8_t m) {
        std::size_t n = 0;
        mark[s.index(start)] = m;
        stack.push_back(start);
        while (!stack.empty()) {
            const Cell c = stack.back();
            stack.pop_back();
            ++n;
            const Cell nb[6] = {{c.x + 1, c.y, c.z}, {c.x - 1, c.y, c.z}, {c.x, c.y + 1, c.z},
                                {c.x, c.y - 1, c.z}, {c.x, c.y, c.z + 1}, {c.x, c.y, c.z - 1}};
            for (const Cell& q : nb) {
                if (!s.inBounds(q) || mark[s.index(q)] != 0 || !palette.solid(s.at(q))) continue;
                mark[s.index(q)] = m;
                stack.push_back(q);
            }
        }
        return n;
    };
    for (int z = 0; z < s.sizeZ; ++z)
        for (int x = 0; x < s.sizeX; ++x) {
            const Cell c{x, 0, z};
            if (palette.solid(s.at(c)) && mark[s.index(c)] == 0) flood(c, 1);
        }
    for (int y = 1; y < s.sizeY; ++y)
        for (int z = 0; z < s.sizeZ; ++z)
            for (int x = 0; x < s.sizeX; ++x) {
                const Cell c{x, y, z};
                if (!palette.solid(s.at(c)) || mark[s.index(c)] != 0) continue;
                const std::size_t n = flood(c, 2);
                add(IssueKind::ImpossibleCollision, c,
                    "unanchored solid island of " + std::to_string(n) + " cells");
            }
    if (s.doors.empty()) add(IssueKind::NavHole, {}, "no door cells");
    int doorRegion = -1;
    for (const Cell& d : s.doors) {
        if (!s.inBounds(d)) {
            add(IssueKind::NavHole, d, "door outside volume");
            continue;
        }
        if (d.x != 0 && d.z != 0 && d.x != s.sizeX - 1 && d.z != s.sizeZ - 1)
            add(IssueKind::NavHole, d, "door not on room boundary");
        if (palette.solid(s.at(d)) || palette.solid(s.at(Cell{d.x, d.y + 1, d.z}))) {
            add(IssueKind::ImpossibleCollision, d, "door blocked by solid block");
            continue;
        }
        const int r = nav.regionAt(d);
        if (r < 0) {
            add(IssueKind::NavHole, d, "door has no walkable floor");
            continue;
        }
        if (doorRegion < 0) doorRegion = r;
        else if (r != doorRegion) add(IssueKind::NavHole, d, "door unreachable from first door");
    }
    return out;
}

struct ImportResult {
    bool ok = false;
    world::RoomPrefab prefab;
    NavGrid nav;
    std::vector<ImportIssue> issues;
};

// The only route from schematic-like data to an engine-native RoomPrefab (Section 4.4):
// runs the static, collision and nav checks against a platform budget and emits the prefab
// plus its baked nav grid only when no issue was found.
inline ImportResult convertSchematic(const Schematic& s, const BlockPalette& palette,
                                     const PlatformBudget& budget) {
    ImportResult r;
    r.issues = validateSchematic(s, palette, budget);
    if (!r.issues.empty() && r.issues.front().kind == IssueKind::BadDimensions) return r;
    r.nav = bakeNav(s, palette);
    std::vector<ImportIssue> more = validateCollisionAndNav(s, palette, r.nav);
    r.issues.insert(r.issues.end(), more.begin(), more.end());
    if (!r.issues.empty()) return r;
    r.prefab = world::RoomPrefab{s.id, s.role, s.sizeX, s.sizeY, s.sizeZ, s.weight};
    r.ok = true;
    return r;
}

} // namespace lt::tools

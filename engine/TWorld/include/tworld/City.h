#pragma once
// Legend Engine T - Milestone B: one vertical-slice city (design doc section 4.3).
// "Aurelia": the Order capital. The hub core is authored (fixed buildings at fixed offsets:
// gate, research hall, training courtyard, Sealkeeper hall, market...). The surrounding
// districts (farms, mines, shrines, residential) are seeded, so every world gets unique
// outskirts. Layout is pure data so it can be validated and saved/reloaded.
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "tcore/Random.h"
#include "tworld/WorldCoord.h"

namespace lt::world {

enum class BuildingKind : std::uint8_t {
    Gate = 0, ResearchHall, TrainingCourtyard, SealkeeperHall, Market, Inn, Smithy,
    House, Farm, Mine, Shrine, Waystone
};

inline const char* buildingKindName(BuildingKind k) noexcept {
    switch (k) {
        case BuildingKind::Gate: return "gate";
        case BuildingKind::ResearchHall: return "research_hall";
        case BuildingKind::TrainingCourtyard: return "training_courtyard";
        case BuildingKind::SealkeeperHall: return "sealkeeper_hall";
        case BuildingKind::Market: return "market";
        case BuildingKind::Inn: return "inn";
        case BuildingKind::Smithy: return "smithy";
        case BuildingKind::House: return "house";
        case BuildingKind::Farm: return "farm";
        case BuildingKind::Mine: return "mine";
        case BuildingKind::Shrine: return "shrine";
        case BuildingKind::Waystone: return "waystone";
    }
    return "unknown";
}

enum class DistrictKind : std::uint8_t { Farmland = 0, MiningCamp, ShrineGrove, Residential };

inline const char* districtKindName(DistrictKind k) noexcept {
    switch (k) {
        case DistrictKind::Farmland: return "farmland";
        case DistrictKind::MiningCamp: return "mining_camp";
        case DistrictKind::ShrineGrove: return "shrine_grove";
        case DistrictKind::Residential: return "residential";
    }
    return "unknown";
}

// Modular building prefab; footprint in 1 m cells on the XZ plane.
struct BuildingPrefab {
    std::string id;
    BuildingKind kind = BuildingKind::House;
    int sizeX = 8, sizeZ = 8;
    int weight = 1;
};

// Authored hub building at a fixed offset from the city anchor.
struct AuthoredBuilding {
    BuildingPrefab prefab;
    int offsetX = 0, offsetZ = 0;
};

struct DistrictDef {
    DistrictKind kind = DistrictKind::Residential;
    int weight = 1;
    int minBuildings = 3, maxBuildings = 6;
    std::vector<BuildingPrefab> prefabs;
};

struct CityDef {
    std::string id;
    std::string name;
    std::string biome;
    int cellSize = 96;          // city occupies a 3x3 grid of cells; center = authored core
    int lotSize = 24;           // district cells are split into lots, one building per lot
    int minDistricts = 3, maxDistricts = 6;
    std::vector<AuthoredBuilding> core;
    std::vector<DistrictDef> districts;
    std::vector<BuildingKind> requiredServices;  // must appear in the core exactly once
};

struct PlacedDistrict {
    int slot = 0;               // 0..7 ring slot around the core
    DistrictKind kind = DistrictKind::Residential;
};

struct PlacedBuilding {
    std::string prefabId;
    BuildingKind kind = BuildingKind::House;
    BlockPos origin;            // min corner
    int sizeX = 0, sizeZ = 0;
    int district = -1;          // index into districts, -1 = authored core
};

struct CityRoad {
    int district = 0;
    BlockPos from, to;
};

// Ring slot -> cell offset (dx, dz) in cell units, skipping the center.
inline void citySlotOffset(int slot, int& dx, int& dz) noexcept {
    static const int kOff[8][2] = {{-1, -1}, {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}};
    const int s = (slot % 8 + 8) % 8;
    dx = kOff[s][0];
    dz = kOff[s][1];
}

struct CityLayout {
    std::string cityId;
    std::uint64_t seed = 0;
    BlockPos anchor;            // center of the core cell
    int cellSize = 96;
    std::vector<PlacedDistrict> districts;
    std::vector<PlacedBuilding> buildings;

    BlockPos districtCenter(int d) const noexcept {
        int dx = 0, dz = 0;
        citySlotOffset(districts[static_cast<std::size_t>(d)].slot, dx, dz);
        return BlockPos{anchor.x + static_cast<std::int64_t>(dx) * cellSize, anchor.y,
                        anchor.z + static_cast<std::int64_t>(dz) * cellSize};
    }

    const PlacedBuilding* find(BuildingKind k) const noexcept {
        for (const auto& b : buildings)
            if (b.kind == k) return &b;
        return nullptr;
    }

    // One road per district, from the core gate's center to the district center.
    std::vector<CityRoad> roads() const {
        std::vector<CityRoad> out;
        const PlacedBuilding* gate = find(BuildingKind::Gate);
        const BlockPos from = gate ? BlockPos{gate->origin.x + gate->sizeX / 2, anchor.y, gate->origin.z + gate->sizeZ / 2}
                                   : anchor;
        for (std::size_t d = 0; d < districts.size(); ++d)
            out.push_back(CityRoad{static_cast<int>(d), from, districtCenter(static_cast<int>(d))});
        return out;
    }

    std::string serialize() const {
        std::ostringstream os;
        os << "city " << cityId << ' ' << seed << ' ' << anchor.x << ' ' << anchor.y << ' ' << anchor.z << ' '
           << cellSize << ' ' << districts.size() << ' ' << buildings.size() << '\n';
        for (const auto& d : districts) os << "d " << d.slot << ' ' << static_cast<int>(d.kind) << '\n';
        for (const auto& b : buildings)
            os << "b " << b.prefabId << ' ' << static_cast<int>(b.kind) << ' ' << b.origin.x << ' ' << b.origin.y << ' '
               << b.origin.z << ' ' << b.sizeX << ' ' << b.sizeZ << ' ' << b.district << '\n';
        return os.str();
    }

    static bool deserialize(const std::string& text, CityLayout& out) {
        std::istringstream is(text);
        std::string tag;
        std::size_t nd = 0, nb = 0;
        CityLayout l;
        if (!(is >> tag >> l.cityId >> l.seed >> l.anchor.x >> l.anchor.y >> l.anchor.z >> l.cellSize >> nd >> nb) ||
            tag != "city")
            return false;
        if (nd > 8) return false;
        for (std::size_t i = 0; i < nd; ++i) {
            int kind = 0;
            PlacedDistrict d;
            if (!(is >> tag >> d.slot >> kind) || tag != "d") return false;
            if (kind < 0 || kind > static_cast<int>(DistrictKind::Residential) || d.slot < 0 || d.slot > 7) return false;
            d.kind = static_cast<DistrictKind>(kind);
            l.districts.push_back(d);
        }
        for (std::size_t i = 0; i < nb; ++i) {
            int kind = 0;
            PlacedBuilding b;
            if (!(is >> tag >> b.prefabId >> kind >> b.origin.x >> b.origin.y >> b.origin.z >> b.sizeX >> b.sizeZ >>
                  b.district) ||
                tag != "b")
                return false;
            if (kind < 0 || kind > static_cast<int>(BuildingKind::Waystone)) return false;
            b.kind = static_cast<BuildingKind>(kind);
            l.buildings.push_back(b);
        }
        out = l;
        return true;
    }
};

inline bool buildingsOverlap(const PlacedBuilding& a, const PlacedBuilding& b) noexcept {
    return a.origin.x < b.origin.x + b.sizeX && b.origin.x < a.origin.x + a.sizeX &&
           a.origin.z < b.origin.z + b.sizeZ && b.origin.z < a.origin.z + a.sizeZ;
}

// True if the building lies fully inside the cell centered at c with the given size.
inline bool insideCell(const PlacedBuilding& b, BlockPos c, int cellSize) noexcept {
    const std::int64_t half = cellSize / 2;
    return b.origin.x >= c.x - half && b.origin.x + b.sizeX <= c.x + half && b.origin.z >= c.z - half &&
           b.origin.z + b.sizeZ <= c.z + half;
}

inline const BuildingPrefab* pickBuilding(const std::vector<BuildingPrefab>& prefabs, core::Rng& rng) {
    std::uint64_t total = 0;
    for (const auto& p : prefabs)
        if (p.weight > 0) total += static_cast<std::uint64_t>(p.weight);
    if (total == 0) return nullptr;
    std::uint64_t roll = rng.below(total);
    for (const auto& p : prefabs) {
        if (p.weight <= 0) continue;
        if (roll < static_cast<std::uint64_t>(p.weight)) return &p;
        roll -= static_cast<std::uint64_t>(p.weight);
    }
    return nullptr;
}

inline const DistrictDef* pickDistrict(const CityDef& def, core::Rng& rng) {
    std::uint64_t total = 0;
    for (const auto& d : def.districts)
        if (d.weight > 0 && !d.prefabs.empty()) total += static_cast<std::uint64_t>(d.weight);
    if (total == 0) return nullptr;
    std::uint64_t roll = rng.below(total);
    for (const auto& d : def.districts) {
        if (d.weight <= 0 || d.prefabs.empty()) continue;
        if (roll < static_cast<std::uint64_t>(d.weight)) return &d;
        roll -= static_cast<std::uint64_t>(d.weight);
    }
    return nullptr;
}

// Deterministic assembly: same def + anchor + seed => identical city on every platform.
// The core is copied verbatim; districts take distinct ring slots and place one building
// per randomly chosen lot, so district buildings never overlap each other.
inline CityLayout generateCity(const CityDef& def, BlockPos anchor, std::uint64_t seed) {
    CityLayout city;
    city.cityId = def.id;
    city.seed = seed;
    city.anchor = anchor;
    city.cellSize = def.cellSize;
    if (def.core.empty() || def.cellSize <= 0 || def.lotSize <= 0 || def.lotSize > def.cellSize ||
        def.minDistricts < 0 || def.maxDistricts < def.minDistricts || def.maxDistricts > 8)
        return city;

    for (const auto& a : def.core) {
        PlacedBuilding b;
        b.prefabId = a.prefab.id;
        b.kind = a.prefab.kind;
        b.origin = BlockPos{anchor.x + a.offsetX, anchor.y, anchor.z + a.offsetZ};
        b.sizeX = a.prefab.sizeX;
        b.sizeZ = a.prefab.sizeZ;
        b.district = -1;
        city.buildings.push_back(b);
    }

    core::Rng rng(core::hashCombine(seed, 0x43495459414E4348ull));
    int slots[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 7; i > 0; --i) std::swap(slots[i], slots[rng.below(static_cast<std::uint64_t>(i + 1))]);

    const int span = def.maxDistricts - def.minDistricts + 1;
    const int count = def.minDistricts + static_cast<int>(rng.below(static_cast<std::uint64_t>(span)));
    const int lotsPerSide = def.cellSize / def.lotSize;
    const int lotCount = lotsPerSide * lotsPerSide;

    for (int i = 0; i < count; ++i) {
        const DistrictDef* dd = pickDistrict(def, rng);
        if (!dd) break;
        const int dIndex = static_cast<int>(city.districts.size());
        city.districts.push_back(PlacedDistrict{slots[i], dd->kind});
        const BlockPos c = city.districtCenter(dIndex);
        const std::int64_t minX = c.x - def.cellSize / 2, minZ = c.z - def.cellSize / 2;

        std::vector<int> lots(static_cast<std::size_t>(lotCount));
        for (int l = 0; l < lotCount; ++l) lots[static_cast<std::size_t>(l)] = l;
        for (int l = lotCount - 1; l > 0; --l)
            std::swap(lots[static_cast<std::size_t>(l)], lots[rng.below(static_cast<std::uint64_t>(l + 1))]);

        const int bSpan = std::max(0, dd->maxBuildings - dd->minBuildings) + 1;
        const int want = std::min(lotCount, dd->minBuildings + static_cast<int>(rng.below(static_cast<std::uint64_t>(bSpan))));
        for (int k = 0; k < want; ++k) {
            const BuildingPrefab* p = pickBuilding(dd->prefabs, rng);
            if (!p || p->sizeX > def.lotSize || p->sizeZ > def.lotSize) continue;
            const int lot = lots[static_cast<std::size_t>(k)];
            const int lx = lot % lotsPerSide, lz = lot / lotsPerSide;
            const int padX = def.lotSize - p->sizeX, padZ = def.lotSize - p->sizeZ;
            PlacedBuilding b;
            b.prefabId = p->id;
            b.kind = p->kind;
            b.origin = BlockPos{minX + static_cast<std::int64_t>(lx) * def.lotSize + (padX > 0 ? static_cast<int>(rng.below(static_cast<std::uint64_t>(padX + 1))) : 0),
                                anchor.y,
                                minZ + static_cast<std::int64_t>(lz) * def.lotSize + (padZ > 0 ? static_cast<int>(rng.below(static_cast<std::uint64_t>(padZ + 1))) : 0)};
            b.sizeX = p->sizeX;
            b.sizeZ = p->sizeZ;
            b.district = dIndex;
            city.buildings.push_back(b);
        }
    }
    return city;
}

// Validation gate: required services exactly once in the core, distinct district slots,
// every building inside its cell, valid district indices, positive sizes, no overlaps.
inline bool validateCity(const CityLayout& c, const CityDef& def, std::string* why = nullptr) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (c.buildings.empty()) return fail("empty");
    if (c.cellSize <= 0) return fail("cell");
    for (BuildingKind k : def.requiredServices) {
        int n = 0;
        for (const auto& b : c.buildings)
            if (b.kind == k && b.district == -1) ++n;
        if (n != 1) return fail("service");
    }
    bool used[8] = {};
    for (const auto& d : c.districts) {
        if (d.slot < 0 || d.slot > 7 || used[d.slot]) return fail("slot");
        used[d.slot] = true;
    }
    if (static_cast<int>(c.districts.size()) < def.minDistricts || static_cast<int>(c.districts.size()) > def.maxDistricts)
        return fail("district_count");
    for (std::size_t i = 0; i < c.buildings.size(); ++i) {
        const auto& b = c.buildings[i];
        if (b.sizeX <= 0 || b.sizeZ <= 0) return fail("size");
        if (b.district < -1 || b.district >= static_cast<int>(c.districts.size())) return fail("district");
        const BlockPos cell = b.district == -1 ? c.anchor : c.districtCenter(b.district);
        if (!insideCell(b, cell, c.cellSize)) return fail("bounds");
        for (std::size_t j = 0; j < i; ++j)
            if (buildingsOverlap(b, c.buildings[j])) return fail("overlap");
    }
    if (c.roads().size() != c.districts.size()) return fail("roads");
    return true;
}

// The vertical-slice city: Aurelia, Order capital (research tables, training courtyard,
// Sealkeeper politics), set in the temperate Frost Crown foothills.
inline CityDef aurelia() {
    CityDef d;
    d.id = "city.aurelia";
    d.name = "Aurelia";
    d.biome = "frost_crown";
    d.cellSize = 96;
    d.lotSize = 24;
    d.minDistricts = 3;
    d.maxDistricts = 6;
    d.core = {
        {{"bld.aurelia.south_gate", BuildingKind::Gate, 16, 8, 1}, -8, 36},
        {{"bld.aurelia.research_hall", BuildingKind::ResearchHall, 24, 20, 1}, -40, -40},
        {{"bld.aurelia.training_courtyard", BuildingKind::TrainingCourtyard, 28, 24, 1}, 12, -40},
        {{"bld.aurelia.sealkeeper_hall", BuildingKind::SealkeeperHall, 20, 20, 1}, -10, -12},
        {{"bld.aurelia.market", BuildingKind::Market, 24, 16, 1}, -40, 0},
        {{"bld.aurelia.inn", BuildingKind::Inn, 14, 12, 1}, 20, 0},
        {{"bld.aurelia.smithy", BuildingKind::Smithy, 12, 12, 1}, 20, 18},
    };
    d.requiredServices = {BuildingKind::Gate, BuildingKind::ResearchHall, BuildingKind::TrainingCourtyard,
                          BuildingKind::SealkeeperHall, BuildingKind::Market};
    d.districts = {
        {DistrictKind::Farmland, 3, 3, 6,
         {{"bld.farm.wheat", BuildingKind::Farm, 20, 20, 3}, {"bld.farm.barn", BuildingKind::House, 12, 10, 1}}},
        {DistrictKind::MiningCamp, 2, 2, 5,
         {{"bld.mine.shaft", BuildingKind::Mine, 16, 16, 2}, {"bld.mine.bunkhouse", BuildingKind::House, 10, 8, 1}}},
        {DistrictKind::ShrineGrove, 1, 1, 3,
         {{"bld.shrine.aura_node", BuildingKind::Shrine, 12, 12, 2}, {"bld.shrine.waystone", BuildingKind::Waystone, 4, 4, 1}}},
        {DistrictKind::Residential, 3, 5, 10,
         {{"bld.house.cottage", BuildingKind::House, 10, 10, 3}, {"bld.house.manor", BuildingKind::House, 18, 14, 1}}},
    };
    return d;
}

} // namespace lt::world

// Milestone B city tests: Aurelia definition, deterministic assembly, layout validation
// across many seeds, roads, and save/reload round-trip.
#include <cstdint>
#include <string>

#include "TestHarness.h"
#include "tworld/City.h"

using namespace lt::world;

int main() {
    const CityDef def = aurelia();
    const BlockPos anchor{512, 64, -256};

    TEST_SECTION("definition");
    {
        CHECK(def.id == "city.aurelia");
        CHECK(def.biome == "frost_crown");
        CHECK(!def.core.empty());
        CHECK(!def.districts.empty());
        CHECK(def.minDistricts <= def.maxDistricts);
        CHECK(def.maxDistricts <= 8);
        for (BuildingKind k : def.requiredServices) {
            int n = 0;
            for (const auto& a : def.core)
                if (a.prefab.kind == k) ++n;
            CHECK_EQ(n, 1);
        }
        for (const auto& d : def.districts)
            for (const auto& p : d.prefabs) {
                CHECK(p.sizeX <= def.lotSize);
                CHECK(p.sizeZ <= def.lotSize);
            }
    }

    TEST_SECTION("determinism");
    {
        const CityLayout a = generateCity(def, anchor, 42);
        const CityLayout b = generateCity(def, anchor, 42);
        CHECK(a.serialize() == b.serialize());
        bool differs = false;
        for (std::uint64_t s = 43; s < 75 && !differs; ++s)
            if (generateCity(def, anchor, s).serialize() != a.serialize()) differs = true;
        CHECK(differs);
    }

    TEST_SECTION("validation across seeds");
    {
        int bad = 0;
        bool sawFarm = false, sawMine = false;
        for (std::uint64_t s = 0; s < 500; ++s) {
            const CityLayout c = generateCity(def, anchor, s);
            std::string why;
            if (!validateCity(c, def, &why)) { ++bad; continue; }
            int coreCount = 0;
            for (const auto& b : c.buildings) {
                if (b.district == -1) ++coreCount;
                if (b.kind == BuildingKind::Farm) sawFarm = true;
                if (b.kind == BuildingKind::Mine) sawMine = true;
            }
            if (coreCount != static_cast<int>(def.core.size())) ++bad;
            if (!c.find(BuildingKind::Gate)) ++bad;
        }
        CHECK_EQ(bad, 0);
        CHECK(sawFarm);
        CHECK(sawMine);
    }

    TEST_SECTION("roads");
    {
        const CityLayout c = generateCity(def, anchor, 9);
        const auto roads = c.roads();
        CHECK_EQ(roads.size(), c.districts.size());
        const PlacedBuilding* gate = c.find(BuildingKind::Gate);
        CHECK(gate != nullptr);
        for (const auto& r : roads) {
            CHECK(r.to == c.districtCenter(r.district));
            if (gate) CHECK(r.from.x == gate->origin.x + gate->sizeX / 2);
        }
    }

    TEST_SECTION("validator rejects broken layouts");
    {
        CityLayout empty;
        CHECK(!validateCity(empty, def));
        const CityLayout c = generateCity(def, anchor, 7);
        CHECK(validateCity(c, def));

        CityLayout overlap = c;
        overlap.buildings[1].origin = overlap.buildings[0].origin;
        CHECK(!validateCity(overlap, def));

        CityLayout noService = c;
        for (auto& b : noService.buildings)
            if (b.kind == BuildingKind::ResearchHall) b.kind = BuildingKind::House;
        CHECK(!validateCity(noService, def));

        CityLayout outOfBounds = c;
        outOfBounds.buildings[0].origin.x += 1000;
        CHECK(!validateCity(outOfBounds, def));

        CityLayout dupSlot = c;
        if (dupSlot.districts.size() > 1) {
            dupSlot.districts[1].slot = dupSlot.districts[0].slot;
            CHECK(!validateCity(dupSlot, def));
        }

        CityDef broken = def;
        broken.core.clear();
        CHECK(generateCity(broken, anchor, 1).buildings.empty());
    }

    TEST_SECTION("save and reload");
    {
        const CityLayout c = generateCity(def, anchor, 0xBEEFull);
        CityLayout back;
        CHECK(CityLayout::deserialize(c.serialize(), back));
        CHECK(back.serialize() == c.serialize());
        CHECK(validateCity(back, def));
        CHECK(!CityLayout::deserialize("garbage", back));
        CHECK(!CityLayout::deserialize("city x 1 0 0 0 96 1 0\nd 9 0\n", back));
    }

    return lt::test::finish("city");
}

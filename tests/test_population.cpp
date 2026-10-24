// Section 5 population tests: seasons, deterministic region budgets and filler compatibility
// (biome, faction, season, landmark, recent player history, per-region cooldown).
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "TestHarness.h"
#include "tworld/Population.h"

using namespace lt::world;

namespace {

FillerTemplate tmpl(const std::string& id, std::vector<std::string> biomes, std::string faction,
                    std::uint32_t story, std::uint32_t population) {
    FillerTemplate t;
    t.def.id = id;
    t.def.biomes = std::move(biomes);
    t.def.faction = std::move(faction);
    t.def.cooldownHours = 18;
    t.storyCost = story;
    t.populationCost = population;
    return t;
}

}  // namespace

int main() {
    TEST_SECTION("seasons");
    CHECK(seasonForDay(0) == Season::Spring && seasonForDay(29) == Season::Spring);
    CHECK(seasonForDay(30) == Season::Summer && seasonForDay(60) == Season::Autumn);
    CHECK(seasonForDay(90) == Season::Winter && seasonForDay(120) == Season::Spring);
    CHECK(std::string(toString(Season::Winter)) == "winter");

    TEST_SECTION("region budget");
    const RegionBudget b = makeRegionBudget(40, 8, 1234);
    const RegionBudget again = makeRegionBudget(40, 8, 1234);
    CHECK(b.population == again.population && b.story == again.story);
    CHECK(b.population >= 30 && b.population <= 50 && b.story >= 6 && b.story <= 10);
    const RegionBudget tiny = makeRegionBudget(3, 2, 99);
    CHECK(tiny.population == 3 && tiny.story == 2);  // a quarter of 3 or 2 rounds to no spread
    bool varies = false;
    for (std::uint64_t s = 0; s < 32; ++s) {
        const RegionBudget o = makeRegionBudget(40, 8, s);
        varies = varies || o.population != b.population || o.story != b.story;
    }
    CHECK(varies);

    TEST_SECTION("event fits");
    RegionContext ctx;
    ctx.regionX = 2;
    ctx.regionZ = -5;
    ctx.biome = "verdant_reach";
    ctx.factions = {"free_caravans", "order_of_thaumiel"};
    ctx.season = Season::Summer;
    ctx.landmarks = {"landmark.old_road"};
    EventLedger ledger;
    const FillerTemplate caravan =
        tmpl("event.caravan_under_siege", {"verdant_reach", "salt_dune"}, "free_caravans", 2, 6);
    CHECK(eventFits(caravan, ctx, ledger, 0));
    RegionContext other = ctx;
    other.biome = "frost_crown";
    CHECK(!eventFits(caravan, other, ledger, 0));
    other = ctx;
    other.factions = {"ash_tribunal"};
    CHECK(!eventFits(caravan, other, ledger, 0));
    other = ctx;
    other.recentEvents = {"event.caravan_under_siege"};
    CHECK(!eventFits(caravan, other, ledger, 0));
    FillerTemplate eclipse = tmpl("event.shrine_eclipse", {}, "", 1, 0);
    eclipse.seasons = {Season::Winter};
    CHECK(!eventFits(eclipse, ctx, ledger, 0));
    other = ctx;
    other.season = Season::Winter;
    CHECK(eventFits(eclipse, other, ledger, 0));
    FillerTemplate nest = tmpl("event.wyvern_nest", {}, "", 1, 3);
    nest.landmarks = {"landmark.cliff_nest", "landmark.old_road"};
    CHECK(eventFits(nest, ctx, ledger, 0));
    other = ctx;
    other.landmarks = {"landmark.shrine"};
    CHECK(!eventFits(nest, other, ledger, 0));

    TEST_SECTION("cooldown blocks repeats");
    EventInstance& running = ledger.spawn(caravan.def, ctx.regionX, ctx.regionZ, 10);
    CHECK(!eventFits(caravan, ctx, ledger, 10));  // still a rumour in this region
    other = ctx;
    other.regionX = 3;
    CHECK(eventFits(caravan, other, ledger, 10));  // other regions are unaffected
    CHECK(running.advance(12) && running.advance(14));
    CHECK(running.resolve(caravan.def, true, 16) && running.settle(20));
    CHECK(!eventFits(caravan, ctx, ledger, 37));
    CHECK(eventFits(caravan, ctx, ledger, 38));

    TEST_SECTION("fill region");
    FillerTemplate never = tmpl("event.never_weighted", {}, "", 0, 0);
    never.weight = 0;
    const std::vector<FillerTemplate> pool = {
        caravan,
        tmpl("event.lost_research_party", {}, "order_of_thaumiel", 2, 2),
        tmpl("event.bandit_siege", {"verdant_reach"}, "", 3, 8),
        tmpl("event.frost_warden_hunt", {"frost_crown"}, "", 1, 1),  // wrong biome
        eclipse,  // winter only; ctx is summer
        nest,     // fits through landmark.old_road
        never,    // weight 0 is never picked
    };
    auto listed = [](const std::vector<std::string>& v, const std::string& s) {
        for (const auto& x : v)
            if (x == s) return true;
        return false;
    };
    EventLedger fillLedger;
    RegionBudget big{100, 100};
    const std::vector<std::string> placedAll = fillRegion(pool, ctx, big, fillLedger, 777, 50);
    CHECK_EQ(placedAll.size(), std::size_t(4));
    CHECK(listed(placedAll, "event.caravan_under_siege") && listed(placedAll, "event.lost_research_party"));
    CHECK(listed(placedAll, "event.bandit_siege") && listed(placedAll, "event.wyvern_nest"));
    CHECK(!listed(placedAll, "event.frost_warden_hunt") && !listed(placedAll, "event.shrine_eclipse"));
    CHECK(!listed(placedAll, "event.never_weighted"));
    CHECK(big.story == 100 - 8 && big.population == 100 - 19);
    CHECK_EQ(fillLedger.events().size(), placedAll.size());
    bool rumoursHere = true;
    for (std::size_t i = 0; i < fillLedger.events().size() && i < placedAll.size(); ++i) {
        const EventInstance& e = fillLedger.events()[i];
        rumoursHere = rumoursHere && e.defId == placedAll[i] && e.state == EventState::Rumour &&
                      e.regionX == ctx.regionX && e.regionZ == ctx.regionZ && e.stateSinceHour == 50;
    }
    CHECK(rumoursHere);
    RegionBudget more{100, 100};
    CHECK(fillRegion(pool, ctx, more, fillLedger, 777, 51).empty());  // all still running here
    CHECK(more.story == 100 && more.population == 100);

    TEST_SECTION("fill budget and determinism");
    EventLedger la, lb;
    RegionBudget ba{10, 4}, bb{10, 4};
    const std::vector<std::string> pa = fillRegion(pool, ctx, ba, la, 4242, 7);
    const std::vector<std::string> pb = fillRegion(pool, ctx, bb, lb, 4242, 7);
    CHECK(!pa.empty());
    CHECK(pa == pb && la.save() == lb.save());
    CHECK(ba.population == bb.population && ba.story == bb.story);
    std::uint32_t spentStory = 0, spentPopulation = 0;
    bool exhausted = true;
    for (const FillerTemplate& t : pool) {
        if (listed(pa, t.def.id)) {
            spentStory += t.storyCost;
            spentPopulation += t.populationCost;
        } else if (t.weight > 0 && t.storyCost <= ba.story && t.populationCost <= ba.population &&
                   eventFits(t, ctx, la, 7)) {
            exhausted = false;  // an affordable, fitting template was left unplaced
        }
    }
    CHECK(spentStory + ba.story == 4 && spentPopulation + ba.population == 10);
    CHECK(exhausted);
    bool seedsVary = false;
    for (std::uint64_t s = 0; s < 32 && !seedsVary; ++s) {
        EventLedger lv;
        RegionBudget bv{10, 4};
        seedsVary = fillRegion(pool, ctx, bv, lv, s, 7) != pa;
    }
    CHECK(seedsVary);
    EventLedger lz;
    RegionBudget zero{0, 0};
    CHECK(fillRegion(pool, ctx, zero, lz, 1, 1).empty() && lz.events().empty());

    return ::lt::test::finish("population");
}

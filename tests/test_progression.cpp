// Sections 6 and 11 progression tests: starting class kits, rank scaling, research cost
// affinity, per-family weapon masteries, and faction reputation tiers.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "TestHarness.h"
#include "game/Progression.h"

using namespace lt::game;

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

constexpr std::string_view kPrimalAspects[] = {"motion", "form", "life", "decay", "light", "memory"};

constexpr bool reputationIsConstexpr() {
    Reputation r;
    r.adjust(Faction::VerdantCourt, 600);
    return r.standing(Faction::VerdantCourt) == Standing::Honoured;
}

} // namespace

// The progression math is constexpr; prove it evaluates at compile time.
static_assert(rankUpCost(1) == 125);
static_assert(masteryLevelFor(100) == 1);
static_assert(aspectResearchCost(StartClass::Thaumaturge, "memory", 100) == 75);
static_assert(reputationIsConstexpr());

int main() {
    TEST_SECTION("class kits");
    CHECK_EQ(kClassKits.size(), kStartClassCount);
    for (std::size_t i = 0; i < kStartClassCount; ++i) {
        const auto c = static_cast<StartClass>(i);
        const ClassKit& k = kitFor(c);
        CHECK(k.cls == c);
        CHECK(familyIndex(k.mainFamily) >= 0);
        CHECK(k.offFamily.empty() || familyIndex(k.offFamily) >= 0);
        CHECK(k.healthMul > 0.0f && k.staminaMul > 0.0f && k.focusMul > 0.0f);
        CHECK(toString(c) != std::string_view("unknown"));
    }
    CHECK(toString(StartClass::AshenMonk) == std::string_view("ashen_monk"));
    CHECK(kitFor(StartClass::Warden).offFamily == std::string_view("shield"));
    CHECK(kitFor(StartClass::Ranger).mainFamily == std::string_view("bow"));
    for (std::size_t i = 0; i < kFactionCount; ++i)
        CHECK(toString(static_cast<Faction>(i)) != std::string_view("unknown"));
    CHECK(toString(Faction::FreeCaravans) == std::string_view("free_caravans"));

    TEST_SECTION("rank");
    const RankStats w1 = rankStats(StartClass::Warden, 1);
    CHECK(approx(w1.health, 115.0f));
    CHECK(approx(w1.stamina, 88.0f));
    CHECK(approx(w1.focus, 42.5f));
    CHECK(approx(w1.equipCapacity, 30.0f));
    const RankStats w10 = rankStats(StartClass::Warden, 10);
    CHECK(approx(w10.health, 239.2f));
    CHECK(approx(w10.equipCapacity, 43.5f));
    CHECK(w10.stamina > w1.stamina && w10.focus > w1.focus);
    CHECK(approx(rankStats(StartClass::Ranger, 0).health, rankStats(StartClass::Ranger, 1).health));
    CHECK(approx(rankStats(StartClass::Ranger, 500).health, rankStats(StartClass::Ranger, kMaxRank).health));
    CHECK(rankStats(StartClass::Thaumaturge, 1).focus > rankStats(StartClass::Warden, 1).focus);
    CHECK_EQ(rankUpCost(1), std::uint64_t{125});
    CHECK_EQ(rankUpCost(10), std::uint64_t{2600});
    CHECK_EQ(rankUpCost(0), rankUpCost(1));
    CHECK(rankUpCost(20) > rankUpCost(19));

    TEST_SECTION("research cost");
    CHECK_EQ(aspectResearchCost(StartClass::Thaumaturge, "memory", 100), 75u);
    CHECK_EQ(aspectResearchCost(StartClass::Thaumaturge, "decay", 100), 100u);
    CHECK_EQ(aspectResearchCost(StartClass::Golemancer, "decay", 40), 30u);
    // No class is locked out of any aspect, and no aspect costs more than its base.
    for (const ClassKit& k : kClassKits) {
        for (std::string_view a : kPrimalAspects) {
            const std::uint32_t cost = aspectResearchCost(k.cls, a, 100);
            CHECK(cost > 0 && cost <= 100);
        }
    }
    // Each primal aspect is the affinity of exactly one starting class.
    for (std::string_view a : kPrimalAspects) {
        int owners = 0;
        for (const ClassKit& k : kClassKits)
            if (k.affinityAspect == a) ++owners;
        CHECK_EQ(owners, 1);
    }

    TEST_SECTION("masteries");
    CHECK_EQ(kWeaponFamilies.size(), kWeaponFamilyCount);
    for (std::size_t i = 0; i < kWeaponFamilyCount; ++i)
        CHECK_EQ(familyIndex(kWeaponFamilies[i]), static_cast<int>(i));
    CHECK_EQ(familyIndex("lightsaber"), -1);
    CHECK_EQ(masteryLevelFor(0), 0u);
    CHECK_EQ(masteryLevelFor(99), 0u);
    CHECK_EQ(masteryLevelFor(100), 1u);
    CHECK_EQ(masteryLevelFor(299), 1u);
    CHECK_EQ(masteryLevelFor(300), 2u);
    CHECK_EQ(masteryLevelFor(5499), kMaxMasteryLevel - 1);
    CHECK_EQ(masteryLevelFor(5500), kMaxMasteryLevel);
    CHECK_EQ(masteryLevelFor(std::numeric_limits<std::uint64_t>::max()), kMaxMasteryLevel);

    Masteries m;
    CHECK(m.knows("spear", MasteryMove::Light));
    CHECK(m.knows("spear", MasteryMove::Heavy));
    CHECK(!m.knows("spear", MasteryMove::Sprint));
    CHECK(!m.addXp("lightsaber", 1000));
    CHECK(!m.knows("lightsaber", MasteryMove::Light));
    CHECK_EQ(m.xp("lightsaber"), std::uint64_t{0});
    CHECK(m.addXp("spear", 300));
    CHECK_EQ(m.level("spear"), 2u);
    CHECK(m.knows("spear", MasteryMove::Charged));
    CHECK(!m.knows("spear", MasteryMove::Air));
    CHECK_EQ(m.level("axe"), 0u);
    CHECK_EQ(m.deflectBonusTicks("spear"), 0u);
    CHECK(m.addXp("spear", 5500));
    CHECK_EQ(m.level("spear"), kMaxMasteryLevel);
    CHECK(m.knows("spear", MasteryMove::GuardCounter));
    CHECK(m.knows("spear", MasteryMove::Execution));
    CHECK_EQ(m.deflectBonusTicks("spear"), 2u);
    CHECK(m.addXp("spear", std::numeric_limits<std::uint64_t>::max()));
    CHECK_EQ(m.xp("spear"), std::numeric_limits<std::uint64_t>::max());

    TEST_SECTION("reputation");
    Reputation rep;
    for (std::size_t i = 0; i < kFactionCount; ++i) {
        const auto f = static_cast<Faction>(i);
        CHECK_EQ(rep.value(f), 0);
        CHECK(rep.standing(f) == Standing::Neutral);
        CHECK(rep.canTrade(f));
        CHECK(!rep.patrolsHostile(f));
        CHECK(approx(rep.priceMultiplier(f), 1.0f));
    }
    CHECK_EQ(rep.adjust(Faction::KharDuunMiners, 100), 100);
    CHECK(rep.standing(Faction::KharDuunMiners) == Standing::Neutral);
    CHECK_EQ(rep.adjust(Faction::KharDuunMiners, 1), 101);
    CHECK(rep.standing(Faction::KharDuunMiners) == Standing::Trusted);
    CHECK(approx(rep.priceMultiplier(Faction::KharDuunMiners), 0.9f));
    CHECK_EQ(rep.adjust(Faction::VeyrSmugglers, 5000), Reputation::kMax);
    CHECK(rep.standing(Faction::VeyrSmugglers) == Standing::Honoured);
    CHECK(approx(rep.priceMultiplier(Faction::VeyrSmugglers), 0.8f));
    CHECK_EQ(rep.adjust(Faction::MourgeCults, -101), -101);
    CHECK(rep.standing(Faction::MourgeCults) == Standing::Distrusted);
    CHECK(approx(rep.priceMultiplier(Faction::MourgeCults), 1.25f));
    CHECK(rep.canTrade(Faction::MourgeCults));
    CHECK_EQ(rep.adjust(Faction::AshTribunal, -500), -500);
    CHECK(rep.standing(Faction::AshTribunal) == Standing::Hostile);
    CHECK(!rep.canTrade(Faction::AshTribunal));
    CHECK(rep.patrolsHostile(Faction::AshTribunal));
    CHECK(approx(rep.priceMultiplier(Faction::AshTribunal), 0.0f));
    CHECK_EQ(rep.adjust(Faction::AshTribunal, std::numeric_limits<std::int32_t>::min()), Reputation::kMin);
    CHECK_EQ(rep.value(Faction::OrderOfThaumiel), 0);
    CHECK_EQ(rep.value(Faction::FreeCaravans), 0);

    return ::lt::test::finish("progression");
}
